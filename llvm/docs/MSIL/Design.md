# Design: an LLVM backend that emits .NET (MSIL/CIL) assemblies from C

Status: draft v2 for review, 2026-10-12; updated 2026-10-21 with the approved Phase 2 decisions (`recon/p2/decision-brief.md`) and the Phase 1 as-built corrections (section 12); updated 2026-10-28 with the approved Phase 3 decisions (`recon/p3/decision-brief.md`, with R4 and R5 as the user resolved them) and the Phase 2 as-built notes (Status.md section 6, items 16-23)
Scope: C (via clang) to fully managed, IL-only .NET assemblies. Flat exported C API only. No C++/CLI object model, and no C++ interop or name-mangling work. Like LLVM itself, the project ships compiler tools only (`clang`, `llc`, `msil-ld`, ...); build systems (CMake, MSBuild, ...) are separate projects that integrate the compiler.

**Decisions so far (from review of v1):**
- The work lives in a fork of llvm-project.
- The backend is a conventional LLVM target modeled on the WebAssembly target (section 2).
- The libc is selected per target, the same way native C toolchains do it (section 6).
- Targeting .NET Framework is supported, not only modern .NET (section 7).

---

## 1. Goals and non-goals

**Goals**

1. `clang --target=<msil triple> foo.c -o foo.dll` produces an IL-only PE assembly that runs on the CLR with no native code inside it.
2. Calls to symbols that live in native shared libraries become P/Invoke calls with blittable signatures.
3. Static linking happens at the IR level when inputs are bitcode, and at the CIL-object level otherwise. Either way, the code ends up merged into the output assembly.
4. An assembly built by this toolchain can stand in for a shared library. Another C program can link against it, and the calls become ordinary managed calls into that assembly.
5. Exported C functions show up as public static methods on a single class, so C# can call them directly. That is the whole managed-facing API.
6. The C library is a choice of target, as it is in C: glibc, musl, UCRT/MSVCRT, Darwin libSystem. A portable libc for .NET is a planned exploration after phase 3 (section 6).

**Non-goals (for now)**

- A managed object model, GC references in C code, or C++/CLI-style mixed assemblies. Two narrower features are later goals (user, 2026-10-22), after phase 3 at the earliest: C source calling managed (.NET) methods directly, and inline IL.
- C++ beyond whatever already works as plain C with mangled names. Mangled names are legal CIL identifiers, so it costs nothing to leave the door open.
- Being callable *from native C* (exporting symbols from the assembly to native callers). Callbacks into managed code via function pointers handed to native code are in scope; that is a different thing (see 4.5).
- Verifiable/safe IL. The output is unverifiable, full-trust IL, the same as `/clr:pure` or `unsafe` C#.

---

## 2. Key decision: a conventional target modeled on WebAssembly

The backend is an ordinary LLVM target: SelectionDAG instruction selection producing MachineInstrs, an MC layer, an AsmPrinter, an object writer, and an lld-based linker. Its structure copies `llvm/lib/Target/WebAssembly`, because WebAssembly already solves most of what a stack-machine target with typed locals needs.

### 2.1 What we take from the WebAssembly target

| WebAssembly piece | Role for CIL |
|---|---|
| No register allocation: virtual registers survive to the end, then `WebAssemblyExplicitLocals` turns them into typed locals | Same. CIL locals are unlimited and typed, so this maps one to one. |
| `WebAssemblyRegStackify`: keeps single-use values on the operand stack instead of in locals | Same pass, essentially unchanged. CIL has the same expression-tree shape. |
| Signatures tracked per function (`WebAssemblyFunctionInfo` params/results, `MCSymbolWasm` signatures) | CIL needs exact signatures for `call`, `calli`, method definitions and P/Invoke declarations. We reuse this mechanism. |
| `call_indirect` with a type signature | Maps to `calli` with a stand-alone signature. |
| `WebAssemblyFixFunctionBitcasts`, `WebAssemblyAddMissingPrototypes` | Needed for the same reason: K&R-style and mismatched-prototype calls must still have exact typed signatures. |
| Legalization (i128, odd-width ints, vectors) via the normal SelectionDAG legalizer | We get it for free instead of writing IR-level lowering. |
| `WebAssemblyLowerEmscriptenEHSjLj` | Starting point for setjmp/longjmp (4.6). |
| Shadow stack (`__stack_pointer`) and variadic lowering (`ExpandVariadics`, an argument buffer) | A per-thread shadow stack only for dynamic allocas that a loop can repeat; the variadic argument buffer lives in the caller's frame, and the `va_list` keeps the native layout (4.2, 4.7). |
| Wasm object format with typed symbols and relocations, plus `wasm-ld` | lld/wasm is the template for `msil-ld`'s driver and LTO; symbol resolution follows lld/ELF. CIL objects are ECMA-335 modules (5.1), not a wasm-style container; the wasm `linking` section is the checklist for their link-info record. |
| `.s` text that can be reassembled | `-S` emits ILAsm-compatible text. |

### 2.2 What we drop

- `WebAssemblyCFGStackify` and `WebAssemblyFixIrreducibleControlFlow`. CIL allows arbitrary branches within a method, as long as the operand stack is consistent at each branch target, so no structured control flow is needed. RegStackify already keeps the stack empty across block boundaries.
- Wasm-specific exception handling, tables, and memory-index details.

### 2.3 What we add

- **Pointer vs integer.** In MachineInstrs a pointer is just an `i64`, which is fine: CIL treats unmanaged pointers as `native int` anyway. On 64-bit triples every 64-bit integer is `native int` as well, and `int64` is never emitted (plan D2): PEVerify rejects mixing the two, and one CIL type per register class keeps the stack types exact.
- **Struct-by-value at P/Invoke boundaries.** Internal calls pass aggregates indirectly, so the backend only sees scalars there. A native callee that takes or returns a struct by value needs a real CIL value type in its P/Invoke signature, so the runtime can apply the native ABI. Clang's native ABI lowering already says how. An aggregate it coerces to scalars passes as those scalars, and on x86-64 a result returned through a hidden pointer (`sret`) is that pointer as the first argument, as the native ABI passes it. What remains becomes an explicit-layout value type of the same size: a SysV aggregate passed in memory (`byval`) or returned in two registers (`{i64, i64}`, `{double, double}`, `{i64, double}`, with `int64` and `float64` fields), and SysV `__int128`. Objects describe it in their link-info record (size, alignment, eightbyte classes), and `msil-ld` emits it into the output (from phase 3). The CLR then applies the native ABI, except that it places such a value type at an 8-byte stack slot, so `msil-ld` adds a padding argument before a 16-byte-aligned one where the native ABI aligns it. AArch64 passes `sret` in x8, so there the result is a value type as well; the AArch64 shapes and Win64 `__int128` stay link errors until a runtime can check them. Native-callable thunks of such functions (4.5) use the same value types.
- **Assembly-level output:** objects carry method definitions, data images and relocation tables; `msil-ld` synthesizes the module initializer, `'<msil>Main'`, the P/Invoke stubs and the export class (`-S` runs the same builder for a one-object self-link), plus assembly references. Phase 1 hard-codes `[mscorlib]`; from phase 2 on, `msil-ld` takes the BCL references from the reference assemblies it is given, and binds to `[mscorlib]` when it is given none (5.2, 7.1).

### 2.4 Why not a direct IR-to-CIL translator (v1's recommendation)

It would preserve slightly more type information, but it would mean writing our own legalization and our own object and linking story. Following WebAssembly gets legalization, MC, object files, and an lld linker structure from a target that upstream actively maintains, and keeps the fork close to upstream conventions. The type information we would lose is recovered where it matters, at P/Invoke boundaries (2.3).

---

## 3. Target triples, data models and ABI

C bakes the ABI in at compile time (`sizeof(void*)`, `sizeof(long)`, `wchar_t`, struct layouts, and the libc's own structs such as `struct stat`). CIL's `native int` is late-bound, but C cannot be. Choosing a libc means choosing that libc's ABI, and on Linux and macOS that ABI depends on the CPU architecture as well.

**Triples (decided): the native triple plus an `msil` object-format suffix.** An MSIL target is "this exact native platform and ABI, but emit CIL instead of machine code". LLVM triples already carry an object format at the end of the environment component (`-elf`, `-coff`, `-macho`, `-wasm`), so we add `msil` as a new `Triple::ObjectFormatType`.

| Triple | libc | Data model |
|---|---|---|
| `x86_64-unknown-linux-gnu-msil`, `aarch64-unknown-linux-gnu-msil` | glibc | LP64 |
| `x86_64-unknown-linux-musl-msil`, `aarch64-unknown-linux-musl-msil` | musl | LP64 |
| `x86_64-pc-windows-msvc-msil`, `aarch64-pc-windows-msvc-msil` | UCRT (or MSVCRT) | LLP64, 16-bit `wchar_t` |
| `x86_64-w64-windows-gnu-msil` | MinGW | LLP64 |
| `aarch64-apple-darwin-msil` | libSystem | LP64 (phase 4) |
| `i686-pc-windows-msvc-msil` | UCRT | ILP32 (phase 5) |
| `msil64-unknown-dotnet` (planned exploration after phase 3) | Portable .NET libc (section 6) | LP64 |

What this means in the fork:

- **Clang reuses its native `TargetInfo` and ABI lowering unchanged**, so the data model, struct layout, `va_list`, predefines (`__x86_64__`, `__linux__`, `_WIN32`, ...) and target features are the native ones by construction, plus `__MSIL__`, with two exceptions. On Linux triples `__ELF__` is defined as in a native build (clang defines it only for the ELF object format), because `msil-ld` links them with ELF's rules (`.init_array`, `__start_`/`__stop_`, weak symbols, visibility, `DT_SONAME`). And atomics wider than 8 bytes are never reported lock-free (`__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16` undefined, `__atomic_always_lock_free(16, 0)` false), because the CLR has no 16-byte atomic (4.8). When the object format is `msil`, a thin wrapper adds `__MSIL__` (and `__ELF__` on Linux triples), caps the lock-free atomic width at 8 bytes, rejects inline assembly, and maps `long double` to `double`. The one exception to the inline-assembly rule is an empty asm statement such as `asm volatile("" ::: "memory")`, a common compiler barrier, which becomes a call of a private empty method that the JIT does not inline (plan D36, amended in phase 3: an IL comment did not stop the JIT from hoisting loads across it). Inline IL, the MSIL counterpart of inline assembly, is a later goal (user, 2026-10-22). From plan milestone M12 on, empty asm statements with operands (`asm volatile("" : : "r"(x) : "memory")`, Rust's `black_box`) are accepted too and become compiler-only barriers that keep their operands alive.
- **Backend selection:** `TargetRegistry::lookupTarget` picks the MSIL target when the triple's object format is `msil`, whatever the architecture. That's a small hook in TargetRegistry. The MSIL backend therefore sees x86-64- or AArch64-shaped IR: native ABI coercions, `llvm.x86.*` / `llvm.aarch64.*` intrinsics (mapped per 3.2), and the native `va_list` layout.
- **Stock-clang compatibility:** bitcode from an unmodified clang with `--target=x86_64-unknown-linux-gnu -flto` is nearly identical, so `msil-ld` accepts it too, as a documented compatibility path with a warning once per link. It lacks `__MSIL__` and the inline-asm check; on x86-64 windows-msvc its `volatile` accesses of up to 8 bytes are atomic acquire/release accesses (`/volatile:ms`, clang's default there, which `x86_64-pc-windows-msvc-msil` shares), which the backend lowers like other atomics (4.8); on x86-64 Linux and MinGW its `long double` has the native layout; and it arrives vectorized for the native CPU, so its IL is about 27% larger.
- **Native `va_list` everywhere:** because clang lowers `va_arg` for the native ABI, our own variadic functions also use the native `va_list` layout. Passing a `va_list` to a native `vprintf` then needs no conversion (4.7).

- Pointer size is fixed at compile time. Every triple names a CPU, so the PE's machine type is set (AMD64/ARM64/I386) and the runtime refuses to load the assembly on the wrong architecture, the same as a C# project built with `/platform:x64`. Option for later: a Windows x64 assembly that uses no arch-specific intrinsics could be marked AnyCPU-64, since x64 and ARM64 Windows share every layout.
- Headers come from a sysroot for that libc and platform (`--sysroot`), as in any cross-compile. Because clang uses the native `TargetInfo`, those headers see what they see in a native build, apart from `__MSIL__` and the lock-free answers for 16-byte atomics (above).
- `long double` is `double` everywhere. On x86-64 Linux and MinGW (80-bit x87 format) and on AArch64 Linux (IEEE binary128) that differs from native, so `long double` interop on those triples is a documented limitation; on windows-msvc and AArch64 windows-gnu `long double` is natively `double`. The helper archive of those triples (section 6) forwards the native `long double` functions (`sqrtl`, `strtold`, ...) to their `double` counterparts, so such programs link and compute in double precision; structures with `long double` members still differ from native, and so do `printf` and `scanf` formats with `L` (4.7).
- The calling convention for internal calls is our own (aggregates indirect). At P/Invoke boundaries we follow the native ABI through the CLR's own marshalling of blittable value types (2.3).

### 3.1 Windows

Windows is a first-class target, not just "UCRT":

- **Headers and libraries** come from the Windows SDK and UCRT (via `--sysroot`, or the usual MSVC environment detection in the clang driver). `windows.h` works as it does for native clang-cl/MSVC builds.
- **Win32 APIs** (kernel32, user32, advapi32, ...) are ordinary P/Invokes. The SDK's import libraries (`kernel32.lib`, ...) already record which DLL exports each symbol, so `msil-ld` reads them for the symbol-to-DLL mapping, exactly like `link.exe` does.
- **x64 and ARM64** are separate triples (`x86_64-pc-windows-msvc-msil`, `aarch64-pc-windows-msvc-msil`), but they share the LLP64 data model, struct layouts and `char*` `va_list`, so code without arch-specific intrinsics behaves identically on both. .NET Framework runs them as well (section 7).
- **32-bit Windows** (`i686-pc-windows-msvc-msil`) needs calling conventions in signatures: `__stdcall` and `__cdecl` map to `calli unmanaged stdcall/cdecl` and `DllImport(CallingConvention=...)`. On 64-bit these attributes are ignored, as on native x64.
- **MinGW** (`x86_64-w64-windows-gnu-msil`): the same, but with MinGW headers and msvcrt/UCRT, for code that builds with MinGW but not MSVC.
- Native variadic functions are never called with the CLR's `vararg` convention, although the CLR supports it on Windows. Windows triples use the lowering of the other triples (4.7), and call a native variadic function that has no v-variant with fixed arity, passing floating-point values as their integer bits.

### 3.2 SIMD

SIMD is supported. .NET exposes hardware intrinsics that map almost one to one onto C's, and codegen switches (7.1) let the compiler use them:

- **Generic LLVM vector types and operations** (`<4 x float>`, `<16 x i8>`, shuffles, ...) become `System.Runtime.Intrinsics.Vector128<T>`/`Vector256<T>`/`Vector512<T>` locals and operations.
- **Architecture-specific intrinsics** (`immintrin.h` → `llvm.x86.*`, `arm_neon.h` → `llvm.aarch64.neon.*`) become calls to `System.Runtime.Intrinsics.X86.{Sse,Sse2,...,Avx2,Avx512F}` and `System.Runtime.Intrinsics.Arm.AdvSimd`, which the JIT turns into the same instructions. So target features (`-mavx2`, ...) are honored exactly as in a native build.
- **A CPU without a feature** the code was compiled for throws `PlatformNotSupportedException`, the managed equivalent of the native illegal-instruction crash. Code that does its own runtime dispatch (`__builtin_cpu_supports`) maps that check to `IsSupported`. Without the switches `__builtin_cpu_supports` reports no feature (a private helper defines `__cpu_model`), so such code takes its portable path.
- **Without those switches** (the default, and the only choice on .NET Framework, where none of these APIs exist), vectors are scalarized by the legalizer and arch-specific intrinsics are rejected unless they have a scalar fallback (`_mm_pause` becomes `Thread.SpinWait(1)`, fences `Interlocked.MemoryBarrier`, `crc32` a private helper, `rdtsc` a monotonic tick helper, and MSVC's `__fastfail` `Environment.FailFast`, whose exit status differs from the native one).
- Intrinsics with no .NET equivalent are lowered to scalar code where LLVM can expand them, or rejected with an error naming the intrinsic.

---

## 4. Memory model

The C program never touches the GC heap. All C-visible memory is unmanaged and never moves, so pointers are plain `native int` and pinning is never needed.

### 4.1 Globals

- Each output assembly gets one unmanaged data segment, laid out by `msil-ld` from the objects' data (same-named sections concatenated, `__start_`/`__stop_` symbols provided) and allocated by the linker-synthesized module initializer (`<Module>::.cctor`, which the CLR has supported since 1.0; two phases in libraries, 5.4), which fills it by copying from an embedded RVA data blob. Global addresses are `segment_base + offset`, held in a static field. `-S` output is a one-object self-link with the same shape (2.3).
- Relocations (globals whose initializers contain addresses of other globals or functions) are applied by the module initializer from a relocation table.
- Constructors (`llvm.global_ctors`, and the triple's constructor sections such as `.preinit_array` (executables only, first), `.init_array` and `.CRT$XC*`) run after relocation, in priority order across all objects. On Linux triples a constructor may take glibc's `(int argc, char **argv, char **envp)` or a prefix of it, and gets `main`'s `argc` and `argv` and the process environment; on Windows triples, whose C runtime passes nothing, a constructor that takes arguments is an error. A null function in `llvm.global_ctors` ends the list, as on every LLVM target.
- `exit`, `_exit`, `_Exit`, `quick_exit` and `abort` are the host C library's, P/Invoked like any native function. `atexit` and `at_quick_exit` forward to the host C library's exported registration functions, with the handler's native-callable address (4.5): `__cxa_atexit(f, 0, __dso_handle)` and `__cxa_at_quick_exit(f, __dso_handle)` on glibc, as glibc's `libc_nonshared.a` defines them, and `_crt_atexit` and `_crt_at_quick_exit` on the UCRT. `__dso_handle` is defined per assembly on ELF triples.
- Destructors (`llvm.global_dtors`, `__attribute__((destructor))`, `.fini_array*`, `.CRT$XT*`) are registered with the same C library function. Each assembly registers one runner for its destructors before its constructors run, as glibc's `__libc_start_main` registers `_dl_fini`, so the C library runs them on the thread that calls `exit`, after the `atexit` handlers that the assembly's constructors and `main` registered and before it flushes stdio. ELF triples run them in the reverse of the constructor order, as glibc does. Windows triples run them in ascending section-name order (`.CRT$XTA` before `.CRT$XTX`, the default priority), as the static C runtime orders them; the DLL C runtime that these triples bind never runs them natively. A library registers its runner when it initializes, before the program does, so its destructors run after the program's, in reverse initialization order.
- .NET Framework 4.8 is the exception: the UCRT's `exit` hands a process whose EXE is managed to the CLR's shutdown before it runs its own table. So on Framework the output also subscribes `AppDomain.ProcessExit`, where a run-once guard calls the C library's `_cexit`, which runs the process's exit table (every assembly's `atexit` handlers and destructor runners) and flushes stdio, on the CLR's finalizer thread. Framework stops ProcessExit handlers after about 2.3-2.6 s in all, a documented limit. Framework also reaches ProcessExit from native `_exit`, `_Exit` and `quick_exit`, so there the handlers and destructors run for them too and `at_quick_exit` handlers do not run, a documented Framework-only limitation. On modern .NET the C library's table runs, as in a native program.

### 4.2 The stack

- Allocas that never escape are promoted by LLVM before we see them.
- Remaining static allocas: a single `localloc` at function entry, sized for the whole frame. A constant-size `localloc` of 32 bytes or less can be only 8-byte aligned (.NET Framework 4.8 for 8 to 32 bytes; .NET 8 and .NET 10 for 16 and 32 bytes in Tier-0 code and for 8 bytes in optimized code), so frames assume 8-byte alignment, and a frame holding an object aligned above 8 allocates `MaxAlign - 8` extra bytes and aligns its base up (plan D13). Frames are not zeroed: `.locals` is never `init`.
- A dynamic alloca that no loop can repeat is a `localloc`. It runs at most once per call, so the `localloc`'s lifetime until the function returns is invisible to C, and the CLR reclaims the memory on every unwind (an exception, a `longjmp`). One on a cycle of the control-flow graph would grow the CLR stack on every iteration, so it uses a per-thread shadow stack (chunked unmanaged memory hung off a `[ThreadStatic]` field), like WebAssembly's `__stack_pointer`, with the `stacksave`/`stackrestore` around it and fault clauses that restore it on unwind.

### 4.3 Heap

`malloc` comes from the selected libc. With a host libc (glibc, UCRT, ...) it is the real native `malloc` via P/Invoke, so pointers can be exchanged freely with native libraries. With a future portable libc it would be backed by `NativeMemory` or `Marshal.AllocHGlobal` (7.1).

### 4.4 Threads and TLS

C threads are the host libc's threads (`pthread_create`, `CreateThread`, `_beginthreadex`, `thrd_create`), which are CLR threads once they run managed code: the thread entry goes through a native-callable thunk (4.5). `thread_local` variables live in a per-thread unmanaged TLS block hung off a `[ThreadStatic]` field, allocated lazily and initialized from a TLS template, which is the same pattern as ELF TLS. The block is freed when its thread ends (a finalizable per-thread holder). A library exports a `thread_local` variable through its address accessor, which returns the calling thread's address (5.4).

Thread-exit destructors that C or Rust code registers (`__cxa_thread_atexit_impl`, and `FlsAlloc` and `tss_create` on Windows) go to a per-thread list, which the toolchain's wrapper of the thread entry runs when the thread ends, because the host runs its own thread-exit callbacks after the CLR has detached the thread. Threads started by native code run no such list, and `pthread_key_create` destructors stay native on Linux.

### 4.5 Function pointers

A C function pointer may be called from C in the same assembly, from another assembly, or by native code (for example a `qsort` comparator passed to native `qsort`).

- **Representation:** a function pointer value is always a native-callable entry point, so pointers handed to native code are correct by construction.
  - Modern .NET, with the `UnmanagedCallersOnly` switch of `msil-ld` (`--uco-thunks`, 7.1; the attribute is bound from the references): the address of an `[UnmanagedCallersOnly]` thunk, obtained with `ldftn`.
  - Default, and the only choice on .NET Framework, which has no `UnmanagedCallersOnly`: the module initializer creates one delegate per address-taken function, keeps it alive in a static array, and stores `Marshal.GetFunctionPointerForDelegate` in the thunk table. This is slower to call from native code, but correct.
- **Identity.** On .NET Framework, two `ldftn` of one method in different methods can give different values (precode versus compiled code), so every address-taken function has one `native int` address field that the module initializer sets once; code and relocations read the field, and it is the slot the thunk table above fills later (plan section 12, M9 row).
- **Fast path:** indirect calls from our own code look the pointer up in this assembly's table of native-callable entry points (an exact lookup: delegate thunks come from a runtime-wide stub heap and `UnmanagedCallersOnly` entry points are precodes, so no range compare can recognize them) and, on a hit, `calli` the *managed* method directly, which skips the native transition and keeps exceptions (`longjmp`, 4.6) out of native frames. Otherwise they use `calli unmanaged cdecl`. The address of a native function is its native entry point.
- Each assembly's table holds only its own functions, and assemblies share no table (section 6). A pointer to a function of another assembly therefore takes the native path, through that assembly's thunk, and a `longjmp` out of such a callback leaves through the thunk, which stops it on Linux triples (4.6). For the same reason a call through such a pointer fails fast, with a message that names the function, wherever a native caller would: a C variadic function (4.7), or one whose signature passes a value that MSIL cannot pass to native code.
- Only address-taken functions get thunks.
- A C function that native code calls asynchronously, from a signal or a vectored exception handler, runs as a reverse P/Invoke. When such a call interrupts managed code on the same thread, the CLR ends the process (measured for vectored handlers on .NET 8 on a hardware exception), so these handlers work only where the native program would also have run them outside its own code. On Linux triples `msil-ld` warns when a program installs a handler with `signal` or its relatives.

### 4.6 setjmp/longjmp

`setjmp` call sites are rewritten (starting from WebAssembly's Emscripten-style SjLj lowering) so that `longjmp` throws a BCL exception type (`System.Runtime.InteropServices.ExternalException`, whose `HResult` names the target frame's status block, recorded in the first 16 bytes of the `jmp_buf`), which wrappers around the calls of the function that called `setjmp` catch with an IL exception filter that matches the `jmp_buf`; no exception type has to be shared between assemblies. With a host libc, the program's `setjmp`/`longjmp` calls are intercepted by the compiler and never reach the native ones, which could not unwind managed frames; `msil-ld` never binds them to a native library. A `longjmp` out of a callback through native frames works on Windows only: the CLR on Linux aborts the process when a managed exception crosses native frames, so on Linux triples a thunk stops any `longjmp` that would leave it, with a clear message. That includes a callback that another assembly calls through a function pointer (4.5). Every `longjmp` is a first-chance CLR exception, which debuggers and `FirstChanceException` handlers see.

### 4.7 Variadic functions

- **Between our own functions:** the caller writes the variadic arguments into a buffer in its own static frame, much like the WebAssembly lowering (LLVM's `ExpandVariadics` pass, with MSIL ABIs). But because clang lowers `va_arg` for the native ABI (section 3), `va_start` in the callee builds a *native-layout* `va_list` over that buffer: the SysV x86-64 `__va_list_tag` with register-save offsets set to "exhausted" and the overflow area pointing at the buffer, the AAPCS64 struct with `__stack` pointing at it, or a plain `char*` on Windows and Apple ARM64.
- **Calling native variadic functions** (host-libc triples, for example `printf` in glibc). CoreCLR supports the native `vararg` convention only on Windows, and .NET Framework only on Windows as well, so we never use it (3.1):
  1. *Known v-variants.* A call to `printf(fmt, ...)` becomes a call of native `vprintf(fmt, va)`. The `va_list` is built over the argument buffer exactly as in our own callees (above), and our `va_list` values can likewise be passed straight to native `v*` functions. A native `va_list` parameter is pointer-sized on every supported ABI, so the P/Invoke signature is simply `(native int, native int)`. `msil-ld` keeps a table per C runtime that maps the libc's variadic functions to their v-variants (glibc's and msvcrt's). The UCRT needs none: its headers define the `printf` family inline over `__stdio_common_v*`, and code that declares `printf` itself gets toolchain-authored definitions from the helper archive (section 6).
  2. *Fixed-arity treatment.* Functions with no v-variant (`open`, `fcntl`, `ioctl`, ...) are on a built-in fixed-arity list per triple and are called as ordinary fixed-arity functions, with a signature per call site. For integer and pointer arguments this is ABI-correct on x86-64 SysV, Windows x64, and Linux AArch64, but not on Apple ARM64, which will need another route. Windows passes floating-point values as their integer bits, where its variadic convention reads them. On SysV a fixed-arity call cannot pass a floating-point value (the CLR leaves `AL`, which glibc reads, undefined), so that is a compile-time error. An attribute that names further fixed-arity functions may come with the managed-calls work.
  3. Anything else is a link error that names the function.
- On triples whose native `long double` is wider than `double`, a `long double` argument of a native variadic function is passed as a `double`, so clang warns about `printf`-family formats with `L` there and rejects `scanf`-family ones, which would write past the object.
- Native code cannot call a C variadic function through a pointer, because the arguments arrive in the native convention; such a call (and one from another assembly, which takes the same native path, 4.5) fails fast with a message that names the function.
- A future portable libc would avoid all of this: everything variadic is our own code.

### 4.8 Atomics

- Atomic loads and stores of up to 8 bytes are `volatile.`-prefixed accesses (every ordering, because the JIT would hoist a plain load out of a loop), and a sequentially consistent store adds `Interlocked.MemoryBarrier`. Read-modify-write operations and compare-exchange on 4- and 8-byte objects are `Interlocked` calls (or a compare-exchange loop where `Interlocked` has no member); 1- and 2-byte ones are a compare-exchange loop on the containing 32-bit word. These are atomic by address, so they work across assemblies and against native code.
- 16-byte, generic-size and misaligned atomics take the lock path that compiler-rt's `atomic.c` would serve natively, and get it from the platform's process-wide library, as native code does. On Linux triples that is `libatomic.so.1`: the program links `-latomic`, as a native build does, and `msil-ld` binds the `__atomic_*` calls to it like any native function. On Windows triples a small private helper adapts the `__atomic_*` calls to the MSVC STL's `msvcp140_atomic_wait.dll` (`__std_atomic_compare_exchange_128` for 16-byte objects), part of the Visual C++ runtime, so only a program that uses such atomics needs the VC++ redistributable. Every assembly of a process, and native code that uses the same library, then agree on each object.
- A compiler-only barrier (an empty `asm` with a memory clobber, `atomic_signal_fence`) is a call of a private empty method that the JIT cannot inline, since the JIT reorders memory accesses across nothing else.

---

## 5. Linking model

### 5.1 Object files

Following WebAssembly, there are two object kinds, and the linker accepts both, including mixed together in archives:

- **CIL object files** (`clang -c` default): ECMA-335 modules (netmodules: no Assembly row) whose definitions are metadata rows and whose cross-object references are named metadata references (MemberRefs on placeholder types, ImplMap stubs on `msil.unresolved`), plus a versioned link-info record in a module-level custom attribute: C link semantics, data images, data relocations, constructor lists, section names and the triple. ildasm and ILSpy read them; a COFF symbol table in the envelope serves `nm` and `ar`.
  - **Shape.** A PE32+ envelope with no Assembly row, an MVID that is a hash of the content, and a Module name that does not depend on the `-o` path. Objects carry no `.cctor`, no `Main` and no data image field: `msil-ld` synthesizes them (4.1, 5.2). An object has no spill classes, so one translation unit holds at most the runtimes' per-type member counts (65,524 methods and 65,534 fields in `<Module>`); `msil-ld`'s outputs spill past them into private classes.
  - **References.** A reference to an undefined function is one MemberRef per (name, call-site signature), so `msil-ld` can check every call against the definition (5.2). The placeholder ImplMap form is `pinvokeimpl("msil.unresolved" nomangle cdecl)`, the shape `csc -target:module` emits for `[DllImport("msil.unresolved")]`.
  - **The link-info record** (version 1 is specified field by field against the ELF gABI, lld/ELF and wasm's `linking` section before it is frozen): a header with the triple, the format version and the codegen-switch and ABI flags; symbols with binding, visibility, common size and alignment, TLS and no-strip flags; aliases; weak undefined symbols; COMDAT groups with a selection kind; data segments with section names and flags; data relocations; constructor and destructor lists with priorities; call-site signature flags (no prototype, vararg, byval and sret size and alignment, dllimport, an explicit import module and name); from phase 3 (minor versions 1.1-1.3 of record version 1), a fixed-arity call-site flag (4.7), optional BCL substitution requests (7.1) and aggregate descriptors (size, alignment, eightbyte classes) for structs passed by value to native code (2.3); and linker options (`#pragma comment(lib)`).
  - Archives are built with `llvm-ar` on every triple. A per-object Portable PDB carrier, keyed by the object's MethodDef rows, is reserved for phase 4 debug information.
- **Bitcode objects** (`-flto`): linked at the IR level and code-generated at link time. Static libraries built with `-flto` give full IR-level static linking and whole-program optimization. `-flto -c` (or `-emit-llvm -c`) writes bitcode; `clang -c` writes a CIL object.

### 5.2 The linker (`msil-ld`, an lld flavor modeled on `wasm-ld`)

1. Load objects and archives, including the triple's `clang_rt.builtins` archive (compiler-rt's builtins built for that MSIL triple as CIL objects, from phase 3), which the driver passes after the default libraries, as native links pass compiler-rt; pull in archive members on demand; and run LTO on any bitcode.
2. Resolve every remaining undefined symbol, in this order:
   1. **Objects and archives** (static linking), with lld/ELF's resolution rules (weak, common, COMDAT, aliases, lazy extraction).
   2. **Referenced managed assemblies** built by this toolchain: read their symbol table (5.4). A match becomes a `call` into that assembly's export class. Any other reference assembly carries no such table and is used only to bind BCL types and members (7.1).
   3. **Native shared libraries**, including the selected libc: read the native library's export table found via `-L`/`-l` (`llvm::object` handles ELF, COFF and Mach-O) to learn which library provides the symbol. A match becomes a P/Invoke stub (5.3). Only default-version ELF symbols bind (P/Invoke resolves through `dlsym`); symbols that exist only in native static code are link errors unless the toolchain provides them; native data is imported through the global's address field, filled at startup by a private helper (`NativeLibrary` when the reference assemblies define it; otherwise `LoadLibraryW`/`GetProcAddress` from kernel32.dll on Windows triples and `dlopen`/`dlsym` on Linux triples). For cross-compiles, the sysroot supplies the files a native link reads: ELF `.so` files and GNU ld scripts, or import libraries on Windows. Names of compiler runtime helpers (compiler-rt's `__`-prefixed library-call names such as `__muldc3`) and the `setjmp`/`longjmp` family never bind to a native library: they come from the helper archive (section 6) or the compiler (4.6), or are link errors. The one exception is the lock path of atomics: its `__atomic_*` functions come from the platform's atomic library (4.8).
   4. Anything left is a link error.
3. Lay out one data segment, synthesize the module initializer and `'<msil>Main'`, check every cross-object call signature (an adapter and a warning where native behaviour can be matched, otherwise a link error naming both objects; a call of a compiler runtime helper must match exactly), bind every BCL type and member by searching the `--reference` assemblies (a built-in `[mscorlib]` table when none is given; 7.1), and write the PE.

`-lfoo` lookup per search directory: `libfoo.a` (static) → a managed `foo.dll` with our symbol table (5.4) → native `libfoo.so` / an import library / `foo.dll` / `libfoo.dylib` (P/Invoke). `libfoo.a` counts only if it holds CIL or bitcode members; native archives are skipped. `-Bstatic`/`-Bdynamic` force one or the other, as usual. The libc is just one of the triple's default libraries (section 8), so it can be linked statically when a static build of it exists for the target (5.3, section 6).

### 5.3 P/Invoke shape

- `DllImport` with only blittable types (integers, floats, `native int`, explicit-layout blittable structs). With only blittable types, the runtime does not marshal by copying even on .NET Framework. An `msil-ld` switch (`--disable-runtime-marshalling`, for .NET 7+, bound from the references) also sets `[assembly: DisableRuntimeMarshalling]` (7.1).
- Each `DllImport` names the library as a native link records it: on ELF the library's `DT_SONAME` (`libc.so.6`, `libm.so.6`, `libz.so.1`), or its file name when it has none; on Windows the DLL file name from the import library or the DLL, verbatim with its `.dll` suffix (`api-ms-win-crt-*-l1-1-0.dll`, `kernel32.dll`), and `#N` for an import by ordinal; on Mach-O (later) the install name. Bare names are not used: `"z"` or `"libz.so"` loads only where a development symlink exists, `"libc"` works only through a CoreCLR special case, and .NET Framework does not append `.dll` to a dotted name. Every import is `ExactSpelling` and `CDecl` (`nomangle cdecl`), so an entry point binds exactly as named (without `nomangle`, `GetModuleHandle` silently binds to `GetModuleHandleA`).
- `[SuppressGCTransition]` (modern .NET) is emitted only for declarations marked for it, by a later per-declaration attribute (it is unsafe for functions that block or call back), and is bound from the references like any attribute (7.1).

### 5.4 Exports and assembly-to-assembly linking

- `-shared` produces a library assembly (a DLL image). Every exported function becomes `public static` on one export class (`-Wl,--export-class=Contoso.Zlib.Native`; by default the assembly name, as a class without a namespace). On Windows triples the exported symbols are the `dllexport` ones when any exist, otherwise every default-visibility symbol (the GNU ld rule); on other triples every default-visibility symbol is exported.
- Exported data is exposed as an accessor `'<msil>addr_x'()` and a property `x`; every exported function also gets an address accessor `'<msil>addr_f'()`, so a pointer to it has the exporter's own value on every runtime (4.5). Exported addresses are never public fields. A `thread_local` export is a data symbol with the ThreadLocal flag; its accessor `'<msil>addr_x'()` returns the calling thread's address, so importers keep calling the accessor (4.4).
- Libraries are linked with `-Bsymbolic` and `-z defs` semantics, because an assembly has no global symbol scope: a library binds its own references to its own definitions (no interposition), `-shared` output has no undefined symbols, and an object that defines a symbol a linked library also exports gets a warning.
- Libraries (`-shared` outputs) initialize in two phases. First a `beforefieldinit` class `'<msil>Data'`, whose `.cctor` allocates and fills the data segment, sets the address fields and applies the relocations within the assembly, without touching another assembly. Then `<Module>::.cctor`, which runs `ldtoken` once per linked library in link order, applies the cross-assembly relocations and runs the constructors (4.1). A program runs the same steps, in the same order, in its one module initializer: no assembly links against a program, so nothing waits on its first phase, and its linked output keeps the shape of `-S` output.
- In a library whose `'<msil>Data'` fields spill into further classes, two threads that first use the library at the same time may let one of them read a spilled field before the first phase stored it; phase 3's threads fix it by running the first phase inside the module initializer of such a library.
- A custom attribute carries the symbol table (name, kind, signature), the triple, the C runtime identity, the data model and an ABI version. Its type is a private attribute class emitted into the library, so no support assembly is needed (section 6). The class allows multiple instances, so an assembly holding several targets (5.6) is not precluded, and the table shares its codec with the objects' link-info record (5.1). That way a later link resolves symbols exactly, and refuses to link an assembly built for a different triple or C runtime, for example glibc against UCRT or LP64 against LLP64.

### 5.5 Emitting the PE

- **Milestone 1:** `-S` emits ILAsm-compatible text, and `ilasm` produces the assembly. There are no objects or linker yet. Two ilasm limits apply to this path: identifiers of at most 1023 bytes (a longer C name is a compile error) and float32 signaling-NaN constants, which come out quiet. Phase 2's PE writer writes signaling NaNs exactly but keeps the identifier limit, because PEVerify flags longer names; `-S` output keeps both.
- **Phase 2:** the MC object writer writes ECMA-335 module objects (5.1) and `msil-ld` writes the PE, both in C++ through one ECMA-335 library (section 8). `-S` still prints a runnable module, the object plus a one-object self-link (2.3) whose imports come from the fixed Phase 1 import table (section 12); object-form text is printed behind a flag. `ilasm` stays a test oracle: `clang -c` objects equal ilasm-assembled object-form text canonically, and outputs round-trip through `ildasm` and `ilasm`.

### 5.6 Shipping multiple targets

The ABI is baked in per target, so there is one assembly per target. Combining the per-target assemblies is the business of the user's build system, not of the compiler. A single multi-target ("fat") assembly is not planned for phases 1-5; it may be considered after them.

---

## 6. libc

The libc is a target choice, as in native C (section 3).

- **Host libcs (glibc, musl, UCRT/MSVCRT, libSystem):** the default is dynamic, exactly like a native build. libc functions are P/Invoked from the system's shared libc, with the variadic handling from 4.7. Headers come from that platform's sysroot.
- **Static libc:** possible when the libc can be built for our target and its OS layer can run without native code. For musl, that means replacing its syscall layer, because CIL cannot make syscalls directly. The practical options are P/Invoking the kernel interface through a tiny dynamic dependency (which defeats the point) or the portable layer below. So static host libcs are a later investigation, not a promise.
- **Portable .NET libc (`-unknown-dotnet`), a planned exploration after phase 3 (user goal, 2026-10-22: a platform-independent build):** a real libc compiled for our target, statically linked, with its OS layer over the BCL. Candidates are wasi-libc with a managed WASI preview1 layer (a small, documented interface), musl with a managed syscall shim, or picolibc. Revisit only once the host-libc targets are solid.
- **No runtime support assembly.** Generated code calls .NET (BCL) APIs directly wherever one exists: a `[ThreadStatic]` field plus `NativeMemory` or `Marshal.AllocHGlobal` for the per-thread shadow stack (4.2) and the `thread_local` block (4.4), `AppDomain.ProcessExit` for the `atexit` handlers and destructors on .NET Framework 4.8, whose C runtime does not run its own exit table for a managed program (4.1), `Environment.GetCommandLineArgs` and `Environment.CommandLine` for argv and, in executables of Windows triples, for `GetCommandLineW`/`A` and the UCRT's argv accessors (whose native versions would include the `dotnet` host's own arguments), and `Interlocked` and the `volatile.` prefix for atomics (4.8). f16 conversions come from compiler-rt's helpers, because `System.Half` quiets signaling NaNs and does not exist on .NET Framework. Newer APIs are used only as 7.1 allows: individual codegen switches, capability from the reference assemblies `msil-ld` binds against, and defaults that run on .NET Framework 4.8 and modern .NET alike. Where no suitable BCL API exists, or the default must also run on .NET Framework 4.8, the backend or `msil-ld` emits small helpers into the output assembly, private to it, of three kinds: IL that the toolchain generates for services that need .NET types (the thunk table, the TLS holder, the Framework exit fallback); per-module IR helpers where `-S` output must contain the body (the setjmp helpers, i128 division); and compiler-rt's builtins built for each MSIL triple (`clang_rt.builtins`, which also holds a few C helpers of the toolchain's own, such as the `long double` forwarders of section 3), which the driver passes to `msil-ld` as native links pass compiler-rt. An example is C complex `__muldc3`/`__divdc3`, whose Annex G infinity and NaN rules `System.Numerics.Complex` does not follow. `-S` output, which no linker sees, gets no archive helper: a feature that needs one is an error there. For setjmp/longjmp no exception type is shared (4.6), the module initializer builds the thunk table (4.5), generated code builds native `va_list`s (4.7), and process startup is the generated `Main` that calls `main` (section 12), or `wmain` on Windows triples when the program defines only that. Every output assembly is therefore self-contained: it needs no runtime support assembly, and its only dependencies are the BCL, the native libraries it P/Invokes, and any assemblies it was explicitly linked against (5.2). Calls to the C math library go to the libc selected above, as in a native build, except where a 7.1 switch permits a specific `Math` API such as `Math.FusedMultiplyAdd`, and except the exact substitutions: where clang emits the LLVM intrinsic of a function whose `Math` member gives the host libc's results bit for bit on every runtime, NaN bits included (`sqrt`, `floor`, `ceil` as `-Math.Floor(-x)`, `trunc`, `fabs`, `round`, `rint`, `nearbyint`, `roundeven`), it maps to that member by default. The rest of the math library is not mapped to `Math`/`MathF`, whose .NET Framework results differ from libm's.
- **No state shared between assemblies.** Every output has its own copy of the toolchain's private state (the thunk table, the TLS block, the shadow stack). Assemblies share no managed object: there is no name looked up at run time, no shared type and no handshake between them. Where C needs one object per process on the host-libc triples, that object is the host C library's or the platform's, as it is for native modules (a portable libc would decide its own when it is designed): the C library's exit table (4.1), the platform's atomic library for the lock path of atomics (4.8), and C library state such as `errno`, stdio and the heap. A function pointer that another assembly owns is called like a native one (4.5).

---

## 7. .NET Framework and modern .NET

### 7.1 Codegen switches and assembly references

The IL instructions we need (`calli unmanaged`, `localloc`, `cpblk`/`initblk`, `unaligned.`, `volatile.`, `tail.`, module initializers, RVA fields) have existed since CLR 2.0. A handful of library APIs and attributes differ between Framework and modern .NET (table below). There is no runtime profile and no target-framework option:

- **Codegen switches.** Individual, independent switches permit specific newer APIs or attributes, for example "may use `Math.FusedMultiplyAdd`", "emit `DisableRuntimeMarshalling`" or "use `UnmanagedCallersOnly` thunks instead of delegate thunks". Their defaults keep the generated code running on .NET Framework 4.8 and modern .NET alike, as phase 1 output does. A build system may combine switches into a "net8.0" setting if it wants; the compiler has no profile concept. Each switch is an MSIL target feature (`-mmsil-<name>` / `-mno-msil-<name>`, per function, so an LTO link keeps each translation unit's choice), or an `msil-ld` option for what only the linker emits. The first ones are `-mmsil-bitops`, `-mmsil-fma` and `-mmsil-minmax`, and `msil-ld`'s `--disable-runtime-marshalling` and `--uco-thunks`; every switch is off by default on every triple.
- **Capability from the references**, where that is possible: an API or attribute is usable only if some reference assembly defines it. Only `msil-ld` sees the references (5.2), so that is where this applies: an attribute such as `DisableRuntimeMarshalling`, or a newer API that a switch lets codegen use, is bound like any other BCL member and needs a reference that defines it. A switch whose member gives the same results as its fallback, bit for bit, is optional instead: without a reference that defines the member, `msil-ld` keeps the fallback and reports it under `--verbose`, so a program's results never depend on which references it was given.
- **Assembly references** work as in Roslyn (`csc`): there is no core-library switch. Phase 1 hard-codes `[mscorlib]`, which .NET's `mscorlib` facade makes run everywhere. Without references, and in `-S` output, which no linker sees, BCL members bind to `[mscorlib]` as in phase 1, limited to members that `mscorlib` provides on both runtimes (a built-in table in `msil-ld`; a member outside it is an error that asks for a reference), so `clang foo.c -o foo.exe` keeps running everywhere. From phase 2 on, `msil-ld` is given reference assemblies (`--reference=<file|dir>` or `--reference <path>`, repeatable, with no short form, because lld's `-r` means `--relocatable`; managed `.dll` inputs are references too) and binds every BCL type and member the generated code needs (`Marshal.AllocHGlobal`, `Interlocked.MemoryBarrier`, `Math`, attributes such as `DisableRuntimeMarshalling`, ...) by searching them, and references whichever assembly defines it. .NET Framework's reference assemblies give `mscorlib`, which loads on both runtimes. .NET 8's give references such as `System.Runtime`, `System.Runtime.InteropServices` and `System.Threading` (where `Interlocked` lives), at version 8.0.0.0, so the output then loads only on .NET 8 and later.

What differs between the runtimes (the table below, and 3.2 for SIMD) decides which switches exist and what their safe defaults are.

| Need | Modern .NET (.NET 8) | .NET Framework 4.8 |
|---|---|---|
| Native-callable function pointers | `[UnmanagedCallersOnly]` + `ldftn` under `--uco-thunks`; kept-alive delegates by default | Kept-alive delegates + `Marshal.GetFunctionPointerForDelegate` |
| Unmanaged allocation | `NativeMemory` | `Marshal.AllocHGlobal` (or the libc's `malloc`) |
| Disable marshalling | `DisableRuntimeMarshalling` | Not needed for blittable signatures |
| Bit operations | `BitOperations` and `BinaryPrimitives` intrinsics | Inline IL sequences |
| Floating-point math | `Math.FusedMultiplyAdd`, `Math.CopySign`, `MathF`, `BitConverter.SingleToInt32Bits`; `Math.Min`/`Math.Max` order -0.0 below +0.0 | The exact `Math` substitutions of section 6, which both runtimes use; otherwise generic expansions or C math library calls. `Math.Min(-0.0, 0.0)` is +0 |
| 128-bit integers | Division, remainder and floating-point conversions are one private function per operation and module (compiler-rt's algorithm as IR, merged across objects by COMDAT; division by a constant stays inline); the rest is expanded inline by LLVM; `Int128`/`UInt128` are not used, since they are no faster | The same code |
| Half-precision floating point | compiler-rt's helpers in the output assembly (`System.Half` differs on signaling NaNs; at most a later switch) | The same helpers |
| Atomics | `Interlocked` (`And`/`Or` from .NET 5, 8- and 16-bit `Exchange`/`CompareExchange` from .NET 9; neither used by default) | `Interlocked` on 32- and 64-bit objects; compare-exchange loops for the rest (the default on both) |
| Lazy library loading | `NativeLibrary` when the references define it, else as in the other column | `LoadLibraryW`/`GetProcAddress` via kernel32; `dlopen`/`dlsym` on Linux triples |
| Process exit (the host C library's `exit`, 4.1) | The C library runs its `atexit` table, which holds the handlers and destructors | The UCRT's `exit` hands a managed EXE to the CLR's shutdown before it runs its table, so the handlers and destructors run in `AppDomain.ProcessExit`, which Framework cuts off after about 2.3-2.6 s |
| Valid triples | All | Windows triples. Framework runs only on Windows. |

### 7.2 Mono and Unity

Mono and Unity are out of scope for the plan, but nothing in the default output (7.1) should preclude them. Worth a check in phase 4.

---

## 8. Toolchain and driver integration

- `llvm/lib/Target/MSIL/`: a WebAssembly-shaped target (ISel, ExplicitLocals, RegStackify, AsmPrinter, MC, object writer).
- `llvm/lib/CLI/` (namespace `llvm::cli`, constants in `llvm/BinaryFormat/CLI.h`): an ECMA-335 reader and writer shared by the MC object writer and `msil-ld`, for objects, outputs and reference assemblies.
- `llvm/lib/TargetParser/Triple.cpp`: the `msil` object format. `llvm/lib/MC/TargetRegistry.cpp`: select the MSIL target by object format.
- `clang/lib/Basic/Targets/`: a thin wrapper over the native `TargetInfo` when the object format is `msil` (`__MSIL__`, `__ELF__` on Linux triples, a lock-free atomic width of at most 8 bytes, no inline asm until the later inline-IL goal, `long double` = `double`).
- No MSIL ABI lowering in clang (`clang/lib/CodeGen/Targets/`): the native target's serves, and P/Invoke signatures come from the IR (2.3).
- `clang/lib/Driver/ToolChains/MSIL.{h,cpp}`: `MSILToolChain` over the native toolchain (its sysroot, header and library search), the default C libraries per triple (for example `-lc` on Linux; on windows-msvc `ucrt.lib`, `kernel32.lib` and `oldnames.lib`, plus a toolchain import list that binds the 11 `mem*`/`str*`/`wcs*` functions of the VC runtime to `ucrtbase.dll`, so the default libraries need no VC++ redistributable) and the triple's `clang_rt.builtins` archive after them (section 6), and invoking `msil-ld`. The default output name is `a.exe` on every MSIL triple: programs are EXE images and `-shared` gives a DLL image, because .NET Framework starts only EXE images and `dotnet` runs only `.dll` and `.exe` names. An executable that can run on .NET Framework (linked without references, or against the .NET Framework's reference assemblies or `netstandard` 2.0 or earlier) is named `<name>.exe`; on a Windows triple it starts directly on .NET Framework, and everywhere through `dotnet` with a runtimeconfig.json. One bound to a .NET reference pack (`System.Runtime`) or to `netstandard` 2.1 or later runs only on .NET and is named `<name>.dll`, run as `dotnet <name>.dll` with the runtimeconfig.json that the build system writes. `msil-ld` warns when such an executable is named `.exe`, and writes no apphost. Managed `.dll` inputs become references, or libraries to link against when they carry our symbol table (5.4).
- `compiler-rt/lib/builtins`: built for each MSIL triple as a CIL archive (`clang_rt.builtins`), from a reviewed source list, together with the toolchain's own C helpers (section 6). It leaves out `atomic.c`: the lock path of atomics comes from the platform's library (4.8).
- `lld/MSIL/` (`msil-ld`): an lld flavor modeled on `lld/wasm` (driver, LTO) with lld/ELF's resolution rules, hosted in `lld` as `msil-ld`.
- Like Roslyn (`csc`), which writes only the assembly and its PDB, the driver and `msil-ld` write neither `foo.runtimeconfig.json` (modern .NET) nor `foo.exe.config` (Framework). Those come from the build system or the user, as the .NET SDK's MSBuild targets write them for C#. The test harness writes its own runtimeconfig for test runs; that is test tooling, not the compiler.

---

## 9. Prior art (from memory; to be verified with a short survey before phase 1)

- **LLVM WebAssembly target and `wasm-ld`:** the structural template (section 2).
- **LLVM's own MSIL backend** (LLVM 2.x, removed around 2.8): an IR-walking ILAsm printer. Useful for its type mapping. It was removed for lack of maintenance.
- **MSVC `/clr:pure`** (deprecated): proof that C compiled to pure IL with unmanaged pointers works on the CLR, including on .NET Framework.
- **gcc-cil / GCC4NET** (about 2006–2009): a GCC backend emitting CIL from C, with a CIL linker.
- **Cesium:** a C compiler in C# that targets CIL. Relevant for stdlib and interop choices.
- **rustc_codegen_clr:** Rust to CIL. The most current reference for runtime limits with unmanaged-pointer-heavy IL.
- **LLILC:** the reverse direction (CIL to LLVM). Background on how the runtime treats unmanaged pointers and GC transitions.
- **llvm-mos:** a model for running a long-lived out-of-tree target fork with its own SDK.

---

## 10. Risks

1. **JIT behavior on LLVM-shaped code.** RyuJIT falls back to minimal optimization for very large methods, and aggressive inlining produces those. Mitigation: an IL-size-aware inline cost, outlining, early measurement. The Framework JIT is more conservative still.
2. **Host-libc ABI fidelity.** Arch-specific triples, variadic shims, `long double`, and libc structs read through P/Invoke. Each host libc needs its own conformance run.
3. **Native variadic calls** beyond the v-variant table and the fixed-arity list are unsupported.
4. **Function-pointer cost** where native-to-managed calls go through delegates: always on .NET Framework, and on modern .NET unless the `UnmanagedCallersOnly` switch is on (4.5).
5. **Fork maintenance.** Keep the diff to new directories plus registration hooks, as WebAssembly and llvm-mos do.
6. **ECMA-335 library effort** in phase 2: the reader and writer (about 9k lines with tests) are the dominant cost; the container choice moves only a few thousand lines.
7. **Debugging** needs Portable PDB generation (Windows PDB for Framework) from LLVM debug locations.
8. **Semantics gaps:** C11 atomics rest on the JIT honoring `volatile.` for relaxed and acquire loads and on `Interlocked` being a full barrier, which litmus and hoisting tests check on every runtime the gates run; `fenv.h` rounding modes; and signals.
9. **Stack depth.** `dotnet x.dll` on Windows runs `main` on the host's main thread, whose stack is about 1.5 MB (an ilasm-built `.exe` on .NET Framework: 4 MB; native Linux programs: 8 MB by default), and a CIL frame is larger than the native one, at `-O0` especially: about 19,000 `-O0`-shaped frames overflow it, and a .NET stack overflow ends the process. The generated `Main` could later run `main` on a thread with a configurable stack size.
10. **Exceptions through native frames.** The CLR on Linux aborts the process when a managed exception crosses native frames (a `longjmp` out of a callback under native `qsort`), so such code works on Windows only.

---

## 11. Phased plan

| Phase | Outcome |
|---|---|
| **0. Bootstrap** | Fork llvm-project and copy the WebAssembly target's skeleton into an experimental `MSIL` target: the `msil` object format in `Triple`, backend selection by object format, the clang `TargetInfo` wrapper, a driver stub. CI on Linux with the .NET SDK and ilasm, plus Windows CI for .NET Framework. |
| **1. Single file to running assembly** (first milestone) | `-S` to `.il`, assembled with ilasm, runs on modern .NET. |
| **2. Objects and linker** | CIL objects as ECMA-335 modules with a link-info record, `msil-ld`, direct PE writer, multiple TUs, archives, bitcode/LTO, `-shared` exports, assembly-to-assembly linking, P/Invoke resolution from native export tables, BCL references bound from reference assemblies (7.1). |
| **3. libc and runtime** | Runtime support through the BCL and private helpers in the output assembly (section 6: toolchain-generated IL and compiler-rt's builtins built for MSIL), the codegen switches for newer .NET APIs (7.1), dynamic stack allocation, `thread_local`, destructors and `atexit` on the host C library's exit table, variadic functions, native-callable function pointers and threads, setjmp/longjmp, atomics, structs by value at P/Invoke boundaries, and conformance of the glibc and UCRT host-libc triples. Real programs run: zlib, Lua, SQLite. |
| **4. Conformance and performance** | GCC torture tests and llvm-test-suite per triple, benchmarks vs native clang, inliner tuning, full SIMD intrinsic coverage, debug info, musl and Darwin triples. |
| **5. Production toolchain** | 32-bit triples, static host libcs where viable. |

**As built (Phase 2, 2026-10-27).** Phase 2 delivered every outcome of its row (Status.md section 1): `clang -c` writes CIL objects with link-info record v1, frozen since M20; `msil-ld` writes the PE, binds native libraries, reference assemblies and library assemblies; objects are the default route and `-S` plus ilasm a test oracle. Nine programs of several files and the link-semantics matrix match their native builds through objects and LTO on .NET 8, .NET 10, .NET Framework 4.8 and WSL, and 149 of c-testsuite's 150 libc-free tests do so through objects on .NET 8 for `x86_64-pc-windows-msvc-msil` (the 150th defines a variadic function). What Phase 2 reports as an error instead of running is Phase 3's: thread-local data, dynamic allocas, destructors and `atexit`, variadic definitions and calls, function pointers passed to native code, setjmp/longjmp, atomics, structs passed to or returned from native functions by value, and the compiler-rt helpers.

---

## 12. First milestone

**"fib.c runs on dotnet."**

`clang --target=x86_64-unknown-linux-gnu-msil -O2 -ffreestanding -S prog.c -o prog.il`, then `ilasm -dll prog.il`, then `dotnet prog.dll`, with a correct exit code and output.

In scope:
- A target skeleton copied from WebAssembly: SelectionDAG ISel for scalar integer and FP arithmetic, comparisons, casts, branches, `switch`, loads and stores, direct and indirect calls. ExplicitLocals and RegStackify carried over.
- A text AsmPrinter emitting a complete ILAsm module: methods, a module initializer for globals and relocations, and static frames via `localloc`.
- Function pointers within the module (`ldftn` + `calli`). The native-callable thunk design comes later.
- One hard-coded P/Invoke, `puts` → `DllImport("libc")` on Linux, to prove the import path end to end.
- A generated `Main` that calls `main`.

Exit criteria:
- About 25 small C programs (fib, sieve, quicksort over a global array, struct-heavy code, a function-pointer dispatch table, a string table with relocations) compile at `-O0` and `-O2`, run, and match native clang's output, as a lit suite in CI.
- FileCheck tests on the emitted `.il`.
- `ilverify` reports no stack-shape errors (unverifiable pointer use is expected).

**As built (Phase 1, 2026-10-20).** Phase 1 met these criteria with the following differences, all recorded in ImplementationPlan.md and Status.md:
- The commands are `ilasm -dll -pe64 -x64 prog.il` (`-pe64 -arm64` for aarch64 triples; plain `ilasm -dll` writes a 32-bit AnyCPU image), and `dotnet prog.dll` needs a `prog.runtimeconfig.json` next to the assembly, written by the user or the build system as section 8 says (or `dotnet exec --runtimeconfig <file> prog.dll`; GettingStarted.md section 5). Without it the host stops with exit status `0x80008083`.
- Instead of one hard-coded P/Invoke there is a fixed import table (plan D14): `puts` and `putchar` from the C library and the C math library, per triple (`libm.so.6` on glibc, because glibc keeps libm separate and CoreCLR maps only the bare name `libc`; on Windows only the functions the C runtime DLL exports under that name), plus any declaration with the `msil-import-module` IR attribute. Every other undefined function is a compile error until `msil-ld` (5.2), which names libraries as 5.3 says.
- Each address-taken function has one `native int` address field that the module initializer sets with `ldftn` once; code and relocations read it, because .NET Framework can give two `ldftn` of one method different values (4.5).
- The generated `Main` calls `main(void)` or `main(int, char **)`; `argv` is NULL-terminated and built from `Environment.GetCommandLineArgs()`, so `argv[0]` is the assembly path under `dotnet x.dll`; any other `main` signature is an error (plan D27).
- Verification is the policy of plan D21 (approved 2026-10-12): MSILStackCheck, force-JIT of every method, ILVerify with an allow-list and PEVerify on Windows, clean on the assembly and on a neutralized ildasm/ilasm copy (Status.md section 4). `ilverify` alone cannot check stack shapes: it stops at the first error of a method and cannot verify `calli`.

---

## 13. Open questions

1. Should Mono and Unity be an explicit target, or just "not precluded"?

# Design: an LLVM backend that emits .NET (MSIL/CIL) assemblies from C

Status: draft v2 for review, 2026-10-12
Scope: C (via clang) to fully managed, IL-only .NET assemblies. Flat exported C API only. No C++/CLI object model, and no C++ interop or name-mangling work.

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
6. The C library is a choice of target, as it is in C: glibc, musl, UCRT/MSVCRT, Darwin libSystem. A portable libc for .NET is deferred (section 6).

**Non-goals (for now)**

- A managed object model, GC references in C code, or C++/CLI-style mixed assemblies.
- C++ beyond whatever already works as plain C with mangled names. Mangled names are legal CIL identifiers, so it costs nothing to leave the door open.
- Being callable *from native C* (exporting symbols from the assembly to native callers). Callbacks into managed code via function pointers handed to native code are in scope; that is a different thing (see 4.5).
- Multiple targets in one assembly. Each target produces its own assembly (5.6).
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
| Shadow stack (`__stack_pointer`) and variadic lowering (pointer to an argument buffer) | Same model (4.2, 4.7). |
| Wasm object format with typed symbols and relocations, plus `wasm-ld` | Template for a CIL object format and `msil-ld` (section 5). |
| `.s` text that can be reassembled | `-S` emits ILAsm-compatible text. |

### 2.2 What we drop

- `WebAssemblyCFGStackify` and `WebAssemblyFixIrreducibleControlFlow`. CIL allows arbitrary branches within a method, as long as the operand stack is consistent at each branch target, so no structured control flow is needed. RegStackify already keeps the stack empty across block boundaries.
- Wasm-specific exception handling, tables, and memory-index details.

### 2.3 What we add

- **Pointer vs integer.** In MachineInstrs a pointer is just an `i64`, which is fine: CIL treats unmanaged pointers as `native int` anyway.
- **Struct-by-value at P/Invoke boundaries.** Internal calls pass aggregates indirectly, so the backend only sees scalars there. A native callee that takes or returns a struct by value needs a real CIL value type in its P/Invoke signature, so the runtime can apply the native ABI. Clang attaches the C-level signature to `dllimport`-candidate declarations as metadata, and the backend emits matching value types with explicit layout.
- **Assembly-level output:** method definitions, the module initializer, the P/Invoke table, the export class, and assembly references, all emitted by the AsmPrinter and object writer.

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
| `aarch64-apple-darwin-msil` | libSystem | LP64 |
| `i686-pc-windows-msvc-msil` | UCRT | ILP32 (later) |
| `msil64-unknown-dotnet` (deferred) | Portable .NET libc (section 6) | LP64 |

What this means in the fork:

- **Clang reuses its native `TargetInfo` and ABI lowering unchanged**, so the data model, struct layout, `va_list`, predefines (`__x86_64__`, `__linux__`, `_WIN32`, ...) and target features are exactly the native ones by construction. When the object format is `msil`, a thin wrapper adds `__MSIL__`, rejects inline assembly, and maps `long double` to `double`.
- **Backend selection:** `TargetRegistry::lookupTarget` picks the MSIL target when the triple's object format is `msil`, whatever the architecture. That's a small hook in TargetRegistry. The MSIL backend therefore sees x86-64- or AArch64-shaped IR: native ABI coercions, `llvm.x86.*` / `llvm.aarch64.*` intrinsics (mapped per 3.2), and the native `va_list` layout.
- **Stock-clang compatibility:** bitcode from an unmodified clang with `--target=x86_64-unknown-linux-gnu -flto` is nearly identical, so `msil-ld` can accept it too (lacking only `__MSIL__` and the inline-asm check).
- **Native `va_list` everywhere:** because clang lowers `va_arg` for the native ABI, our own variadic functions also use the native `va_list` layout. Passing a `va_list` to a native `vprintf` then needs no conversion (4.7).

- Pointer size is fixed at compile time. Every triple names a CPU, so the PE's machine type is set (AMD64/ARM64/I386) and the runtime refuses to load the assembly on the wrong architecture, the same as a C# project built with `/platform:x64`. Option for later: a Windows x64 assembly that uses no arch-specific intrinsics could be marked AnyCPU-64, since x64 and ARM64 Windows share every layout.
- Headers come from a sysroot for that libc and platform (`--sysroot`), as in any cross-compile. Because clang uses the native `TargetInfo`, those headers see exactly what they see in a native build.
- `long double` is `double` everywhere. On glibc x86-64 that differs from native, so `long double` interop on that triple is a documented limitation.
- The calling convention for internal calls is our own (aggregates indirect). At P/Invoke boundaries we follow the native ABI through the CLR's own marshalling of blittable value types (2.3).

### 3.1 Windows

Windows is a first-class target, not just "UCRT":

- **Headers and libraries** come from the Windows SDK and UCRT (via `--sysroot`, or the usual MSVC environment detection in the clang driver). `windows.h` works as it does for native clang-cl/MSVC builds.
- **Win32 APIs** (kernel32, user32, advapi32, ...) are ordinary P/Invokes. The SDK's import libraries (`kernel32.lib`, ...) already record which DLL exports each symbol, so `msil-ld` reads them for the symbol-to-DLL mapping, exactly like `link.exe` does.
- **x64 and ARM64** are separate triples (`x86_64-pc-windows-msvc-msil`, `aarch64-pc-windows-msvc-msil`), but they share the LLP64 data model, struct layouts and `char*` `va_list`, so code without arch-specific intrinsics behaves identically on both. .NET Framework runs them as well (section 7).
- **32-bit Windows** (`i686-pc-windows-msvc-msil`) needs calling conventions in signatures: `__stdcall` and `__cdecl` map to `calli unmanaged stdcall/cdecl` and `DllImport(CallingConvention=...)`. On 64-bit these attributes are ignored, as on native x64.
- **MinGW** (`x86_64-w64-windows-gnu-msil`): the same, but with MinGW headers and msvcrt/UCRT, for code that builds with MinGW but not MSVC.
- Native `vararg` calls are supported by the CLR on Windows. We still use the v-variant lowering (4.7) for consistency, and keep native vararg as a fallback for functions without a v-variant.

### 3.2 SIMD

SIMD is supported. .NET exposes hardware intrinsics that map almost one to one onto C's:

- **Generic LLVM vector types and operations** (`<4 x float>`, `<16 x i8>`, shuffles, ...) become `System.Runtime.Intrinsics.Vector128<T>`/`Vector256<T>`/`Vector512<T>` locals and operations.
- **Architecture-specific intrinsics** (`immintrin.h` → `llvm.x86.*`, `arm_neon.h` → `llvm.aarch64.neon.*`) become calls to `System.Runtime.Intrinsics.X86.{Sse,Sse2,...,Avx2,Avx512F}` and `System.Runtime.Intrinsics.Arm.AdvSimd`, which the JIT turns into the same instructions. So target features (`-mavx2`, ...) are honored exactly as in a native build.
- **A CPU without a feature** the code was compiled for throws `PlatformNotSupportedException`, the managed equivalent of the native illegal-instruction crash. Code that does its own runtime dispatch (`__builtin_cpu_supports`) maps that check to `IsSupported`.
- **On .NET Framework (net48)** none of these APIs exist, so vectors are scalarized by the legalizer and arch-specific intrinsics are rejected unless they have a scalar fallback.
- Intrinsics with no .NET equivalent are lowered to scalar code where LLVM can expand them, or rejected with an error naming the intrinsic.

---

## 4. Memory model

The C program never touches the GC heap. All C-visible memory is unmanaged and never moves, so pointers are plain `native int` and pinning is never needed.

### 4.1 Globals

- Each module gets one unmanaged data segment allocated in the module initializer (`<Module>::.cctor`, which the CLR has supported since 1.0), initialized by copying from an embedded RVA data blob. Global addresses are `segment_base + offset`, held in a static field.
- Relocations (globals whose initializers contain addresses of other globals or functions) are applied in the module initializer from a small relocation table.
- `llvm.global_ctors` run after relocation, in priority order. Destructors and `atexit` handlers run at process exit (`AppDomain.ProcessExit`).

### 4.2 The stack

- Allocas that never escape are promoted by LLVM before we see them.
- Remaining static allocas: a single `localloc` at function entry, sized for the whole frame.
- Dynamic allocas and `stacksave`/`stackrestore` use a per-thread shadow stack (a `[ThreadStatic]` unmanaged block), like WebAssembly's `__stack_pointer`.

### 4.3 Heap

`malloc` comes from the selected libc. With a host libc (glibc, UCRT, ...) it is the real native `malloc` via P/Invoke, so pointers can be exchanged freely with native libraries. With a future portable libc it would be backed by `NativeMemory` or `Marshal.AllocHGlobal` (7.2).

### 4.4 Threads and TLS

C threads are CLR threads. `thread_local` variables live in a per-thread unmanaged TLS block hung off a `[ThreadStatic]` field, allocated lazily and initialized from a TLS template, which is the same pattern as ELF TLS. When the program uses a host libc's threads (`pthread_create`), the thread entry goes through a native-callable thunk (4.5) and the same lazy TLS setup applies.

### 4.5 Function pointers

A C function pointer may be called from C in the same assembly, from another assembly, or by native code (for example a `qsort` comparator passed to native `qsort`).

- **Representation:** a function pointer value is always a native-callable entry point, so pointers handed to native code are correct by construction.
  - Modern .NET: the address of an `[UnmanagedCallersOnly]` thunk, obtained with `ldftn`.
  - .NET Framework: there is no `UnmanagedCallersOnly`. Instead, the module initializer creates one delegate per address-taken function, keeps it alive in a static array, and stores `Marshal.GetFunctionPointerForDelegate` in the thunk table. This is slower to call from native code, but correct.
- **Fast path:** indirect calls from our own code first check whether the pointer is in this assembly's thunk table (one range compare), and if so `calli` the *managed* method directly, which skips the native transition. Otherwise they use `calli unmanaged cdecl`.
- Only address-taken functions get thunks.

### 4.6 setjmp/longjmp

`setjmp` call sites are rewritten (starting from WebAssembly's Emscripten-style SjLj lowering) so that `longjmp` throws a managed exception caught in the function that called `setjmp`. With a host libc, the program's `setjmp`/`longjmp` calls are intercepted by the compiler and never reach the native ones, which could not unwind managed frames.

### 4.7 Variadic functions

- **Between our own functions:** the caller writes the variadic arguments into a buffer on the shadow stack, much like the WebAssembly lowering (LLVM's `ExpandVariadics` pass). But because clang lowers `va_arg` for the native ABI (section 3), `va_start` in the callee builds a *native-layout* `va_list` over that buffer: the SysV x86-64 `__va_list_tag` with register-save offsets set to "exhausted" and the overflow area pointing at the buffer, the AAPCS64 struct with `__stack` pointing at it, or a plain `char*` on Windows and Apple ARM64.
- **Calling native variadic functions** (host-libc triples, for example `printf` in glibc). CoreCLR supports the native `vararg` convention only on Windows, and .NET Framework only on Windows as well, so we do not rely on it:
  1. *Known v-variants.* A call to `printf(fmt, ...)` is lowered to native `vprintf(fmt, va)`. The `va_list` is built over the argument buffer exactly as in our own callees (above), and our `va_list` values can likewise be passed straight to native `v*` functions. A native `va_list` parameter is pointer-sized on every supported ABI, so the P/Invoke signature is simply `(native int, native int)`. A table in the target maps the libc's variadic functions to their v-variants.
  2. *Fixed-arity treatment.* Functions with no v-variant (`open`, `fcntl`, `ioctl`, ...) are declared in our headers with `__attribute__((msil_fixed_arity))` and called as ordinary fixed-arity functions. This is ABI-correct on x86-64 SysV, Windows x64, and Linux AArch64, but not on Apple ARM64, where those headers instead route through managed shims.
  3. Anything else is a compile-time error that names the function.
- A future portable libc would avoid all of this: everything variadic is our own code.

---

## 5. Linking model

### 5.1 Object files

Following WebAssembly, there are two object kinds, and the linker accepts both, including mixed together in archives:

- **CIL object files** (`clang -c` default): an MC-emitted container, modeled on the wasm object format, holding method bodies in CIL, data segments, relocations, and a symbol table with typed signatures.
- **Bitcode objects** (`-flto`): linked at the IR level and code-generated at link time. Static libraries built with `-flto` give full IR-level static linking and whole-program optimization.

### 5.2 The linker (`msil-ld`, an lld flavor modeled on `wasm-ld`)

1. Load objects and archives, pull in archive members on demand, and run LTO on any bitcode.
2. Resolve every remaining undefined symbol, in this order:
   1. **Objects and archives** (static linking).
   2. **Referenced managed assemblies** built by this toolchain: read their export table (5.4). A match becomes a `call` into that assembly's export class.
   3. **Native shared libraries**, including the selected libc: read the native library's export table found via `-L`/`-l` (`llvm::object` handles ELF, COFF and Mach-O) to learn which library provides the symbol. A match becomes a P/Invoke stub. For cross-compiles where the native library is not present, the sysroot supplies symbol lists (or import libraries on Windows).
   4. Anything left is a link error.
3. Lay out data segments, apply relocations, and write the PE.

`-lfoo` lookup per search directory: `libfoo.a` (static) → a managed `foo.dll` with our export metadata → native `libfoo.so` / `foo.dll` / `libfoo.dylib` (P/Invoke). `-Bstatic`/`-Bdynamic` force one or the other, as usual. The libc is just the default `-lc` for the triple, so it can be linked statically when a static build of it exists for the target (5.3, section 6).

### 5.3 P/Invoke shape

- `DllImport` with only blittable types (integers, floats, `native int`, explicit-layout blittable structs). With only blittable types, the runtime does not marshal by copying even on .NET Framework. On .NET 7+ we also set `[assembly: DisableRuntimeMarshalling]`.
- Library names are emitted without prefixes or suffixes (`"z"`, not `"libz.so.1"`) so the runtime's probing finds `libz.so`, `z.dll`, or `libz.dylib`. Versioned sonames such as glibc's `libc.so.6` are taken from the native library's `DT_SONAME` when the linker reads it.
- `[SuppressGCTransition]` is available as an opt-in attribute on modern .NET.

### 5.4 Exports and assembly-to-assembly linking

- `-shared` produces a library assembly. Every default-visibility function becomes `public static` on one export class (`-Wl,--export-class=Contoso.Zlib.Native`). Exported data is exposed as address accessors.
- An assembly-level custom attribute carries the symbol table (name, kind, signature) and the triple. That way a later link resolves symbols exactly, and refuses to link an assembly built for a different triple, for example glibc against UCRT or LP64 against LLP64.

### 5.5 Emitting the PE

- **Milestone 1:** `-S` emits ILAsm-compatible text, and `ilasm` produces the assembly. There are no objects or linker yet.
- **Phase 2:** the MC object writer and `msil-ld` write the PE and ECMA-335 metadata directly, in C++. `ilasm` stays useful as a test oracle: the `.il` output must round-trip through `ilasm` and `ildasm`.

### 5.6 Shipping multiple targets

The ABI is baked in per target, so there is one assembly per target. Package several targets with NuGet's `runtimes/<rid>/lib/<tfm>/` layout and let .NET's own resolution (`deps.json`) pick the right one. A single multi-target ("fat") assembly is a non-goal.

---

## 6. libc

The libc is a target choice, as in native C (section 3).

- **Host libcs (glibc, musl, UCRT/MSVCRT, libSystem):** the default is dynamic, exactly like a native build. libc functions are P/Invoked from the system's shared libc, with the variadic handling from 4.7. Headers come from that platform's sysroot.
- **Static libc:** possible when the libc can be built for our target and its OS layer can run without native code. For musl, that means replacing its syscall layer, because CIL cannot make syscalls directly. The practical options are P/Invoking the kernel interface through a tiny dynamic dependency (which defeats the point) or the portable layer below. So static host libcs are a later investigation, not a promise.
- **Portable .NET libc (`-unknown-dotnet`), deferred, not in the current plan:** a real libc compiled for our target, statically linked, with its OS layer written in C# over the BCL. Candidates are wasi-libc with a managed WASI preview1 layer (a small, documented interface), musl with a managed syscall shim, or picolibc. Revisit only once the host-libc targets are solid.
- **Runtime support assembly** (`MsilC.Runtime.dll`, C#): the shadow stack, TLS blocks, setjmp exception, thunk table, native `va_list` construction, and process startup (`Main(string[])` → `main(argc, argv)`). It multi-targets `net48` and `net8.0`, and later may be merged into each output assembly.

---

## 7. Runtime profiles: .NET Framework and modern .NET

### 7.1 Profile selection

The IL instructions we need (`calli unmanaged`, `localloc`, `cpblk`/`initblk`, `unaligned.`, `volatile.`, `tail.`, module initializers, RVA fields) have existed since CLR 2.0. A handful of library APIs differ between Framework and modern .NET, so codegen takes a profile flag, `-mdotnet=net48|net8.0`. The default is `net8.0`.

| Need | Modern .NET (net8.0) | .NET Framework (net48) |
|---|---|---|
| Native-callable function pointers | `[UnmanagedCallersOnly]` + `ldftn` | Kept-alive delegates + `Marshal.GetFunctionPointerForDelegate` |
| Unmanaged allocation | `NativeMemory` | `Marshal.AllocHGlobal` (or the libc's `malloc`) |
| Disable marshalling | `DisableRuntimeMarshalling` | Not needed for blittable signatures |
| Bit operations | `BitOperations` and `BinaryPrimitives` intrinsics | Inline IL sequences |
| 128-bit integers | Expanded by the legalizer either way | Same |
| Lazy library loading | `NativeLibrary` | `LoadLibrary`/`GetProcAddress` via kernel32 (Windows only) |
| Valid triples | All | Windows triples. Framework runs only on Windows. |

### 7.2 Mono and Unity

Mono and Unity are out of scope for the plan, but nothing in the net48 profile should preclude them. Worth a check in phase 4.

---

## 8. Toolchain and driver integration

- `llvm/lib/Target/MSIL/`: a WebAssembly-shaped target (ISel, ExplicitLocals, RegStackify, AsmPrinter, MC, object writer).
- `llvm/lib/TargetParser/Triple.cpp`: the `msil` object format. `llvm/lib/MC/TargetRegistry.cpp`: select the MSIL target by object format.
- `clang/lib/Basic/Targets/`: a thin wrapper over the native `TargetInfo` when the object format is `msil` (`__MSIL__`, no inline asm, `long double` = `double`).
- `clang/lib/CodeGen/Targets/MSIL.cpp`: ABI lowering, plus attaching C-level signatures to external declarations (2.3).
- `clang/lib/Driver/ToolChains/MSIL.{h,cpp}`: sysroot and libc selection, the default `-lc`, the runtime assembly, invoking `msil-ld`. Managed `.dll` inputs become references.
- `lld/MSIL/` (`msil-ld`): an lld flavor modeled on `lld/wasm`.
- Executables also get `foo.runtimeconfig.json` (modern .NET) or `foo.exe.config` (Framework). Later: an MSBuild SDK and NuGet packaging.

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
4. **Function-pointer cost**, especially on .NET Framework, where native-to-managed calls go through delegates.
5. **Fork maintenance.** Keep the diff to new directories plus registration hooks, as WebAssembly and llvm-mos do.
6. **Object format and PE writer effort** in phase 2.
7. **Debugging** needs Portable PDB generation (Windows PDB for Framework) from LLVM debug locations.
8. **Semantics gaps:** the CLR memory model vs C11 atomics edge cases, `fenv.h` rounding modes, and signals.

---

## 11. Phased plan

| Phase | Outcome |
|---|---|
| **0. Bootstrap** | Fork llvm-project and copy the WebAssembly target's skeleton into an experimental `MSIL` target: the `msil` object format in `Triple`, backend selection by object format, the clang `TargetInfo` wrapper, a driver stub. CI on Linux with the .NET SDK and ilasm, plus Windows CI for .NET Framework. |
| **1. Single file to running assembly** (first milestone) | `-S` to `.il`, assembled with ilasm, runs on modern .NET. |
| **2. Objects and linker** | CIL object format, `msil-ld`, direct PE writer, multiple TUs, archives, bitcode/LTO, `-shared` exports, assembly-to-assembly linking, P/Invoke resolution from native export tables. |
| **3. libc and runtime** | Runtime assembly, the glibc and UCRT host-libc triples (P/Invoke, variadic shims), the net48 profile, setjmp, TLS, atomics. Real programs run: zlib, Lua, SQLite. |
| **4. Conformance and performance** | GCC torture tests and llvm-test-suite per triple, benchmarks vs native clang, inliner tuning, full SIMD intrinsic coverage, debug info, musl and Darwin triples. |
| **5. Production toolchain** | MSBuild SDK and NuGet packaging (per-target `runtimes/<rid>` assemblies), 32-bit triples, static host libcs where viable. |

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

---

## 13. Open questions

1. Should Mono and Unity be an explicit target, or just "not precluded"?

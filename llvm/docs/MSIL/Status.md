# MSIL backend: status of Phase 1

State after milestone M12 (2026-10-21, branch `msil`, with upstream llvm-project main at
`64f50ffd4f1c` merged). Phase 1 (milestones M0-M10) is the "first milestone" of
[Design.md](Design.md) section 12: one C file compiled with `clang -S` to ILAsm text,
assembled with `ilasm`, run on .NET. M11 followed up the first upstream merge. M12, the first
milestone of Phase 2, fixed the backend items every Phase 2 path needs (aliases, weak
references, empty `asm` statements with operands, inline `__int128` division, shared frame slots
for structs passed by value) and gave the WSL legs a native Linux oracle and a private .NET
runtime; object files and `msil-ld` follow in M13-M23 (plan section 6). How to build and use it
is in [GettingStarted.md](GettingStarted.md); the decisions referred to as D1-D38 are in
section 3 of [ImplementationPlan.md](ImplementationPlan.md), and its section 12 records every
later amendment.

## 1. Design.md section 12 exit criteria

| Criterion | How Phase 1 meets it |
|---|---|
| About 25 small C programs compile at `-O0` and `-O2`, run, and match native clang's output, as a lit suite in CI | `msil/test/M1/11-programs/` has 20 programs (fib, sieve, quicksort over a global array, mergesort, n-queens, hanoi, matrix multiplication, crc32, a string table with relocations, a bytecode VM dispatching through a function-pointer table, fnv1a, string functions, bignum addition, life, fixed-point mandelbrot, collatz, a struct-heavy simulation, linked lists and binary trees in static pools, K&R code). With the struct and function-pointer programs of `08-fnptr/`, `09-structs/` and `10-calls/` and the other execution tests in `msil/test/M1/` (143 in all: 99 C programs, 43 IR files and one test that compiles a C program with `-g`), each test runs at `-O0` and `-O2` and its transcript (stdout and exit status) must equal the one a native clang build produces (on Windows linked with the compiler-rt builtins of the Visual Studio LLVM, for the `__int128` helpers a native build calls; for the Linux triple in WSL, a native Linux build linked on Windows and run in WSL, the E7 oracle of GettingStarted.md section 7); the gate also runs every C program of `msil/test/M1` at `-O1`, `-O3`, `-Os` and `-Oz` and every IR test that uses `%{msil_ll}` (40 of the 43) at `-O1` and `-O3` (the `more-levels` leg, on .NET 8); the three trap tests `01-int/trap-call-before-div.ll`, `01-int/trap-call-before-rem.ll` and `03-control/trap.ll`, like the `-g` test, have explicit RUN lines that pin `-O0` and `-O2` in every leg. The gate runs the suite on .NET 8 (tiered and with `DOTNET_TieredCompilation=0`), .NET 10, .NET Framework 4.8, and in WSL on a private .NET 10 for `x86_64-unknown-linux-gnu-msil` (tiered and optimizing JIT), assembles the programs with the Framework ilasm, the modern Windows ilasm and the Linux ilasm, and round-trips them through the modern ildasm and ilasm (Design.md 5.5). It also runs the upstream regression tests of the 36 upstream files the fork changes (about 300 lit tests and all of `TargetParserTests` and `MCTests`; the upstream-hooks check of GettingStarted.md section 7). CI (`.github/workflows/msil-ci.yml`) runs the same scripts as the local gate; it has a manual trigger only until the repository owner enables more |
| FileCheck tests on the emitted `.il` | 119 llc and MIR tests in `llvm/test/CodeGen/MSIL` (112 `.ll`, 7 `.mir`; both x86_64 triples, the aarch64 triples where the ABI differs, ilasm round trip where a module is complete), 20 clang tests in `clang/test/CodeGen/MSIL` (including `design-milestone1.c`, Design.md 12's literal command on fib), the driver tests in `clang/test/Driver/msil-toolchain.c` and unit tests in `llvm/unittests/Target/MSIL` |
| `ilverify` reports no stack-shape errors | replaced by the verification policy of section 4 (decision D21, approved by the user 2026-10-12), enforced in every gate |

## 2. What works

### Toolchain and output

- `clang --target=<msil triple> -S` and `llc` write ILAsm text; `ilasm -dll -pe64 -x64` (the
  .NET Framework 4.8 ilasm or the NuGet ilasm 10.0.12) assembles it. One C file (one LLVM module)
  is one assembly. Every optimization level works.
- The module references only `mscorlib` (D10), so one assembly runs on .NET 8, 9 and 10 and, for
  Windows triples, on .NET Framework 4.8 (as an `.exe`). The gate runs .NET 8, 10 and Framework
  4.8; .NET 9 was only covered by the planning experiments.
- C functions become static methods of `<Module>`: `public` for external linkage, `assembly`
  otherwise. A generated `'<msil>Main'(string[])` is the entry point. It calls `main(void)`, or
  `main(int, char **)` with a NULL-terminated `argv` built from
  `Environment.GetCommandLineArgs()` (`argv[0]` is the `.dll` path under `dotnet x.dll`); any
  other signature of `main` is an error (D27).
- The assembly is named after the source file's stem, or `-mllvm -msil-assembly-name=<name>`
  (D28).
- Unsupported input ends in an error that names the function or feature, with exit status 1,
  never a crash.
- `-mllvm` passes backend options from clang as llc takes them, and llc's stop points
  (`-stop-after=`, `-stop-before=`) work at every MSIL pass from `msil-isel` on: llc stops
  there, and the MIR it prints passes the machine verifier. The MIR of instruction selection
  reads back and compiles on with `-start-after=msil-isel` to the same IL as the full pipeline
  (GettingStarted.md section 8). Reading MIR back has three limits, all from upstream code;
  none of them changes the IL of a normal compile:
  - MIR with a floating-point infinity or NaN constant reads back at no stop point,
    `msil-isel` included: `CONST_F32` and `CONST_F64` print it as `float +inf`,
    `double -inf` or `double +qnan`, which the MIR parser rejects
    (`expected a floating point literal`), an upstream MIR printer and parser mismatch that
    WebAssembly shows too.
  - For a function that takes block addresses (computed goto), the IL of
    `-start-after=msil-isel` lacks the `@tmpN: // Address of block that was removed by CodeGen`
    labels that the full pipeline prints; every other line is the same. IndirectBrExpand, which
    runs before instruction selection, replaces each `blockaddress` with the block's number, so
    the IR inside the MIR takes no block address any more, while the full pipeline still prints
    an unused label for each block whose address the input IR took.
  - MIR printed at `branch-folder` or a later stop point does not read back yet when a `switch`
    became a jump table (`llc -run-pass=none` asserts `Cannot create an empty jump table!`:
    BranchFolder empties the table the MSIL `switch` does not reference, an upstream MIR
    printer and parser issue that WebAssembly shows too).

### Triples (D6)

| Triple | clang | llc | Executed by the gate |
|---|---|---|---|
| `x86_64-pc-windows-msvc-msil` | yes | yes | Windows: .NET 8 (tiered and optimizing JIT), .NET 10, .NET Framework 4.8 |
| `x86_64-unknown-linux-gnu-msil` | yes | yes | WSL Ubuntu: a private .NET 10 (tiered and optimizing JIT) against a native Linux build (the E7 oracle), also assembled by the Linux ilasm |
| `x86_64-unknown-linux-musl-msil`, `x86_64-w64-windows-gnu-msil` | yes | yes | FileCheck and ilasm only |
| `aarch64-unknown-linux-gnu-msil`, `aarch64-unknown-linux-musl-msil`, `aarch64-pc-windows-msvc-msil`, `aarch64-w64-windows-gnu-msil` | yes (native AArch64 DataLayout and ABI) | yes | FileCheck and ilasm (`-pe64 -arm64`) only |
| `aarch64-apple-darwin*-msil` | `unknown target triple` (phase 4) | accepted, untested | no |
| `i686-*-msil`, `x86_64-*-gnux32-msil` | `unknown target triple` (i686: phase 5) | `unsupported target triple ... (Phase 1 supports x86_64 and aarch64 with 64-bit pointers)` | no |
| `x86_64-pc-win32-msil` (normalizes to `x86_64-pc-windows-msil`) | `unknown target triple`: Windows MSIL triples name `msvc` or `gnu` | accepted as windows-msvc | no |

clang uses the native target's ABI, data layout and predefines and adds `__MSIL__`; `long double`
is `double` on every MSIL triple, which differs from native on Linux and x86_64 MinGW (section 3).
Linux MSIL triples do not define `__ELF__` (section 6, item 5).

On the aarch64 triples clang classifies aggregates as AAPCS64 does natively. Since upstream
`de7794fcf781` (merged with `64f50ffd4f1c`), a non-HFA aggregate argument smaller than 8 bytes is
passed as an integer of its exact size (`i8` ... `i56`, formerly `i64`), so 1-4-byte structs are
`int32` IL parameters and 5-7-byte ones stay `native int`; results did not change. Both sides of
every call come from the same clang and no Phase-1 import takes an aggregate, so nothing can
disagree. `clang/test/CodeGen/MSIL/aarch64-aggregate-abi.c` and
`llvm/test/CodeGen/MSIL/struct-args-aapcs64.ll` pin these shapes on all four aarch64 triples and
assemble them with `ilasm -arm64`; execution stays x86_64-only (D6).

### C and LLVM IR

- Integers of every width: 32-bit values are `int32`; pointers and 64-bit integers are
  `native int` (D2; `int64` is never emitted). `__int128` arithmetic, shifts and compares are
  expanded inline, and since M12 so are `__int128` division and remainder by any divisor, the
  conversions between `__int128` and `float` or `double`, saturating ones included, and the
  signed and unsigned conversions from `__bf16` to `__int128` (of a `__bf16` read from memory:
  a `__bf16` parameter or return value itself still names a helper, section 3) (LLVM's
  ExpandIRInsts, as NVPTX and AMDGPU use it; plan section 12, M12 rows on Q36), so no
  compiler-rt helper is called. The expansion is large: a division or remainder by a variable
  is about 600-1,050 IL instructions per site (626-761 at `-O2` and 783-955 at `-O0` for a bare
  one; up to 1,053 in backlog recon 2.2's test functions), one by a constant that is not a
  power of two about 460-650 (exact division too, which no longer multiplies by the inverse),
  one by a power of two a few dozen, and a conversion to floating point 350-570. Phase 3 may
  replace it by one helper per assembly or by `Int128`/`UInt128` behind a switch (P3-B).
- `float` and `double`: arithmetic, remainder (`frem` is CIL `rem`), all comparisons NaN-exact,
  every integer conversion correctly rounded on every runtime (D17, and a round-to-odd step for
  int64 to float32), saturating conversions, `llvm.minimum`/`maximum`/`minimumnum`/`maximumnum`
  as compares and selects (`fmin` and `fmax` are math-library calls), and constrained (strict)
  operations as their plain forms. `fabs`, `copysign` and the `isnan` family use inline integer
  expansions.
- Control flow: branches, loops, `switch` (CIL `switch` for jump tables up to 4096 entries,
  compare chains otherwise; D35), computed `goto`, irreducible control flow, traps
  (`ldnull; throw`).
- Calls: direct calls, calls through function pointers (managed `calli`), K&R definitions and
  calls without a prototype (MSILAddMissingPrototypes), calls through a mismatched function type
  (MSILFixFunctionBitcasts wrappers), structs passed by value (copied by the caller) and returned
  through `sret`, on the SysV and Win64 ABIs (and the AArch64 ABIs on the aarch64 triples, FileCheck
  and ilasm only). The caller's copies share frame slots (M12): the K-th by-value struct argument
  of every call in a function is copied, before each call, into the function's K-th byval slot,
  which is as large and as aligned as the largest such argument, so ten calls of a 64-byte struct
  need 64 bytes of frame, not 640, and two by-value arguments of one call never share a slot.
  Only the SysV x86_64 triples pass structs `byval` (in memory); the Win64 and AArch64 ABIs pass
  large structs by reference to a copy that clang makes.
- Memory: loads and stores of every scalar width, `volatile` and unaligned accesses (`volatile.`
  and `unaligned.` prefixes), `memcpy`/`memset`/`memmove` (`cpblk`, `initblk`, and
  `System.Buffer::MemoryCopy` when the copy may overlap or the length is variable; D15). Static
  frames: one `localloc` per function for address-taken locals, local arrays and over-aligned
  objects (D13).
- Global variables: initialized and zero-initialized data, string literals, pointers to globals and
  functions in initializers (relocations), constructors (`__attribute__((constructor))`) in
  priority order, linkonce/weak/comdat definitions as ordinary ones (D12, D26). Everything lives
  in one unmanaged data segment that `<Module>::.cctor` allocates and fills.
- Unused inline functions: a `linkonce` or `linkonce_odr` function definition that nothing uses
  outside its own body is dropped before it is compiled or diagnosed, and so are the local
  (`static`) functions and other such definitions that only dropped bodies used (M12). A header's
  inline functions that the program does not call therefore cost nothing: the UCRT `stdio.h`
  defines `sprintf` and its helpers inline, and at `-O0` clang emits them, with calls of
  functions that are not imports. Global variables are never dropped, and neither is a local
  function that nothing referenced in the first place.
- Function addresses: each function whose address is used gets one `'<msil>fn_<name>'` field that
  the module initializer sets with `ldftn`, so a function has a single address on every runtime
  (section 12, M9 row).
- Aliases (M12): `__attribute__((alias("f")))`, `static`, `weak` and array aliases, and the
  aliases that rustc makes of identical functions. Every use of an alias of a defined function,
  or of a global variable at any constant offset, becomes its aliasee, through chains of aliases
  and in code, initializers and the constructor list alike, so `&alias == &aliasee`.
  A function alias with external, `weak` or `weak_odr` linkage also gets a `public` forwarding
  method under its own name, which calls the aliasee (a plain `call`, never `jmp`, which the
  verifiers reject), so the name stays a method and `main` may be an alias. Other aliases (an
  offset into a function, a function-typed alias of a variable, an alias of an ifunc, a
  `thread_local` alias of a variable that is not thread-local, such as
  `extern __thread int a __attribute__((alias("g")))` on a plain `int g`) and ifuncs stay errors
  (section 3).
- Undefined weak symbols (M12): a `__attribute__((weak))` declaration that nothing defines is
  null, as a native static link makes it: `if (f)`, `&weak_var == 0` and initializers that take
  its address see null. A direct call of such a function runs a trap stub
  `'<msil>undef_<name>'` (`ldnull`, `throw`: an unhandled `System.NullReferenceException`,
  where a native call of address 0 crashes), and so does a constructor entry that names it. A
  weak declaration of a Phase-1 import (`puts`, a math function, an `"msil-import-module"`
  declaration) stays a P/Invoke, and a weak thread-local declaration
  (`extern __thread int v __attribute__((weak))`) keeps the `thread_local` error (section 3).
- Aliases, undefined weak symbols, the dropped inline functions and the section error of
  section 3 are rules for a whole program, which every `-S` and llc output is. CIL objects
  (from M19) will keep aliases and weak symbols as object symbols for `msil-ld` to resolve
  (plan 5.8.10).
- Fences: `atomic_thread_fence` calls `Interlocked.MemoryBarrier`; `atomic_signal_fence` and the
  empty barrier `asm volatile("" ::: "memory")` are compiler-only barriers (D36).
- Empty `asm` statements with operands (M12): the `DoNotOptimize` idioms of benchmark harnesses
  and Rust's `black_box`, such as `asm volatile("" : : "r"(v) : "memory")`, with inputs of any
  kind (`"r"`, `"m"`, `"r,m"`, `"g"`, `"i"`), read-write operands (`"+r"`, `"+m"`, `"+rm"`;
  `"+r"` on a `_Bool` and on a local register variable such as `register int x asm("eax")`
  included), outputs tied to an input (`"=r"(x) : "0"(y)`, also with alternatives, as `"=r,r"`
  with `"0,0"`) and memory outputs (`"=m"`). clang emits them exactly as for the native triple,
  and the backend turns each into the compiler barrier above plus a use of every input that
  keeps its computation alive (`llvm.fake.use`, for integer inputs of up to 64 bits, pointers,
  `float` and `double`). An `asm` with instructions, an output not tied to an input (`"=r"`,
  `"=rm"`), a condition-flag output (`"=@ccz"` on x86, `"=@cceq"` on AArch64; read-write
  `"+@ccz"` and tied ones too, because native code sets it from the flags after the statement),
  an output in memory tied to an input of another floating-point type (`"=rm"` on a `float`
  with a `double` `"0"`, which native x86 cannot compile either), `asm goto` and Microsoft
  `__asm` stay errors (section 3). Each accepted output holds what native x86 code leaves in it:
  - A register output tied to an input is that input: zero-extended for a `_Bool` `"+r"`, and
    converted between pointer and integer for a pointer tied to an integer of its width, as the
    native register holds it. A tie written with alternatives (`"=r,r"` with `"0,0"`) is read
    from the first alternative that has one.
  - An output that allows a register and memory, tied to an input (`"=rm"`, `"=g"` or `"=X"`
    with `"0"`, read-write `"+rm"`; also `"=r"` on a non-scalar such as `_Complex int`), which
    clang passes in memory, receives its input after the statement, as native code does when it
    takes the register. A narrower output gets the input's low bytes (a `char` output tied to an
    `int` input gets its low byte).
  - With alternatives that include memory (`"=r,m"` or `"=m,r"` with `"0,0"`, `"+r,m"`), native
    x86 takes the memory alternative, so the memory keeps its contents and the output its old
    value, as for a memory output tied to an input (`"=m"` with `"0"`). On the AArch64 triples
    `"Q"` is a memory alternative too: `"=r,Q"` with `"0,0"` keeps the memory's contents, as
    native AArch64 code does.
- Fixed-length generic vector types are scalarized by the legalizer (scalable vectors cannot be
  and are an error, section 3). `-g` is accepted, but no debug information is written.

### The Phase-1 import table (D14)

There is no linker in Phase 1, so a function that is not defined in the module can only be a
P/Invoke from this table; any other undefined function is a compile error. Every declaration is
`pinvokeimpl("<library>" cdecl) ... preservesig` with a blittable scalar signature.

| Triple (x86_64 and aarch64) | `puts`, `putchar` | C math library |
|---|---|---|
| `*-linux-gnu-msil` | `libc` | `libm.so.6` (glibc keeps libm separate; CoreCLR maps only `libc`) |
| `*-linux-musl-msil` | `libc` | `libc` |
| `*-windows-msvc-msil` | `ucrtbase` | `ucrtbase`: every function below except `frexpf`, `ldexpf` and `hypotf`, which the UCRT implements inline in its headers (`frexpf` and `ldexpf` from `llvm.frexp`/`llvm.ldexp` are computed in double) |
| `*-windows-gnu-msil` | `msvcrt` | `msvcrt`: only `sqrt`, `floor`, `ceil`, `fmod`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh`, `exp`, `log`, `log10`, `pow`, `ldexp`, `frexp`, `modf` and their `f` variants except `ldexpf` and `frexpf` (what `msvcrt.dll` exports) |

The math functions are `sqrt`, `cbrt`, `floor`, `ceil`, `trunc`, `round`, `rint`, `nearbyint`,
`fmod`, `remainder`, `fma`, `fmin`, `fmax`, `fdim`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`,
`atan2`, `sinh`, `cosh`, `tanh`, `exp`, `exp2`, `expm1`, `log`, `log2`, `log10`, `log1p`, `pow`,
`ldexp`, `frexp`, `modf`, `hypot` and their `f` variants. In LLVM IR, a function declaration with
the attribute `"msil-import-module"="<library>"` (and optionally `"msil-import-name"="<entry>"`)
is imported from that library.

## 3. What Phase 1 does not do

| Feature | Phase 1 | Planned |
|---|---|---|
| Object files (`-c`), linking several files, archives, LTO | driver error: `emitting object files is not yet supported for MSIL target ...` | phase 2 (P2-A, P2-B) |
| Any other external function (`malloc`, `printf`, `exit`, `qsort`, ...) or external variable | error: `... is not a Phase-1 import`, `external global variable ... (phase 2 linker)`; a `weak` declaration that nothing defines is null instead (section 2) | phase 2: `msil-ld` resolves symbols against native libraries |
| Variadic functions: definitions and calls | error | phase 3 (Design.md 4.7) |
| The `va_arg` instruction in LLVM IR (other front ends, hand-written IR; clang expands C's `va_arg` itself) | error: `the va_arg instruction is not supported for MSIL yet (phase 3 variadics)`. One of a struct or array type is reported before instruction selection, which crashed on it (as it does natively), and its function gets no method (M12) | phase 3 (Design.md 4.7) |
| `thread_local` / `_Thread_local` | error: `thread-local variable @<name> is not supported for MSIL yet`, also for a `weak` declaration that nothing defines (which is not made null, unlike other weak declarations) | phase 3 (Design.md 4.4) |
| Dynamic `alloca`, VLAs, `stacksave`/`stackrestore`, `__builtin_frame_address` | error (`... phase 3 shadow stack`) | phase 3 (Design.md 4.2) |
| `setjmp`/`longjmp` | error | phase 3 (Design.md 4.6) |
| Atomics (`_Atomic`, `__atomic_*`, `__sync_*`) | error (only fences work) | phase 3 |
| Global destructors, `atexit` | error | phase 3 (Design.md 4.1) |
| `long double` | is `double` (8 bytes, 53-bit mantissa) on every MSIL triple, not an error. Differs from native on x86_64 Linux (glibc and musl) and x86_64 MinGW (`x86_64-w64-windows-gnu`), where it is the 80-bit x87 format in 16 bytes, and on aarch64 Linux, where it is IEEE binary128 in 16 bytes; matches native on windows-msvc (x86_64 and aarch64) and `aarch64-w64-windows-gnu` | permanent (Design.md 3; section 6, item 14) |
| Conversions between floating point and `__int128` under strict floating point (constrained intrinsics: `-ffp-model=strict`, `#pragma STDC FENV_ACCESS ON`), conversions between `_Float16` and `__int128` and from `__int128` to `__bf16` (helpers `__extendhfsf2`, `__truncsfhf2`, `__extendbfsf2`, `__truncsfbf2`), `_Float16`/`__bf16` arithmetic, `__bf16` parameters, call arguments and return values even without arithmetic (`__truncsfbf2`, `__extendbfsf2`; `_Float16` ones compile), C `_Complex` multiplication and division | error naming the runtime helper (`__fixdfti`, `__floattidf`, `__extendhfsf2`, `__muldc3`, ...). `__int128` division, the plain conversions and those from `__bf16` to `__int128` are inline since M12 (section 2) | phase 3: BCL types where available, helpers in the output assembly otherwise |
| `__float128`, `x86_fp80`, rounding-mode changes, floating-point environment access | error (CIL rounds to nearest and has no FP exceptions) | |
| Inline and module-level assembly, other than empty `asm` statements with or without operands (section 2): instructions, outputs not tied to an input, condition-flag outputs (`"=@cc<cond>"`, `"+@cc<cond>"`, tied or not), an output in memory tied to an input of another floating-point type, `asm goto`, Microsoft `__asm`, file-scope `asm` | error: clang's `inline assembly is not supported for the MSIL target '...'` (`Microsoft-style inline assembly ...`, `file-scope assembly ...`), llc's `inline assembly is not supported for MSIL` | never (Design.md 3) |
| `section` on a global variable (`__attribute__((section(...)))`, `__declspec(allocate(...))`), such as a constructor registered in `.init_array` or `.CRT$XCU` | error: `global variable @<name> in section "<section>" is not supported for MSIL yet (phase 2 CIL objects)`, one per variable in module order. Before M12 the section was dropped silently, so such a constructor never ran. A section on a function is ignored, and clang's own sectioned globals (`llvm.metadata`) are not affected | phase 2: from M20, whole-program output lays out named sections as a one-object link does |
| Aliases other than those of section 2 (an offset into a function, a function-typed alias of a variable, an alias of an ifunc, a `thread_local` alias of a variable that is not thread-local) and ifuncs (from LLVM IR only: clang ignores `__attribute__((ifunc(...)))` on the MSIL triples with an `unknown attribute` warning) | error: `alias @<name> is not supported for MSIL`, `ifunc @<name> is not supported for MSIL` | phase 2: CIL objects (M19) keep aliases as object symbols for `msil-ld` (plan 5.8.10); ifuncs stay errors |
| Function pointers passed to native code (callbacks) | function pointer values are managed; no Phase-1 import takes a callback | phase 3 thunks (Design.md 4.5) |
| Exports (`-shared`), assembly-to-assembly linking, structs by value in P/Invoke signatures | no | phase 2 (P2-C), Design.md 2.3 |
| BCL intrinsics (`BitOperations`, `Math`, `MathF`, `Vector128`, ...) | none: bit operations, `fabs` and `copysign` use inline expansions, math calls are P/Invokes (D16) | phase 3 codegen switches |
| The codegen switches for newer .NET APIs of Design.md 7.1 (`DisableRuntimeMarshalling`, `Math.FusedMultiplyAdd`, `UnmanagedCallersOnly` thunks, ...) | none exist; Phase-1 output already is what their defaults will give: code that runs on .NET Framework 4.8 and modern .NET alike | phase 3 (P3-A) |
| Architecture-specific intrinsics (`llvm.x86.*`, `llvm.aarch64.*` from `immintrin.h`, `arm_neon.h`) | error naming the intrinsic: `intrinsic '<name>' is not supported for MSIL` (`llvm.x86.sse42.crc32.32.32` from `_mm_crc32_u32`, `llvm.x86.rdtsc` from `__rdtsc`, ...). Intrinsics that clang lowers to generic IR (`_mm_add_ps`, `_mm_popcnt_u32`, ...) still compile | phase 4 SIMD (Design.md 3.2) |
| SVE and SME scalable vector types (`__SVInt32_t`, `svint32_t`, `__SVCount_t`; in IR `<vscale x N x T>`, `target("aarch64.svcount")`, `llvm.vscale`), reachable on the aarch64 MSIL triples with `-march=...+sve` or `__attribute__((target("sve")))` | error naming the type or intrinsic: `scalable vector type '<vscale x 4 x i32>' is not supported for MSIL` | phase 4 SIMD (Design.md 3.2) |
| Debug information (Portable PDB) | `-g` accepted, nothing emitted | phase 4 |
| Darwin and i686 triples | unknown target triple | phases 4 and 5 |
| Stack protector (`-fstack-protector*`, `/GS`), control-flow guard, patchable function entries | ignored with a warning (clang-cl's implicit `/GS` is not passed) | |
| Basic block sections, late function splitting, basic block address map, static data partitioning (`-fbasic-block-sections=`, `-fsplit-machine-functions`, `-fbasic-block-address-map`, `-fpartition-static-data-sections`) | `-fsplit-machine-functions`, `-fbasic-block-sections=all` and `=list=`, `-fbasic-block-address-map` and `-fpartition-static-data-sections` are a driver error (`unsupported option '...' for target '...'`), because MSIL is not ELF; `-fbasic-block-sections=none` is accepted, and `-fbasic-block-sections=labels` (a deprecated alias) passes the driver as `-fbasic-block-address-map` and ends in the backend's warning `basic block address map is not supported for MSIL; ignored`. Passed to cc1 or llc directly (`-fsplit-machine-functions`, `-mllvm -function-splitting=`, `--basic-block-sections=`, ...), the options are ignored, the address map with that warning: the IL is the same as without them | never: CIL methods have no sections |

### Limits

- An IL identifier is at most 1023 bytes, because both ilasms reject longer names; a longer C
  name is a compile error.
- `memset` with a variable length is limited to 4 GiB (`initblk` takes a 32-bit size).
- ilasm sets the quiet bit of float32 signaling-NaN constants (`ldc.r4`), with both ilasm versions;
  float64 constants are exact. This lasts until the phase-2 PE writer.
- A prototype-less imported function whose address is used but which is never called directly
  cannot get a signature and is an error; whether the optimizer turned the indirect call into a
  direct one decides, so such code can fail at `-O0` and compile at `-O2`. Declare imports with a
  prototype (section 12, M9 row).
- On Linux, as for native programs, only the low 8 bits of the exit status survive.

## 4. Verification policy

Design.md 12 asks that "`ilverify` reports no stack-shape errors". ILVerify alone cannot check that:
it stops at the first error of a method and cannot verify any method that contains `calli`. The
user approved this replacement on 2026-10-12 (decision D21, and its refinement in plan section 12,
rows 'D21 and M0.b item (7) refined' and the user confirmation after it). A module passes when all
of these are clean:

1. **MSILStackCheck**, the last machine pass of the backend, always on: it recomputes the stack
   depth before and after every instruction, checks that every branch and join agrees on it and
   that no block falls off the end of a method, and computes the exact `.maxstack`. A violation is
   an internal compiler error, never a module.
2. **jitall** (`msil/utils/msil-jitall.py`): every method is JIT-compiled with
   `RuntimeHelpers.PrepareMethod` on the runtime under test (in WSL for Linux triples), under a
   timeout and a memory cap. `main` does not run; `<Module>::.cctor` does, as it would before
   `main`.
3. **ILVerify** (`dotnet-ilverify` 10.0.12) with the allow-list `msil/utils/ilverify-allowed.txt`:
   `Unverifiable` (localloc, cpblk, initblk), `InitLocals` (`.locals` without `init`), `StackByRef`
   only as `[found Native Int] Expected ByRef on the stack.`, and `UnmanagedPointer`. Every other
   code fails.
4. **PEVerify** (Windows): every message fails except the unverifiable-by-design ones and exactly
   `[found Native Int][expected unmanaged pointer] Unexpected type on the stack.` (the native-int
   addresses passed to `System.Buffer::MemoryCopy`, D15). Two shapes that D12 and D2 always
   produce are notes only when the second pass runs: the RVA image copy in `<Module>::.cctor`
   (`Cannot modify an imaged based (RVA) static`, at that exact `ldsflda`) and a function address
   used as a `native int` (`ldftn`).
5. **A neutralized second pass** (`msil-verify.py`): an allowed construct hides the errors behind
   it, so the assembly is disassembled with ildasm, every allowed construct is replaced by
   something verifiable with the same stack effect (`localloc`, `ldftn` and the image `ldsflda`
   become `native int` zeros; `cpblk`, `initblk`, `ldind`, `stind` and `calli` become calls of
   checking stubs), reassembled, and verified again by ILVerify and PEVerify with no allowance but
   the `MemoryCopy` message.

Stack *types* are checked only by the verifiers (the JIT accepts most type errors silently), and
mixing of `native int` and `int64` only by PEVerify, so that check runs on Windows. Every gate
passes `--param msil_require_verify=1`, which makes a missing ILVerify, ildasm, jitall, PEVerify
(Windows), ilasm or runtime an error instead of a skipped step (D38). On top of verification, every
program runs and its transcript is compared with the native build's, under Tier-0 and fully
optimizing JIT.

## 5. Runtime differences

What differs between the runtimes Phase 1 targets, and how the backend deals with it:

| Behaviour | .NET 8 | .NET 10 | .NET Framework 4.8 | Phase 1 |
|---|---|---|---|---|
| `conv.r.un` of a 64-bit value | rounds twice | correct | rounds twice | never emitted for 64-bit sources; unsigned 64-bit to FP uses a branch-free sequence of signed conversions (D17) |
| `conv.r4` of an int64 that the JIT constant-folds | correct | correct | folded as `(float)(double)x`, rounds twice | the value is first rounded to odd at bit 11, so both paths give the correctly rounded float |
| Floating point to integer out of range (C undefined behaviour) | x86 "indefinite" value | saturates | indefinite | programs must not depend on it; `llvm.fptosi.sat`/`fptoui.sat` are lowered explicitly |
| Alignment of a constant-size `localloc` of 32 bytes or less | only 8 for 16 and 32 bytes in Tier-0 code, and for 8 bytes in optimized code | same as .NET 8 | only 8 for 8 to 32 bytes | frames assume 8 and realign when an object needs more, over-allocating `MaxAlign - 8` bytes (D13) |
| `localloc` zeroed under `.locals init` | yes | yes | no | `.locals` is never `init`; C does not expect zeroed frames |
| Two `ldftn` of one function in different methods | equal | equal | can differ (precode vs. compiled code) | one address field per function, set once in `<Module>::.cctor` (M9) |
| Overlapping constant-size `cpblk` | not what `memmove` gives | same as .NET 8 | a third result | `cpblk` only for `memcpy`, where overlap is undefined; `memmove` uses `Buffer.MemoryCopy` |
| Inconsistent stack depth at a join | accepted | the JIT hung and grew past 40 GB | not measured | MSILStackCheck rejects such code at compile time |
| `Math.Min(-0.0, +0.0)` | -0.0 | -0.0 | +0.0 | `Math.Min`/`Max` are not used; min/max are compares and selects (D16) |
| Main-thread stack on Windows | 1.5 MB (the reserve of `dotnet.exe`) | same | 4 MB (ilasm's default for a 64-bit `.exe`; `ilasm -stack=` sets it) | see below |

- **Deep recursion.** `dotnet x.dll` on Windows runs `main` on the host's main thread, whose
  stack is 1.5 MB, while native Linux programs get 8 MB by default (`ulimit -s`, which the Linux
  `dotnet` host's main thread also uses), and a CIL frame (the `localloc` frame plus the JIT's own
  frame) is larger than the native one, at `-O0` especially: about 19,000 frames of an
  `-O0`-shaped recursion fill the .NET 8 main thread on Windows (plan M9.d, review experiment
  `recon/artifacts/review-cil/deep.il`). A stack overflow terminates the .NET process and cannot be
  caught, so a program that recurses deeply natively may fail under .NET.
- **Traps and undefined behaviour** surface as .NET exceptions where native code gets a signal: a
  trap is an unhandled `System.NullReferenceException`, integer division by zero a
  `System.DivideByZeroException`. On Windows an unhandled exception exits with `0xE0434352`.
- **Tiered compilation.** Short programs run almost entirely as Tier-0 code, which is why the
  gate repeats the suite with `DOTNET_TieredCompilation=0`; no difference in results is known.

## 6. Design.md updates

The 14 Design.md updates this section proposed were applied to [Design.md](Design.md) on
2026-10-21, with the user's approval, together with the Phase 2 plan, the Phase 2 decisions
(recon `p2/decision-brief.md` section 3) and the consistency fixes of recon `p2/backlog.md`
section 7. Item 4 was amended: a `DllImport` names the library as a native link records it (the
ELF `DT_SONAME`, the exact Windows DLL file name), always `ExactSpelling` and `CDecl`, never a bare
name and never `libc` (Design.md 5.3). Where each item went, for the references above:

| Items | Design.md |
|---|---|
| 1, 2, 3, 4, 8, 13 | the "As built (Phase 1)" note at the end of section 12; item 4 also 5.3, item 8 also 4.5 |
| 5, 10, 11, 14 | section 3 (predefines, inline assembly, triple table, `long double`) |
| 6 | 4.2 |
| 7 | section 10, risk 9 |
| 9 | 2.3 |
| 12 | 5.5 |

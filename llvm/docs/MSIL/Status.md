# MSIL backend: status of Phase 1

State after milestone M11 (2026-10-20, branch `msil`, with upstream llvm-project main at
`64f50ffd4f1c` merged). Phase 1 (milestones M0-M10) is the "first milestone" of
[Design.md](Design.md) section 12: one C file compiled with `clang -S` to ILAsm text,
assembled with `ilasm`, run on .NET. How to build and use it is in
[GettingStarted.md](GettingStarted.md); the decisions referred to as D1-D38 are in section 3 of
[ImplementationPlan.md](ImplementationPlan.md), and its section 12 records every later amendment.

## 1. Design.md section 12 exit criteria

| Criterion | How Phase 1 meets it |
|---|---|
| About 25 small C programs compile at `-O0` and `-O2`, run, and match native clang's output, as a lit suite in CI | `msil/test/M1/11-programs/` has 20 programs (fib, sieve, quicksort over a global array, mergesort, n-queens, hanoi, matrix multiplication, crc32, a string table with relocations, a bytecode VM dispatching through a function-pointer table, fnv1a, string functions, bignum addition, life, fixed-point mandelbrot, collatz, a struct-heavy simulation, linked lists and binary trees in static pools, K&R code). With the struct and function-pointer programs of `08-fnptr/`, `09-structs/` and `10-calls/` and the other execution tests in `msil/test/M1/` (136 in all: 93 C programs, 42 IR files and one test that compiles a C program with `-g`), each test runs at `-O0` and `-O2` and its transcript (stdout and exit status) must equal the one a native clang build produces; the gate also runs every C program of `msil/test/M1` at `-O1`, `-O3`, `-Os` and `-Oz` and every IR test that uses `%{msil_ll}` (39 of the 42) at `-O1` and `-O3` (the `more-levels` leg, on .NET 8); the three trap tests `01-int/trap-call-before-div.ll`, `01-int/trap-call-before-rem.ll` and `03-control/trap.ll`, like the `-g` test, have explicit RUN lines that pin `-O0` and `-O2` in every leg. The gate runs the suite on .NET 8 (tiered and with `DOTNET_TieredCompilation=0`), .NET 10, .NET Framework 4.8, and in WSL on .NET 10 for `x86_64-unknown-linux-gnu-msil` (tiered and optimizing JIT), assembles the programs with the Framework ilasm, the modern Windows ilasm and the Linux ilasm, and round-trips them through the modern ildasm and ilasm (Design.md 5.5). It also runs the upstream regression tests of the 36 upstream files the fork changes (about 300 lit tests and all of `TargetParserTests` and `MCTests`; the upstream-hooks check of GettingStarted.md section 7). CI (`.github/workflows/msil-ci.yml`) runs the same scripts as the local gate; it has a manual trigger only until the repository owner enables more |
| FileCheck tests on the emitted `.il` | 111 llc and MIR tests in `llvm/test/CodeGen/MSIL` (104 `.ll`, 7 `.mir`; both x86_64 triples, the aarch64 triples where the ABI differs, ilasm round trip where a module is complete), 19 clang tests in `clang/test/CodeGen/MSIL` (including `design-milestone1.c`, Design.md 12's literal command on fib), the driver tests in `clang/test/Driver/msil-toolchain.c` and unit tests in `llvm/unittests/Target/MSIL` |
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
| `x86_64-unknown-linux-gnu-msil` | yes | yes | WSL Ubuntu: .NET 10 (tiered and optimizing JIT), also assembled by the Linux ilasm |
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
  expanded inline, and so are division by a power of two and exact division by a constant.
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
  and ilasm only).
- Memory: loads and stores of every scalar width, `volatile` and unaligned accesses (`volatile.`
  and `unaligned.` prefixes), `memcpy`/`memset`/`memmove` (`cpblk`, `initblk`, and
  `System.Buffer::MemoryCopy` when the copy may overlap or the length is variable; D15). Static
  frames: one `localloc` per function for address-taken locals, local arrays and over-aligned
  objects (D13).
- Global variables: initialized and zero-initialized data, string literals, pointers to globals and
  functions in initializers (relocations), constructors (`__attribute__((constructor))`) in
  priority order, linkonce/weak/comdat definitions as ordinary ones (D12, D26). Everything lives
  in one unmanaged data segment that `<Module>::.cctor` allocates and fills.
- Function addresses: each function whose address is used gets one `'<msil>fn_<name>'` field that
  the module initializer sets with `ldftn`, so a function has a single address on every runtime
  (section 12, M9 row).
- Fences: `atomic_thread_fence` calls `Interlocked.MemoryBarrier`; `atomic_signal_fence` and the
  empty barrier `asm volatile("" ::: "memory")` are compiler-only barriers (D36).
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
| Any other external function (`malloc`, `printf`, `exit`, `qsort`, ...) or external variable | error: `... is not a Phase-1 import`, `external global variable ... (phase 2 linker)` | phase 2: `msil-ld` resolves symbols against native libraries |
| Variadic functions: definitions and calls | error | phase 3 (Design.md 4.7) |
| `thread_local` / `_Thread_local` | error | phase 3 (Design.md 4.4) |
| Dynamic `alloca`, VLAs, `stacksave`/`stackrestore`, `__builtin_frame_address` | error (`... phase 3 shadow stack`) | phase 3 (Design.md 4.2) |
| `setjmp`/`longjmp` | error | phase 3 (Design.md 4.6) |
| Atomics (`_Atomic`, `__atomic_*`, `__sync_*`) | error (only fences work) | phase 3 |
| Global destructors, `atexit` | error | phase 3 (Design.md 4.1) |
| `long double` | is `double` (8 bytes, 53-bit mantissa) on every MSIL triple, not an error. Differs from native on x86_64 Linux (glibc and musl) and x86_64 MinGW (`x86_64-w64-windows-gnu`), where it is the 80-bit x87 format in 16 bytes, and on aarch64 Linux, where it is IEEE binary128 in 16 bytes; matches native on windows-msvc (x86_64 and aarch64) and `aarch64-w64-windows-gnu` | permanent (Design.md 3; section 6, item 14) |
| `__int128` division by a variable or by a constant that is neither a power of two nor exact, conversions between floating point and `__int128`, `_Float16`/`__bf16` arithmetic, C `_Complex` multiplication and division | error naming the runtime helper (`__divti3`, `__extendhfsf2`, `__muldc3`, ...) | phase 3: BCL types where available, helpers in the output assembly otherwise |
| `__float128`, `x86_fp80`, rounding-mode changes, floating-point environment access | error (CIL rounds to nearest and has no FP exceptions) | |
| Inline and module-level assembly (other than the empty barrier above) | error | never (Design.md 3) |
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
- Every `byval` argument gets its own copy in the caller's frame, so frames of functions with many
  struct-by-value calls are larger than native ones.
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

## 6. Proposed Design.md updates

Design.md is the user's document; agents do not edit it. These changes would make it match
Phase 1 as built. Each names the passage, the proposed change and the evidence.

1. **Section 12, `dotnet prog.dll` needs a runtimeconfig.** The milestone command `ilasm -dll
   prog.il`, then `dotnet prog.dll` does not run: without `prog.runtimeconfig.json` the host
   stops with "The library 'hostpolicy.dll' (Linux: 'libhostpolicy.so') required to execute the
   application was not found" and exit status `0x80008083` (131 in 8 bits) on both hosts (recon
   `tests-ci.md` E2; re-checked on Windows with .NET 8 on 2026-10-15 and in WSL Ubuntu on
   2026-10-16 with an `x86_64-unknown-linux-gnu-msil` build). Section 8 already says the
   compiler never writes the file. Proposed: "then `dotnet prog.dll`, with a
   `prog.runtimeconfig.json` written by the user or the build system (or `dotnet exec
   --runtimeconfig <file> prog.dll`)", and a pointer to GettingStarted.md section 5.
2. **Section 12, ilasm options.** `ilasm -dll prog.il` writes a 32-bit "AnyCPU" image (PE32,
   machine 0x14C), which a 32-bit runtime would load although the code assumes 64-bit pointers;
   the Framework ilasm's `-pe64` alone means Itanium (recon `cil-runtime.md` 1.3). Section 3 says
   the PE's machine type is set from the triple. Proposed: `ilasm -dll -pe64 -x64 prog.il`
   (`-pe64 -arm64` for aarch64 triples).
3. **Section 12, verification criterion.** Replace "`ilverify` reports no stack-shape errors
   (unverifiable pointer use is expected)" with the policy of section 4 above (D21 and its
   refinement, both approved by the user): MSILStackCheck, jitall, ILVerify with the allow-list
   and PEVerify on Windows, clean on the assembly and on a neutralized ildasm/ilasm copy. ILVerify
   stops at the first error per method and cannot verify `calli`, so the literal sentence cannot
   be checked on real C code.
4. **Section 12 "one hard-coded P/Invoke", sections 5.2 and 5.3.** Phase 1 has the import table of
   D14 (section 2 above), not one hard-coded `puts` and not "every undefined function becomes a
   P/Invoke": `puts`, `putchar` and the C math library, plus functions with the
   `msil-import-module` IR attribute; every other undefined function is a compile error, as
   5.2 step 4 makes it a link error. Library names are per triple; on glibc the math library is
   `libm.so.6`, because glibc keeps libm separate and CoreCLR maps only the bare name `libc`, so
   5.3's "names without prefixes or suffixes" does not hold for it. On Windows a function is
   imported only if the C runtime DLL exports it under that name (`msvcrt.dll` has no `hypot`,
   `cbrt`, `round`, ...; `ucrtbase.dll` no `frexpf`, `ldexpf`, `hypotf`), which is 5.2 step 2.3
   applied by hand.
5. **Section 3, predefines.** "Predefines ... are exactly the native ones by construction" does not
   hold for `__ELF__`: clang defines it only for the ELF object format, so Linux MSIL triples do
   not define it (plan section 10, decision 9: left undefined in Phase 1 and pinned by a test;
   revisit when glibc headers are used in phase 3). Proposed: "exactly the native ones, plus
   `__MSIL__` and minus `__ELF__` on Linux triples".
6. **Section 4.2, localloc alignment.** Add: a constant-size `localloc` of 32 bytes or less can be
   only 8-byte aligned on every runtime: .NET Framework 4.8 for 8 to 32 bytes, .NET 8 and .NET 10
   for 16 and 32 bytes in Tier-0 code and for 8 bytes in optimized code (review experiment
   `recon/artifacts/review-cil/align16.il`; D13 names Framework and .NET 8, and a re-run on
   2026-10-15 showed .NET 10 behaving like .NET 8). The frame therefore assumes 8-byte alignment;
   a frame with an object aligned above 8 bytes allocates `MaxAlign - 8` extra bytes and aligns
   its base up (D13). `.locals` is never `init`.
7. **Section 10 (risks) or 4.2, the main-thread stack.** Record the roughly 1.5 MB main-thread
   stack of `dotnet.exe` on Windows (native Linux programs: 8 MB by default; an ilasm-built `.exe`
   on .NET Framework: 4 MB) together with the larger CIL frames as a known difference for deep
   recursion: about 19,000 `-O0`-shaped frames overflow it, and a .NET stack overflow ends the
   process. A later phase could let the generated `Main` run `main` on a thread with a
   configurable stack size.
8. **Section 12 "Function pointers within the module (`ldftn` + `calli`)" and 4.5.** On .NET
   Framework, `ldftn` of the same method in two methods can give different values (precode vs.
   compiled code), which breaks C's function-pointer equality. Phase 1 therefore gives each
   address-taken function one `'<msil>fn_<name>'` field that the module initializer sets with
   `ldftn` once; code and relocations read the field (plan section 12, M9 row). That field is the
   per-function slot that 4.5's thunk table fills later.
9. **Section 2.3, pointer vs integer.** Add that every 64-bit integer, not only a pointer, is
   `native int` on 64-bit triples and `int64` is never emitted (D2): PEVerify rejects mixing the
   two, and one CIL type per register class keeps the stack types exact.
10. **Section 3, inline assembly.** "Rejects inline assembly" has one exception: an empty asm
    statement (`asm volatile("" ::: "memory")`, a common compiler barrier) is accepted and becomes
    a compiler-only fence (plan section 12, M1.d row; D36).
11. **Section 3, triple table.** Mark `aarch64-apple-darwin-msil` as later (phase 4), as
    `i686-pc-windows-msvc-msil` is: clang rejects Darwin and i686 MSIL triples until the
    `UserLabelPrefix` rework (D6).
12. **Section 5.5, Milestone 1.** Note the two ilasm limits Phase 1 lives with until the phase-2 PE
    writer: identifiers of at most 1023 bytes (longer C names are a compile error), and float32
    signaling-NaN constants come out quiet.
13. **Section 12, generated `Main`.** Record its contract (D27): `main(void)` or
    `main(int, char **)`; `argv` is NULL-terminated and `argv[0]` is the assembly path (what
    `Environment.GetCommandLineArgs()` returns under `dotnet x.dll`), not the native program path;
    any other `main` signature is an error.
14. **Section 3, `long double`.** "On glibc x86-64 that differs from native" names one case of
    several. `long double` is `double` (8 bytes, `__LDBL_MANT_DIG__` 53) on every MSIL triple;
    natively it is the 80-bit x87 format in 16 bytes on x86-64 Linux (glibc and musl) and x86-64
    MinGW, and IEEE binary128 in 16 bytes on AArch64 Linux (glibc and musl); only windows-msvc
    (x86-64 and AArch64) and AArch64 windows-gnu match (`clang -ffreestanding -E -dM`, native vs.
    `-msil` triple, 2026-10-16). Proposed: "On x86-64 Linux and MinGW (80-bit) and on AArch64
    Linux (binary128) that differs from native, so `long double` interop on those triples is a
    documented limitation; on windows-msvc and AArch64 windows-gnu `long double` is natively
    `double`."

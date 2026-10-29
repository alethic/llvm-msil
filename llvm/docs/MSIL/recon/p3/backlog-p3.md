# Phase 3 recon: backlog triage, Design.md and plan review, late-goal guard, upstreaming (`backlog-p3`)

Recon for the Phase 3 plan, 2026-10-28, at `cc4c79787d6a` (M23, branch `msil`). Scratch:
`C:\llvm-msil-scratch\p3\backlog-p3`. Supporting files (sources, scripts, trimmed logs, no binaries):
[backlog-p3-files/](backlog-p3-files/). Tools: the shared build's `clang`, `llc`, `msil-ld`, `llvm-nm`,
`llvm-cil-dump` and `ld.lld` (never rebuilt), the .NET Framework 4.8 ilasm, .NET 8 and .NET 10 on Windows,
.NET Framework 4.8, the private WSL runtimes 8.0.31 and 10.0.13 (`$HOME/.dotnet-msil`), the .NET SDK for one
C# probe, and read-only `gh`. Nothing was installed. No tracked file was changed.

**Second pass.** A first pass of this recon (same HEAD) ran while the sibling recons `lifecycle`,
`atomics-threads`, `fp-helpers` and `tests-gates-p3` were unfinished, and was cut short by a pause. This pass
re-checked every citation at HEAD (all hold), reconciled the report with those four reports (section 7
lists where the Phase 3 recons disagree with each other and a recommendation for each), triaged four section
12 rows the first pass missed (T32-T34), and turned three unverified risks into measurements: SysV `__int128`
by value (E9), vectored exception handlers under the CLR (E10), and `long double` through native variadic
calls (measured by fp-helpers F13). `hosted-libc` has no report yet. Where an item belongs to a sibling, this
report points to the sibling and does not derive it again. Where this report adds evidence or disagrees, it says so.

---

## 0. Summary

1. **Every recorded item is triaged** (section 2). That covers:
   - 34 plan section 12 items: rows with milestone `-` or naming P3/P3-A/P3-B/'Phase 3 input', grouped;
   - the 25 rows of Status.md section 3, its 6 known gaps and 13 limits;
   - the HANDOFF items: the Rust experiment, the fidelity note, the known limits after M20/M22, and the other open issues;
   - the open user questions.

   Each item has a verdict at HEAD, a size and a home, and most land in a sibling's area. **Seven items have no sibling owner and are missing from plan section 7** (2.6):
   - struct-by-value P/Invokes;
   - command-line fidelity;
   - TLS exports of libraries;
   - the spilled `'<msil>Data'` race (atomics-threads 9.3 now points at it, without a design);
   - `.preinit_array`;
   - the data-import retry in the application directory;
   - `__ELF__` (plan section 10 item 9).
2. **Struct-by-value P/Invokes need no clang metadata**, so Design.md 2.3 and section 8's planned
   `clang/lib/CodeGen/Targets/MSIL.cpp` are obsolete. Measured (E5, E9):
   - A hidden-pointer (`sret`) result is ABI-exact as a scalar P/Invoke whose first parameter is the pointer, on both x86-64 ABIs.
   - SysV's other cases work with explicit-layout value types: glibc `lldiv`'s `{i64, i64}` in RAX:RDX, a 24-byte MEMORY-class argument, and an `__int128` in registers.
   - **New (E9): the CLR places a 16-byte value-type argument at an 8-byte stack slot.** An `__int128`, or any 16-byte-aligned aggregate, that lands on the stack after an odd number of 8-byte stack arguments is read from the wrong address. `.pack 16` does not help.
   - The fix: msil-ld inserts one `int64` pad parameter, which it can compute from the SysV classification. That made the case exact on WSL .NET 8 and .NET 10.
   - `System.Int128` is refused by value in P/Invokes on .NET 8 and .NET 10 (`MarshalDirectiveException`), so it is no alternative.
   - fp-helpers 4.5 adds demand: `cexp`, `csqrt` and the other `<complex.h>` functions return `double _Complex` (SysV XMM0:XMM1; Win64 `sret`). msil-ld rejects them today.
   - Recommend P3-B.
3. **Command-line fidelity** (E2):
   - Under `dotnet`, `GetCommandLineW`/`A` return the host's command line.
   - UCRT's `__argc`/`__argv` are NULL under every host, Framework included.
   - On Linux, `program_invocation_name` is the dotnet host's path.
   - `main`'s argv is right everywhere.
   - `Environment.CommandLine` (mscorlib) gives the program's own line on .NET 8/10 and the process line on Framework, so one private IL helper fixes Windows executables.
4. **Retarget artifacts reproduce at HEAD** (E1): `_mm_pause`, MSVC `__fastfail`, Rust's `int $$0x29` asm, a
   dead sse4.2 dispatch path and `__builtin_cpu_supports` (`undefined symbol: __cpu_model`). helpers-switches A1
   has the fallbacks.
5. **Vectored exception handlers (new, E10).** Rust std on Windows registers one (`AddVectoredExceptionHandler(0, ...)`).
   - Measured with a delegate-thunk handler, Phase 3's default callback form:
     - Framework 4.8 calls it on every managed throw (code `0xE0434352`) and survives.
     - .NET 8 calls it on every managed throw and survives. On a **hardware** exception (a null dereference that becomes `NullReferenceException`) the process dies with `Fatal error. Invalid Program: attempted to call a UnmanagedCallersOnly method from managed code`, whether the handler was registered first or last.
     - .NET 10 never calls it for managed throws (the new managed exception handling). It dies on a hardware exception only if the handler is registered first.
   - Natively such a program crashes on that fault anyway, so only the failure message changes. Recommend allowing it as an ordinary callback and documenting it (BL17).
6. **Consistency across the Phase 3 recons** (section 7). Six disagreements need one answer each in the brief:
   - i128: helpers-switches H5 (archive) vs fp-helpers I1 (outlined COMDAT IR);
   - the function-pointer fast path: sjlj S6 "thunk ranges" vs callbacks CB1's exact lookup;
   - f16: Design.md's System.Half vs fp-helpers F1's helpers everywhere;
   - destructors: Design.md's ProcessExit vs lifecycle L1's `exit` on the calling thread;
   - the Linux oracle: lifecycle L10 (`start2.c`) vs tests-gates-p3 D9 (E7h);
   - the timing of the intrinsics switch: helpers-switches S4 vs plan P4-A.

   Two recons independently propose **process-wide state found by name in AppDomain data**: atomics Q-AT6's 16-byte lock table and callbacks CB4's table registry. Recommend one versioned key convention, written into Design.md section 6 (BL18).
7. **Status.md section 6 items 16-22** (section 3):
   - accept 17, 18, 20 and 22 as written;
   - amend 16 (149 of 150 c-testsuite tests, Windows triple only; the Phase 3 error list), 19 (state the Phase 3 result) and 21 (netstandard 2.0 and earlier; direct start only on Windows triples);
   - add **item 23**, the data-import helper as built.

   Exact texts are in section 3.
8. **Design.md and plan section 7** (section 4): 41 Design.md places change for Phase 3. Each row names the
   sibling decision it depends on. Section 4.2 has full replacement paragraphs for P3-A and P3-B, updated with the lifecycle, atomics and
   fp-helpers results. Plan P3-A still says switches are "module flags or TargetOptions", which the switch-carrier
   decision replaces.
9. **Late goals** (section 5): no recommended Phase 3 choice precludes a late goal. Six small additions keep
   doors open:
   - `exit`/`atexit` defined only when the link leaves them undefined;
   - the function-pointer dispatch as one IR step with a mode flag;
   - one aggregate descriptor in the record;
   - helper state in per-concern holder classes;
   - switches as target features;
   - the AppDomain key convention carrying a version and leaving room for an instance scope.
10. **Upstreaming** (section 6). The Phase 3 choices that matter:
    - compiler-rt builtins for MSIL, including `atomic.c`, built in compiler-rt;
    - MSIL ABIs inside `ExpandVariadics`;
    - switches as target features (the WebAssembly precedent);
    - in-tree tests of every feature that need no .NET;
    - few new upstream hooks.

    GitHub (E7, re-checked): the default branch is `msil`, and `MSIL CI` has **never run**.

---

## 1. Method and experiments

Each item was checked against the source at HEAD (file and line cited) and, where cheap, reproduced with the
shared build. Runtime claims were run on .NET Framework 4.8, .NET 8 and .NET 10 (Windows), and on WSL .NET 8
and .NET 10 where Linux matters.

| Id | What | Result | Files |
|---|---|---|---|
| E1 | Retarget artifacts on both x86_64 triples: `_mm_pause`, MSVC `__fastfail`, Rust's `int $$0x29` asm, a dead `sse4.2` dispatch path, `__builtin_cpu_supports`, and the Rust IR forms through `llc` | each diagnosed as in summary item 4 | `e1-retarget/` |
| E2 | What a program sees of its command line. C on windows-msvc-msil (Framework direct, `dotnet x.exe`, `dotnet exec`), C on linux-gnu-msil (WSL .NET 8), and C# `Environment.CommandLine` on .NET 8, .NET 10 and Framework | summary item 3 | `e2-cmdline/` |
| E3 | Stock-clang bitcode built with `/volatile:ms`, linked by msil-ld; MSIL clang with and without `-fms-volatile` | Link error `undefined symbol: __atomic_store_4` (also `_8` and `__atomic_load_4`). In object mode `-fms-volatile` leaves undefined `__atomic_*` symbols and gives no compile error | `e3-msvolatile/` |
| E4 | Who reads `__ELF__` | glibc 2.43 headers: 0 files. compiler-rt: `int_lib.h:51` (test) and `:70` (`#error Unsupported target`), `assembly.h`. LLVM libc: `io.h:31`, `time-macros.h:8`, `abort_utils.h:18`. Lua: `luaconf.h:324`. clang predefines it natively, but not for `x86_64-unknown-linux-gnu-msil` | `e4-elf/` |
| E5 | Structs by value at native boundaries | `lldiv` is refused on both triples. `div` (8 bytes, `i64`) exits 92 on Framework and WSL .NET 8. `sret` as a scalar first argument exits 42 (Win64: Framework and .NET 8; SysV 24 bytes: WSL .NET 8). `il/pinv16.il` (SysV two-register result and a 24-byte byval argument) exits 42 on WSL .NET 8 and .NET 10 | `e5-sbv/` |
| E6 | What `clang -c` writes | `-c` writes an MZ (PE) object. `-flto -c`, `-emit-llvm -c` and `-flto=thin -c` write bitcode (`BC`) | `e6-design/` |
| E7 | GitHub state (read-only `gh`), re-checked in this pass | Default branch `msil`, repository public. Workflow `MSIL CI` (id 391960041) is active with **0 runs**. Upstream's scheduled workflows run on `msil` and skip | `e7-github/` |
| E8 | **New.** Visual Studio 18 detection by the shared build's clang outside the dev shell (`INCLUDE`/`LIB` unset) | Native `x86_64-pc-windows-msvc` and the MSIL triple behave the same. Only legacy `Microsoft Visual Studio 8/9/10` include paths and the Windows Kits *lib* dirs are found, so `stdio.h` is not found. The cause was not isolated: the COM Setup API path in `llvm/lib/WindowsDriver/MSVCPaths.cpp` is compiled in (`USE_MSVC_SETUP_API`). Not MSIL-specific; every gate runs inside `msil-env.ps1` | `e8-vsdetect/` |
| E9 | **New.** SysV `__int128` by value through P/Invokes (test `.so` built with `build\bin\clang` and `ld.lld`; IL with ilasm 4.8; WSL .NET 8 and .NET 10) | Correct: `mul3p1(i128)` in registers, `split5` (only R9 left, so the i128 goes to stack offset 0), and the struct twin `stk7s`. **Wrong** on both runtimes: `stk7(6 GPRs, one int64 on the stack, i128)`, with a pack-8 or a pack-16 value type. Natively the i128 is read at `24(%rsp)` (16-aligned), but the CLR passes it at `16(%rsp)`. **An explicit `int64` pad parameter fixes it** (`i128pad.il`: exit 42 on .NET 8 and .NET 10). `System.Int128` by value: `MarshalDirectiveException: System.Int128 and System.UInt128 cannot be passed by value to unmanaged` on .NET 8 and .NET 10 | `e9-i128/` |
| E10 | **New.** `AddVectoredExceptionHandler` with a delegate-thunk handler (C#; net48, net8.0, net10.0 on Windows; registered first or last; 5 managed throws, or 1 null dereference) | Framework 4.8: 5 calls for 5 throws (`0xE0434352`) and 1-2 calls for the null dereference (`0xC0000005`); survives. .NET 8.0.33: 5 calls for 5 throws and survives; the null dereference is **fatal** (`attempted to call a UnmanagedCallersOnly method from managed code`) whether the handler is registered first or last. .NET 10.0.14: 0 calls for throws; the null dereference is fatal when the handler is first, and makes 0 calls when it is last (Rust std's order) | `e10-veh/` |

Two measurement notes for later agents:
- **WSL exit codes.** `wsl -d Ubuntu -- bash -c '...; echo $?'` from Git Bash printed `exit=0` for a program that returns 92, because `$?` was expanded before WSL's bash ran. The same command in a script file prints 92 (`e5-sbv/run2.log`). The harness uses scripts, so it is not affected.
- **Piped exit codes.** In `e10-veh/run.log` the bracketed exit codes are those of a `tr` pipe and mean nothing. The first unpiped run showed exit 6 for the fatal cases.

---

## 2. Triage

Sizes:
- XS: under 50 lines;
- S: 50-200 lines;
- M: 200-800 lines;
- L: more (code plus tests).

"Home" is one of:
- a P3-A or P3-B milestone area;
- a small fix;
- Phase 4/5;
- after Phase 3 (late goals);
- needs the user;
- carry forward (a standing rule, no work);
- drop.

### 2.1 Plan section 12 rows with milestone `-` or naming P3, P3-A, P3-B or 'Phase 3 input'

| # | Rows (plan line) | Item | At HEAD | Size | Home |
|---|---|---|---|---|---|
| T1 | 8477-8488, 8525, 8533, 8621-8626, 8700-8702, 8724, 8765, 8784 | Plan history and standing rules: plan v1/v2, installs, section 10 answers, the D21 refinement, no attribution, the branch model, compiler tools only, no fat assembly, no runtimeconfig, Roslyn-style references, switches instead of `-mdotnet`, no runtime support assembly, the Phase 2 plan and its WP reading rules (1.1, 1.2a), CI pins, `CLITests` naming, the Q37 Design.md edits | Binding; nothing to build. The 2026-10-15 row's "Not decided: libm" is answered by helpers-switches L1/S5 | - | carry forward |
| T2 | 8683 | Items (1)-(10) deferred after the upstream merge | All ten are closed by M11 (plan line 5877: M11's goal names every item) | - | drop |
| T3 | 8691 | The upstream-hooks manifest | The rule stands. Phase 3 adds hooks, each listed with its upstream tests in the milestone that adds it: compiler-rt builtins CMake, `int_lib.h` and `atomic.c`; `ExpandVariadics.cpp`; clang `Options.td`/`Attr.td`; `clang/lib/Basic/Targets/MSIL.h` is the fork's own file; possibly the `__ELF__` predefine in `InitPreprocessor.cpp:1526-1528`. HANDOFF 0 (the merge workflow's hooks gate) applies | XS per hook | first Phase 3 milestone (front-loaded hooks) |
| T4 | 8785, 8786, 8787, 8912, 8913, 8991 | Portable triple, managed calls, inline IL, LLVM libc direction, final cleanup, FeatureIdeas 3 | Late goals; guarded in section 5 | - | after Phase 3 |
| T5 | 8932 (1) | Per-object member counts (no spill classes in objects) | True: `BinaryFormat/CLI.h:49-50` (`MaxMethodsPerType = 65524`, `MaxFieldsPerType = 65534`), checked in `MetadataBuilder.cpp:746-756`. The row's "65,524 fields" is a typo; Status.md's 65,534 is right | M | Phase 4 (P4-A) |
| T6 | 8932 (2) | ZERO classification differs between objects and `-S` | True: `MSILDataSegment.cpp:121-127`; behaviour is identical | - | drop (document; the final cleanup may unify) |
| T7 | 8932 (3) | 64-bit relocation loop | True | S | Phase 5 (32-bit triples) |
| T8 | 9012, 9048 | Spilled `'<msil>Data'` fields and a two-thread first use | The shape is pinned by `lld/test/MSIL/shared-data-class.test` (spill.ll at `--msil-type-member-limit=4`). The race was not reproduced; it needs two managed threads, which C reaches only once P3-B's threads exist (atomics-threads 9.3) | S: a `<Module>::.cctor` that runs phase 1 before any spilled field is read, in libraries whose fields spill; a listing test; a two-thread run on .NET 8, .NET 10 and Framework | P3-B thread milestone (the row's own placement) |
| T9 | 9013 | `thread_local` exports of libraries | True: `lld/MSIL/Exports.cpp:313-318` refuses (`thread-local storage is not supported yet`). `lld/MSIL/SymbolTable.cpp:683-684` already reads `LibraryDataSymbol::ThreadLocal` for the kind check, and `CLI/SymbolTable.h:67` has `ThreadLocal = 0x2` | S | P3-A TLS milestone (frame-tls D-FT7: an accessor per symbol) |
| T10 | 9079 | Linux-triple coverage that the phase gate leaves to CI | True: in `lit-legs.ps1` `ctest` has no `-Wsl` twin, and `lto-wsl` runs four directories | S | tests-gates-p3 D8: a `ctest-wsl` leg and a wider `lto-wsl` in the phase gate now, since CI has never run (E7) |
| T11 | 9080 | c-testsuite after P3-B | True: `libc-free.txt:146` is the one `diag` entry. tests-gates-p3 4.2: of the 70 excluded tests, 68 call `printf`; the IR scan finds native variadic calls in 69, variadic definitions in 2 (00140, 00204) and a dynamic alloca in 1 (00207 at -O0) | S | P3-B variadics flips 00140; the 70 go to a second list (tests-gates-p3 D7) with the host-libc milestone |
| T12 | 9081 | Seven P3 matrix rows; two oracle gaps | True: `matrix.txt` lines 41, 43, 78, 95-98. `msil/utils/linux-oracle/start.c:18-34` runs `.init_array`, `main` and `exit`, but no `.fini_array`. The E7 sysroot's `libc.so.6` has no `atexit` (glibc puts it in `libc_nonshared.a`) | S per row, S for the oracle | each P3 feature milestone; the oracle with lifecycle's milestone (section 7 C5: E7h) |
| T13 | 9082 | Matrix checker gaps | True as the row describes | S | tests-gates-p3 5.3, with the first milestone that flips a P3 row |
| T14 | 9083 | First run of `msil-ci.yml` | True: never run (E7, re-checked), although the default branch is already `msil` | - | needs the user (one dispatch; BL10) |
| T15 | 9084 | Milestone gate time | True | - | needs the user (Q32 for Phase 3; tests-gates-p3 D1/D2) |
| T16 | 9085 | Tool timeouts under host load | True | XS | tests-gates-p3 D3 (automated retry in `lit-legs`) |
| T17 | 9086 | Design.md updates 16-22 | Section 3 | XS | needs the user (BL1) |
| T18 | 8501 | Explicit `-fms-volatile` gives atomic accesses | True: in objects they become undefined `__atomic_*` symbols (E3) | - | P3-B atomics (Q-AT11 restores the native default) |
| T19 | 8544, 8706, 8714 | i128 division hook, inline expansion, bfloat <-> i128 | Done in M12 (inline) | - | fp-helpers I1 or helpers-switches H5 (section 7 C1) |
| T20 | 8552, 8554, 8604, 8605 | Tests that pin "(phase 3)" helper and atomic texts (`call-errors.ll`, `atomic-errors.ll`, `ext-loads.ll`) | True. tests-gates-p3 4.1 counts 44 tests that pin a Phase 3 diagnostic: 26 CodeGen, 9 lld, 7 `08-semantics`, 2 `09-hosted` | XS each | retire them with the helper archive and atomics |
| T21 | 8591, 8599 | Frame and SjLj diagnostic texts (`dynamic-alloca-error.ll`) | True (frame-tls 2.4 lists them) | XS | P3-A frames, P3-B SjLj |
| T22 | 8661 | `intrinsic '<name>' is not supported for MSIL` | True (E1) | - | stays for intrinsics without a fallback; A1 adds fallbacks |
| T23 | 8668, 8680 | `va_end`, `va_copy`, IR `va_arg` | VAEND Expand and VACOPY Expand/Custom since M10; aggregate `va_arg` diagnosed since M12 | - | P3-B variadics (V8: keep reporting IR `va_arg`) |
| T24 | 8721 | `extern_weak` `thread_local` | True | XS | P3-A TLS |
| T25 | 8746, 8749 | `areTypesABICompatible` refuses half/bfloat by value | True (`MSILTargetTransformInfo.cpp:56-65`, comment "until phase 3") | XS | lift when f16/bf16 pass as `int32` bits (fp-helpers F2, helpers-switches H4) |
| T26 | 8754 | lld's pre-extraction of bitcode archive members that define RuntimeLibcalls names | True (`lld/MSIL/Driver.cpp:1576-1578`) | - | keep lld's rule; a *CIL* helper archive is not affected (helpers-switches E5); drop |
| T27 | 8797 | Native `long double` functions | True (fp-helpers F13: `sqrtl`, `expl`, `strtold` refused on the Linux triple) | S | fp-helpers L1 (forward them in the helper archive; needs the user) |
| T28 | 8802 | The 46-name callback list | True (`lld/MSIL/NativeLibraries.cpp` ~60-140) | - | callbacks (CB1: every function pointer is native-callable, so the list retires) |
| T29 | 8877 | Variadic function referenced only by address | True | - | P3-B variadics |
| T30 | 8879 | `NATIVE_INCOMPATIBLE` | True; it also covers `sret` (E5: `Signature 0x0A000001 'lldiv' SRET size=16 align=8`) | - | struct-by-value (F1); callbacks CB7 (trap thunk until aggregate descriptors) |
| T31 | 8968 | Rust note in Status.md and GettingStarted.md | True | - | after P3-B, re-run the Rust std variants without stand-ins as a demo, not a gate (rustc is not a test tool) |
| T32 | 8681, 8682 | **(missed by pass 1)** The upstream merge of 2026-10-19 and the `run-utc-tests.py` contract | History. The merge procedure (HANDOFF 0: hooks gate, merge subject, manifest update) is the input for BL12 | - | carry forward |
| T33 | 9070 | **(missed by pass 1)** c-testsuite list format as built: `# rule:`/`# count:` headers; a `diag` entry carries `# P3-A ...` or `# P3-B ...`; a diag entry requires `msil-prog-pipeline=obj` | A contract for Phase 3. The milestone that flips 00140 deletes the `diag` suffix and the `# P3-B` tag; the second list (T11) needs its own rule and count | - | carry forward (P3-B variadics; tests-gates-p3 D7) |
| T34 | 9073 | **(missed by pass 1)** matrix.txt vocabulary and check-matrix.py rules: a milestones column naming `P3`/`P3-A`/`P3-B` marks a P3 row; transcript lines need a `<stem>.expected` | A contract for Phase 3: each flip of a P3 row turns the `diag:` column into a transcript with `.expected` (or `.linux.expected`). Lifecycle L11 adds a `rule:` native kind for hand-written Windows transcripts, which extends this vocabulary | - | carry forward (each P3 feature milestone) |

### 2.2 Status.md section 3, "What Phase 2 does not do"

| # | Row | At HEAD | Home |
|---|---|---|---|
| S1 | other external functions/variables | Done for links; `-S` and llc keep D14's errors | keep the `-S` freeze (BL8) |
| S2 | variadic definitions and calls | true | P3-B (variadics) |
| S3 | the `va_arg` instruction | true | P3-B (variadics V8) |
| S4 | `thread_local` | true | P3-A (frame-tls) |
| S5 | dynamic `alloca`, VLAs, stacksave/restore, `__builtin_frame_address` | true | P3-A (frame-tls; D-FT8 returns FP for depth 0) |
| S6 | `setjmp`/`longjmp` "error" | **Wrong for the Linux triple's object pipeline**: msil-ld binds `_setjmp`, `__sigsetjmp`, `longjmp`, ... natively, and they work by accident (sjlj section 1) | P3-B (sjlj S10: a link error first); correct Status.md now (BL15) |
| S7 | atomics | True. In object mode they surface as undefined `__atomic_*` symbols (E3), or bind to `libatomic.so.1` under `-latomic` (atomics-threads 0.1) | P3-B (atomics-threads; Q-AT12 stops native binding) |
| S8 | destructors, `atexit` | true (T12) | P3-A (lifecycle) |
| S9 | `long double` = `double` | Permanent; native `l` functions are T27 | fp-helpers L1-L3 |
| S10 | constrained FP <-> i128, f16/bf16, `_Complex` multiply/divide | true | P3-B (fp-helpers F1-F3, I1; helpers-switches H1) |
| S11 | `__float128`, `x86_fp80`, rounding modes, `fenv.h` | True; the CLR has no rounding-mode control (fp-helpers L3 keeps `__float128` predefined but unusable) | keep the errors; no Phase 3 work |
| S12 | inline and module asm | true | never (inline IL is a late goal) |
| S13 | sections MSIL cannot lay out | Destructor tables belong to lifecycle. **`.preinit_array` ("a later milestone") has no owner** (lifecycle 1.2 lists it only among the unsupported sections; `llvm/lib/CLI/ImageLayout.cpp:53-57` classifies it `Unsupported`, with the comment "MSIL has no such phase yet"). Its entries run before every `.init_array` entry, only in executables, which the module initializer can do with the constructor machinery (XS) | `.preinit_array`: P3-A (lifecycle's milestone). `__start_`/`__stop_` on Windows: never. `.CRT$XP*`: keep unsupported (lifecycle L14) |
| S14 | aliases, ifuncs | true | ifuncs: Phase 4 or drop (a resolver called in phase 1 of the module initializer would work; no corpus program needs one) |
| S15 | callbacks | True. Rust std on Windows passes a function to `AddVectoredExceptionHandler`, which is not on the list (F7, E10) | P3-B (callbacks) |
| S16 | structs by value, `__int128`, vectors, `_Float16` in P/Invoke signatures, "phase 3" | True (E5, E9); **in no P3-A/P3-B text** | Structs and SysV `__int128`: P3-B (F1, BL2, BL16). Win64 `__int128` (by reference in, `<2 x i64>` in XMM0 out): keep the error. Vectors and `_Float16`: Phase 4 or never |
| S17 | exports | done (M22) | - |
| S18 | BCL intrinsics | true | P3-A (helpers-switches S4, S5) |
| S19 | codegen switches | True; M21's optional request strengths exist with no producer | P3-A (helpers-switches S1-S3) |
| S20 | architecture intrinsics, "phase 4 SIMD" | True (E1) | Scalar fallbacks in P3-A (A1). System.Runtime.Intrinsics: Phase 4 per plan P4-A (section 7 C6) |
| S21 | SVE/SME | true | Phase 4 |
| S22 | debug information | true | Phase 4 |
| S23 | Darwin, i686 | true | Phases 4, 5 |
| S24 | stack protector, CFG, patchable entries | ignored with a warning | permanent |
| S25 | basic block sections etc. | true | never |

### 2.3 Known gaps and limits (Status.md section 3)

| # | Item | Home |
|---|---|---|
| G1 | cross-host image determinism (item 12's default) | keep the default; revisit after the first CI run (T14) |
| G2 | CI licences of CoreMark and c-testsuite | needs the user (BL9) |
| G3 | no `untested` matrix rows; P3 rows pin diagnostics | each P3 milestone flips its rows (T12, T34) |
| G4 | callbacks not on the list are not diagnosed | P3-B callbacks: CB1 makes every address-taken function native-callable, which removes the hazard |
| G5, G6 | cost of c-testsuite; phase gate time | tests-gates-p3 (D1, D2, D6); Q32 as amended 2026-10-28 |
| L1 | identifiers of at most 1023 bytes | permanent (Q27) |
| L2 | variable-length `memset` limited to 4 GiB | permanent; Phase 4 could loop `initblk` (XS) |
| L3 | a P/Invoke records only the library name, no symbol version | permanent; symbol versions are hosted-libc's concern |
| L4 | native data imports do not probe the application directory: "retrying ... is left to a later milestone" | **Small fix, no owner.** When the references define it, use `NativeLibrary.Load(string, Assembly, DllImportSearchPath?)`, which probes like `DllImport`, instead of `Load(string)`. Otherwise retry `dlopen`/`LoadLibraryW` with `AppContext.BaseDirectory` (on mscorlib/Framework: the assembly's directory) when a bare name fails. XS, `lld/MSIL/Relocations.cpp:494-560`. P3-B host-libc milestone |
| L5 | per-object member counts | Phase 4 (T5) |
| L6 | ZERO classification | drop (T6) |
| L7 | 64-bit relocation loop | Phase 5 (T7) |
| L8 | `--gc-sections` rules | permanent (ld.lld parity) |
| L9 | images bound to a .NET pack run only there | permanent |
| L10 | ilasm quiets float32 sNaN constants | permanent (tool) |
| L11 | prototype-less imported function used only by address | permanent |
| L12 | library limits: no interposition, cycle order, one P/Invoke stub per assembly (so a native function's address differs between assemblies), JIT-time failures, name = stem, imported variable size | Permanent, except two. CB5 removes the native-function-address limit in P3-B: the address of a native function is its native entry point. The variable-size warning needs undefined data sizes in the record (Phase 4 or drop) |
| L13 | 8-bit exit status on Linux | permanent (lifecycle 2: Windows 32 bits, Linux low 8 bits) |

### 2.4 HANDOFF items (open issue 15, the fidelity note, the known-limit rows)

The Rust experiment's std table (`C:\llvm-msil-scratch\rust-exp\std\out\table.txt`: rustc 1.99 fat-LTO
bitcode, variants v0-v8) gives the Phase 3 demand per program:

| What the std variants contain (per variant) | linux-gnu | windows-msvc | Home |
|---|---|---|---|
| thread-local globals | 2-5 | 2-6 | P3-A TLS (frame-tls 5) |
| atomic operations (`load atomic`, `cmpxchg`, `store atomic`, `atomicrmw`) | 188-355 | 82-200 | P3-B atomics |
| variadic call sites: `fcntl`, `open64`, `syscall` | 3, 5, 14-22 | - | P3-B variadics. The fixed-arity list must cover `syscall`: glibc's is variadic, and Rust std calls it for `getrandom`, `statx` and futexes |
| `llvm.x86.sse2.pause` (spin_loop) | 8 | 4 | P3-A A1 fallback |
| `int $$0x29` inline asm (`__fastfail`, std's abort path) | - | 8-15 | P3-A A1 fallback: recognize the asm string in MSILPrepareIR; clang's `__fastfail` builtin goes through its existing MSIL branch (`CGBuiltin.cpp:1951-1960`) |
| signal/sigaction | explicit imports in the experiment | - | callbacks CB8 (allow with a warning) |
| `thread::spawn` | fails (pthread_create) | fails (CreateThread) | P3-B callbacks, TLS and atomics (atomics-threads 9: 8,000,000 exact with delegate thunks on every runtime) |
| `AddVectoredExceptionHandler(0, vectored_handler)` (declared in every Windows variant; `std::sys::pal::windows::stack_overflow`) | - | takes a Rust function; not on the callback list | P3-B callbacks; measured in E10 (F7, BL17) |
| dead x86 intrinsics in runtime-dispatch paths (crc32fast's `pclmulqdq` at opt-level s/z) | yes | yes | P3-A: `__cpu_model` reports no feature (A1). The dead path still needs the intrinsic to *compile*: E1 `dispatch.c` deletes the whole function. So A1's "PNSE lowering of dead-path intrinsics only on request" matters for such crates |
| GetCommandLineW fidelity | - | yes | P3-A small item (F2, BL4) |
| Windows links need `LIB`/`-L` for the VS 18 MSVC and SDK lib dirs ("this clang does not detect VS 18") | - | yes | Reproduced (E8). Not MSIL-specific: native clang misses the headers too. No Phase 3 work; recheck after the upstream merge (BL12); the harness always runs inside `msil-env.ps1` |

### 2.5 Questions still open with the user (inputs section 4)

| Question | Recommendation | Where |
|---|---|---|
| Approve Status.md section 6 items 16-22 | approve with the Phase 3 plan commit; 16/19/21 amended, item 23 added | BL1, section 3 |
| Plan section 10 item 11: may CI fetch and run CoreMark and c-testsuite? | allow | BL9 (tests-gates-p3 D11) |
| Q38: `msil` as default branch, one dispatch of `msil-ci.yml` | the default branch is already `msil` (E7); dispatch once now | BL10 (tests-gates-p3 D12) |
| Q32 for Phase 3 milestone gates | narrow the standard step to `net8-core` + `p3-full` + conditional harness, keep 20-30 minutes, judged on load-normalized minutes | BL11 (tests-gates-p3 D1, D2) |
| atexit order across several output assemblies; C `exit` through the host libc bypassing ProcessExit | Per-assembly lists. The toolchain's `exit` runs them on the calling thread, then the libraries' lists through `'<msil>fini'` in reverse initialization order, then `Environment.Exit`; ProcessExit only as fallback. Measured: native `exit` reaches ProcessExit on Framework only, and `Environment.Exit` inside ProcessExit deadlocks .NET 8/10 | lifecycle L1-L5, L8, L12 |
| per-thread `[ThreadStatic]` buffers: first-use allocation, never freed | a finalizable holder frees them (2000/2000 on every runtime) | frame-tls D-FT5 |
| setjmp/longjmp across assemblies (the exception must carry the `jmp_buf` and the value) | `ExternalException` + `HResult` + an armed 16-byte status prefix; no shared type | sjlj S2, S3, S6; callbacks CB4 |
| libm to Math/MathF | host libc by default; exact substitutions without a switch need the user | helpers-switches S5, L1 |
| default switch settings on Linux/Darwin triples; `-mavx2` vs the SIMD switches | Every switch is off on every triple. Native target features (`-mavx2`) keep their D7 role (clang's ABI, and which intrinsics headers offer) and never enable a BCL API by themselves. The System.Runtime.Intrinsics switch (Phase 4, S20) maps the intrinsics they allow, each guarded by `IsSupported` | helpers-switches S4, A1 |
| how the static C helper archive reaches msil-ld | The driver already passes `<resource-dir>/lib/<triple>/clang_rt.builtins.lib` or `libclang_rt.builtins.a` when it exists (`clang/lib/Driver/ToolChains/MSIL.cpp:247-253`); build it from compiler-rt per MSIL triple | helpers-switches H1, H2 |
| architecture-specific intrinsics (scalar fallbacks, System.Runtime.Intrinsics) | fallbacks in P3-A; System.Runtime.Intrinsics in Phase 4 | BL5, helpers-switches A1 |

### 2.6 Findings that no sibling recon owns

- **F1. Structs by value at P/Invoke boundaries** (E5, E9). The x86-64 rules, measured:
  - *Win64.* Aggregates of 1, 2, 4 or 8 bytes are already integers in IR (`div` returns `i64` and works
    today). Larger ones are passed as a pointer to a caller copy, which is already a scalar, and returned through `sret`.
    `sret` is the first argument (RCX) and comes back in RAX, so `void f(native int sret, ...)` is the exact
    native call (UCRT `lldiv`: exit 42 on Framework 4.8 and .NET 8). `cexp` and the other `<complex.h>`
    functions returning `double _Complex` have the same shape (fp-helpers 4.5).
  - *SysV.* clang coerces aggregates of up to 16 bytes to one or two eightbytes. A two-eightbyte *result*
    (`{i64, i64}`, `{double, double}`, `{i64, double}`) needs a 16-byte value type whose fields carry the
    classes (`int64`/`float64`). The CLR then returns it in RAX:RDX, XMM0:XMM1 or RAX:XMM0 (glibc `lldiv`
    through `[0] int64, [8] int64`: exit 42 on .NET 8 and .NET 10). A MEMORY-class (`byval`) argument needs
    only a value type of the right size (`.pack 1 .size 24`, no fields): exit 42. A MEMORY-class result is `sret`
    in RDI and RAX, and the scalar form works (`make24`: exit 42).
  - *SysV `__int128` and 16-byte-aligned aggregates* (E9). In registers, and at an even stack slot, a two-`int64`
    value type is exact. At an odd stack slot the CLR passes it 8-aligned, while native code reads it 16-aligned.
    msil-ld knows the classification (it can count GPRs and stack slots from the IR signature and the
    descriptor), so it inserts one `int64` pad parameter before the argument (`i128pad.il`: exit 42 on .NET 8
    and .NET 10). The same rule covers any 16-byte-aligned aggregate passed in memory (`_Alignas(16)`, a member of
    type `__int128`). Hence the descriptor below must carry the alignment.
  - *AArch64* passes `sret` in X8, not X0, so there an `sret` result needs a value-type return, and HFAs need
    typed fields. *Unverified*: there is no aarch64 execution host here.
  - **What it takes.** The record already carries `SRET` and the byval list (`MSILLink.h`). A two-register result
    and an aligned aggregate need an aggregate descriptor in the record: size, alignment, eightbyte classes. That
    is the same extension callbacks CB7 waits for before thunks of `NATIVE_INCOMPATIBLE` functions stop being trap
    thunks. msil-ld emits one private explicit-layout value type per (size, field classes), uses it in the P/Invoke stub with
    an IL adapter that moves the value to and from the caller's memory, and inserts pad parameters on SysV.
    Size M: record field and codec, writer, msil-ld stub adapter, tests on both triples. Recommended home:
    P3-B, before or with host-libc conformance.
  - **Which APIs need it.** Few in the C libraries: `lldiv` and `imaxdiv` (two registers on SysV, `sret` on Win64),
    and `cexp`, `csqrt`, `cpow`, ... (`double _Complex`). `div` and `ldiv` are 8 bytes on Win64, as is `div`
    on SysV; they are scalars and work today. Many in user and third-party native libraries.
- **F2. Command-line fidelity** (E2).
  - Windows: under `dotnet`, `GetCommandLineW`/`A` include the host's arguments. UCRT's `__argc`/`__argv`
    (`__p___argc`, `__p___argv`) are NULL under `dotnet` *and* under .NET Framework (the host's CRT never
    configured them), while `main`'s argv is right.
  - Linux: `program_invocation_name` is `$HOME/.dotnet-msil/dotnet`, `program_invocation_short_name` is
    `dotnet`, and `/proc/self/cmdline` is the host's line; glibc's `err`, `warn` and `error` print that name.
    `main`'s argv and the arguments a glibc-style constructor receives are right (M22).
  - Fix (recommended, BL4): in **executables**, msil-ld binds `GetCommandLineW` and `GetCommandLineA` to private IL
    helpers that return `Marshal.StringToHGlobalUni`/`Ansi(Environment.CommandLine)`, computed once.
    `Environment.CommandLine` is the program's own line on .NET 8 and .NET 10 and the process line on Framework;
    it is an mscorlib member, so no switch is needed. `__p___argc`/`__p___argv`/`__p___wargv` bind to helpers over
    the argv that `'<msil>Main'` already builds. Libraries keep the native API: natively a DLL sees its host's line.
    On Linux, document; writing `program_invocation_name` into the host's libc is intrusive. Size S.
- **F3. `__ELF__`** (E4): BL3. helpers-switches 2.3 independently needs either `__ELF__` or a `__MSIL__`
  branch in `int_lib.h:51` (its E1: 171 sources stop at `int_lib.h:70` `#error Unsupported target` without it).
- **F4. Stock-clang `/volatile:ms`** (E3): resolved by P3-B atomics (Q-AT11). Design.md 3's sentence changes then
  (D-6).
- **F5. Status.md corrections now.** The setjmp row (S6). Plan row 8932's "65,524 fields" (T5) is a typo for
  65,534; the plan is append-only, so a section 12 row notes it.
- **F6. GitHub state** (E7): `msil` is the default branch, so Q38's first half is done. `MSIL CI` has 0 runs, so
  the 330-minute timeouts and cross-host determinism are still unchecked.
- **F7. Vectored exception handlers** (E10). Rust std on Windows calls `AddVectoredExceptionHandler(0,
  vectored_handler)` from `std::sys::pal::windows::stack_overflow` (seen in the Rust experiment's IR, every
  Windows variant). Once Phase 3 makes the handler a delegate thunk:
  - Framework 4.8 and .NET 8 call it on every managed throw, including the `ExternalException` of every `longjmp` (sjlj S2). That is a reverse P/Invoke per throw, harmless and slower.
  - .NET 10 does not call it for managed throws.
  - On a hardware exception in managed code (a null dereference, which the CLR turns into `NullReferenceException`), .NET 8 ends the process with `Fatal error. Invalid Program: attempted to call a UnmanagedCallersOnly method from managed code`, registered first or last. .NET 10 does the same only when the handler is registered first.

  Natively the same fault ends the process too. So the difference is the message (and that no C#
  caller could catch the NRE), not a working program that breaks. `AddVectoredContinueHandler` and
  `SetUnhandledExceptionFilter` are the same family (not run). Recommendation in BL17.
- **F8. `long double` through native variadic functions** (now measured by fp-helpers F13). On x86_64 Linux, native
  `printf("%Lf", x)` given a `double` printed `nan` or the previous call's value, because glibc reads 16 bytes from the
  overflow area. fp-helpers L2 recommends a clang warning for `%L` formats on the wide-native triples; a real fix
  would need a clang call-site marker (on demand). The first pass flagged this as unverified; it is settled.

### 2.7 Other HANDOFF open issues (not named by the task, triaged for completeness)

| HANDOFF item | At HEAD | Home |
|---|---|---|
| 1, 3 (D21 refinement; native int/int64 mixing caught only by PEVerify) | Standing. Phase 3 amends D21 twice: atomics Q-AT1 (a `native int` where `int32&` is expected, which both verifiers reject) and possibly callbacks (thunk tables) | P3-B atomics (Q-AT1 needs the user, since D21 was the user's) |
| 4 (`msil-env.ps1` rebuilds the dev-shell cache under lit's per-run TEMP, so `gtest.test`/`expect-error.test` are slow) | True: `msil-env.ps1:21` caches in `GetTempPath()`, which lit sets per run | tests-gates-p3 WP 2 (harness cost), XS |
| 5 (setup-dotnet SHA pin) | Done: both uses are pinned (`msil-ci.yml:156`, `:315`); no `TODO(user)` left | drop |
| 6 (rotate the feed PATs) | user action | needs the user (unchanged) |
| 11 (lit-legs refuses arguments with blanks) | Not re-checked | tests-gates-p3 WP 1 (gate tooling), XS |
| 12 (history cleanup list) | user decision: later | needs the user (not Phase 3) |
| 14 (`llvm-mc` prints LLVM's crash banner for MSIL triples) | True: `llvm/lib/MC/MCParser/AsmParser.cpp:808-812` calls `reportFatalUsageError` | the inline-IL goal (it brings an MSIL asm parser), or any milestone owning that hook; XS |

---

## 3. Status.md section 6, proposed Design.md updates 16-22

Checked against the code. Recommendation: approve with the Phase 3 plan commit (BL1); item 23 is new.

**16. Section 11, Phase 2 "As built" note: amend.** The text says "c-testsuite's 150 libc-free tests through
objects on .NET 8". In fact 149 pass and one (00140) pins a Phase 3 diagnostic, and only on the Windows triple
(Status.md section 1). Its list of Phase 3 errors omits dynamic allocas and structs by value. Replacement text
(after the section 11 table):

> *As built (Phase 2, 2026-10-27).* Phase 2 delivered every outcome of its row (Status.md section 1): `clang
> -c` writes CIL objects with link-info record v1, frozen since M20; `msil-ld` writes the PE, binds native
> libraries, reference assemblies and library assemblies; objects are the default route and `-S` plus ilasm a
> test oracle. Nine programs of several files and the link-semantics matrix match their native builds through
> objects and LTO on .NET 8, .NET 10, .NET Framework 4.8 and WSL, and 149 of c-testsuite's 150 libc-free tests
> do so through objects on .NET 8 for `x86_64-pc-windows-msvc-msil` (the 150th defines a variadic function).
> What Phase 2 reports as an error instead of running is Phase 3's: thread-local data, dynamic allocas,
> destructors and `atexit`, variadic definitions and calls, function pointers passed to native code,
> setjmp/longjmp, atomics, structs passed to or returned from native functions by value, and the compiler-rt
> helpers.

**17. 5.1, bitcode objects: accept as written.** Verified (E6): `clang -c` writes an MZ object; `-flto -c`,
`-emit-llvm -c` and `-flto=thin -c` write bitcode. Replace "Until CIL objects exist, `clang -c` writes
bitcode." with:

> `-flto -c` (or `-emit-llvm -c`) writes bitcode; `clang -c` writes a CIL object.

**18. 5.1, per-type limits: accept as written.** Verified: `BinaryFormat/CLI.h:49-50`, the error
`cannot write CLI metadata: type '<Module>' has <n> methods (limit 65524)` (`MetadataBuilder.cpp:746-756`),
spill classes only in msil-ld's images. Add to the "Shape" bullet:

> An object has no spill classes, so one translation unit holds at most the runtimes' per-type member counts
> (65,524 methods and 65,534 fields in `<Module>`); `msil-ld`'s outputs spill past them into private classes.

**19. 5.2, step 1, helper archive: amend.** "(from phase 3)" is right for the Phase 2 text, but the Phase 3
plan delivers the archive, so the approved text should say what will be true. The driver already passes the
archive when it exists (`clang/lib/Driver/ToolChains/MSIL.cpp:247-253`). Replacement for the start of step 1
(if helpers-switches H1/H2 are accepted):

> 1. Load objects and archives, including the triple's `clang_rt.builtins` archive (compiler-rt's builtins
>    built for that MSIL triple as CIL objects, from phase 3), which the driver passes after the default
>    libraries, as native links pass compiler-rt; pull in archive members on demand; and run LTO on any
>    bitcode.

If the user approves item 19 before the helper decisions, use the Phase 2 wording: "including the toolchain's
static C helper archive (from phase 3), linked like compiler-rt builtins".

**20. 4.1, constructors with arguments: accept as written.** Verified:
- `MSILAsmPrinter.cpp:1137-1151` (`collectConstructors`), with the error `global constructor '<f>' takes arguments, which MSIL does not pass` off the Linux shapes, and its twin at `:2745`;
- `OutputSegment.cpp:352-390`: msil-ld passes argc, argv and envp as far as the signature takes them.

Add to 4.1's constructor bullet:

> On Linux triples a constructor may take glibc's `(int argc, char **argv, char **envp)` or a prefix of it, and
> gets `main`'s `argc` and `argv` and the process environment; on Windows triples, whose C runtime passes
> nothing, a constructor that takes arguments is an error. A null function in `llvm.global_ctors` ends the
> list, as on every LLVM target.

**21. Section 8, output names: amend.** The code (`lld/MSIL/Writer.cpp:1040-1068`) also treats netstandard 2.0
and earlier as runnable on Framework, and "runs directly" holds only for Windows triples. Add this after "The default output name is `a.exe` on every MSIL triple ...":

> An executable that can run on .NET Framework (linked without references, or against the .NET Framework's
> reference assemblies or `netstandard` 2.0 or earlier) is named `<name>.exe`; on a Windows triple it starts
> directly on .NET Framework, and everywhere through `dotnet` with a runtimeconfig.json. One bound to a .NET
> reference pack (`System.Runtime`) or to `netstandard` 2.1 or later runs only on .NET and is named
> `<name>.dll`, run as `dotnet <name>.dll` with the runtimeconfig.json that the build system writes. `msil-ld`
> warns when such an executable is named `.exe`, and writes no apphost.

**22. 5.4, spilled `'<msil>Data'` fields: accept as written.** It is the T8 limit; Phase 3's fix replaces it.

> In a library whose `'<msil>Data'` fields spill into further classes, two threads that first use the library
> at the same time may let one of them read a spilled field before the first phase stored it; phase 3's
> threads fix it by running the first phase inside the module initializer of such a library.

**23 (new). 5.2, step 2.3, the data-import helper.** Design.md says "(`NativeLibrary`, or
`LoadLibrary`/`GetProcAddress` on .NET Framework)". As built (`lld/MSIL/Relocations.cpp:494-560`,
`NativeLibraries.cpp:178-200`), msil-ld uses `NativeLibrary.Load`/`GetExport` when the references define both members. Otherwise it uses
`LoadLibraryW`/`GetProcAddress` from kernel32.dll on Windows triples and `dlopen`/`dlsym` from the
link's native libraries on ELF triples, whatever the runtime. Replacement for the parenthesis:

> (`NativeLibrary` when the reference assemblies define it; otherwise `LoadLibraryW`/`GetProcAddress` from
> kernel32.dll on Windows triples and `dlopen`/`dlsym` on Linux triples)

The 7.1 table's "Lazy library loading" row follows:
- modern .NET column: "`NativeLibrary` when the references define it, else as in the other column";
- Framework column: "`LoadLibraryW`/`GetProcAddress` via kernel32; `dlopen`/`dlsym` on Linux triples".

---

## 4. Design.md and plan section 7 for Phase 3

### 4.1 Design.md sentences to update

"Depends" names the sibling decision the replacement assumes. With another answer the sentence still changes,
to that answer's text. Line numbers are at HEAD and were re-checked in this pass.

| Id | Where (line) | Now (abridged) | Replacement | Depends |
|---|---|---|---|---|
| D-1 | header (3) | "Status: draft v2 ...; updated 2026-10-21 with the approved Phase 2 decisions ..." | append "; updated <date> with the approved Phase 3 decisions (`recon/p3/decision-brief.md`) and the Phase 2 as-built notes (Status.md section 6, items 16-23)" | - |
| D-2 | 1, goal 6 (23) | "A portable libc for .NET is deferred (section 6)." | "A portable libc for .NET is a planned exploration after phase 3 (section 6)." | - |
| D-3 | 2.1 table (49) | "Shadow stack (`__stack_pointer`) and variadic lowering (pointer to an argument buffer) \| Same model (4.2, 4.7)." | "Shadow stack (`__stack_pointer`) and variadic lowering (`ExpandVariadics`, an argument buffer) \| A per-thread shadow stack only for dynamic allocas that a loop can repeat; the variadic argument buffer lives in the caller's frame, and the `va_list` keeps the native layout (4.2, 4.7)." | frame-tls D-FT1, variadics V1 |
| D-4 | 2.3 (61) | "Clang attaches the C-level signature to `dllimport`-candidate declarations as metadata, and the backend emits matching value types with explicit layout." | "Clang's native ABI lowering already says how: an aggregate it coerces to scalars passes as those scalars, and a result returned through a hidden pointer (`sret`) is that pointer as the first argument on x86-64, as the native ABI passes it. What remains, a SysV aggregate passed in memory (`byval`) or returned in two registers (`{i64, i64}`, `{double, double}`, `{i64, double}`), and SysV `__int128`, becomes an explicit-layout value type of the same size, with `int64` and `float64` fields for the register cases, which objects describe in their link-info record (size, alignment, eightbyte classes) and `msil-ld` emits into the output; the CLR then applies the native ABI, except that it places such a value type at an 8-byte stack slot, so `msil-ld` adds a padding argument before a 16-byte-aligned one where the native ABI aligns it (from phase 3). AArch64 passes `sret` in x8, so there the result is a value type as well." | BL2, BL16 |
| D-5 | 3 (88) | "... plus `__MSIL__` and minus `__ELF__` on Linux triples (clang defines `__ELF__` only for the ELF object format; revisit with glibc headers in phase 3, plan section 10 item 9)" | (BL3 = define, Q-AT7 = cap) "... plus `__MSIL__`, with two exceptions: on Linux triples `__ELF__` is defined as in a native build, because `msil-ld` links them with ELF's rules (`.init_array`, `__start_`/`__stop_`, weak symbols, visibility, `DT_SONAME`); and atomics wider than 8 bytes are never reported lock-free (`__GCC_HAVE_SYNC_COMPARE_AND_SWAP_16` undefined, `__atomic_always_lock_free(16, 0)` false), because the CLR has no 16-byte atomic (4.8)." (BL3 = keep) "... minus `__ELF__` on Linux triples (clang defines it only for the ELF object format; glibc's headers never test it, and the toolchain's own C sources test `__MSIL__`)" plus the same atomics exception | BL3, Q-AT7 |
| D-6 | 3 (90) | "on windows-msvc it needs `-fno-ms-volatile` until phase 3 (stock clang makes `volatile` accesses atomic there)" | "on windows-msvc its `volatile` accesses of up to 8 bytes are atomic acquire/release accesses (`/volatile:ms`, stock clang's default there, which MSIL triples now share), which the backend lowers like other atomics (4.8)" | Q-AT11 |
| D-7 | 3.1 (107) | "We still use the v-variant lowering (4.7) for consistency, and keep native vararg as a fallback ..." | variadics recon section 10's text (no native-vararg fallback; Windows calls native variadic functions with fixed arity, floating-point values as integer bits) | variadics V2 |
| D-8 | 3.2 (115-116) | "... maps that check to `IsSupported`." / "... arch-specific intrinsics are rejected unless they have a scalar fallback." | Append to 115: "Without the switches `__builtin_cpu_supports` reports no feature (a private helper defines `__cpu_model`), so such code takes its portable path." Replace the end of 116: "... unless they have a scalar fallback (`_mm_pause` becomes `Thread.SpinWait(1)`, fences `Interlocked.MemoryBarrier`, `crc32` a private helper, MSVC `__fastfail` `Environment.FailFast`)." | helpers-switches A1 |
| D-9 | 4.1 (129) | "Constructors (`llvm.global_ctors`, ... such as `.init_array` and `.CRT$XC*`) run after relocation ... Destructors and `atexit` handlers run at process exit (`AppDomain.ProcessExit`)." | "Constructors (`llvm.global_ctors`, and the triple's constructor sections such as `.preinit_array` (executables only, first), `.init_array` and `.CRT$XC*`) run after relocation, in priority order across all objects. Destructors (`llvm.global_dtors`, `.fini_array*`, `.CRT$XT*`) and `atexit` handlers run in the C library's order when the program calls `exit` or returns from `main`: the toolchain's `exit`, emitted into each output, runs the assembly's handlers and destructors on the exiting thread, then those of the libraries it links, and calls `Environment.Exit`; `AppDomain.ProcessExit` runs them for an assembly whose exit did not run, such as a library used by a .NET program." (lifecycle L16), plus item 20's text | lifecycle L1, L5, L6, L16 (needs the user) |
| D-10 | 4.2 (135) | "Dynamic allocas and `stacksave`/`stackrestore` use a per-thread shadow stack (a `[ThreadStatic]` unmanaged block), like WebAssembly's `__stack_pointer`." | "A dynamic alloca that no loop can repeat is a `localloc`; one on a cycle of the control-flow graph, with the `stacksave`/`stackrestore` around it, uses a per-thread shadow stack (chunked unmanaged memory hung off a `[ThreadStatic]` field), like WebAssembly's `__stack_pointer`." | D-FT1, D-FT2 (needs the user) |
| D-11 | 4.4 (143) | "... allocated lazily and initialized from a TLS template, which is the same pattern as ELF TLS." | append: "The block is freed when its thread ends (a finalizable per-thread holder). A library exports a `thread_local` variable through its address accessor, which returns the calling thread's address (5.4)." | D-FT5, D-FT7, BL7 |
| D-12 | 4.5 (153) | "Fast path: ... check whether the pointer is in this assembly's thunk table (one range compare) ..." | "**Fast path:** indirect calls from our own code look the pointer up in this assembly's table of native-callable entry points (an exact lookup: delegate thunks come from a runtime-wide stub heap and `UnmanagedCallersOnly` entry points are precodes, so no range compare can recognize them) and, on a hit, `calli` the managed method directly, which skips the native transition and keeps exceptions (`longjmp`, 4.6) out of native frames. Otherwise they use `calli unmanaged cdecl`. The address of a native function is its native entry point." | callbacks CB1, CB5 (section 7 C2) |
| D-13 | 4.6 (158) | "... `longjmp` throws a BCL exception type, which the function that called `setjmp` catches with an IL exception filter that matches the `jmp_buf` ..." | "... `longjmp` throws a BCL exception type (`System.Runtime.InteropServices.ExternalException`, whose `HResult` names the target frame's status block, recorded in the first 16 bytes of the `jmp_buf`), which wrappers around the calls of the function that called `setjmp` catch with an IL exception filter ... A `longjmp` out of a callback through native frames works on Windows only: the CLR on Linux aborts the process when a managed exception crosses native frames." | sjlj S1-S5 (S5 needs the user) |
| D-14 | 4.7 (162-167) | the variadic model | Variadics recon section 10's texts: the buffer is in the caller's frame; "a table in the target" becomes msil-ld's table per C runtime; a fixed-arity list is built in, plus the attribute; SysV fixed-arity calls cannot pass floating-point values. Add (fp-helpers L2): "On triples whose native `long double` is wider than `double`, a `long double` argument of a native variadic function is passed as a `double`, so `printf`-family formats with `L` get a warning." | variadics V1-V7, fp-helpers L2 |
| D-15 | new 4.8 | (no atomics subsection) | "### 4.8 Atomics. Atomic loads and stores of up to 8 bytes are `volatile.`-prefixed accesses (every ordering, because the JIT would hoist a plain load out of a loop), a sequentially consistent store adds `Interlocked.MemoryBarrier`, and read-modify-write operations and compare-exchange on 4- and 8-byte objects are `Interlocked` calls (or a compare-exchange loop where `Interlocked` has no member); 1- and 2-byte ones are a compare-exchange loop on the containing 32-bit word. 16-byte and unaligned atomics are lock-based (compiler-rt's `atomic.c` in the helper archive, section 6), with one lock table for the whole process, so a program and its libraries agree. A compiler-only barrier (an empty `asm` with a memory clobber, `atomic_signal_fence`) is a call of a private empty method that the JIT cannot inline, since the JIT reorders memory accesses across nothing else." | atomics-threads Q-AT2-Q-AT8 (Q-AT6 needs the user) |
| D-16 | 5.1 (182) | item 17 | item 17 | - |
| D-17 | 5.1 shape | item 18 | item 18 | - |
| D-18 | 5.2 step 1 (186) | item 19 | item 19 (amended) | H1, H2 |
| D-19 | 5.2 step 2.3 (190) | item 23 | item 23, plus: "Names of compiler runtime helpers (compiler-rt's `__`-prefixed library-call names such as `__atomic_load_16` or `__muldc3`) never bind to a native library: they come from the helper archive (section 6), or are link errors." | item 23; H3, Q-AT12, fp-helpers I2 |
| D-20 | 5.3 (198, 200) | "A codegen switch (for .NET 7+) also sets `[assembly: DisableRuntimeMarshalling]` (7.1)." / "`[SuppressGCTransition]` (modern .NET) is emitted only under its own codegen switch ..." | "An `msil-ld` switch (for .NET 7+, bound from the references) also sets `[assembly: DisableRuntimeMarshalling]` (7.1)." / "`[SuppressGCTransition]` (modern .NET) is emitted only for declarations marked for it (it is unsafe for functions that block or call back), and is bound from the references like any attribute (7.1)." | helpers-switches S4 |
| D-21 | 5.4 | (no TLS or exit text) | Add: "A `thread_local` export is a data symbol with the ThreadLocal flag; its accessor `'<msil>addr_x'()` returns the calling thread's address, so importers keep calling the accessor." Add: "A library's export class also has a private `'<msil>fini'` member that runs the library's exit handlers and destructors; a program's `exit` calls those of its libraries in reverse initialization order (4.1)." Plus item 22 | BL7; lifecycle L5, L13 |
| D-22 | 6 (228) | "... or C helpers compiled by this toolchain and linked statically by `msil-ld`, the way LLVM links compiler-rt builtins." | "... or compiler-rt's builtins built for each MSIL triple (`clang_rt.builtins`), which the driver passes to `msil-ld` as native links pass compiler-rt." | H1, H2 |
| D-23 | 6 (228, end) | "... mapping the math library to `Math`/`MathF` in general is not decided." | (S5 = yes) "Functions whose `Math` member is bit-identical to the host libc on every runtime (`sqrt`, `floor`, `ceil` as `-Math.Floor(-x)`, `trunc`, `round`, `rint`, `nearbyint`, `fabs`) map to `Math` by default; the rest stay host-libc calls unless a switch permits `Math`/`MathF`." | helpers-switches S5, L1 (needs the user) |
| D-24 | 7.1 (238) | "Codegen switches. Individual, independent switches ..." | append: "Each is an MSIL target feature (`-mmsil-<name>` / `-mno-msil-<name>`, per function, so an LTO link keeps each translation unit's choice), or an `msil-ld` option for what only the linker emits." | S1, S2, CB3 |
| D-25 | 7.1 table (251) | "128-bit integers \| Expanded inline ... `Int128`/`UInt128` only behind a switch" | (I1 = c2, recommended in section 7 C1) "Division, remainder and floating-point conversions are one private function per operation and module (compiler-rt's algorithm as IR, merged across objects by COMDAT); the rest is expanded inline; `Int128`/`UInt128` only behind a switch" (same text in both columns) | fp-helpers I1 vs helpers-switches H5 |
| D-26 | 8 (268) | "`clang/lib/CodeGen/Targets/MSIL.cpp` (planned): ABI lowering, plus attaching C-level signatures to external declarations (2.3)." | "No MSIL ABI lowering in clang: the native `TargetInfo`'s serves, and P/Invoke signatures come from the IR (2.3)." (the file does not exist at HEAD) | BL2 |
| D-27 | 8 (269), new bullet | driver bullet; no compiler-rt bullet | Driver: insert after "... so no VC++ redistributable is needed)": "and the triple's `clang_rt.builtins` archive after them (section 6)". New bullet: "`compiler-rt/lib/builtins`: built for each MSIL triple as a CIL archive (`clang_rt.builtins`), including `atomic.c` with an MSIL lock table." | H1, H2, Q-AT6 |
| D-28 | 10 risks | (none on native frames) | add "10. **Exceptions through native frames.** The CLR on Linux aborts the process when a managed exception crosses native frames (a `longjmp` out of a callback under native `qsort`), so such code works on Windows only." | sjlj S5, callbacks CB9 |
| D-29 | 11, phase 3 row (309) | "Runtime support through the BCL and helpers in the output assembly (section 6), the glibc and UCRT host-libc triples (P/Invoke, variadic shims), the codegen switches for newer .NET APIs (7.1), setjmp, TLS, atomics. Real programs run: zlib, Lua, SQLite." | "Runtime support through the BCL and private helpers in the output assembly (section 6: toolchain-generated IL and compiler-rt's builtins built for MSIL), the codegen switches for newer .NET APIs (7.1), dynamic stack allocation, `thread_local`, the C exit protocol (destructors, `atexit`, `exit`), variadic functions, native-callable function pointers and threads, setjmp/longjmp, atomics, structs by value at P/Invoke boundaries, and conformance of the glibc and UCRT host-libc triples. Real programs run: zlib, Lua, SQLite." | - |
| D-30 | 11, after the table | (none) | item 16 | - |
| D-31 | 3 (88), D36 sentence | "... `asm volatile("" ::: "memory")`, a common compiler barrier, which becomes a compiler-only fence (plan D36)." | "... which becomes a call of a private empty method that the JIT does not inline (plan D36, amended in phase 3: an IL comment did not stop the JIT from hoisting loads across it)." | Q-AT8 |
| D-32 | 3 (95), `long double` | "... so `long double` interop on those triples is a documented limitation ..." | append: "The helper archive of those triples forwards the native `long double` functions (`sqrtl`, `strtold`, ...) to their `double` counterparts, so such programs link and compute in double precision; structures with `long double` members still differ from native." | fp-helpers L1 (needs the user) |
| D-33 | 10 risks, item 8 (297) | "the CLR memory model vs C11 atomics edge cases" | "C11 atomics rest on the JIT honoring `volatile.` for relaxed and acquire loads and on `Interlocked` being a full barrier, which litmus and hoisting tests check on every runtime the gates run" | atomics-threads 5, 14 |
| D-34 | 6 (228), runtime services | "... `AppDomain.ProcessExit` for global destructors and `atexit` (4.1), ... `Interlocked`/`Volatile` for atomics, and `System.Half` for f16 where available." | "... `Environment.Exit` for the C exit protocol, with `AppDomain.ProcessExit` as its fallback (4.1), ... `Interlocked` and the `volatile.` prefix for atomics (4.8); f16 conversions come from compiler-rt's helpers, because `System.Half` quiets signaling NaNs and does not exist on .NET Framework." | lifecycle L1; Q-AT2; fp-helpers F1 |
| D-35 | 7.1 table (252), half | "Half-precision floating point \| `System.Half` \| Helpers in the output assembly (section 6)" | "Half-precision floating point \| compiler-rt's helpers in the output assembly (`System.Half` differs on signaling NaNs; at most a later switch) \| the same helpers" | fp-helpers F1 |
| D-36 | 7.1 table, new row | (none) | "Atomics \| `Interlocked` (`And`/`Or` from .NET 5, 8- and 16-bit `Exchange`/`CompareExchange` from .NET 9; neither used by default) \| `Interlocked` on 32- and 64-bit objects; compare-exchange loops for the rest (the default on both)" | atomics-threads Q-AT4, Q-AT5 |
| D-37 | 7.1 table, new row | (none) | "Process exit \| `Environment.Exit` (calling it inside `ProcessExit` deadlocks) \| `Environment.Exit` (`ProcessExit` handlers are cut off after about 2 s)" | lifecycle 3 |
| D-38 | 5.1, link-info record | "... call-site signature flags (no prototype, vararg, byval and sret size and alignment, dllimport, an explicit import module and name); and linker options ..." | append before "and linker options": "; from phase 3 (record v1.1), managed requests with optional substitutes (7.1) and aggregate descriptors (size, alignment, eightbyte classes) for structs passed by value to native code (2.3)" | S3, BL2 |
| D-39 | 4.5, representation (149-150) | "Modern .NET, with the `UnmanagedCallersOnly` codegen switch (7.1) ..." | "Modern .NET, with the `UnmanagedCallersOnly` switch of `msil-ld` (7.1; the attribute is bound from the references) ..." | CB3 |
| D-40 | 4.5, new bullet | (none) | "A C function that native code calls asynchronously, from a signal or a vectored exception handler, runs as a reverse P/Invoke; when such a call interrupts managed code on the same thread the CLR ends the process (measured for vectored handlers on .NET 8 on a hardware exception), so these handlers work only where the native program would also have run them outside its own code." | callbacks CB8, BL17 |
| D-41 | 6, after "no exception type is shared" | (none) | "Where C semantics need state shared by every assembly of a process (the lock table of lock-based atomics, 4.8, and the registry of function-pointer tables, 4.5), the assemblies find it by a versioned name in `AppDomain` data, never through a shared type." | BL18 (needs the user) |

Two section 6 sentences need the user regardless:
- the portable libc's candidate list ("Candidates are wasi-libc ..., musl ..., or picolibc") stays until the user picks LLVM libc formally (plan row 8912 says the list is unchanged until then);
- the proposed text when they do: "The preferred candidate is LLVM's in-tree libc, whose OS layer calls `System.*` through the managed-calls goal; wasi-libc, musl with a managed syscall shim and picolibc were compared."

### 4.2 Plan section 7: replacement paragraphs

Section 7's intro (the "Every phase that needs widely-included header edits ... front-loads them into its first
milestone" sentence) gains a Phase 3 list: "Phase 3's first milestone front-loads clang `Options.td` (`-mmsil-*`),
clang `Attr.td` (`msil_fixed_arity`, if kept), `BCLMembers.def` and `BinaryFormat/MSILLink.h` (record v1.1's
subsections, 5.8.9), the `ExpandVariadics` hook, compiler-rt's builtins CMake and `int_lib.h`, and the `__ELF__`
predefine (if BL3)." The plan title "implementation plan for Phases 0-2" becomes "Phases 0-3".

**P3-A** replaces the paragraph that starts "No runtime support assembly". Bracketed parts follow the decisions named:

> No runtime support assembly (section 12, 2026-10-15): the generated code calls the BCL directly wherever an
> API exists, and an output assembly depends only on the BCL, the native libraries it P/Invokes and the
> assemblies it was explicitly linked against (P2-C); process startup is the generated '<msil>Main' (D27, M3;
> argv from Environment.GetCommandLineArgs). P3-A provides private helpers of three kinds and the rule that
> chooses between them: compiler-rt's builtins built by this toolchain for each MSIL triple into a
> `clang_rt.builtins` CIL archive that the driver passes to msil-ld after the default libraries (the archive's
> build, content and driver input, backlog S3) for compiler-rt's families; IL helpers that the trailer builder
> generates for runtime services that need .NET types; and per-module `linkonce_odr` IR helpers where `-S` needs a
> body. No compiler-runtime helper name is ever bound to a native library. On these: dynamic allocas and
> stacksave/stackrestore [`localloc` for an alloca on no cycle, a per-thread chunked shadow stack otherwise];
> thread_local through a [ThreadStatic] pointer to an unmanaged per-thread block initialized from the TLS
> template, freed at thread exit and exported from libraries through the address accessor (section 12,
> 2026-10-27); the C exit protocol as private IL in every output [atexit, __cxa_atexit, exit and the rest of the
> family defined when the link leaves them undefined; per-assembly handler lists; exit runs the handlers and the
> llvm.global_dtors, .fini_array and .CRT$XT destructors on the exiting thread, then those of the linked libraries,
> then Environment.Exit; AppDomain.ProcessExit only as the fallback], and .preinit_array before the constructors
> of executables; scalar fallbacks for the architecture intrinsics that ports and Rust retarget leave behind
> (pause, fences, crc32, `__fastfail`, `__cpu_model`; Design.md 3.2) and, in executables of Windows triples, the
> program's own command line for GetCommandLineW/A and the UCRT argv accessors; individual, independent codegen
> switches as MSIL target features (`-mmsil-<name>`) plus msil-ld options for what only the linker emits, among
> them DisableRuntimeMarshalling, Math.FusedMultiplyAdd and UnmanagedCallersOnly thunks instead of delegate
> thunks (P3-B); and BCL intrinsics for bit ops, rounding, min/max and bitcasts (isel.md decision 11), each behind
> its switch (D16) [except the substitutions that are exact on every runtime]. Where that is possible, an API or
> attribute is also usable only if some reference assembly defines it (P2-B; M21's optional-substitute request,
> carried in record v1.1). The defaults keep the generated code running on .NET Framework 4.8 and modern .NET
> alike, as Phase 1 output does (which runtimes the output loads on depends on its references, P2-B), and what
> Design.md 7.1 (and 3.2 for SIMD) records about the differences decides which switches exist and their safe
> defaults. There is no profile: a build system may combine switches into "net8.0", but the compiler has no
> such concept (section 12, 2026-10-14). The C math library stays a host-libc import (D14), apart from the
> specific APIs the switches permit [and the exact substitutions].

**P3-B** replaces the paragraph that starts "Variadic definitions and calls":

> Variadic definitions and calls (ExpandVariadics with MSIL ABIs that build the native va_list over an
> argument buffer in the caller's frame; native variadic functions through their v-variants or with fixed
> arity, from msil-ld's tables per C runtime and the msil_fixed_arity attribute); native-callable function
> pointers: the address of every address-taken function is a native-callable entry point (kept-alive delegate
> thunks by default, [UnmanagedCallersOnly] thunks behind their P3-A switch), indirect calls dispatch through
> an exact per-assembly lookup to a managed calli or a calli unmanaged cdecl, and the address of a native
> function is its native entry point; threads started through the host libc (pthread_create, CreateThread,
> _beginthreadex, thrd_create) and their thread-exit destructors, and the fix of the spilled '<msil>Data' race
> of libraries (section 12, 2026-10-27); setjmp/longjmp via a BCL exception type [ExternalException] that
> wrappers in the setjmp caller catch with an IL exception filter matching the jmp_buf (from the Emscripten
> SjLj lowering; no exception type is shared between assemblies); C11 atomics and the __sync builtins
> (volatile. loads and stores, Interlocked read-modify-writes on 4- and 8-byte objects, partword compare-exchange
> loops for 1 and 2 bytes, compiler-rt's lock-based atomic.c from the helper archive for 16 bytes and the generic
> forms [with one lock table per process], the lock-free width capped at 8 bytes, and a compiler barrier the JIT
> respects for D36's single-thread fences), which also lets stock-clang /volatile:ms bitcode link and restores
> the native -fms-volatile default; f16 and bf16 conversions, C complex multiplication and division (__muldc3,
> __divdc3, ...; System.Numerics.Complex does not follow C Annex G's infinity and NaN rules) and the other
> compiler-rt calls the legalizer emits through the helper archive (System.Half at most behind a later switch: it
> quiets signaling NaNs); i128 division, remainder and the FP<->i128 conversions, expanded inline since M12 (Q36),
> [as per-module COMDAT functions with compiler-rt's algorithm] (backlog S12); the native long double functions
> forwarded to their double counterparts on the triples whose native long double is wider, and a warning for
> %L formats there; structs by value at P/Invoke boundaries (Design.md 2.3: sret as the first argument on
> x86-64, explicit-layout value types for SysV byval arguments, two-register results and __int128, with a padding
> argument before a 16-byte-aligned stack argument), which also completes the thunks of such functions; the
> retry of native data imports in the application directory; glibc and UCRT host-libc conformance (zlib, Lua,
> SQLite).

### 4.3 Other plan text that Phase 3 amends (as section 12 rows)

- D14's "Library calls whose name starts with `__` ... are errors until phase 3": in objects and LTO they
  resolve from the helper archive. `-S` keeps the frozen import table (BL8).
- D16, D19, D26 (thread_local, `llvm.global_dtors`; lifecycle L9 keeps FINI entries and no `LowerGlobalDtors`),
  D21 (atomics Q-AT1) and D36 (atomics Q-AT8): each feature milestone amends its decision.
- Section 10 item 9 (`__ELF__`) gets its answer (BL3). Items 11 and 12 stay open until answered (BL9, G1).
- 5.12.10: the tool-timeout retry rule for every lit step (T16), and the gate budget answer (BL11).
- 5.12.9 and the c-testsuite list contract (T33): the second list for the 70 hosted tests.
- Row 8932's typo (T5): a row noting that "65,524 fields" should read 65,534.

---

## 5. Late-goal guard

What each Phase 3 choice does to each late goal. "Eases" means the later work reuses it. "Keep" names the
small addition that keeps the door open. Nothing recommended *precludes* a goal.

| Phase 3 choice (recommended form) | Portable triple `msil64-unknown-dotnet` + LLVM libc | Managed calls from C (+ FeatureIdeas 3 typed/delegate overloads) | Inline IL | FeatureIdeas 1, 2, 4-7 |
|---|---|---|---|---|
| **Helper delivery**: compiler-rt builtins as a per-triple CIL archive, plus toolchain-generated IL helpers, plus per-module IR helpers where `-S` needs a body (helpers-switches H1/H2) | **Eases**: the triple gets its own archive. compiler-rt needs a `__MSIL__` branch in `int_lib.h:51` (neither `__ELF__` nor `_WIN32` will hold there) and drops the x86 sources. `COMPILER_RT_USE_LIBC_MATH` links the builtins to LLVM libc's math. **Keep:** helpers never call the host libc. compiler-rt builtins are freestanding (`atomic.c`'s lock table is IL), and IL helpers use only BCL members | Neutral. IL helpers bind BCL members through M21's binder, the path managed calls will use | neutral | **FI1** (class placement): toolchain helpers stay private in `<Module>` or a holder, never placed by the user's mapping. **FI6** (enclosed instance): **keep** helper *state* (TLS holder, exit lists, thunk table, shadow stack) in one holder class per concern, so an instance-scoped variant can replace it; compiler-rt builtins are stateless except `__cpu_model`. **FI7** (several triples): helper names in `<Module>` would collide (fp-helpers 10, helpers-switches 2.6), so a per-triple holder comes later. A *bitcode* helper archive would preclude nothing, but is extracted into every LTO link |
| **Switch mechanism**: MSIL target features per function, plus msil-ld options; the reference check as M21's optional substitute in record v1.1 (S1-S3, CB3) | **Eases**: generic switches apply, architecture ones do not. Keep switch names architecture-free, except `*-intrinsics` | **Eases**: "a member named by the binder, with a fallback" is a subset of "C calls a .NET member that msil-ld binds" | Inline IL is explicit, so switches should not gate it | **FI2** ("feature flags in general", which proposed module flags or TargetOptions) is settled by this choice; update FI2's text when the brief decides. Module flags would have lost per-TU choices under LTO (`Min`/`Max`/`Error` merge rules, helpers-switches E10) |
| **TLS**: `[ThreadStatic]` pointer to an unmanaged block from a template; `'<msil>Thread'` holder; accessor for exports; finalizable holder (frame-tls D-FT3-7) | **Eases**: LLVM libc's `errno` becomes an ordinary `thread_local`, and `__cxa_thread_atexit`-style destructors use the reserved slot | **Eases**: C# reads a library's TLS through the accessor, and managed threads entering C get their block lazily | neutral | **FI6**: per-(instance, thread) blocks would replace the one lookup; the TP hoisted per function keeps that a single change. **FI7**: `'<msil>Thread'` per triple. A `[ThreadStatic]` field per variable (rejected) would have precluded stable addresses |
| **C exit protocol**: IL in every output; `atexit`/`exit`/... defined when the link leaves them undefined; per-assembly lists; `exit` on the calling thread; ProcessExit fallback (lifecycle L1-L5) | **Eases, with the keep already in lifecycle's design.** A statically linked libc that defines `exit`/`atexit` (LLVM libc does, `libc/src/stdlib/`) wins over the synthesized definitions. The portable triple then needs (a) a trailer mode that leaves the destructor tables to the libc, with `__init_array_start`/`__fini_array_start` defined as ld.lld does, and (b) an OS-layer `_Exit` that calls `Environment.Exit` (lifecycle 5.6). Both are additive | Neutral. ProcessExit still runs the lists of a library used by a C# host | neutral | **FI6**: per-assembly fields move into the instance; the per-assembly design eases that. Keep the handler list as a data structure with a "run all" entry, ProcessExit being one trigger, so an instance's `Dispose` can be another |
| **Variadics ABI**: native `va_list` over a caller buffer; msil-ld tables per C runtime (variadics V1-V7) | **Eases**: it needs its own `VariadicABIInfo` (the `char*` form is simplest), an empty fixed-arity list and no v-variant table; every variadic function is then ours. Its `long double` should be `double` from the start (fp-helpers 10), so the `%L` hazard does not arise | None: a .NET method is never C-variadic | none | **FI6** variant (b) (WebAssembly-style sandbox) uses the same buffer model. **FI3**: an exported variadic function takes a native `va_list`; its C#-friendly overload is FI3's job |
| **Callbacks**: every function pointer is a native-callable entry point; exact-lookup dispatch; native addresses are native entry points (CB1, CB5, CB10) | **Keep:** the dispatch is one IR step in `MSILPrepareIR` (CB10) with a mode flag in the record/ABI ("function pointer model"). Then a managed-only model (no native code on the portable triple: `'<msil>fn_'` holds the `ldftn`, indirect calls are managed) is a mode, not a fork | **Eases** (and FI3): C function pointers are real native pointers, so a C# delegate becomes one through `GetFunctionPointerForDelegate`, and `delegate* unmanaged` values (UCO methods) pass directly; a managed-only representation would have precluded this | Documentation rule: inline IL that calls a C function pointer uses the dispatch (or `calli unmanaged`), never a managed `calli` of the value | **FI6** (b) replaces the model with table indices; not precluded. **FI7**: tables per variant |
| **setjmp exception type**: `ExternalException`, `HResult` match, 16-byte `jmp_buf` prefix, per-module IR helpers (sjlj S1-S4) | **Eases**: it needs only a `jmp_buf` of at least 16 bytes (LLVM libc's `setjmp` is per-arch asm, unusable either way) | .NET exceptions pass the wrapper filters (type test). C# `finally` blocks run between `longjmp` and its target (two-pass EH), which is the safe behaviour | The whole-body `.try` capability is a first step towards user EH regions | No global state; FI6 and FI7 unaffected. A private exception class per assembly would have broken cross-assembly `longjmp`, and the user's rule forbids a shared one |
| **Atomics**: `volatile.`/`Interlocked`; partword CAS; compiler-rt `atomic.c` with a process-wide lock table; compiler-barrier helper; lock-free cap (Q-AT1-Q-AT8) | **Eases**: the lowering is triple-independent and needs no libc; the lock table and the barrier are IL helpers it reuses (atomics 14) | V1's call shape (`Interlocked` on a `native int` address) is what hand-written IL would use | Inline IL can call the same members | **FI4** (managed memory/refs): V1 eases atomics on GC references (real byrefs into the same calls). **FI6**: the process-wide table is correct across instances (atomicity is per address, not per instance). **FI7**: one table for all triples in a process, which is right |
| **Process-wide named state** (AppDomain data keys for the atomic lock table and the function-table registry; BL18) | **Eases**: the same keys work there | **Keep:** document the key convention, so a C# library built by another tool can join it if managed calls ever need that | none | **FI6**: **keep** room for an instance qualifier in the key scheme (`<msil>v1:<concern>[:<instance>]`); per-process keys are right for atomics and the registry, and an instance model may want per-instance keys for other state. **FI7**: keys are triple-independent |
| **FP helpers**: f16/bf16 and complex from compiler-rt; i128 as outlined COMDAT IR; `long double` forwarding on wide triples (fp-helpers F1-F3, I1, L1) | **Eases**: every helper is architecture-free C or IR. System.Half and Int128 bodies fit only if the portable triple drops .NET Framework | Not precluded: msil-ld synthesizing a C symbol's body from a BCL member (a later System.Half switch) is a small instance of binding a C name to a .NET member | none | **FI7**: outlined `__msil_*` names collide like other helpers; a per-triple holder later |
| **Struct-by-value P/Invokes** (BL2, BL16) | n/a (no native libraries) | **Keep:** one aggregate descriptor (size, alignment, eightbyte classes) in the record, shared by P/Invoke stubs, callback thunks (CB7), and later managed calls and FI3's typed overloads, which need the same value types | neutral | **FI4** (managed memory): unaffected (value types are copies) |
| **`-S` and D14** (BL8): keep the frozen import table; Phase 3 features inside one module also work in `-S` | neutral | neutral | Inline IL brings the ILAsm parser that `.il` inputs lack (Q20) | - |
| **`__ELF__`** (BL3) | The portable triple has its own TargetInfo. LLVM libc reads `__ELF__` as a bare-metal proxy (`io.h:31`, `time-macros.h:8`, `abort_utils.h:18`), so defining it there would select LLVM libc's bare-metal OS layer, whose `__llvm_libc_stdio_write`-style hooks could be the managed-calls layer. Decide in that phase; the Linux triples' choice does not bind it | none | none | none |
| **Command-line helpers** (BL4); **VEH policy** (BL17) | n/a (the portable triple's startup is ours) | none | none | none |
| **Diagnostics**: new Phase 3 texts | - | - | - | Final cleanup: new texts avoid "phase N" ("not supported for MSIL yet"), so the cleanup has fewer tests to touch (BL14) |

---

## 6. Upstreaming (user question of 2026-10-27)

What upstream asks, as this checkout states it:
- **`llvm/docs/DeveloperPolicy.md`, "Adding a New Target"** (line 846 on):
  - new targets always start *experimental*, with at least one maintainer in `Maintainers.md`;
  - an active community providing buildbots and fixes;
  - code free of contentious IR changes, and conformance to the coding standards;
  - documentation of the ISA/ABI, or a free simulator;
  - an RFC on Discourse, then numbered patches merged together: docs, maintainers and triple support first; TableGen and lowering next; extensive lit tests last.
  - Promotion to official needs three stable months, broad small tests, `check-all` and, where applicable, `test-suite` passing in at least one configuration shown publicly (buildbots).
- **`llvm/docs/AIToolPolicy.md`**:
  - tools are allowed with a human in the loop who reviews everything and can answer for it;
  - substantial tool-generated content is labelled (for example an `Assisted-by:` trailer);
  - "extractive" contributions, whose review cost exceeds their value, are refused;
  - new contributors should start small.

The fork today:
- about 22,600 lines in `llvm/lib/Target/MSIL`, 13,700 in `llvm/lib/CLI`, 19,200 in `lld/MSIL` and 350 in the clang driver toolchain;
- 55 hooked upstream files (`msil/utils/upstream-hooks.txt`);
- 140 CodeGen tests, 131 of them with an *optional* `%if ilasm` round trip, so they run without .NET;
- 235 files under `lld/test/MSIL`: 53 tests have `REQUIRES: llvm-cil-dump` (an in-tree tool), ilasm appears only under `%if ilasm`, and none runs .NET;
- 23 clang test files.

ECMA-335 is a public standard and the CLR is free, which meets the documentation/simulator rule.

Phase 3 choices that affect upstream adoption:

1. **Helpers in compiler-rt, not a private tree.** Upstream would take the C helpers only as compiler-rt
   builtins for the MSIL triples, as it has them for `wasm32`. helpers-switches H2(a) puts the source list in
   compiler-rt's CMake and adds `lib/builtins/msil/` for MSIL-only C files. Atomics adds `atomic.c` with an MSIL lock
   table (an IL primitive the C calls), and fp-helpers the f16/bf16/complex families plus a generated `longdouble.c`.
   A private `msil/` helper tree would have to be rewritten before upstreaming. The IL helpers generated by
   `lld/MSIL` and `llvm/lib/CLI` are part of the target and linker and need nothing extra. Recommend H2(a).
2. **Variadics inside `ExpandVariadics`.** The variadics recon's hook in
   `llvm/lib/Transforms/IPO/ExpandVariadics.cpp` (`VariadicABIInfo::create`) is how upstream wants per-target
   variadic ABIs: WebAssembly, NVPTX and AMDGPU use it. A copied pass in the target would be less reviewable.
3. **Switches as target features** follow WebAssembly (`-msimd128`, `-mbulk-memory`): clang `Options.td`
   entries plus MSIL subtarget features in `MSIL.td`. Module flags or custom TargetOptions would add upstream
   surface without precedent.
4. **In-tree tests that need no external tools.** Every Phase 3 feature should get llc/FileCheck tests in
   `llvm/test/CodeGen/MSIL` and `lld/test/MSIL` (with the optional ilasm round trip) besides its execution
   tests in `msil/test`, as Phases 1-2 did. `check-llvm`, `check-lld` and `check-clang` then cover MSIL with no
   .NET installed, which `check-all` for an official target requires. Keep the clang-side changes (the lock-free
   cap, `__ELF__`, the `%L` warning) covered by `clang/test` alone. The `msil/` external project (harness,
   corpora, oracles) is not upstreamable as it stands. Its execution role maps to `llvm-test-suite` with a
   run-under wrapper (`dotnet` plus a runtimeconfig) and to buildbots.
5. **Hosted C is what `test-suite` needs.** Its programs call `printf` everywhere, so P3-B's variadics and
   host-libc conformance are the prerequisite for P4-A's "llvm-test-suite per triple" and for the
   test-suite rule above. Keeping `clang foo.c -o foo.exe` runnable with no extra flags (default libraries,
   the builtins archive by default) eases a test-suite configuration. E8 shows that on Windows this still needs
   the dev shell, as native clang does here.
6. **Few new upstream hooks.** Each hook is a merge cost now and review surface later. Phase 3 needs some (T3).
   Prefer target-local mechanisms where one exists:
   - MSILPrepareIR recognizing Rust's `int $$0x29` asm, instead of a clang change;
   - the `__fastfail` builtin through the MSIL branch that `CGBuiltin.cpp:1951-1960` already has;
   - the lock-free cap in `clang/lib/Basic/Targets/MSIL.h`, the fork's own file.

   The contentious parts for an RFC are Phase 1's, not Phase 3's: the `msil` object format in the triple, target selection by object format in `TargetRegistry.cpp`, and the clang `TargetInfo` wrapper.
7. **Buildbots and CI.** `MSIL CI` has never run (E7). A public Linux job with only `dotnet` installed (ILVerify
   and jitall; PEVerify is Windows-only and optional) is the forerunner of a buildbot. Phase 3's gates should
   stay runnable on Linux; tests-gates-p3 D8 adds `ctest-wsl`.
8. **AIToolPolicy.** No Phase 3 design choice changes it, but it shapes the upstream path:
   - every upstream patch needs a human author who has reviewed it, can defend it, and labels tool use;
   - the fork's own rule (no attribution trailers in fork commits) is independent of upstream PRs, whose descriptions would carry the label;
   - the size of the work argues for upstreaming in small, reviewable series after an RFC;
   - the final cleanup goal (plan and milestone references out of comments, docs rewritten as the current state) is a prerequisite either way. BL14 keeps Phase 3 from adding to it.
9. **Upstream merge before Phase 3.** The last merge took upstream `64f50ffd4f1c` (2026-10-19). Phase 3 touches
   upstream files (compiler-rt, `ExpandVariadics.cpp`, clang options). Merging before its first milestone (BL12)
   keeps those patches against current upstream and spreads the merge cost (HANDOFF 0 has the procedure).
10. **Process-wide AppDomain keys and the VEH policy** are runtime conventions inside the output, with no upstream
    surface.

---

## 7. Consistency across the Phase 3 recons

The ten finished recons agree on almost everything. These are the places where two of them, or a recon and
Design.md, give different answers. The brief should settle each.

| # | Topic | Position A | Position B | Recommendation and why |
|---|---|---|---|---|
| C1 | i128 division, remainder, FP <-> i128 | helpers-switches H5: the archive's `__divti3` family in objects/LTO, inline in `-S` | fp-helpers I1 (c2): outlined per-module `linkonce_odr` COMDAT functions with compiler-rt's algorithm as IR, in every mode | **I1 (c2).** It works in `-S` too, so `-S` and objects keep one shape. It avoids the Win64 i128 libcall convention hazard (fp-helpers F2). It cuts IL by 58-63% and JIT code by 62-67%, and the algorithm runs 1.5-2.4x faster than ExpandIRInsts' (F8). It is measured, where H5 is not |
| C2 | Recognizing our own function pointers at indirect calls | sjlj S6: extend the fast path "to the thunk ranges of linked toolchain assemblies" | callbacks CB1 (A2) and CB4: an exact per-assembly lookup; no range exists (callbacks 2.9); cross-assembly via the miss path first, then a process-wide registry (CB4 b) | **Callbacks' formulation.** Delegate thunks come from a runtime-wide stub heap and UCO entry points are precodes, so there is no range to test. sjlj S6's goal (libpng's cross-assembly `&longjmp`) is met by CB4 (b) |
| C3 | f16 | Design.md 6 and 7.1, plan P3-B: System.Half where switches and references allow | fp-helpers F1 (a): compiler-rt helpers everywhere; helpers-switches S4: `msil-half` in a second wave | **F1 (a).** System.Half differs from native on the 1,022 signaling-NaN halves and does not exist on Framework. Design.md text D-34/D-35 |
| C4 | Where destructors and `atexit` run | Design.md 4.1 and 6, plan P3-A: `AppDomain.ProcessExit` | lifecycle L1: the toolchain's `exit` on the calling thread, ProcessExit only as fallback | **Lifecycle.** ProcessExit runs on the finalizer thread (wrong TLS), is cut off after 2.3-2.6 s on Framework, and deadlocks .NET 8/10 if `Environment.Exit` is called inside it. Design.md text D-9 needs the user (L16) |
| C5 | Linux native oracle for destructors, `atexit` and threads | lifecycle L10: extend E7's `start.c` (`start2.c`) | tests-gates-p3 D9: E7h, the glibc sysroot's real `crt1.o`/`crti.o`/`crtn.o` + `libc_nonshared.a` | **E7h** for `msil/test/M3` and the P3 matrix rows. It is real glibc startup, which pthreads and hosted programs need anyway. E7 stays for M1/M2 (D9). Lifecycle measured `start2` equal to `crt1.o` in 7 modes, so either is correct; one oracle is cheaper to keep |
| C6 | System.Runtime.Intrinsics switches | helpers-switches S4: `msil-x86-intrinsics`/`msil-arm-intrinsics` among the initial switches | plan P4-A: "full SIMD (Vector128/256/512 and arch intrinsics)" in Phase 4 | **Phase 4**, unless the user wants them earlier. Phase 3's A1 scalar fallbacks cover the retarget artifacts, and vectors stay scalarized |
| C7 | Process-wide state between assemblies | lifecycle L4: per-assembly lists (no shared state) | atomics Q-AT6 (b): process-wide lock table by name in AppDomain data; callbacks CB4 (b): process-wide registry by name | **No conflict once a rule exists.** Share state by name only where C semantics require one process-wide object: atomicity across assemblies, and identity of function pointers across assemblies. Keep everything else per assembly. One key convention, `<msil>v<N>:<concern>` (BL18) |

**Helper delivery map** (H1's rule applied to every recon's helpers; one place for the planners):

| Helper | Kind | Owner recon |
|---|---|---|
| compiler-rt families: f16/bf16, complex, powi, `atomic.c` (16-byte and generic atomics), `__cpu_model` (MSIL `cpu_model.c`), crc32c | C in `clang_rt.builtins` (CIL archive) | helpers-switches H1/H2, fp-helpers WP-FP1, atomics Q-AT6 |
| native `long double` forwarders (`longdouble.c`, generated) | C in the archive of the wide triples | fp-helpers L1 |
| i128 division, remainder, conversions | per-module `linkonce_odr` IR (COMDAT) | fp-helpers I1 (C1) |
| setjmp/longjmp wrappers and `'<msil>longjmp'` | per-module IR | sjlj S4 |
| C exit protocol (`exit`, `atexit`, `__cxa_atexit`, `on_exit`, `at_quick_exit`, `quick_exit`, `_Exit` on Windows, `'<msil>fini'`) | IL from the trailer builder (`llvm/lib/CLI/Trailer.cpp`), only for names the link leaves undefined | lifecycle L2 |
| TLS holder, block allocation, finalizable holder, export accessors | IL (trailer) + backend access sequences | frame-tls D-FT3-7 |
| shadow stack | IL helper + backend | frame-tls D-FT2 |
| thunk tables, delegate creation, lookup table, registry | IL (trailer) | callbacks CB2, CB11, CB4 |
| compiler barrier, atomic lock-table primitive | IL (msil-ld) | atomics Q-AT8, Q-AT6 |
| command-line helpers (Windows executables) | IL (msil-ld) | this report, BL4 |
| scalar intrinsic fallbacks (pause, fences, fastfail) | backend lowering to BCL calls | helpers-switches A1 |

---

## 8. Decisions

**BL1. Approve Status.md section 6 items 16-22 (and new item 23)?** (needs the user)
Options: (a) approve now, as amended in section 3; (b) approve with the Phase 3 plan commit; (c) keep Design.md
as is. **Recommend (b)**, with items 16, 19 and 21 as amended and item 23 added: the Phase 3 plan edits
Design.md anyway (section 4), and item 19's text depends on H1/H2. Evidence: section 3 (each item checked in the
code).

**BL2. Where do structs by value at P/Invoke boundaries go?** (needs the user: scope)
Options: (a) a P3-B work item with the host-libc milestone: x86-64 `sret` as a scalar first argument first,
then SysV byval and two-register value types, with the record's aggregate descriptor shared with callback
thunks; (b) Phase 4; (c) keep the link error. **Recommend (a).** Evidence:
- E5 and E9: every shape was measured on the runtimes that matter;
- Status.md section 3 already promises "phase 3";
- callbacks CB7 waits for the same record extension;
- fp-helpers 4.5: the `<complex.h>` functions need it;
- Design.md 2.3 is a settled goal.

Size M.

**BL3. `__ELF__` on the Linux MSIL triples** (plan section 10 item 9; needs the user)
Options: (a) keep it undefined and add a `__MSIL__` branch to compiler-rt's `int_lib.h`; (b) define it, as in a
native build. **Recommend (b).** Evidence (E4):
- glibc's headers never test it, and clang's headers do not either;
- `msil-ld` links Linux triples with ELF's rules, so code that tests `__ELF__` for sections, visibility or aliases gets what it expects. Lua's `LUAI_FUNC` becomes `visibility("internal")`, so a `-shared` Lua exports what the native `.so` exports;
- compiler-rt builds unchanged for these triples (helpers-switches E1 built 142 sources with `-D__ELF__=1`).

Risk: code that uses ELF-only asm directives under `__ELF__` (`.symver`) would fail with the inline-asm error instead of taking a portable path. None was found in the corpus or the hosted-libc downloads. The portable triple needs the `__MSIL__` branch in compiler-rt either way. Size XS.

**BL4. Command-line fidelity.** (technical)
Options: (a) document only; (b) in executables of Windows triples, bind `GetCommandLineW`/`A` and
`__p___argc`/`__p___argv`/`__p___wargv` to private IL helpers (`Environment.CommandLine`, the argv of
`'<msil>Main'`), and document Linux's `program_invocation_name`; (c) (b) plus writing `program_invocation_name`
and `program_invocation_short_name` on Linux at startup. **Recommend (b).** Evidence: E2. Size S.

**BL5. Retarget artifacts (pause, `__fastfail`, dead x86 intrinsics).** (technical)
**Recommend helpers-switches A1** (P3-A): `Thread.SpinWait(1)`; `Environment.FailFast` for both spellings
(clang's builtin through its MSIL branch, Rust's asm string in MSILPrepareIR); and a `__cpu_model` helper with no
features. Evidence: E1 reproduces every artifact; the Rust std table counts 4-8 pause and 8-15 `int $$0x29`
sites per program.

**BL6. Home of the spilled `'<msil>Data'` race fix.** (technical)
Options: the P3-B thread milestone; the first P3 milestone that owns `lld/MSIL/Synthetic.cpp`; a fix loop when a
user hits it. **Recommend the P3-B thread milestone**, where the two-thread test belongs. Evidence: T8,
atomics-threads 9.3.

**BL7. `thread_local` exports of libraries.** (technical)
**Recommend** that the TLS milestone sets `ThreadLocal` (0x2) and the accessor returns the calling thread's address.
Evidence: T9; frame-tls D-FT7.

**BL8. `-S` and D14 in Phase 3.** (technical)
Options: (a) keep `-S`'s import table frozen. Phase 3 features that stay inside one module (TLS, dynamic
allocas, setjmp, atomics up to 8 bytes, variadic definitions, the exit protocol, outlined i128) work in `-S`;
helper and native-import uses go only through objects and LTO. (b) Let `-S` import Phase 3's helpers. **Recommend (a).**
Evidence: Q5 (the `-S` oracle); helpers-switches S3 ("`-S` text always uses the fallback"); sjlj S4 and lifecycle
L2 put their helpers where `-S` sees them.

**BL9. CI licences of CoreMark and c-testsuite** (plan section 10 item 11; needs the user)
Options: allow CI to fetch and run them; keep them out. **Recommend allowing.** Evidence:
- The pinned CoreMark tree is Apache-2.0 code (`LICENSE.md`'s second part) plus EEMBC's trademark acceptable-use agreement. The agreement restricts use of the CoreMark *mark*, for example with modified copies; it does not restrict running the code.
- c-testsuite's harness is MIT (`LICENSE`). Its `tests/LICENSE` says each test's licence is found through its `.otags` file, which names the source project only: 150 tests from scc, 69 from tinycc. Those projects' licences are *not read here*.
- CI downloads and runs the tests; it does not redistribute them.

Until then no run covers c-testsuite on the Linux triple, except `ctest-wsl` (BL13).

**BL10. Q38: the first dispatch of `msil-ci.yml`.** (needs the user: a user action)
**Recommend dispatching once now**, before Phase 3's gates are fixed. The default branch is already `msil` (E7,
re-checked), and the run is the only check of the 330-minute timeouts and of cross-host determinism.

**BL11. Q32 for Phase 3 milestone gates.** (needs the user)
Options: narrow the standard step (`net8-core` + `p3-full` + the harness only when harness files change, the
rest post-commit); or raise the budget to the measured 45-90 minutes. **Recommend narrowing**, keeping 20-30
minutes judged on load-normalized minutes. Evidence: plan row 9084; tests-gates-p3 D1/D2 (net8 is 2.0x a lean
composite in CPU; `net8-core` projects to about 50% of net8).

**BL12. An upstream merge before Phase 3's first milestone.** (needs the user)
**Recommend yes** (section 6 item 9).

**BL13. Linux-triple coverage in the phase gate** (row 9079). (technical)
**Recommend** a `ctest-wsl` leg and wider `lto-wsl` paths now, not waiting for CI. Evidence: T10, E7,
tests-gates-p3 D8.

**BL14. Phase names in new diagnostics.** (technical)
**Recommend** that Phase 3's new texts say "not supported for MSIL yet" without a phase number, and permanent
limits "not supported for MSIL". Evidence: the final cleanup goal (row 8913); 44 tests pin Phase 3 texts that
change in Phase 3 anyway (T20).

**BL15. Status.md corrections now** (the setjmp row on the Linux object pipeline; row 8932's field count).
(technical) **Recommend** folding them into the first Phase 3 milestone's documentation WP.

**BL16. SysV `__int128` and 16-byte-aligned aggregates at P/Invoke boundaries.** (technical; part of BL2's scope)
Options: (a) a two-`int64` value type, with msil-ld inserting an `int64` pad parameter where the SysV
classification puts a 16-byte-aligned argument at an odd stack slot; (b) keep `__int128` and aligned aggregates
link errors; (c) `System.Int128`. **Recommend (a)** on SysV triples (XS on top of BL2). Win64 `__int128`
(passed by reference, returned as `<2 x i64>` in XMM0) stays an error. Evidence: E9 (correct in registers and at
even slots; wrong at an odd slot on .NET 8 and .NET 10 even with `.pack 16`; the pad gives exit 42 on both;
`System.Int128` is refused by value on .NET 8 and .NET 10).

**BL17. C functions registered as vectored exception handlers** (`AddVectoredExceptionHandler`,
`AddVectoredContinueHandler`, `SetUnhandledExceptionFilter`; Rust std registers one in every Windows program).
(technical) Options: (a) ordinary callbacks (a thunk) and documentation; (b) intercept them in executables as
no-ops that return a unique non-null cookie; (c) (a) plus a link-time warning, as callbacks CB8 (b) does for
`signal`. **Recommend (a)**, with the documentation sentence D-40. Evidence (E10):
- the handler runs per managed throw on Framework and .NET 8 (harmless), never on .NET 10;
- it is fatal only on a hardware exception in managed code, which natively also ends the process; only the message differs;
- (b) would change a Win32 API's behaviour for every Windows program to fix a message;
- (c) would warn for every Rust std program.

**BL18. Process-wide state shared between assemblies by name** (atomics Q-AT6 (b), callbacks CB4 (b)). (needs the
user: a cross-assembly convention beside the "no shared exception type" rule) Options: (a) allow it only where C
semantics need one process-wide object, through one documented, versioned key convention
(`AppDomain.CurrentDomain.GetData/SetData("<msil>v1:<concern>")` under an interned-string lock, values only of BCL
types such as `IntPtr` arrays); (b) per-assembly state only (16-byte atomics then lose atomicity between a program
and its libraries; cross-assembly function pointers always take the 17-31 ns miss path); (c) each recon its own
key. **Recommend (a)**, with Design.md sentence D-41. Evidence: atomics 7 (measured shared across separately
assembled libraries on 4.8, 8 and 10); callbacks 2.10 (the same mechanism measured); lifecycle L4 shows where
per-assembly is the right answer.

**BL19. Resolve the recon disagreements as in section 7** (C1-C7). (technical, except C4's Design.md text, which
is the user's.) **Recommend** each row's recommendation.

---

## 9. Work-package ideas (homes for the items no sibling owns)

| WP idea | Files (disjoint) | Size | Milestone area |
|---|---|---|---|
| Struct-by-value P/Invokes. The record aggregate descriptor (codec, validator, dumper); the writer side (`getABIDescriptor`); msil-ld value types, stub adapters and the SysV pad rule. The x86-64 `sret` relaxation goes first | `llvm/include/llvm/BinaryFormat/MSILLink.h`, `llvm/lib/CLI/LinkInfo*.cpp`, `llvm/lib/Target/MSIL/MSILAsmPrinter.cpp` (descriptor), `lld/MSIL/Signatures.cpp`, `lld/MSIL/NativeLibraries.cpp`, new `lld/MSIL/ValueTypes.cpp`; tests `lld/test/MSIL/native-struct-*.test`, `msil/test/M3/sbv/` (lldiv, cexp, a 24-byte byval, `__int128` at an odd stack slot) | M (650-950) | P3-B (with callbacks' thunk descriptor, CB7) |
| Command-line helpers for Windows executables | `lld/MSIL/Synthetic.cpp` (IL helpers), the binding table in `lld/MSIL/NativeLibraries.cpp`; tests | S (150) | P3-A |
| `thread_local` exports | `lld/MSIL/Exports.cpp`, accessor emission | S | P3-A TLS |
| Spilled `'<msil>Data'` race | `lld/MSIL/Synthetic.cpp`; a listing test and a two-thread run | S | P3-B threads |
| Native data import: probe the application directory | `lld/MSIL/Relocations.cpp`, `llvm/include/llvm/CLI/BCLMembers.def` (the 3-argument `NativeLibrary.Load`) | XS | P3-B host-libc |
| `.preinit_array` | `llvm/lib/CLI/ImageLayout.cpp` (`classifySection`: a new class beside `ConstructorTable`), `lld/MSIL/OutputSegment.cpp` (runs it first, executables only), `llvm/lib/CLI/Trailer.cpp` if the trailer emits the calls | XS | P3-A lifecycle |
| `__ELF__` on Linux MSIL triples | `clang/lib/Basic/Targets/MSIL.h` (or the existing `InitPreprocessor.cpp:1526-1528` test), `clang/test/Preprocessor/` | XS | first P3 milestone |
| VEH documentation (BL17) and the callbacks list entry | Status.md / GettingStarted.md; a Windows `msil/test/M3` case that registers a handler and throws (a deterministic transcript on Framework/.NET 8/.NET 10 when no hardware exception is raised) | XS | P3-B callbacks |
| AppDomain key convention (BL18): one IL helper both users call | `llvm/lib/CLI/Trailer.cpp` or `lld/MSIL/Synthetic.cpp` (one `'<msil>shared'(string)` helper), BCL members in `BCLMembers.def` | XS-S | the first of atomics-16 or CB4 (b) |
| Docs: Design.md items 16-23 and section 4, Status.md corrections | `llvm/docs/MSIL/Design.md` (user-approved edits only), `Status.md` | S | first P3 milestone |

Header impact of these ideas:
- `MSILLink.h` and `BCLMembers.def` are included by `llvm/lib/CLI`, the MSIL target and `lld/MSIL`, so tens of TUs rebuild, not upstream-wide. Front-load both into the first P3 milestone with helpers-switches WP-S2 and atomics WP 2.
- `clang/lib/Basic/Targets/MSIL.h` is included only by `Targets.cpp`.

---

## 10. Risks and what was not verified

- **AArch64** `sret` (x8) and HFA classification for struct-by-value P/Invokes: *unverified* (no aarch64
  execution host).
- **The SysV pad rule** was measured for one shape (`__int128` after one stack argument). The general rule (count
  GPR/SSE registers and stack eightbytes per the classification; pad before every 16-byte-aligned memory argument
  at an odd slot) follows the ABI document and is *untested* beyond that shape; the WP's tests must cover mixed
  integer/float and several aligned arguments.
- **VEH**: `AddVectoredContinueHandler` and `SetUnhandledExceptionFilter` were not run. Hardware exceptions other
  than a null dereference (integer divide by zero) were not run. UCO thunks (the switch) were not run separately,
  but the fatal message names UCO for a delegate thunk too.
- **`__ELF__` defined** may route some code to ELF-only asm; none was found in the corpus or the hosted-libc
  downloads (Lua, SQLite, zlib).
- **GetCommandLineW interception** changes what a Win32 API returns inside programs. That is by design; libraries keep
  the native result. `Environment.CommandLine` was measured on .NET 8, .NET 10 and Framework 4.8 (Windows only).
- **c-testsuite test licences** (scc, tinycc): the `.otags` name the projects. The projects' licence files were not read.
- **The spilled `'<msil>Data'` race** was not reproduced (T8).
- **VS 18 detection** (E8): the cause was not isolated (a COM Setup API query failing in this environment, or a
  detection gap). Native clang behaves the same.
- The replacement texts assume sibling decisions that may change: D-FT1, H1/H2, S1-S5, CB1-CB10, sjlj S1-S5,
  variadics V1-V7, lifecycle L1-L16, Q-AT1-Q-AT12 and fp-helpers F1-L3. Each dependent text is marked in section 4.1.
- `hosted-libc` had no report when this pass was written. Its findings may add rows to F1 (which libc APIs
  pass structs by value), to D-14 and to the P3-B paragraph.

---

## Appendix: files (`backlog-p3-files/`)

| Path | What |
|---|---|
| `e1-retarget/` | `pause.c`, `fastfail.c`, `rustff.c`, `dispatch.c`, `cpu.c`, `rust-artifacts.ll`, `results.txt` (every diagnostic, both triples) |
| `e2-cmdline/` | `cmdline.c` + `run.ps1` + `run.log` (Windows: Framework, `dotnet x.exe`, `dotnet exec`); `linux/progname.c` + `run-wsl.log`; `cs/` (`Program.cs`, `cs.csproj`, `cs-multi.csproj`, `run.log`, `run-multi.log`: `Environment.CommandLine` on .NET 8, .NET 10, Framework 4.8) |
| `e3-msvolatile/` | `vol.c`, `results.txt` |
| `e4-elf/` | `results.txt` (grep counts and predefines) |
| `e5-sbv/` | `lldiv.c`, `div.c`, `div2.c`, `sret.c` (Win64 `sret`), `sret24.c` + `nat24.c` (SysV `sret`), `nat24b.c` + `il/pinv16.il` (SysV two-register result and byval), run scripts and logs (`results.txt`, `run3.log`, `il/run.log`) |
| `e6-design/` | `f.c`, `results.txt` (what `clang -c` writes) |
| `e7-github/` | `results.txt` (default branch, workflow runs) |
| `e8-vsdetect/` | `hello.c`, `results.txt` (paths the driver finds outside the dev shell, native and MSIL) |
| `e9-i128/` | `nat128.c` (the native side), `build.sh` (`clang` + `ld.lld` for x86_64-unknown-linux-gnu), `nat128-asm-excerpt.txt` (where the native code reads each argument), `i128.il` (pack-8/pack-16 value types), `i128n.il` (`System.Int128`), `i128pad.il` (the pad parameter), `rc8.json`/`rc10.json`, `run-wsl.sh`/`run-wsl2.sh` and their logs |
| `e10-veh/` | `Program.cs`, `veh.csproj` (net48; net8.0; net10.0), `run.log` (per runtime, trigger and registration order) |

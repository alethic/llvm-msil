# Phase 3 decision brief: runtime support, host-libc programs and plan

- Date: 2026-10-28. Checkout: branch `msil` at `cc4c79787d6a` (M23, the Phase 2 exit). Key: `decision-brief` (the
  Phase 3 synthesizer).
- Built from the ten Phase 3 recon reports in this directory (index in section 10) and the verdicts of three judges
  (cost-risk, C fidelity, .NET value). The judges wrote no report files: their scores reached this brief through the
  recon workflow, and their experiments (J1-J12) are in `C:\llvm-msil-scratch\p3\judge-<lens>\`. No new experiment
  was run for this brief. Every number comes from a linked report or a judge.
- Source lines re-read in this checkout for this brief: `MSILTargetMachine.cpp:268-297` (today's IR pass order),
  `clang/lib/Driver/ToolChains/MSIL.cpp:247-253` (the driver already appends `clang_rt.builtins` when it exists),
  `ExpandVariadics.cpp:1126` (`VariadicABIInfo::create`), `MSILPrepareIR.cpp:1075-1079, 1406-1433, 2320-2325`
  (variadic, destructor and TLS errors), `MSILISelLowering.cpp:396` (`setMaxAtomicSizeInBitsSupported(0)`),
  `lld/MSIL/NativeLibraries.cpp:80` (the callback list), `msil/utils/jitall/build-jitall.py:214` against `:422-425`
  (`--csharp` compiles without `-optimize+`), `ImplementationPlan.md:8359` (Q11). clang's options file is
  `clang/include/clang/Options/Options.td`; the recons' `clang/include/clang/Driver/Options.td` does not exist.
- Lenses in the score lines: **CR** cost-risk, **CF** C fidelity, **DV** .NET value. Scores are out of 10.
- Reading order: section 1 (the shape, 2 minutes), section 2.0 (the answer sheet, 1 minute), then the entries of
  section 2 you want to look at (the contested ones are marked; about 10 minutes). Sections 3-10 are reference.

---

## 1. The recommended Phase 3 shape

- **Twelve milestones, M24-M35, ordered by the real program each one unblocks** (R1): foundations and guards, the
  helper archive, variadics (zlib), setjmp/longjmp (Lua), native-callable function pointers and threads (`lua.c`),
  the C exit protocol, atomics (SQLite), `thread_local`, the cyclic shadow stack, codegen switches, structs by value,
  then the exit. P3-A and P3-B stay as labels in the documents. There is **no front-loaded header milestone**: no
  Phase 3 change needs an IR intrinsic, `RuntimeLibcalls.td` or `CallingConv.h`, and the one `Attr.td` change (the
  `msil_fixed_arity` attribute) moves to the managed-calls work (R14, R21).
- **M24 first removes live Phase 2 hazards.** The Linux object pipeline binds glibc's `setjmp`/`longjmp` natively and
  "works" by undefined behaviour. f16 helpers bind to `libgcc_s` and compute 0x0000 instead of 0x3E00. A
  compiler-runtime signature mismatch only warns, then crashes with `NullReferenceException`.
  `asm volatile("":::"memory")` is an IL comment, so pre-C11 spin loops hang on Framework, .NET 8 TC=0 and .NET 10.
- **Private helpers come in three kinds** (R2):
  - compiler-rt's builtins built for each MSIL triple as a CIL archive (`clang_rt.builtins`), which the driver
    already passes to msil-ld when it exists;
  - IL from the trailer builder for runtime services that need .NET types (exit lists, thunks, TLS holder), shared by
    `-S` and msil-ld;
  - per-module `linkonce_odr` IR where `-S` needs a body (setjmp helpers, i128).

  There is no bitcode archive and no runtime assembly.
- **Every C function pointer becomes a native-callable value**: a kept-alive delegate thunk by default, an
  `[UnmanagedCallersOnly]` thunk under `msil-ld --uco-thunks`. Each indirect call looks the value up in an exact
  per-assembly table: a hit is a managed `calli`, a miss a `calli unmanaged cdecl` (R3).
- **`exit` drives the C exit sequence on the calling thread**; `AppDomain.ProcessExit` is only a fallback (R4).
  Assemblies share state through one versioned `AppDomain` key convention, used only where C needs a process-wide
  object (R5).
- **Variadics** use LLVM's ExpandVariadics with MSIL ABIs and a native-layout `va_list`. Native variadic functions
  are called through their v-variants or with fixed arity, never with CLR vararg (R14).
- **setjmp/longjmp** comes from the Emscripten SjLj lowering: wrappers whose whole body is an IL `.try` with a fixed
  filter; `longjmp` throws `ExternalException` (R8).
- **Atomics** lower inline to `volatile.` accesses and `Interlocked` calls, with compare-exchange loops for the rest.
  16-byte atomics come from compiler-rt's `atomic.c` with one process-wide lock table.
- **`thread_local`** is a `[ThreadStatic]` pointer to an unmanaged per-thread block copied from the TLS template and
  freed by a finalizable holder.
- **Dynamic allocas** are a `localloc` when no loop repeats them and a chunked per-thread shadow stack when one does
  (R6).
- **Codegen switches** are MSIL target features (`-mmsil-<name>`), all off on every triple. Exact `Math`
  substitutions become defaults after property tests (R9, R10). The recommended defaults need no switch and no
  reference, and run on .NET Framework 4.8 and modern .NET alike.
- **The corpus** is zlib 1.3.2 (full), Lua 5.4.9 with its test suite and SQLite 3.54.1, whose licences all allow CI.
  At the exit they run on both x86_64 triples, at -O0 and -O2, through objects and LTO, on every runtime. c-testsuite
  gains a second list of its 70 printing tests, with 138 of 140 test-triple pairs expected to pass.
- **Your text changes under these recommendations** (each decision names its text):
  - Design.md 4.1 (ProcessExit), 4.2 (shadow stack for every dynamic alloca), 4.5 ("one range compare"), 3 (two
    predefines), 3.1 (native vararg fallback), 6 and 7.1 (System.Half; switches as "module flags or TargetOptions");
  - plan D16 (BCL intrinsics only behind switches) and D21 (the verifier allow-list);
  - the plan's `msil_fixed_arity` attribute.

---

## 2. Decisions that need you

### 2.0 Answer sheet

Answer by number, for example "R1-R24 as recommended, except R13 (b)". **Contested** marks decisions where the
judges disagree; the entry spells out the trade-off.

| # | Decision | Recommended | Judges |
|---|---|---|---|
| R1 | Order of the Phase 3 milestones | Program-driven (section 5), with function pointers before lifecycle, threads and TLS | **Contested** |
| R2 | How helpers are delivered and built, and how the archive reaches msil-ld | compiler-rt CIL archive + trailer IL + per-module IR; a tracked direct-compile script | build method contested |
| R3 | C function pointers and thunks | A2: native-callable values, exact table; eager delegates (straight-line); `--uco-thunks` | agree |
| R4 | C exit protocol (Design.md 4.1, 6) | `exit` on the calling thread, libraries after, `Environment.Exit`; ProcessExit only as fallback | agree |
| R5 | State shared by all assemblies of a process | One `<msil>v1:<concern>` convention, used by the 16-byte lock table and a root-exit slot; registry deferred | **Contested** |
| R6 | Dynamic allocas (Design.md 4.2) | Hybrid H, staged: acyclic `localloc` early, cyclic shadow stack after TLS | mostly agree |
| R7 | Thread-exit destructors of C and Rust | Intercept the registrations; run them on toolchain-started threads and the main thread | agree |
| R8 | setjmp/longjmp: cost and native frames | Accept E1 and its cost; Windows-only across native frames, a clear abort on Linux | agree |
| R9 | Codegen switches: carrier, spelling, reference check, first list | Target features `-mmsil-<name>`; Q11 optional substitution only for bit-exact members; all off | reference check contested |
| R10 | Exact BCL substitutions by default; libm | Yes, after property tests (amends D16); libm stays P/Invoke | mostly agree |
| R11 | Verification policy (D21) | Amend once: Interlocked on native int, delegate `.ctor`, EH filters | agree |
| R12 | Predefines (Design.md 3) | Define `__ELF__` on Linux triples; cap the lock-free width at 8 bytes | agree |
| R13 | Windows-triple destructors; hand-written expected files | Run `.CRT$XT` and destructors before the flush; `rule:` expected files | mostly agree |
| R14 | Variadics: three departures from the plan | No attribute in Phase 3; toolchain-authored UCRT printf; no CLR vararg | agree |
| R15 | `long double` on wide-native triples | Forward the `l` functions; `%L` warning, scanf `%L` an error | **Contested** |
| R16 | Architecture intrinsics, `__fastfail`, `rdtsc` | Scalar fallbacks; `FailFast`; a tick helper; SIMD in Phase 4 | `__fastfail` contested |
| R17 | Signal and vectored exception handlers | Allow; a link warning for the signal family on Linux; document | agree |
| R18 | Structs by value at P/Invoke boundaries | Phase 3, M34 | agree |
| R19 | Native compiler-rt in the Linux helper oracle | Yes | agree |
| R20 | Milestone gates (Q32); the primary modern runtime | A narrow gate with CF's conditions; .NET 10 primary | **Contested** |
| R21 | Upstream merge; front-loading headers | Merge before M24; no header milestone | front-load contested |
| R22 | Status.md items 16-22 (and a new 23) for Design.md | Approve with the Phase 3 plan commit (16, 19, 21 amended) | - |
| R23 | May CI fetch CoreMark and c-testsuite? | Allow | - |
| R24 | Q38: first dispatch of `msil-ci.yml` | Dispatch once now (`msil` is already the default branch) | - |

R22-R24 are the questions of inputs section 4 that no recon decision covers. Section 4 maps the other questions.

### R1. Order of the Phase 3 milestones (**contested**)

**Question.** Plan section 7 implies P3-A (frames, TLS, lifecycle, helpers, switches) before P3-B (variadics,
callbacks, setjmp, atomics, host libc). Keep that order, or order the milestones by the real program each one
unblocks?

**Options.**
- (a) Plan order, P3-A then P3-B.
- (b) Program-driven: foundations, helper archive, variadics (zlib), setjmp (Lua), function pointers and threads
  (`lua.c`), exit protocol, atomics (SQLite), TLS, cyclic shadow stack, switches, structs by value, exit.
- (c) Feature-parallel milestones with two or three features each.

**Judges.** CR: (a) 4, (b) 8, (c) 3. CF and DV did not score the question, but each proposed a sequence:
- CF: guards first, then P3-A (archive, frames, TLS, lifecycle), then P3-B (variadics, callbacks, setjmp, atomics,
  structs, SQLite).
- DV: P3-A first too, but with function pointers moved into P3-A ahead of lifecycle, because the exit-handler runner,
  TLS destructors, thread starts, `qsort` and `signal` all call C function pointers.

**Evidence.**
- None of zlib, Lua and SQLite uses `thread_local`, a VLA or `alloca`, a struct by value or a switch
  ([hosted-libc](hosted-libc.md) 8.1). P3-A's two largest items unblock no hosted program: TLS with the shadow stack
  (about 4-5k lines with tests) and the switch framework (about 1.2k).
- Behind a simulation of Phase 3, the three programs need:
  - zlib full: only variadics; it then matches native on all five runtime configurations;
  - Lua: variadics and setjmp, plus `signal` for `lua.c`;
  - SQLite: variadics, atomics, callbacks, `atexit`, `wmain`, `-DSQLITE_OMIT_SEH` and the fixed-arity address
    adapter.
- Seven recons edit the same five hot files: `MSILPrepareIR.cpp`, `MSILAsmPrinter.cpp`, `Trailer.cpp`,
  `BCLMembers.def` and `NativeLibraries.cpp`. With one owner per file per milestone, (c) collapses into giant WPs.

**Trade-off.** CF orders by correctness (silent hazards first) and DV by the hub that everything consumes (function
pointers). The recommended sequence keeps both where it matters:
- The silent-hazard guards land in M24.
- Function pointers move up to M28, ahead of lifecycle, threads and TLS, which consume them. Lifecycle's handler
  runner then calls through the dispatch from the start and needs no interim helper.

What (b) gives up against CF's order: today's undiagnosed callback hazard (a function address passed to a native
function that is not on the callback list) stays open until M28 instead of about M26. Under (a), two things would
split: the thread-exit destructors (their trampolines need thunks), and the exit-handler runner, which would need an
interim managed-`calli` helper that switches over when thunks land.

**Recommendation.** (b), as outlined in section 5.

### R2. How private helpers are delivered and built (build method **contested**)

**Question.** What form do the "C helpers compiled by this toolchain and linked statically" take? How is the archive
built, and how does it reach msil-ld (inputs section 4)?

**Options.**
- Delivery:
  - (a) IL helpers only.
  - (b) Three kinds: compiler-rt's builtins as a per-triple CIL archive for compiler-rt's families; IL from the
    trailer builder (`cli::buildTrailer`) for runtime services that need .NET types; per-module `linkonce_odr` IR
    where `-S` must contain a body.
  - (c) A bitcode helper archive.
- Build:
  - (i) Teach compiler-rt's CMake about MSIL triples (`LLVM_BUILTIN_TARGETS`).
  - (ii) A tracked script that runs compiler-rt's stock CMake per triple.
  - (iii) A tracked direct-compile script with its own reviewed source list.

**Judges.**
- Delivery: CR (a) 3, (b) 9, (c) 3; CF (a) 6, (b) 9, (c) 3; DV (a) 4, (b) 9, (c) 2.
- Build: CR (i) 4, (ii) 5, (iii) 8. DV wants the source list inside compiler-rt plus a script. The helpers-switches
  recon wants (i)'s compiler-rt edits used through (ii), with (i) as the release path.

**Evidence.**
- compiler-rt's builtins compile for the MSIL triples. Linked from a CIL archive, the outputs are bit-identical to
  native on 7 configurations, also through `-flto` ([helpers-switches](helpers-switches.md) E1/E2). The test covers
  Annex G complex cases, `_Float16`, `powi` and i128 overflow. CR re-ran it: 181 sources, of which 153 compile with
  `__ELF__`, in 106 s at `-P4` under load (about 20 s quiet).
- A bitcode archive is a dealbreaker: msil-ld's LTO extracts every member that defines a RuntimeLibcalls name, used
  or not (HS-E5). A CIL archive extracts only what is referenced.
- The driver already appends `<resource-dir>/lib/<triple>/clang_rt.builtins.lib` or `libclang_rt.builtins.a` when
  the file exists (`MSIL.cpp:247-253`). The helpers stay private in `-shared` outputs (HS-E7).
- compiler-rt's stock CMake for an MSIL triple took 262 s to configure and 78 s to build under load, and 34 of 173
  steps failed (HS-E8). Its `CMakeLists.txt` had 52 upstream commits in the last year (CR J12).
- An archive member that holds only a constructor is never extracted. So startup services, such as the UCRT stdio
  options of hosted-libc H3, must be trailer IL, not C in the archive (CR's correction of hosted-libc H3 (b)).

**Trade-off (build).** (iii) needs no upstream edit and costs about 20 s per triple. But its source list is a fork
copy that can drift from compiler-rt, and the risk is a stale archive: the build agent must rebuild it after every
clang or backend change. (i) is the upstreamable form and suits releases.

**Recommendation.** (b), with build (iii) now and (i) recorded as the release and upstreaming path. The archive is
the triple's `clang_rt.builtins`, linked after the default libraries as native links pass compiler-rt. `-S` keeps
D14's frozen import table: a feature that needs an archive helper is a clear `-S` error, never a silent difference
(backlog BL8).

### R3. C function pointers and native-callable thunks

**Question.** How is a C function pointer represented? Which thunk makes it native-callable by default, and how is
`UnmanagedCallersOnly` enabled?

**Options.**
- Representation:
  - A1: every pointer native-callable, and every indirect call is `calli unmanaged`.
  - A2: every pointer native-callable, with an exact per-assembly lookup at each indirect call. A hit makes a managed
    `calli` of the C function; a miss makes a `calli unmanaged cdecl`.
  - B: managed values, converted at P/Invoke boundaries.
  - C: a range check or tagging (Design.md 4.5).
- Default thunk: eager kept-alive delegates (created straight-line or by a table-driven loop); lazy delegates; UCO
  chosen at run time.
- UCO switch: `msil-ld --uco-thunks`, with the attribute bound from the references (an error if none defines it); a
  private attribute class of the same name; a module flag.

**Judges.**
- A2: CR 8, CF 9, DV 9. A1 3/3/5, B 1/3/2, C 0/1/0 (not implementable).
- Eager delegates: CR 8, CF 8, DV 8 (straight-line); DV 6 for the table loop. Run-time UCO 3/5/4.
- `--uco-thunks`: 8/8/8. Private attribute class: CF 4, DV 3.

**Evidence** ([callbacks](callbacks.md)).
- Today's managed `ldftn` values, called by native code under concurrent GC, crashed Framework, .NET 8 and WSL
  .NET 8, and silently corrupted heap data on .NET 10.
- A managed `calli` of a native function stalls other threads' GCs: 1-2 gen0 GCs in 2 s against 755-2,644.
- Kept-alive delegates are correct on all five runtimes: under GC stress, as struct fields read by native code, with
  `qsort`, on native threads and in pthread key destructors.
- No per-assembly address range exists: delegate thunks come from a runtime-wide stub heap, and UCO entry points are
  interleaved precodes. So 4.5's "one range compare" becomes an exact open-addressing table. A hit costs 3.9-6.5 ns,
  against 2.8-4.6 ns for a plain managed `calli`.
- A2 keeps C-to-C indirect calls managed, which `longjmp` needs on Linux: an exception through a thunk aborts the
  process on .NET 8/10 Linux.
- CoreCLR honours `UnmanagedCallersOnlyAttribute` by name and Framework ignores it, so UCO can never be a
  Framework-safe default.
- DV J8: the table-driven creation loop crashes ILVerify in pass 1, and msil-verify pass 2's `ldftn` neutralization
  crashes ILVerify on any delegate construction. The straight-line shape (`new D(f)`, as C# emits it) verifies today.
- Start-up per 1,000 address-taken functions: straight-line 53-128 ms, table-driven 9-55 ms. Programs have 0-5
  address-taken functions (exit corpus), about 191 (Lua) and about 636 (SQLite) (CR J11).

**Recommendation.**
- A2.
- Eager kept-alive delegates, created straight-line by default. The table loop comes only with an msil-verify rule
  and a measured need (for example SQLite on Framework above about 100 ms).
- `--uco-thunks` as an msil-ld option bound from the references.
- No run-time UCO selection: it rests on undocumented by-name matching, and it binds an attribute that no given
  reference defines.
- Design.md 4.5 changes to the exact lookup.

### R4. The C exit protocol (Design.md 4.1 and 6)

**Question.** Design.md says destructors and `atexit` handlers run in `AppDomain.ProcessExit`. Keep that, or let
`exit` drive them?

**Options.**
- (a) ProcessExit, as written.
- (b) The toolchain's `exit` drives them:
  - The trailer defines `exit` and its family in IL, only when the link leaves them undefined: `atexit`,
    `__cxa_atexit`, `on_exit`/`_onexit`, `at_quick_exit` and `quick_exit`.
  - `exit` runs the assembly's handlers (LIFO) and its destructors on the calling thread. It then runs the linked
    libraries' `'<msil>fini'` in simulated reverse initialization order, then calls `Environment.Exit`.
  - ProcessExit is only a non-blocking fallback, subscribed at the end of the module initializer.
  - `'<msil>Main'` becomes `exit(main())` only when the output has exit machinery, so Phase 2 outputs stay
    byte-identical.

**Judges.** (a) CR 1, CF 2, DV 2 (dealbreakers); (b) CR 8, CF 9, DV 9.

**Evidence** ([lifecycle](lifecycle.md) 3-5).
- ProcessExit runs on the finalizer thread on every runtime, with the wrong `thread_local` block and shadow stack.
- Framework cuts handlers off after about 2.3-2.6 s.
- `Environment.Exit` inside ProcessExit deadlocks .NET 8 and .NET 10 (one run hung 35 minutes).
- A native `exit` reaches ProcessExit only on Framework, through UCRT's CorExitProcess path, which also skips the
  native `atexit` table. Neither ucrtbase nor `libc.so.6` exports `atexit`.
- The IL prototype of (b), a program plus a library with mscorlib references only, is correct on all five runtimes,
  including `exit` in a handler, on a second thread and from program and library constructors. Two earlier versions
  deadlocked .NET 8/10 and shaped the design: a handler waiting for a lock, and a subscription inside a running module
  initializer.

**Recommendation.** (b), replacing Design.md 4.1's and section 6's wording (lifecycle L16; text D-9 in section 8).
R5 covers the cross-assembly details and R13 the Windows destructors.

### R5. State shared by every assembly of a process (**contested**)

**Question.** Three C semantics are process-wide:
- the atomicity of a 16-byte object that a program and its `-shared` library share (atomics Q-AT6);
- managed calls through function pointers owned by another toolchain assembly (callbacks CB4, sjlj S6);
- `exit` called inside a library.

Your rules forbid a shared exception type and a support assembly, not shared state. Should assemblies share state by
name, and where?

**Options.**
- (a) Per-assembly state throughout Phase 3:
  - a lock table per assembly;
  - cross-assembly indirect calls take the miss path;
  - `exit` in a library runs that library's sequence and `Environment.Exit`, and the program's handlers run in its
    ProcessExit fallback.
- (b) One documented, versioned `AppDomain` key convention, `<msil>v1:<concern>`, holding BCL-typed values only
  (Monitor on an interned string). It is used only where C needs a process-wide object, and each consumer is adopted
  on its merits.

**Judges.**
- CR: per-assembly first 8, the convention now 5; per-assembly lock table 8, process-wide 5; CB4 miss path first 8.
- CF:
  - process-wide lock table 8, per-assembly 3 (dealbreaker: silent loss of atomicity);
  - registry 8, miss path 5;
  - a root-exit slot (`exit` in any assembly runs the entry assembly's exit) 8, per-assembly 6.
- DV: process-wide lock table 9, per-assembly 3; registry later (miss path first 8, registry now 5).
- backlog BL18: the convention now.

**Evidence.**
- `AppDomain.GetData`/`SetData` under an interned-string lock is shared across separately built assemblies on
  Framework, .NET 8 and .NET 10, using mscorlib-only members ([atomics-threads](atomics-threads.md) E5, callbacks
  2.10).
- Per-assembly lock tables make a 16-byte atomic shared with a library non-atomic, without a diagnostic.
- The registry's only beneficiary is a cross-assembly `longjmp` through a function pointer (libpng's `longjmp_fn`)
  on .NET 8 Linux, where a `calli unmanaged` to a throwing thunk aborts. .NET 10 Linux and every Windows runtime catch
  it.
- No hosted corpus program needs any of the three.
- CF J5: .NET 8 raises ProcessExit on SIGTERM, so a Linux executable's fallback runs C handlers where native code runs
  none.

**Trade-off.** CR weighs cost: none of the three has a corpus consumer, so do not design shared state before it has
a user. CF and DV weigh silent wrong behaviour: a per-assembly lock table loses atomicity silently, and this project
otherwise turns such cases into errors. The registry protects a shrinking case (.NET 8 leaves support on 2026-11-10),
so two judges defer it.

**Recommendation.** (b).
- Document the convention (Design.md 6, text D-41).
- Use it for the root-exit slot (exit milestone, M29). The slot also lets Linux-triple executables skip the
  ProcessExit fallback.
- Use it for the 16-byte lock table (atomics milestone, M30).
- Defer the cross-assembly registry. Indirect calls into another assembly take the miss path, and Linux-triple thunks
  abort with a clear message when a `longjmp` crosses native frames (R8).

### R6. Dynamic allocas (Design.md 4.2)

**Options.**
- A: one `localloc` per dynamic alloca.
- B: a per-thread shadow stack for every dynamic alloca (Design.md 4.2 as written).
- C: `localloc` with per-site reuse.
- H: a `localloc` with a native-int size (a new `LOCALLOC_I64`) for an alloca on no cycle of the CFG, and a chunked
  per-thread shadow stack for one on a cycle. Staged as H0 (acyclic, early) and H1 (cyclic, after TLS).

**Judges.** CR: A 2, B 4, C 3, H in one step 7, H staged 8. CF: A 2, B 8, C 4, H 8. DV: A 1, B 6, C 3, H 8.

**Evidence** ([frame-tls](frame-tls.md)).
- A 64-byte VLA in a loop kills the process (CLR stack overflow) after 23,552 iterations on Windows .NET 8/10, at
  most 16,384 on Framework and 130,048 under WSL. Native code reuses one address for a million iterations (E1).
- An acyclic alloca runs at most once per call, so `localloc`'s until-return lifetime is invisible to C.
- The CLR reclaims `localloc` memory on any unwind (exception, `longjmp`, another assembly). So H needs fault clauses
  only in functions with a cyclic alloca.
- The only corpus consumer is c-testsuite 00207, an acyclic VLA at -O0 (CR J10).
- **Correction.** frame-tls's cost argument (shadow stack 41-174 ns against 11-23 ns per call) was measured on C#
  compiled without `-optimize+`, that is on MinOpts code: `build-jitall.py --csharp` omits the flag. With
  optimization, the two are equal on .NET 8/10, and the shadow stack costs about 7-10 ns more on Framework (DV J1c).
  B is viable on cost; H wins on unwind safety, and because H0 needs neither TLS nor EH clauses.

**Trade-off.** CF scores B equal to H. B is one uniform mechanism. H has two, joined by a cycle analysis that must
run after the setjmp lowering, because setjmp re-entry makes a cycle.

**Recommendation.** H, staged: H0 with variadics (M26), H1 after TLS (M32). Amend Design.md 4.2 on unwind-safety
grounds (text D-10). Until H1 lands, an alloca on a cycle is a clear compile error.

### R7. Thread-exit destructors registered by C or Rust

**Question.** What happens to `FlsAlloc` callbacks, `__cxa_thread_atexit_impl`, and `tss_create` and
`pthread_key_create` destructors?

**Options.**
- (a) Bind them natively through thunks.
- (b) Intercept each registration into a per-thread list (a reserved slot of the TLS block header). Toolchain
  thread-start trampolines run the list, and so does main-thread exit.
- (c) Run them from the holder's finalizer.

**Judges.** (a) CR 2, CF 3, DV 2; (b) CR 8 (staged), CF 8, DV 8; (c) 1/1/1.

**Evidence.**
- On .NET 10 an `FlsAlloc` callback into managed code kills the process at thread exit ("Attempt to execute managed
  code after the .NET runtime thread state has been destroyed"), for managed and native threads alike (frame-tls E7).
- CF J2: `tss_create` destructors of `VCRUNTIME140_THREADS.dll` do the same.
- pthread key destructors run on Linux, but after the CLR has detached the thread: they see a fresh `[ThreadStatic]`
  view on .NET 8 and a re-attached one on .NET 10.
- `ExitThread` and `pthread_exit` skip managed `finally` blocks (E8).
- Rust std uses `FlsAlloc` on Windows, and `__cxa_thread_atexit_impl` or a key on Linux.

**Recommendation.** (b), with CF's completion:
- Intercept `FlsAlloc`, `tss_create` (Windows) and `__cxa_thread_atexit_impl`.
- Wrap the start routines of `pthread_create`, `thrd_create`, `CreateThread` and `_beginthreadex` in a trampoline.
  The threads stay host-libc threads.
- Intercept `pthread_exit`, `thrd_exit`, `ExitThread` and `_endthreadex`, so that the list runs first.
- Keep `pthread_key_create` (and glibc's `tss_create`) native on Linux, documented, with a test that a key destructor
  touching `thread_local` reads no freed block.
- Threads started by native code run no C or Rust TLS destructors (documented).
- Until M31 these registrations are link errors (from M24).

### R8. setjmp/longjmp: the cost and native frames

**Question.** Accept the E1 design, with its per-`longjmp` cost, and with `longjmp` out of native-called callbacks
working on Windows only?

**Options.**
- E1: wrappers around the calls that follow a `setjmp`, each with its whole body in an IL `.try` with a fixed filter.
  `longjmp` arms a status block that a 16-byte `jmp_buf` prefix names, and throws
  `ExternalException(null, (int)statusBlock)`.
- Alternatives: E2, the caller's whole body in one `.try` with values demoted; E3, real EH IR.
- Native frames: document only, or document and add a filter in Linux-triple thunks that aborts with a clear message.

**Judges.** E1: CR 8, CF 9, DV 8 (E2 4/6/5, E3 2/5/4). `ExternalException`: 8/8/8. Accept the cost: CR 8, DV 8, CF
accepts. The Linux filter: CF 8, DV 8.

**Evidence** ([sjlj](sjlj.md)).
- The E1 prototype (real backend IL, with the wrapper and the throw patched in) is correct on all five runtimes.
- The normal path costs 1-3 ns per wrapped call on .NET 8/10 and 5-8 ns on Framework.
- A million round trips cost, per round trip:
  - native glibc about 65 ns, native UCRT x64 about 2 us;
  - E1 on Framework about 27 us;
  - E1 on .NET 8: 26 us on Windows, 44 us on Linux;
  - E1 on .NET 10: 6.6 us on Windows, 8.5 us on Linux.
- A throw through native `qsort` is caught on every Windows runtime and aborts with SIGABRT on .NET 8/10 Linux.
- `ExternalException` is in mscorlib (Framework) and System.Runtime (.NET 8/10 references).

**Recommendation.** Accept E1 and its cost. Document `longjmp` out of native-called callbacks as Windows-only, with
the Linux thunk filter's message. Document that every `longjmp` is a first-chance CLR exception, which debuggers and
`FirstChanceException` handlers see.

### R9. Codegen switches (reference check **contested**)

**Question.** The plan says switches are "module flags or TargetOptions". Which carrier and spelling should they
have, how are references checked, and which switches come first?

**Options.**
- Carrier: MSIL target features per function (`+msil-<name>`, the model of WebAssembly's `-mbulk-memory`); module
  flags; TargetOptions or `cl::opt`.
- Spelling: `-mmsil-<name>`/`-mno-msil-<name>`, with link-time switches as msil-ld options; `-fmsil-<name>`;
  `-mdotnet-<name>`.
- Reference check:
  - Your Phase 2 answer Q11 made API substitutions optional: they fall back to a helper and are reported under
    `--verbose`. That means OptionalSubstitute requests in a new record v1.1 subsection, with token rewriting in
    msil-ld.
  - CR proposes Required binding only: the switch makes codegen reference the member, and M21's binder errors when no
    reference defines it.

**Judges.**
- Target features: CR 8, CF 9, DV 8. Module flags: 3/3/2 (dealbreaker: LTO merges change their meaning).
- Spelling: DV `-mmsil` 8, `-mdotnet` 1.
- Reference check: CR Required 8, Optional 5; CF Optional only for members bit-exact with their fallback 9, Optional
  for all 5; DV Optional 8.
- All three: every switch off on every triple.

**Evidence.**
- Module flags merge per module under LTO: Min turns an absent flag into 0, Max takes the maximum, Error stops the
  link (HS-E10).
- M21's binder already implements the optional strengths, with no producer.
- `Math.Min(-0, +0)` is +0 on Framework (DV J4), so not every BCL member can silently replace its fallback.

**Trade-off.** Required binding saves record v1.1 and the token rewriting (about 400 lines), but contradicts Q11.
CF's condition keeps a program's results independent of which references are given.

**Recommendation.**
- Target features, spelled `-mmsil-<name>`/`-mno-msil-<name>`; link-time switches as msil-ld options
  (`--disable-runtime-marshalling`, `--uco-thunks`).
- Q11's optional substitution (record v1.1) only for members proven bit-exact with their fallback; otherwise a probe
  member, or no switch.
- Every switch off on every triple.
- First list: `msil-bitops`, `msil-fma`, `msil-minmax`, `--disable-runtime-marshalling`, `--uco-thunks`.
- Not in the list:
  - no `msil-int128`: it gives no speed gain over the i128 helpers;
  - System.Runtime.Intrinsics switches stay in Phase 4 (plan P4-A);
  - SuppressGCTransition comes later, as a per-declaration attribute;
  - no SkipLocalsInit: `localloc` frames are already not zeroed (HS-E12).
- msil-ld may choose between identical-result APIs by references for its own code (the M21 `NativeLibrary`
  precedent).
- Schedule: M33. Plan section 7, Design.md 7.1 and FeatureIdeas item 2 follow.

### R10. Exact BCL substitutions by default, and libm

**Question.** D16 puts BCL intrinsics behind switches. May exact substitutions be defaults?

**Options.**
- (a) Keep libm P/Invokes for `sqrt`, `floor` and the rest, and `localloc` slots for bitcasts.
- (b) Map these intrinsics to `Math`, each member only after a property test on every runtime that includes NaN sign
  and payload bits:
  - `llvm.sqrt`, `floor`, `ceil` (as `-Math.Floor(-x)`), `trunc` and `fabs`;
  - `rint`, `nearbyint` and `roundeven` to `Math.Round`; `round` to `Math.Round` with `AwayFromZero`;
  - f64 bitcasts to `BitConverter`; f32 bitcasts through a private explicit-layout union value type.
- libm's other functions: keep the P/Invokes, with an optional later `msil-bcl-math` that never applies on Framework.

**Judges.** (b): CR 5 (performance only, so late), CF 8 (equal to (a) once proven), DV 9. (a): DV 4. libm P/Invoke
as the default: 9/9/8. libm mapped to `Math` by default: 1-2 (Framework gives wrong answers).

**Evidence** (helpers-switches E13-E16 and its FP probe).
- The listed members are bit-identical on 8 configurations, except `Math.Ceiling(-0.5)`, which is +0 on Framework
  (hence `-Floor(-x)`).
- They are 3-13x faster and save about 200 JIT bytes per site.
- The union bitcast is 17 bytes against 61 and verifies.
- Framework's `Math` is wrong for libm: `sin(1e22)` = 1e22, `pow(1, NaN)` = NaN, `exp(-745.13)` = 0.
- With `-fmath-errno` (the Linux default) clang emits calls, so errno behaviour never changes.

**Recommendation.** (b), in M33. libm stays a host import, and D16 is amended.

### R11. Verification policy (D21) for Phase 3's IL shapes

**Question.** D21 fixes the verifier allow-list. Phase 3 brings three shapes it does not allow:
- a native int passed as `T&` to `Interlocked` (atomics V1);
- a delegate `.ctor` fed by `ldftn` (thunks);
- EH clauses with filters (setjmp).

**Options.** V1, plus one D21 amendment for all three shapes, each allowance pinned by listing tests and limited to
its exact shape. Or V2: private aggressive-inlining helpers for atomics, whose bodies need the same exemption.

**Judges.** V1: CR 7, CF 8 (neutral), DV 9. V2: 5/8/6.

**Evidence.**
- A native int as `T&` runs correctly on Framework, .NET 8 and .NET 10, and JITs to `lock xadd`, `xchg` and
  `lock cmpxchg`.
- ILVerify and PEVerify report `[found Native Int][expected address of Int32]` (StackUnexpected) and continue
  (atomics E3).
- DV J5: it is byte for byte the IL that csc emits for `Interlocked.Add(ref *(int*)p, v)` in unsafe C#.
- DV J8: the delegate shape crashes ILVerify under msil-verify pass 2.

**Recommendation.** Amend D21 once. Each allowance arrives with the milestone that first needs it: filters in M27,
delegate construction in M28, Interlocked in M30.

### R12. Two predefine exceptions (Design.md 3)

**Options.**
- `__ELF__`: define it on Linux MSIL triples, or keep it undefined and add a `__MSIL__` branch to compiler-rt's
  `int_lib.h`.
- Lock-free answers: cap `MaxAtomicInlineWidth` at 64 in `MSILTargetInfo` (16-byte atomics never lock-free, layouts
  unchanged), or keep the native answers.

**Judges.** Define `__ELF__`: 8/8/8 (the `int_lib.h` branch 6/6/5). The cap: 9/9/9 (native answers: 1 for CF and DV).

**Evidence.**
- glibc 2.43's headers test `__ELF__` in no file. compiler-rt's `int_lib.h:51/70` needs it: 5 of 176 sources compile
  without it. msil-ld already links Linux triples with ELF's rules ([backlog-p3](backlog-p3.md) E4, HS E1).
- On `aarch64-*-msil`, and on x86-64 with `-mcx16`, clang claims 16-byte atomics are always lock-free. compiler-rt's
  `__atomic_load_16` then becomes `load atomic i128`, which the backend turns into a call of itself (atomics E5/E7).

**Recommendation.** Define `__ELF__`, which answers plan section 10 item 9, and cap the width at 64 (texts D-5).

### R13. Windows-triple destructors and hand-written expected files

**Question.** Native `/MD`, which the Windows MSIL triple binds, never runs `__attribute__((destructor))` or
`.CRT$XT*`. `/MT` runs `.CRT$XT` in ascending order after the stdio teardown, and crashes with 0xC0000005 when a
destructor prints. What should the Windows MSIL triple do?

**Options.**
- (a) Run them after the handlers, in ascending section-name order (`.CRT$XTX` for the default priority), before the
  stdio flush.
- (b) `/MD` fidelity: never run them (drop or diagnose).
- (c) ELF order on every triple.
- Expected files: no native build prints the `p6-dtors` and `crt-xt` transcripts on Windows. Accept hand-written
  Windows expected files under a new `rule: <why>` native kind in `matrix.txt`? This amends the rule that every case
  prints the native transcript or a named diagnostic.

**Judges.** (a) CR 6 (your call, leaning (a)), CF 8, DV 8; (b) CR 6, CF 3, DV 4; (c) CF 5, DV 3. Hand-written files:
CR 6, CF 7, DV 8.

**Evidence** (lifecycle's native probes). Under `/MD`, `crt-xt` prints only `main` and `p6-dtors` prints `BCDM`;
under `/MT` both crash. (a) keeps clang's COFF order: `XTA00101`, `XTC`, `XTC00300`, `XTU`, `XTX` (measured with
`_exit`).

**Recommendation.** (a), with hand-written Windows expected files under `rule:`.

### R14. Variadics: three departures from the plan text

**Options** (one departure each):
- (a) **No `msil_fixed_arity` clang attribute in Phase 3.** Instead, a built-in per-triple fixed-arity list (glibc's
  `open`, `fcntl`, `ioctl`, `syscall`, `execl`, ...; UCRT's 20 exported variadics) plus msil-ld's error naming the
  function. The attribute moves to the managed-calls work, which adds clang attributes anyway.
- (b) **windows-msvc's non-inline printf family as toolchain-authored C** in the helper archive, over
  `__stdio_common_v*`. The alternative, UCRT's own header compiled with `_CRT_STDIO_INLINE` empty, redistributes code
  derived from the Windows SDK.
- (c) **Never CLR vararg**: Design.md 3.1's native-vararg fallback is dropped.

**Judges.** (a): CR 8 (the attribute now 3), CF 8, DV 8. (b): 8/8/8 (UCRT's header: CF 9 for fidelity but
redistribution, DV 4). (c): CLR vararg 1/4/2.

**Evidence** ([variadics](variadics.md)).
- The attribute alone rebuilds 526 clang objects (about 4,000 CPU-s, CR J1), and no other Phase 3 change needs
  `Attr.td`.
- SQLite's and Rust std's natives (`fcntl64`, `mremap`, `open64`, `fcntl`, `syscall`) are on the glibc list.
- ucrtbase exports no `printf`. Six of c-testsuite's 70 printing tests declare `printf` themselves, and a stand-in
  archive fixed all six.
- A vararg P/Invoke throws `InvalidProgramException` on .NET 8/10 Linux.
- Fixed-arity floating-point arguments on SysV were silently wrong in 3 of 24 processes: the CLR leaves AL as
  garbage, and glibc's `sprintf` tests it.

**Recommendation.** All three.

### R15. `long double` on the triples whose native one is wider (**contested**)

**Question.** `long double` is `double` on every MSIL triple (permanent, Design.md 3). On x86_64 Linux and MinGW and
on aarch64 Linux:
- native `sqrtl` and `strtold` are link errors today;
- `printf("%Lf")` through glibc prints garbage;
- `scanf("%Lf")` writes 16 bytes into an 8-byte object.

**Options.**
- `l` functions: (a) keep the link errors; (b) forwarders in the archive (`sqrtl` -> `sqrt`, ...).
- `%L`:
  - (i) nothing;
  - (ii) a clang warning for `%L` on those triples;
  - (iii) msil-ld routes the native printf and scanf v-variants through an archive wrapper that rewrites `L`
    conversions (printf: drop `L` before `aAeEfFgG`; scanf: `L` becomes `l`), with the warning as a backstop.

**Judges.** (b): CR 6 (your call), CF 8, DV 8; (a): CF 5, DV 4. (ii): CR 4 (later), CF 6, DV 8. (iii): CF 9, the only
judge to score it. (i): CF 1.

**Evidence** ([fp-helpers](fp-helpers.md) F13).
- A forwarder member links ahead of `libm.so.6` and runs on WSL .NET 10.
- Native glibc `printf("%Lf")` given a `double` prints `nan`, then a stale value.
- No hosted corpus program uses `%L`.

**Trade-off.** (iii) is exact for computed formats too and removes the scanf corruption. It costs one format scan
per call and wrappers for the wide, `_chk`, `__isoc99` and `__isoc23` variants (about 300 lines). (ii) covers literal
formats, which are nearly all real uses, for about 40 lines.

**Recommendation.** (b) with (ii): the scanf-family diagnostic is an error by default (memory corruption), and the
printf-family one a warning. (iii) comes later if a program needs it. c-testsuite 00204 stays a named `long-double`
exclusion on linux-gnu.

### R16. Architecture intrinsics left by ports and Rust (`__fastfail` **contested**)

**Options.**
- Fallbacks without a switch:
  - `pause` becomes `Thread.SpinWait(1)`;
  - fences and `clflush` become `Interlocked.MemoryBarrier`;
  - `crc32` becomes a table helper in the archive;
  - `bextr` becomes generic IR;
  - `__builtin_cpu_supports` reads a `__cpu_model` that reports no features.
- `__fastfail` (clang's builtin and Rust's `int $$0x29`): `Environment.FailFast`, or kernel32
  `TerminateProcess(GetCurrentProcess(), 0xC0000409)`.
- `rdtsc`: a monotonic tick helper (QueryPerformanceCounter, `clock_gettime`), or an error.
- In every option, `rdrand` stays an error, System.Runtime.Intrinsics goes behind switches in Phase 4, and a PNSE
  lowering of dead paths happens only on request.

**Judges.**
- Fallbacks: CR 7, CF 8, DV 8.
- `__fastfail`: CF FailFast 6, TerminateProcess 8; DV FailFast (8 within its option); CR FailFast within the
  fallbacks (7).
- `rdtsc` helper: CR 6, CF 7 (error 6), DV 8 (error 4).
- PNSE: 2/3/3.

**Evidence** (helpers-switches E17/E18).
- `SpinWait(1)` takes 54-96 ns, against 40-45 ns for `pause`.
- The table CRC32C is exact at 9-19 ns, against 1 ns for SSE4.2.
- `FailFast` exits with 0x80131623, against native 0xC0000409, and Windows Error Reporting holds such processes for
  minutes.
- Rust std has 4-8 `pause` and 8-15 `int $$0x29` sites per program.

**Trade-off.** `FailFast` is the BCL's own primitive and prints a message and a stack. `TerminateProcess` reproduces
the native exit status without the WER delay.

**Recommendation.** The fallbacks as listed. `__fastfail` becomes `Environment.FailFast` (BCL first, and
diagnosable), with the exit-status difference documented. `rdtsc` becomes the documented tick helper. SIMD switches
come in Phase 4.

### R17. Signal handlers and vectored exception handlers that are C functions

**Options.**
- (a) Allow silently.
- (b) Allow, with a link-time warning on Linux triples for `signal`, `bsd_signal`, `sysv_signal`, `__sysv_signal`,
  `sigset` and `ssignal` (`sigaction`: documentation only).
- (c) Keep the link error.
- (d) Later, intercept them onto `PosixSignalRegistration` when a reference defines it.
- Vectored exception handlers (Rust std registers one): ordinary callbacks plus documentation.

**Judges.** (b): CR 8, CF 7, DV 7. (d) as a later idea: CF 8. VEH documentation: DV 7.

**Evidence.**
- A synchronous `raise` runs a thunk handler. An asynchronous `pthread_kill` while the thread runs managed code is
  fatal for both thunk kinds on .NET 8/10 Linux (callbacks `sig.il`).
- Framework and .NET 8 call a vectored handler on every managed throw (harmless); .NET 10 never does.
- A hardware exception with such a handler is fatal on .NET 8, and on .NET 10 when the handler is first. Native code
  crashes there too (backlog E10).
- UCRT's `signal(SIGINT)` runs through `SetConsoleCtrlHandler` on a new thread, so Windows needs no warning.

**Recommendation.** (b); (d) recorded as a later idea; VEH documented.

### R18. Structs by value at P/Invoke boundaries

**Question.** Design.md 2.3 promises structs by value at P/Invoke boundaries, but no Phase 3 text names them. Do them
in Phase 3 (in a milestone near the end), in Phase 4, or keep the link error?

**Judges.** CR: Phase 3 tail 7, Phase 4 6, mid-phase 4. CF: Phase 3 8 (keep errors 7). DV: Phase 3 8 (Phase 4 4).

**Evidence** (backlog E5/E9). Every x86-64 shape was measured on Framework, .NET 8/10 and WSL:
- Win64 `sret` works as a scalar first argument.
- SysV two-eightbyte results and 24-byte `byval` arguments work through value types.
- A SysV `__int128` at an odd stack slot needs one `int64` pad parameter.
- `System.Int128` by value is refused (`MarshalDirectiveException`).

The APIs that need it: `lldiv`, `imaxdiv`, `cexp`/`csqrt` (`double _Complex`), and many third-party libraries. One
aggregate descriptor (size, alignment, eightbyte classes) serves P/Invoke stubs and callback thunks alike.

**Recommendation.** Phase 3, M34. Win64 `__int128` and the unmeasured aarch64 shapes stay link errors.

### R19. Native compiler-rt in the Linux helper-test oracle

**Options.**
- (a) Build native `x86_64-linux-gnu` compiler-rt builtins and link them into the hosted Linux oracle before
  `libgcc_s` for helper-family tests.
- (b) Keep libgcc and restrict the tests to operands where both libraries agree.

**Judges.** (a): CR 7, CF 8, DV 8. (b): DV 4.

**Evidence** (fp-helpers F4/F6).
- libgcc's `__divdc3` differs from compiler-rt on 5,907 of 38,416 double quotients and on 139 float ones.
- libgcc quiets signaling NaNs in f16 and bf16 conversions.
- The CIL helpers match native compiler-rt on all 153,664 complex results.

**Recommendation.** (a).

### R20. Milestone gates (Q32) and the primary modern runtime (**contested**)

**Question.** M23's standard net8 step took 44-87 minutes, against the 20-30 minute budget. What should the standard
gate contain, and which modern runtime should be primary?

**Options.**
- (a) Narrow:
  - `net-core`: the FileCheck directories, M1 and M2, through objects, on one modern runtime plus Framework 4.8,
    with the O3/O6 pipeline-equivalence legs;
  - `p3-full`: `msil/test/M3` on every runtime, with TieredCompilation=0 and WSL;
  - `wsl-p3`;
  - a harness leg only when harness files change, and `update-expected --check` on the changed directories.

  Everything else runs after the commit, and its failures become fix WPs. The gate is judged on load-normalized
  minutes from a calibration composite.
- (b) Keep today's net8 step and raise the budget to 60-90 minutes.
- (c) Gate only on a quiet host.
- Primary modern runtime: .NET 10 or .NET 8.

**Judges.** CR: (a) 9, (b) 3, (c) 2. CF: (a) 7 with conditions, (b) 8, (c) 6. DV: (a) with .NET 10 primary 9, with
.NET 8 7, (b) 4, (c) 2.

**Evidence** ([tests-gates-p3](tests-gates-p3.md)).
- A net8 composite costs 10.8 CPU-s and 77 processes per test; a lean one 5.4 CPU-s (E4).
- `net-core` projects to 50.5% of net8's test time, about 8 quiet minutes.
- The same legs varied 2-3x between M23's runs. The quiet-host model puts the whole standard gate at 17-23 minutes.
- .NET 8 and .NET 9 leave Microsoft's support on 2026-11-10 (published policy, not re-checked in this run), before
  Phase 3 ends.
- .NET 10 differs most for Phase 3 features: its FLS callbacks are fatal and its thread-static addresses move.

**Trade-off.** CF values detection in every milestone: under (a), M1/M2 regressions on other runtimes surface only
after the commit. CF accepts (a) on two conditions: `p3-full` keeps Framework, TieredCompilation=0, .NET 10 and WSL
for every Phase 3 feature test, and `net-core` keeps the O3/O6 legs that compare `-S`, objects and LTO.

**Recommendation.** (a), with CF's two conditions and .NET 10 as the primary modern runtime. The .NET 8 legs move to
the post-commit run and the phase gate. The tooling comes in M24:
- the calibration composite;
- an automated timeout retry: alone, at `-j1`, at most 3 per leg, never masking a hang that times out twice;
- `%{msil_limits}`;
- the harness cost work.

### R21. Upstream merge before M24; no front-loaded header milestone (front-load **contested**)

**Options.**
- Merge upstream before M24, or defer the merge.
- Front-load into M24 `Options.td`, `Attr.td`, `BCLMembers.def`, `MSILLink.h` v1.1, the ExpandVariadics hook and the
  compiler-rt edits; or let each milestone own its rows and record items.

**Judges.** Merge before M24: CR 8 (defer 3), DV 8 (defer 4), CF in favour. Front-loading: CR 4 against per-milestone
ownership 8; DV front-load 8; backlog-p3 front-load.

**Evidence.**
- Measured rebuild classes (CR J1):
  - `Attr.td`: 526 objects, about 4,000 CPU-s;
  - `Options.td`: 81 objects, 2.6% of compile CPU, not the broad clang rebuild two recons assumed;
  - `MSILLink.h`: 42 objects;
  - `BCLMembers.def`: 21 objects.
- With the attribute deferred (R14), no Phase 3 edit is widely included. No IR intrinsic, `RuntimeLibcalls.td` or
  `CallingConv.h` change is needed (sjlj, callbacks, frame-tls).
- The last merge was `64f50ffd4f1c` (2026-10-19). Phase 3 adds hooks to `ExpandVariadics.cpp`, which has 17 upstream
  commits a year.

**Trade-off.** DV wants every rebuild paid once. CR's numbers show the rebuilds are small, and front-loading designs
record subsections and BCL rows months before their consumers.

**Recommendation.** Merge before M24 (HANDOFF 0's procedure). No header milestone. The milestone that needs
`BCLMembers.def` rows appends them (their order fixes `BCLMemberId`), and record items work the same way.

R22-R24 are in section 4.

---

## 3. Technical decisions planning makes (you may override any)

### 3.1 Cross-cutting

| Item | Recommendation |
|---|---|
| IR pass order. The recons' placements form a cycle: variadics wants FixFunctionBitcasts before ExpandVariadics; sjlj wants ExpandVariadics, then MSILLowerSjLj, then FixFunctionBitcasts; callbacks put the dispatch inside MSILPrepareIR | One canonical order, set by a pipeline WP in M26 and pinned by an `llc -debug-pass=Structure` test (CR's order): MSILPrepareIR, MSILAddMissingPrototypes, LowerInvoke, UnreachableBlockElim, MSILFixFunctionBitcasts, ExpandVariadics, MSILLowerSjLj, MSILLowerDynamicAlloca, MSILLowerThreadLocal, the indirect-call dispatch, MSILOptimizeReturned, IndirectBrExpand, `TargetPassConfig::addIRPasses`. Later milestones insert at reserved slots. CF puts FixFunctionBitcasts after the dispatch instead; the pipeline WP settles this with tests of K&R calls into variadic definitions and of setjmp wrappers around bitcast calls |
| Hot files | New code in new files with small hooks: `MSILVariadics.cpp`, `MSILLowerSjLj.cpp`, `MSILLowerIndirectCalls.cpp`, `MSILLowerThreadLocal.cpp`, `MSILLowerDynamicAlloca.cpp`, emitter files beside `Trailer.cpp`. One feature per hot file per milestone; `wmain` and `exit(main())` in one WP |
| Link-info record | Keep ToolchainABIVersion 1 (no v1 file has VARARG). Minor-version items per milestone: `FIXED_ARITY` 0x10 (M26), the optional-request subsection (M33), the aggregate descriptor (M34). v1 already has `ThreadLocal`, `SegTLS`, `TLSOffset32/64` and FINI entries |
| `-S` | D14 stays frozen. Everything the trailer emits (exit machinery, UCRT stdio options, `wmain`, the TLS template, thunks) comes from `cli::buildTrailer`, for `-S` and msil-ld alike |
| EH clauses | Built once, in M27: `MSILTargetStreamer::emitExceptionClause`, the AsmPrinter's whole-body `.try`, and the object writer filling `MethodBodyInfo::Clauses` (lib/CLI and msil-ld already carry clauses). Reused by the Linux thunk filter (M28) and the shadow stack's fault clauses (M32) |
| Calling C function pointers from IL | One lib/CLI helper, which uses the dispatch, for exit handlers, thread-exit lists and trampolines |
| Diagnostics | New texts carry no phase numbers ("not supported for MSIL yet") (BL14) |
| Benchmarks | Optimized builds only. Fix `build-jitall.py --csharp` (add `-optimize+`) in M24, and re-measure before deriving thresholds |
| Debugger markers | `[CompilerGenerated]` on synthesized types and methods, `[DebuggerNonUserCode]` on thunks and wrappers (DV; low priority; verify the debugger effect first) |

### 3.2 Guards and Phase 2 defects (M24)

- msil-ld never binds a compiler-runtime name (a `__` RuntimeLibcalls name) to a native library, and never the
  setjmp family either: `_setjmp`, `setjmp`, `__sigsetjmp`, `sigsetjmp`, `_setjmpex`, `_setjmp3`, `longjmp`,
  `_longjmp`, `siglongjmp`, `__longjmp_chk`. The error for an unresolved one names the helper archive (HS H3, sjlj
  S10, atomics Q-AT12).
- A signature mismatch on a RuntimeLibcalls name is an error, not plan 5.9.8's adapter and warning (fp-helpers I2).
- `FlsAlloc`, `__cxa_thread_atexit_impl`, `tss_create` and `__cxa_atexit` join the callback list, as link errors until
  their milestones.
- D36: single-thread fences and empty `asm` memory barriers become a call of a private NoInlining empty method that
  the trailer synthesizes (atomics Q-AT8). It costs 0.4-1.1 ns, against 4-5 ns for `MemoryBarrier`.
- bf16 passes as its bits in an `int32`, like f16; bf16 -> f32 is inline; `areTypesABICompatible`'s refusal of half
  and bfloat is lifted (HS H4, fp-helpers F2, backlog T25).
- Constrained FP <-> i128 conversions become plain conversions, which removes a compile-time error. Any
  legalizer-built i128 libcall left on Windows triples gets X86-style by-pointer passing (fp-helpers F2).
- Status.md's setjmp row is corrected, and a section 12 row records the typo "65,524 fields" in row 8932.

### 3.3 Helpers and floating point

| Item | Recommendation |
|---|---|
| f16 | compiler-rt helpers everywhere, no System.Half switch. They are bit-identical to native compiler-rt on every input class on 5 runtimes; System.Half quiets the 1,022 sNaN halves and is absent on Framework (fp-helpers F1) |
| C complex | compiler-rt's `__mulsc3`, `__muldc3`, `__divsc3` and `__divdc3`; never System.Numerics.Complex, whose results violate Annex G in 3,896 of 38,416 products and 7,343 quotients (F3) |
| i128 division, remainder and conversions | Keep M12's inline expansion through the hosted milestones. In M33, outline one `linkonce_odr` hidden COMDAT function per operation kind and module: first with ExpandIRInsts bodies (c1), then with compiler-rt's algorithm as IR (c2) after an exhaustive comparison. Constant divisors stay inline (103 IL instructions against 473 for /10) (fp-helpers I1; CR, CF and DV agree on c2 as the end state) |
| `__clear_cache` | Lowers to nothing, as on native x86 |
| libm names | Stay host P/Invokes (`roundeven` is missing from UCRT and `sqrtf16` from every libc, natively too) |

### 3.4 Variadics (M26)

- ExpandVariadics runs in Lowering mode, with MSIL `VariadicABIInfo` classes (SysV, AAPCS64, `char*` for Win64) added
  in `VariadicABIInfo::create` (`ExpandVariadics.cpp:1126`). The hook is listed in `msil/utils/upstream-hooks.txt`
  with its upstream tests (V1).
- The `va_list` keeps the native layout, over a buffer in the caller's static frame (no shadow stack); the SysV tag is
  `{48, 176, buffer, 0}`.
- msil-ld holds the v-variant tables per C runtime: glibc 44 pairs (including `__isoc99`, `__isoc23` and `_chk`),
  msvcrt 64, UCRT none (V7).
- Calls of fixed-arity natives are made per call site: Win64 doubles pass as `int64` bits, and a SysV floating-point
  argument is a compile error (V2).
- A fixed-arity reference that binds to an MSIL variadic definition is an error for now (V6).
- MSILPrepareIR diagnoses `musttail` (the pass would `report_fatal_error`) and every IR `va_arg` on SysV and AAPCS64
  (V8).
- Native code calling a C variadic function through a pointer is diagnosed where visible, and documented (V9).
- The address of a native variadic function is an adapter with the expanded signature that calls the v-variant or
  the fixed-arity P/Invoke, or a link error when neither exists; never the raw native entry. This is CF's exception to
  CB5, and covers hosted-libc H7's SQLite `aSyscall` case. It lands with M28's thunk machinery.
- The AArch64 variadic ABIs are implemented from the ABI documents and stay unverified until an ARM64 runtime exists.

### 3.5 setjmp/longjmp (M27)

- The pass recognizes `_setjmp` (with 1 or 2 arguments, erasing an unused `llvm.frameaddress`/`llvm.sponentry`),
  `setjmp`, `__sigsetjmp`, `sigsetjmp`, `_setjmpex` and `_setjmp3`, and the `longjmp` family including taken
  addresses. `&setjmp` and other `returns_twice` callees are diagnosed.
- The jmp_buf prefix is `{u32 'MSJ1', u32 site label, ptr status block}` (S3). It fits glibc's 200 bytes, UCRT x64's
  256, arm64's 192 and even `__builtin_setjmp`'s 40, and survives `memcpy`.
- The helpers are per-module IR: `'<msil>longjmp'` (`linkonce_odr` hidden), internal wrappers, and a throw stub whose
  body the AsmPrinter writes (S4).
- The pass wraps the calls reachable from a setjmp site, minus intrinsics, inline asm, its own helpers, `willreturn`
  callees and libc functions that take no callback (S11).
- `stacksave` at each site and `stackrestore` on its dispatch path (S7). `sigsetjmp`'s savemask is honoured on Linux
  triples (S9). The `__builtin_setjmp` diagnostics stay (S8).
- Optionally, the filter also tests the exact type (`GetType() == ExternalException`) to exclude subclasses (CF).

### 3.6 Function pointers and threads (M28)

- The address of a native import is its native entry point, obtained through M20's `'<msil>dataimport'` helper. It
  equals `dlsym`/`GetProcAddress` and is the same in every assembly (CB5).
- FailFast trap thunks serve variadic and native-incompatible functions, and are still entered in the table (CB7).
- The dispatch is its own late IR pass at the canonical slot, not part of MSILPrepareIR (CB10, as corrected by CR and
  CF). It marks the native arm with a call-site string attribute and uses a new `CALL_INDIRECT` twin with
  `CallConv::UnmanagedCdecl` (the encoder already writes `SIG_C`). Check that SimplifyCFG does not merge the two arms.
- The table is an exact open-addressing table in unmanaged memory, filled in phase 1 and read lock-free (CB11).
- Lazy thunks for export-only library functions (CB6) come later, as an optimization.
- Threads are host-libc threads (`pthread_create`, `CreateThread`, `_beginthreadex`, `thrd_create`) started through
  thunks, with no interception onto System.Threading.Thread (atomics Q-AT10). A 4-thread counter reached exactly
  8,000,000 with delegate thunks on every runtime.
- The spilled `'<msil>Data'` two-thread race is fixed by running phase 1 inside the module initializer of such
  libraries (BL6).
- A GC-churn host leg runs the callback and thread tests, and also the errno and `GetLastError` probes (`errno2.c`,
  `lasterr2.c`). errno stays the native storage: 0 mismatches in 300,000 iterations on every runtime.
- Windows C11 threads (`VCRUNTIME140_THREADS.dll`) need `-lvcruntime`. This is documented, not a default: Q14 kept the
  VC++ redistributable out of the defaults.

### 3.7 C exit protocol (M29)

- Destructors become FINI entries in record v1 (L9), and `diagnoseGlobalDtors` (`MSILPrepareIR.cpp:1406-1433`) goes.
  LowerGlobalDtors is not used: it interleaves destructors with handlers that constructors register, unlike glibc.
- Destructor order comes from new `cli::getDestructorKey`/`sortDestructors`. ELF runs the reverse of the constructor
  sort; COFF runs ascending section names. A null `.fini_array` slot is an error; null `.CRT$XT` slots are skipped.
- `_Exit` and `_exit` are kernel32's `ExitProcess` on Windows triples (ucrtbase's versions raise ProcessExit on
  Framework) and native on Linux. `abort` stays native (L3).
- ProcessExit is subscribed at the end of the module initializer, because an early subscription deadlocked .NET 8/10
  (L8). `exit` inside the fallback finishes the list, sets `Environment.ExitCode` and terminates natively (L12).
- The cascade follows msil-ld's simulation of the libraries' initialization order through their AssemblyRefs, not the
  command line, which gets diamonds wrong (L13).
- `.CRT$XP*` stays unsupported (L14). `__dso_handle` is defined per assembly on ELF triples (L15). `.preinit_array`
  runs before the constructors of executables (backlog S13).
- Four lld tests switch their native-only example from `atexit` to `pthread_atfork`. `check-mscorlib-safe.py`
  confirms `Environment.set_ExitCode` and `Thread.Sleep`.

### 3.8 Atomics (M30)

- The backend calls `setMaxAtomicSizeInBitsSupported(64)` and `setMinCmpXchgSizeInBits(32)`.
- Loads and stores (Q-AT2, Q-AT3):
  - every atomic load of monotonic or stronger ordering is a `volatile. ldind`, because the JIT hoists plain loads out
    of spin loops;
  - relaxed and release stores are `volatile. stind`;
  - a seq_cst store adds `Interlocked.MemoryBarrier`. The SB litmus test showed 0 forbidden outcomes in 2M rounds on
    5 configurations, against 6,463-80,852 with release-only stores.
- Read-modify-write operations (Q-AT4, Q-AT5):
  - 32- and 64-bit exchange, add, sub and compare-exchange are `Interlocked` calls on the native-int address;
  - and, or, xor, nand, min, max and the FP operations are AtomicExpand compare-exchange loops;
  - 8- and 16-bit operations are a partword loop on the aligned word.
- A dead acquire load gets a fence (Q-AT9). On aarch64 triples a trailing `MemoryBarrier` follows seq_cst RMW and
  compare-exchange, because .NET emits bare `swpal`/`casal` with `ldapr` (CF J6).
- clang's native `-fms-volatile` default comes back on `x86_64-pc-windows-msvc-msil` (Q-AT11).

### 3.9 thread_local and frames (M31, M32)

- TLS option B (D-FT3, D-FT4, D-FT5):
  - one block per thread per assembly: a 64-byte header (shadow-stack state, a destructor-list slot), then the
    `.tdata`/`.tbss` template;
  - a `[ThreadStatic]` native int on a private beforefieldinit `'<msil>Thread'` class with no `.cctor`, which is also
    the finalizable holder;
  - the finalizer frees nothing when `Environment.HasShutdownStarted`, because Framework finalizes reachable holders at
    exit while threads still run.

  One `[ThreadStatic]` field per variable is impossible: such fields move on compacting GCs (E3).
- TP is hoisted once per function (D-FT7):
  - a definition's address is TP plus an offset field;
  - a declaration calls a per-symbol `'<msil>TLSAddr'` accessor, which msil-ld binds locally, to a library's
    `'<msil>addr_x'`, or, for a weak undefined symbol, to TP.

  With optimization, hoisting saves 15-30% (DV J1c), not the 2-4x of the MinOpts numbers.
- No TLS switches: `Marshal.AllocHGlobal` serves (D-FT9). `llvm.thread.pointer` stays an error.
- A library exports a `thread_local` through its accessor, which returns the calling thread's address (BL7, in M31).
- The shadow stack is a chunked list: a 64 KiB first chunk, doubling, one spare, the bottom chunk kept, FailFast when
  an allocation fails, and no size switch (D-FT2).
- `__builtin_frame_address(0)` returns the frame base; depth > 0 stays an error (D-FT8).

### 3.10 Hosted-libc items

| Item | Recommendation and milestone |
|---|---|
| H3: UCRT's per-program printf/scanf options. vcruntime sets 0x24/0x2 at startup; MSIL leaves 0, so `%.0f` of 0.5 prints 1 | Trailer IL in `<Module>::.cctor` before the constructors, when the link defines `__local_stdio_printf_options`; `legacy_stdio_float_rounding` clears the bits. M26 |
| H4: `wmain` | `'<msil>Main'` calls `wmain` with a UTF-16 argv when only `wmain` exists; with `exit(main())`, in M29 |
| BL4: the command line | In Windows executables, `GetCommandLineW`/`A` and `__p___argc`/`__p___argv`/`__p___wargv` bind to private helpers over the program's own line and argv. Under `dotnet` the native ones include the host's arguments, and UCRT's `__argv` is NULL. Linux is documented. M29. The same WP checks the UCRT app type and abort/assert reporting (DV; unverified) |
| H5: SEH | A clang diagnostic that names SEH at `__try`; the corpus builds SQLite with `-DSQLITE_OMIT_SEH`, since CoreCLR does not deliver hardware exceptions to managed filters. M30 |
| H6: the `dlopen` module for glibc data imports | `libdl.so.2` on glibc triples, which removes a GLIBC_2.34 floor. M30 |
| Data-import retry in the application directory | `NativeLibrary.Load(string, Assembly, DllImportSearchPath?)` when the references define it, else a retry with the assembly's directory. XS, M30 |
| `legacy_stdio_definitions` | msil-ld ignores it like `libcmt`, once the UCRT printf helpers exist. M26 |
| H8, H9 | SQLite's defaults (`THREADSAFE=1`) for the shell; Lua's suite as `all.lua` with `_U=true`, checking `final OK !!!` and the exit status |

### 3.11 Tests and oracles

| Item | Recommendation |
|---|---|
| Hosted Linux oracle | E7h for M3 and the P3 matrix rows: the glibc sysroot's real `crt1.o`/`crti.o`/`crtn.o` with `libc_nonshared.a`. E7 stays for M1/M2 (tests-gates D9). Lifecycle's `start2.c` is also correct, but one oracle is cheaper to keep |
| c-testsuite | A second list (`libc.txt`) of the 70 printing tests, compared with `.c.expected`, with its own rule and count and closed-vocabulary exclusions (00204 `long-double` on linux-gnu, 00214 `not-portable` on windows-msvc), never XFAIL. 138 of 140 pairs (D7) |
| Linux coverage | A `ctest-wsl` leg and a wider `lto-wsl` (01-archive, 03-native, 04-shared, 05-refs, 07-equiv) in the phase gate (D8) |
| Thread tests | Deterministic shapes; every address comparison behind a barrier (1 of 20 runs failed without one); bounded spins; a stress leg only in the phase gate (D10) |
| `abort` | Executed on Linux only; Windows gets a link-level check, because WER holds the process for minutes (D14) |
| Matrix | `check-matrix.py` gains legs, per-level diagnostic checks and the `rule:` kind (R13). New rows: `tls-xtu-init`, `tls-shared-export`, `tls-weak-undef`, `tls-threads`, `sjlj-xasm` |
| Rust std | A recorded measure at M32 and at the exit, not a lit leg, since rustc is outside the scope rules (D13) |
| Test layout | `msil/test/M3/00-frames` to `10-hosted`, one directory per feature (tests-gates-p3 4.3) |

### 3.12 Where the judges corrected the recons

1. `Options.td` rebuilds 81 objects (2.6% of compile CPU), `BCLMembers.def` 21 and `MSILLink.h` 42; only `Attr.td`
   (526) is widely included (CR J1). The constraint on `BCLMembers.def` is one owner per milestone, not rebuild cost.
2. The recons' IR pass placements form a cycle, and the dispatch cannot live in MSILPrepareIR (CR, CF); see section
   3.1.
3. hosted-libc H3's option (b) cannot work: an archive member holding only a constructor is never extracted (CR).
4. compiler-rt has 181 sources, of which 153 compile with `__ELF__` (the recon said 142 of 176). With `__ELF__`
   predefined, the x86_64 triples need no `int_lib.h` edit (CR J5).
5. frame-tls's timings come from C# built without `-optimize+`, that is MinOpts code. Three findings are artifacts
   (DV J1b/J1c):
   - the ".NET 8 thread-static slowness": 69-330 ns measured, about 4-7 ns with optimization;
   - "ThreadLocal<T> is faster on .NET 8": with optimization it is slower everywhere;
   - the shadow stack's 5-20x cost.

   The overflow counts, the GC address movement, the FLS deaths and the holder results are semantic and stand.
6. CF's runtime findings:
   - `tss_create` destructors are fatal on .NET 10, like `FlsAlloc` callbacks (CF J2);
   - .NET 8 raises ProcessExit on SIGTERM (CF J5);
   - .NET on ARM64 emits bare `swpal`/`casal` with `ldapr`, so seq_cst RMWs need a trailing fence there (CF J6, from
     crossgen2 output, not hardware).
7. DV's verifier and BCL findings:
   - table-driven delegate creation crashes ILVerify, and msil-verify pass 2 crashes ILVerify on delegate
     construction (DV J8);
   - the atomics V1 IL equals csc's output (DV J5);
   - `Math.Min(-0, +0)` is +0 on Framework (DV J4).
8. CR's confirmations:
   - `setjmp`/`longjmp` bind natively and silently on the Linux object pipeline (CR J3);
   - D36's barrier is an IL comment (CR J4);
   - c-testsuite 00207 is acyclic (CR J10);
   - SQLite has about 636 and Lua about 191 address-taken functions (CR J11).

---

## 4. Questions open with you since Phase 2 (inputs section 4)

| Question | Recommendation | Why |
|---|---|---|
| **R22.** Approve Status.md section 6's Design.md updates 16-22 | Approve them with the Phase 3 plan commit, with 16, 19 and 21 amended and a new item 23 (exact texts in [backlog-p3](backlog-p3.md) section 3), together with the Phase 3 Design.md changes of section 8 that your answers imply | 16 overstates c-testsuite (149 of 150 pass; 00140 pins a Phase 3 diagnostic) and omits dynamic allocas and structs by value. 19 should name `clang_rt.builtins`. 21 misses `netstandard` 2.0, and "runs directly" holds only on Windows triples. 23 records the data-import helper as built (`Relocations.cpp:494-560`). One reviewed Design.md pass instead of two (DV 8, backlog BL1) |
| **R23.** Plan section 10 item 11: may GitHub's runners fetch and run CoreMark and c-testsuite? | Allow. If you prefer caution, allow c-testsuite after an XS check of the scc and tinycc licences | CoreMark's EEMBC agreement restricts the CoreMark mark, not running the code (Apache-2.0). c-testsuite's harness is MIT, and each test carries its scc or tinycc licence (the `.otags` sources were not read). CI does not redistribute either. Without this, c-testsuite has no CI record, and the new `ctest-wsl` leg is its only Linux run |
| **R24.** Q38: the default branch and one dispatch of `msil-ci.yml` | Dispatch once now, before M24; split jobs by build artifact if one nears 330 minutes | `msil` is already the default branch (public). The MSIL CI workflow has 0 runs, so its 330-minute timeouts and cross-host determinism are unchecked (backlog E7, re-checked) |
| Q32 for Phase 3 milestone gates | See R20 | |
| `atexit` order across several output assemblies; a C `exit` through the host libc bypassing ProcessExit | See R4 and R5: per-assembly lists, the cascade in simulated reverse initialization order, the root-exit slot; the toolchain defines `exit` | A native `exit` reaches ProcessExit only on Framework, and skips the native `atexit` table there. `Environment.Exit` inside ProcessExit deadlocks .NET 8/10 |
| Per-thread `[ThreadStatic]` buffers: first-use allocation, never freed | `'<msil>tls'()` allocates lazily, and a finalizable holder frees the block (section 3.9) | 2000 of 2000 blocks were freed for managed and native threads on every runtime (frame-tls E5). Without the holder, each thread leaks the block plus 2-4 KiB |
| setjmp/longjmp across assemblies (the exception must carry the `jmp_buf` and the value) | See R8: the 16-byte prefix, an armed status block in the setjmp caller's frame, and `ExternalException` with the block's address in `HResult`; no shared type | A filter in one assembly matches a throw from another with no shared type (sjlj's IL experiments, 5 runtimes). Through a function pointer owned by another assembly, it aborts on .NET 8 Linux only (R5 defers the registry) |
| libm to Math/MathF | See R10 | |
| Default switches on Linux/Darwin triples; native target features (`-mavx2`) and the SIMD switches | See R9: every switch is off on every triple. Native target features keep their D7 role (clang's ABI, and which intrinsic headers are offered) and never enable a BCL API; System.Runtime.Intrinsics switches come in Phase 4 | Defaults must run on Framework, and a Linux output linked without references binds `[mscorlib]` |
| How the static C helper archive reaches msil-ld | See R2: the driver's existing `getCompilerRT` lookup (`MSIL.cpp:247-253`) passes `<resource-dir>/lib/<triple>/clang_rt.builtins.lib` or `libclang_rt.builtins.a` after the default libraries | Same mechanism as native compiler-rt; no new driver option |
| Architecture-specific intrinsics | See R16 | |

---

## 5. Phase 3 milestone outline

Sizes are lines of code including tests, estimated from the recons' WP ideas and CR's sizing. Each milestone's exit
check runs in the standard gate at one configuration: -O2, objects, the primary modern runtime plus Framework and WSL
.NET 10 (R20). The full matrix (both levels, objects and LTO, every runtime) runs after the commit and at the phase
exit. Before M24 come the upstream merge (R21) and the CI dispatch (R24).

| Milestone | Unblocks | Size |
|---|---|---|
| M24 Foundations: guards, Phase 2 defects, Phase 3 gates | honest failures; the gate budget | 2.0-2.5k |
| M25 The helper archive and compiler-rt's families | f16, bf16, complex, powi | 1.2-1.6k |
| M26 Variadic functions (and acyclic dynamic allocas) | zlib full, c-testsuite's 70 | 3.0-3.5k |
| M27 setjmp/longjmp and IL exception clauses | Lua | 2.5-3.0k |
| M28 Native-callable function pointers and threads | `lua.c`, callbacks, threads | 3.5-4.0k |
| M29 The C exit protocol and Windows startup fidelity | destructors, `atexit`, `wmain` | 2.5-3.0k |
| M30 Atomics and the SQLite shell | SQLite | 2.5-3.0k |
| M31 `thread_local` and thread-exit destructors | TLS, Rust std | 3.0-3.5k |
| M32 The cyclic shadow stack and architecture fallbacks | VLAs in loops, Rust std | 1.5-2.0k |
| M33 Codegen switches, exact BCL substitutions, i128 helpers | performance | 2.5-3.0k |
| M34 Structs by value at P/Invoke boundaries | `lldiv`, `cexp`, third-party APIs | 1.5-2.0k |
| M35 Phase 3 conformance and exit | the phase | about 1k plus the gate |

**M24. Foundations: guards, Phase 2 defects and the Phase 3 gates.**
- Goal: turn today's silent misbehaviour into errors or correct code before any feature depends on it, and make the
  standard gate fit its budget.
- WPs:
  - the msil-ld guards and callback-list additions (section 3.2);
  - backend fixes: the D36 barrier, bf16 as `int32`, constrained FP <-> i128 made plain, Win64 i128 libcall passing;
  - gate tooling: `net-core`, `p3-full`, `wsl-p3`, the calibration composite, the automated timeout retry,
    `%{msil_limits}`, `ctest-wsl`, a wider `lto-wsl`, and `check-matrix.py` legs and vocabulary;
  - the harness cost work as its own WP (a probe cache, one Python process per level, one `wsl.exe` per level),
    verified with `il-snapshot --compare` and full legs;
  - hosted mode with the E7h oracle, the `msil/test/M3` skeleton and the second c-testsuite list's format;
  - docs: Status.md's setjmp row, section 12 rows, `build-jitall.py -optimize+`.
- Exit check:
  - `il-snapshot --compare` shows only the intended D36 change;
  - `barrier-spin.c` exits on Framework, .NET 8 TC=0 and .NET 10 on both OSes;
  - lld tests for each guard (the f16 helper, the setjmp family, the libcall mismatch);
  - `strict_i128.c` runs on the Windows triple;
  - one calibrated `net-core` + `p3-full` + `wsl-p3` run fits inside 20-30 load-normalized minutes.

**M25. The helper archive and compiler-rt's families.**
- Goal: per-triple `clang_rt.builtins` CIL archives.
- WPs:
  - `Targets/MSIL.h`: `__ELF__` on Linux triples and the lock-free cap (R12);
  - `msil/utils/build/build-builtins.py` compiles a reviewed source list (no fp128, x87-only, inline-asm or hosted
    sources) into `<resource-dir>/lib/<triple>/`; the build agent runs it after every clang or backend build;
  - the families: f16, bf16 truncation, complex, `powi`, and the `long double` forwarders on wide triples (R15);
  - msil-ld names the archive in its diagnostics, and the `-S` errors for archive-only helpers are reworded;
  - the Linux oracle gains native compiler-rt (R19).
- Exit check:
  - `helpers.c` (Annex G complex cases, `_Float16` arithmetic and conversions, `powi`, i128 overflow) and a subset of
    fp-helpers' exhaustive hashes equal native compiler-rt on both triples, through objects and LTO;
  - a `-shared` library keeps the helpers private;
  - `sqrtl` and `strtold` link and run on the Linux triple.

**M26. Variadic functions (and acyclic dynamic allocas).**
- Goal: variadic definitions and calls (section 3.4).
- WPs:
  - the pipeline WP: the canonical pass order, pinned;
  - the ExpandVariadics hook, `MSILVariadics.cpp` and the MSILPrepareIR diagnostics;
  - the record's `FIXED_ARITY` (minor version 1.1);
  - msil-ld's v-variant tables and fixed-arity call sites;
  - the UCRT printf/scanf helpers in the archive, and the UCRT stdio options in the trailer (H3);
  - the `%L` diagnostics (R15);
  - H0: `LOCALLOC_I64`, acyclic allocas as `localloc`, `stacksave` and `stackrestore` removed, a clear error for an
    alloca on a cycle;
  - tests: `M3/05-variadics`, the `variadic-def-xtu` transcript, 00140 as a plain entry, the second c-testsuite list.
- Exit check:
  - zlib 1.3.2 full (`example`, `minigzip`; on Linux also with `-D_FORTIFY_SOURCE=2`) matches native on both triples;
  - c-testsuite's second list reaches 138 of 140 pairs (00207 at -O0 through H0);
  - `M3/05-variadics` passes on every runtime, including TieredCompilation=0.

**M27. setjmp/longjmp and IL exception clauses.**
- Goal: E1 (section 3.5) and the EH-clause backend.
- WPs:
  - the clause event and the object writer's path;
  - the AsmPrinter's whole-body `.try` with the fixed filter (`RETURN` as `leave` plus a return local, `.maxstack` at
    least 3), and the throw stub;
  - `MSILLowerSjLj.cpp`;
  - four `BCLMembers.def` rows;
  - D21's filter allowance (R11).
- Exit check:
  - the 14-case `sjlj-semantics.c` against native on both triples and every runtime;
  - zlib's `contrib/puff` and `contrib/blast`;
  - a Lua 5.4.9 embedding driver (`prog1` and `prog2` byte for byte) on both triples;
  - Lua's test suite (`all.lua`, `_U=true`) on both triples, through `lua.c` linked with a test object that defines
    `signal` and `sigaction` as no-ops (so nothing binds natively); the M24 guard proves the real lowering runs;
  - a cross-assembly (`-shared`) longjmp test and the `sjlj-xasm` row.

**M28. Native-callable function pointers and threads.**
- Goal: A2 (R3, section 3.6).
- WPs:
  - trailer thunks: per-signature delegate types, straight-line eager creation, the table, `'<msil>fnmgd'`, trap
    thunks, native-import addresses, adapters for the addresses of native variadic functions;
  - `MSILLowerIndirectCalls.cpp`;
  - msil-ld: thunk descriptors from SIGNATURES, the callback-list error retired except for the thread-exit
    registrations, the signal warning, `--uco-thunks`, the Linux thunk filter;
  - D21's delegate allowance;
  - threads: the `'<msil>Data'` race fix and the GC-churn leg;
  - VEH documentation.
- Exit check:
  - the `native-callback` row as a transcript;
  - `M3/06-callbacks` (`qsort`, `bsearch`, `qsort_r`, `tsearch`, pointers from `dlsym`/`GetProcAddress` called from
    C, identity across objects and libraries) and its `--uco-thunks` variants;
  - a mutex-based `M3/09-threads` on every runtime;
  - the GC-churn leg green;
  - unmodified `lua.c` running Lua's suite on both triples.

**M29. The C exit protocol and Windows startup fidelity.**
- Goal: R4, R5 (the root-exit slot and the `<msil>v1:` key helper) and R13 (section 3.7).
- WPs:
  - lib/CLI destructor keys;
  - the trailer's exit machinery and the ProcessExit fallback;
  - backend FINI entries;
  - the `'<msil>Main'` WP: `exit(main())`, `wmain`, the Windows command-line helpers;
  - `.preinit_array` and `__dso_handle`;
  - oracle and matrix updates: E7h rows, `rule:` files.
- Exit check:
  - `p6-dtors`, `fini-array`, `crt-xt`, `atexit-order` and `09-hosted/glibc-atexit` as transcripts;
  - `M3/02-lifecycle` on every runtime: exit order, `quick_exit`, `_Exit`, exit status per OS, a program-library
    diamond, a C# host of a library with destructors, and the deadlock regressions within their timeouts.

**M30. Atomics and the SQLite shell.**
- Goal: section 3.8, the process-wide lock table (R5), D21's Interlocked allowance, and the remaining SQLite items
  (H5, H6, the data-import retry).
- Exit check:
  - SQLite 3.54.1's shell (`THREADSAFE=1`; `-DSQLITE_OMIT_SEH` on Windows) runs `t1.sql`, `t2.sql` (WAL, checkpoint,
    VACUUM) and `t3.sql` byte for byte against native on both triples;
  - `M3/08-atomics`;
  - on every runtime, a 4-thread atomic counter reaches exactly 8,000,000, and the SB litmus test shows no forbidden
    outcome;
  - the hoisting and barrier spin tests;
  - a 16-byte atomic shared by a program and its `-shared` library.

**M31. `thread_local` and thread-exit destructors.**
- Goal: TLS (section 3.9) and R7.
- WPs:
  - the `'<msil>Thread'` type kind and the TLS template layout in lib/CLI (ImageLayout rule 7);
  - `MSILLowerThreadLocal.cpp` and the backend's TLS rows;
  - msil-ld: template layout, `TLSOffset` relocations, accessors, exports, weak undefined symbols;
  - R7's interceptions and trampolines;
  - five `BCLMembers.def` rows.
- Exit check:
  - `p5-tls` as a transcript;
  - the rows `tls-xtu-init`, `tls-shared-export`, `tls-weak-undef` and `tls-threads`;
  - `tls_threads.c` on every runtime;
  - a Rust-std-shaped test whose TLS destructor runs at thread exit on .NET 10.

**M32. The cyclic shadow stack and architecture fallbacks.**
- Goal: H1 and R16's fallbacks.
- WPs:
  - H1: `MSILLowerDynamicAlloca.cpp`, the shadow stack in the TLS block header, `'<msil>salloc'`/`'<msil>srestore'`,
    the setjmp-site restore, fault clauses;
  - the fallbacks: `pause`, fences, `crc32`, `bextr`, `__cpu_model`, `__fastfail` in both spellings, `rdtsc`.
- Exit check:
  - `vla_cases.c` (VLAs and `alloca` in loops, over-aligned VLAs, recursion) on every runtime, including a longjmp
    through VLA frames;
  - the Rust std experiment recorded without its TLS, atomics, variadic and signal stand-ins (`thread::spawn` runs).

**M33. Codegen switches, exact BCL substitutions and i128 helpers.**
- Goal: R9, R10 and section 3.3's i128 outlining.
- WPs:
  - clang's `Options.td` (`m_msil_Features_Group`), `MSIL.td` SubtargetFeatures and `areInlineCompatible`;
  - record v1.1's optional requests and msil-ld's token rewriting;
  - `msil-bitops`, `msil-fma`, `msil-minmax` and `--disable-runtime-marshalling`;
  - the default substitutions and the f32 union type;
  - i128 outlining (c1, then c2).
- Exit check:
  - `il-snapshot` unchanged with every switch off;
  - `M3/04-switches` on and off, with net8 and net10 references, and the reference check's message;
  - the FP property tests (including NaN bits) on all five configurations;
  - `nt128.c`'s IL and JIT size, and the 36-site function compiled with full optimization.

**M34. Structs by value at P/Invoke boundaries.**
- Goal: R18.
- WPs: the record's aggregate descriptor (a minor version item); the AsmPrinter's ABI descriptor; a new msil-ld
  `ValueTypes.cpp` (value types, stub adapters, the SysV pad rule); thunks for such functions instead of trap thunks.
- Exit check:
  - `lldiv`, `imaxdiv`, `cexp`/`csqrt`, a 24-byte `byval` argument and a `__int128` at an odd stack slot, on both
    triples and every runtime;
  - callbacks with struct parameters through thunks.

**M35. Phase 3 conformance and exit.**
- Goal: everything at once.
- Exit check:
  - zlib full, Lua (`lua.c` and the suite) and SQLite (the shell) on both triples, at -O0 and -O2, through objects and
    LTO, on every runtime including TieredCompilation=0 and WSL;
  - c-testsuite's two lists on both triples;
  - every P3 matrix row;
  - the nine Phase 2 programs;
  - the full phase gate, with every leg and its measured time;
  - Status.md's "Phase 3 as built" and the proposed Design.md as-built note;
  - the Rust measure recorded.

---

## 6. Phase 3 exit corpus

| Program | Pin | Licence | Manifest | Where |
|---|---|---|---|---|
| zlib full (`example`, `minigzip`; Linux also `-D_FORTIFY_SOURCE=2`) | 1.3.2, commit `da607da739fa6047df13e66a2af6b8bec7c2a498`, tree `adf943637db9bd636adc13f302b957a0dbc898dcb80a195f8b73328bdfc5bd28` | Zlib | existing entry `zlib` (Phase 2 built only Z_SOLO's nine files from it) | M26, M35 |
| zlib `contrib/puff`, `contrib/blast` | the same tree | Zlib | existing | M27 |
| Lua (`lua.c` and an embedding driver) | 5.4.9, `lua-5.4.9.tar.gz`, archive `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6`, tree `5ae2103cee75f96abee333a09a559db72c44f8e8d5d626020a27904857689d60` | MIT | new entry `lua` | M27, M28, M35 |
| Lua test suite (`all.lua`, `_U=true`) | 5.4.9, `lua-5.4.9-tests.tar.gz`, archive `7d971845f545ffc09fbb3128a86b2c6524161c70d0fdf0154a16e8c00c343fca`, tree `3bf782c4ae25eeab5e4e739720b685b23ae62f14f0b4a635999858f4fba765e0` | MIT | new entry `lua-tests` | M27, M28, M35 |
| SQLite (amalgamation and shell) | 3.54.1, `sqlite-amalgamation-3540100.zip`, archive `0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9`, tree `4c2d538f44a67d2a8befa009f9e6bbd93e2d2d9c1becc6b97fd9594bef502470` | public domain | new entry `sqlite` | M30, M35 |
| c-testsuite | existing pin `5c7275656d75`; the 150 libc-free tests plus the second list of 70 | MIT harness; each test its scc or tinycc licence | existing | M26, M35 |
| The nine Phase 2 programs | existing pins | as in Phase 2 | existing | regression in every phase gate |
| Link-semantics matrix | the seven P3 rows flip (`p5-tls`, `p6-dtors`, `fini-array`, `crt-xt`, `atexit-order`, `native-callback`, `variadic-def-xtu`); new rows `tls-xtu-init`, `tls-shared-export`, `tls-weak-undef`, `tls-threads`, `sjlj-xasm` | toolchain tests | - | per milestone |
| Feature tests | `msil/test/M3/00-frames` to `10-hosted` | toolchain tests | - | per milestone |
| Rust std experiment | `C:\llvm-msil-scratch\rust-exp\` | - | not a lit leg | recorded at M32 and M35 |

- The three new manifest entries were verified end to end with the tree's unchanged `fetch.py`: 3 of 3 verified,
  3.5 MB in 12 s, and `--check` passes offline. Their trees are the ones every hosted-libc measurement used, and their
  licences allow CI. The alternative Lua pin is 5.5.1 (tree hashes in hosted-libc 9.1); its test suite also passed on
  WSL .NET 8.
- Cost per triple and level: zlib about 10 s of compilation, Lua 25-31 s, SQLite 56-75 s (`sqlite3.c` is a single
  47-49 s translation unit). Each SQLite shell run spends about 1.1-1.3 s in JIT and startup, so its tests use
  `%{msil_limits}`. The phase gate's hosted part costs about 15-25 CPU-minutes of compilation and 10-15 minutes of runs.

---

## 7. Backlog placement

| Item (source) | Home |
|---|---|
| Native setjmp binding on the Linux object pipeline; Status.md's wrong "error" row (sjlj S10, backlog S6, BL15) | M24 |
| f16 helpers bound to `libgcc_s`; `-latomic` binding `__atomic_*` (HS H3, atomics Q-AT12) | M24 |
| A libcall signature mismatch that warns, then crashes (fp-helpers F2/I2) | M24 |
| The D36 compiler-barrier hang (atomics Q-AT8) | M24 |
| bf16 signature mismatch; `areTypesABICompatible` and half/bfloat (HS H4, T25) | M24 |
| Constrained FP <-> i128 compile-time error (Status.md 3) | M24 |
| Tool timeouts under load; gate time; matrix checker gaps; Linux coverage (T10, T13, T15, T16) | M24 |
| Plan row 8932's typo (T5's note) | M24, as a section 12 row |
| P3 matrix oracle gaps (T12) | M24 (E7h); rows per milestone |
| `__ELF__` (plan section 10 item 9) | M25 (R12) |
| `long double` functions on wide triples (T27) | M25 (R15) |
| c-testsuite 00140 and the 70 excluded tests (T11, T33) | M26 |
| A variadic function referenced only by address (T29) | M26 (definitions), M28 (native ones) |
| The 46-name callback list; undiagnosed callbacks off the list (T28, G4) | M28 |
| A native function's address differing between assemblies (L12) | M28 (CB5) |
| The spilled `'<msil>Data'` race (T8, Status.md item 22) | M28 |
| Vectored exception handlers (BL17, F7) | M28 (documentation) |
| `.preinit_array` (backlog S13) | M29 |
| Command-line fidelity under `dotnet` (F2, HANDOFF issue 15) | M29 |
| Explicit `-fms-volatile`; stock-clang `/volatile:ms` (T18, F4) | M30 |
| Data-import retry in the application directory (L4) | M30 |
| `thread_local` exports of libraries; `extern_weak thread_local` (T9, T24) | M31 |
| Rust retarget artifacts: `pause`, `__fastfail`, dead x86 intrinsics, `__cpu_model` (HANDOFF issue 15, BL5) | M32 |
| The i128 division hook and expansion (T19, HS defect c) | M33 |
| `NATIVE_INCOMPATIBLE`; structs by value; SysV `__int128` (T30, S16, BL2, BL16) | M34 |
| The Rust note in Status.md and GettingStarted (T31) | M35 |
| Per-object member counts (T5) | Phase 4 |
| The 64-bit relocation loop (T7) | Phase 5 |
| ifuncs | Phase 4, or drop |
| `.CRT$XP*`; `__float128` and rounding modes; inline asm | stay unsupported |
| CI's first dispatch; the CI corpus licences (T14, G2) | R24, R23 (before M24) |
| Design.md items 16-22 (T17) | R22 (with the plan commit) |

---

## 8. Design.md and plan section 7 changes implied (not edited here)

Design.md is yours. [backlog-p3](backlog-p3.md) section 4.1 gives the exact replacement texts D-1 to D-41, with line
numbers re-checked at HEAD; each row names the decision it depends on. Under the recommendations of this brief:

- **Applied as written:**
  - D-1 (the header), D-2;
  - D-3 (the 2.1 table: a shadow stack only for repeated allocas; a native `va_list`);
  - D-4 (2.3: structs by value);
  - D-5 (3: `__ELF__` defined, 16-byte atomics never lock-free), D-6 (3: `/volatile:ms`);
  - D-7 (3.1: no native vararg), D-8 (3.2: fallbacks);
  - D-9 (4.1: the exit protocol), D-10 (4.2), D-11 (4.4: holder, exports), D-12 (4.5: the exact lookup);
  - D-13 (4.6: `ExternalException`; Windows-only across native frames);
  - D-15 (a new 4.8: atomics);
  - D-16 to D-19 (5.1 and 5.2, items 17-19 and 23), D-21 (5.4: TLS exports, `'<msil>fini'`);
  - D-22 (6: `clang_rt.builtins`), D-23 (6: exact substitutions);
  - D-24 (7.1: target features), D-25 (7.1: i128 as per-module helpers);
  - D-26, D-27 (8), D-28 (10: exceptions through native frames), D-29 and D-30 (11);
  - D-31 (D36), D-32 (`long double` forwarding), D-33 to D-38 (10, 6, 7.1, 5.1);
  - D-39, D-40 (4.5: asynchronous callbacks).
- **D-20** (5.3: DisableRuntimeMarshalling as an msil-ld switch; SuppressGCTransition per declaration) follows R9.
- **Changed by this brief:**
  - D-14 (4.7: variadics) drops the attribute and says instead "a built-in fixed-arity list; an attribute may come
    with managed calls";
  - D-41 (6: the `<msil>v1:` key convention) also names the root-exit slot (R5);
  - section 6's portable-libc candidate sentence stays until you name LLVM libc formally.

**Plan section 7.** backlog-p3 4.2 has the full P3-A and P3-B replacement paragraphs. Under this brief they differ
as follows:
- The intro's front-loading sentence becomes: "Phase 3 needs no widely-included header edit: no IR intrinsic,
  `RuntimeLibcalls.td` or `CallingConv.h` change, and the `msil_fixed_arity` attribute moves to the managed-calls work;
  each milestone appends its own `BCLMembers.def` rows and record items." This replaces backlog's front-load list.
- P3-A and P3-B keep their scope paragraphs; a sentence says the milestones interleave them in program order (R1).
- P3-A: "module flags or TargetOptions" becomes target features (`-mmsil-<name>`) plus msil-ld options, and exact
  substitutions become defaults (R10).
- P3-B:
  - "msil_fixed_arity attribute" becomes the built-in list;
  - "f16 through System.Half where ..." becomes compiler-rt's helpers;
  - i128 becomes per-module COMDAT helpers;
  - the lock table becomes process-wide, under the key convention.
- The plan's title becomes "Phases 0-3".

**Other plan text**, changed by section 12 rows of the milestone that changes it:
- D14: `__` names resolve from the archive in objects and LTO; `-S` stays frozen;
- D16 (M33); D19 and D26 (M29, M31); D21 (M27, M28, M30); D36 (M24);
- section 10 item 9 (M25);
- 5.12.10: the timeout retry and the gate legs (M24);
- 5.12.9 and the c-testsuite list contract (M26);
- plan 5.9.8's adapter rule for RuntimeLibcalls names (M24).

---

## 9. The late goals

Nothing recommended here precludes a late goal ([backlog-p3](backlog-p3.md) section 5 has the full table). What the
recommendations do to each:

- **Portable triple (`msil64-unknown-dotnet`, LLVM libc).** Eased:
  - It gets its own helper archive the same way (R2). It will need a `__MSIL__` branch in `int_lib.h` (neither
    `__ELF__` nor `_WIN32` holds there) and its own choice about `__ELF__`, which would select LLVM libc's bare-metal
    OS layer.
  - The trailer defines `exit` and `atexit` only when the link leaves them undefined, so LLVM libc's own definitions
    win. The triple will need a trailer mode that leaves destructor tables to the libc, and an OS-layer `_Exit` over
    `Environment.Exit`.
  - It needs its own `VariadicABIInfo` (the `char*` form), an empty fixed-arity list and no v-variant table. Its
    `long double` should be `double` from the start.
  - **Keep:** the dispatch is one IR step with a mode flag, so a managed-only function-pointer model (no native code)
    is a mode, not a fork; and helpers never call the host libc.
- **Managed calls from C.** Eased:
  - native-callable function pointers match C#'s `delegate* unmanaged` and `GetFunctionPointerForDelegate`;
  - Q11's optional substitution is a subset of "C calls a .NET member that msil-ld binds";
  - the deferred `msil_fixed_arity` attribute can be batched with that work's clang attributes;
  - M34's aggregate descriptor serves its value types and FeatureIdeas 3's typed overloads.
- **Inline IL.** The whole-body `.try` capability (M27) is a first step toward user EH regions. One documentation
  rule: inline IL that calls a C function pointer uses the dispatch, never a managed `calli` of the value.
- **FeatureIdeas.**
  - Keep helper state (TLS holder, exit lists, thunk table, shadow stack) in one holder class per concern, so an
    enclosed-instance model (FI6) can replace it.
  - Leave room for an instance qualifier in the key scheme: `<msil>v1:<concern>[:<instance>]`.
  - Helper names in `<Module>` would collide in a multi-triple assembly (FI7), so a per-triple holder comes later.
  - Target features settle FI2's "feature flags in general".
- **Final cleanup.** New diagnostic texts carry no phase numbers, so the cleanup touches fewer tests.
- **Toolchain releases.** compiler-rt's CMake route (R2 (i)) is the release form of the archive.

---

## 10. Recon reports (evidence)

| Report | Topic | Decisions here |
|---|---|---|
| [frame-tls](frame-tls.md) | dynamic allocas, the shadow stack, `thread_local` (P3-A) | R6, R7, 3.9 |
| [lifecycle](lifecycle.md) | destructors, `atexit`, `exit` (P3-A) | R4, R5, R13, 3.7 |
| [helpers-switches](helpers-switches.md) | the helper archive, switches, BCL intrinsics, libm, architecture intrinsics (P3-A) | R2, R9, R10, R16 |
| [variadics](variadics.md) | variadic definitions and calls (P3-B) | R14, 3.4 |
| [callbacks](callbacks.md) | native-callable function pointers (P3-B) | R3, R5, R17, 3.6 |
| [sjlj](sjlj.md) | setjmp/longjmp (P3-B) | R8, 3.5 |
| [atomics-threads](atomics-threads.md) | atomics and threads (P3-B) | R5, R11, R12, 3.8 |
| [fp-helpers](fp-helpers.md) | f16, bf16, complex, i128, `long double` (P3-B) | R15, R19, 3.3 |
| [hosted-libc](hosted-libc.md) | zlib, Lua and SQLite on glibc and the UCRT (P3-B) | R1, 3.10, section 6 |
| [tests-gates-p3](tests-gates-p3.md) | tests, corpus, gates | R20, 3.11 |
| [backlog-p3](backlog-p3.md) | backlog triage, Design.md and plan texts, late goals, upstreaming | R21-R24, sections 7-9 |

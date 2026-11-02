# P-0 decision brief: the portable target (msil64-unknown-dotnet), its managed C library and MC-1

Status: P-0 result, 2026-11-01, copied into the repository with the post-M26 plan commit. The user answered every
question on 2026-11-01; the answers are recorded in the plan's section 12 row "Decision brief answers", and the plan
cites them as "brief Q<n>". The contracts 5.18-5.20, the milestone texts and the Design.md edits of that commit
supersede the drafts quoted here where they differ (Q5, Q15 and Q29 changed DM-5, DM-7 and the rows). The brief
summarizes the six P-0 recon reports (`portable-{target,linkrules,loader,libc,state,mc1}.md` beside this file, each
read with its verifier's corrections, which win where they disagree), four drafts of the contracts and milestone
texts (cited below as `draft-contract-5.1x.md` and `draft-milestones.md`; their final versions are in the plan), four
reviews of those drafts (user decisions, dependencies, ownership and mechanics, feasibility) and the orchestrator's
evaluation of the portable program order and its review.

The user's decisions of 2026-11-01 (program order option A, the libc stages, the two triples, `wchar_t`, managed
function pointers, `-static` refused until P-3, the dotnet link family without `__ELF__`, the portable smoke leg in
post-commit runs only, the three C library modes, the shared `-static` state, the lock path) are already recorded in
the plan by the rows of HEAD `e38f9a199bc9` ("Program order", "Portable triples", "Portable target choices"). This
brief does not reopen them. Section 2 asks only what is still open.

Sections: (1) what P-0 measured and found; (2) the decisions still needed; (3) the Design.md edits proposed;
(4) the section 12 rows for the post-M26 plan commit; (5) risks and estimates per milestone; (6) what changed against
the reorder recommendation.

---

## 1. What P-0 measured and found

### 1.1 The loader envelope

P-0 built IL-only images by hand and with today's msil-ld, and ran them on every runtime available on this host:
Windows .NET 8.0.33 and 10.0.14 (`dotnet exec`), .NET Framework 4.8 both started directly as `x.exe` and loaded with
`Assembly.LoadFrom` into a 64-bit Framework process, and WSL Ubuntu .NET 8.0.31 and 10.0.13.

- **A stubless PE32 image runs everywhere.** An IL-only PE32 image with machine I386 and only the `ILONLY` CorFlag
  (Roslyn's AnyCPU), written in msil-ld's current shape (no mscoree import, no import table, no `_CorExeMain` stub, no
  `.reloc`), loads and runs as a 64-bit process on every 64-bit host above. Roslyn's stub and relocations are not needed
  on any runtime. So P-1a does not grow: the PE writer only switches the optional header to PE32 and the machine to
  I386 for the portable triples. The stack reserve stays 4 MB in the PE32 header, because a direct Framework start takes
  the main thread's stack from the image (measured: 1 MB under Roslyn's default, 4 MB with ours).
- **Without a guard, the wrong width gives silently wrong answers.** The same image also loads into every 32-bit
  process measured. A real msil-ld output doing `long long` arithmetic (emitted as `native int` by decision D2) returned
  48 in a 64-bit process and 0 in a 32-bit one (.NET 8/10 x86 and Framework x86), with no diagnostic. So the width guard
  the user asked for ("each refuses a process of the other width") is required for correctness, not just for a nicer
  error.
- **Where the guard goes and how it fails.** A library loaded by a host never runs `'<msil>Main'`, so the guard sits at
  the top of `<Module>::.cctor`. P-0 measured that the module initializer runs before any other member of the module on
  all three runtimes, including a `beforefieldinit` data class and global methods, and that a failure is sticky: every
  later touch of the assembly throws `TypeInitializationException` again. The guard throws
  `PlatformNotSupportedException`. `Environment.FailFast` prints nothing on the Framework console (exit code
  `0x80131623`; the message goes only to the Windows event log), and `Environment.Exit` would kill a host that only
  loaded a library.
- **The byte-order half.** The width test is pure IL. For byte order, `BitConverter.GetBytes(1)[0]` passes the gates'
  verifier (`msil-verify.py`), while reading an `int32` local through `ldind.u1` fails its pass 2. The guard needs four
  BCL rows (the exception type and its `.ctor(string)`, `BitConverter` and `GetBytes(int32)`), not the `IntPtr.Size`,
  `IsLittleEndian` (a field) or `FailFast` rows the recommendation named. The emitter is a new non-hot file
  (`llvm/lib/CLI/TrailerGuard.cpp`) behind one hook in `cli::buildTrailer`.
- **Not measured:** big-endian .NET (s390x), Windows on ARM64 without Framework 4.8.1 (which may start AnyCPU Framework
  EXEs under x86 emulation, where the guard refuses on a 64-bit machine), and 32-bit linux-arm or browser-wasm .NET
  (which the guard refuses, as decided). Status.md documents them. A side finding: the .NET host can resolve an
  assembly to a sibling file with the same simple name (seen on WSL .NET 10), so msil-ld should diagnose a user output
  named `c` that is not the C library.

### 1.2 The target audit (triple, clang, driver, backend)

- **The triple.** `msil32` and `msil64` are new arches and `dotnet` a new OS, appended to `Triple.h` and
  `TripleName.def`. Three upstream behaviours need a change. `Triple::getDefaultFormat` must return MSIL for the two
  arches, because clang keys `__ELF__` on `isOSBinFormatELF()`. `Triple::computeDataLayout` and `getNativeFormatTriple`
  would recurse forever on an MSIL arch. And `RuntimeLibcalls.td` needs an MSIL system library, without which a new arch
  gets no libcalls at all (the generated lookup ends in `return {};`). A non-MSIL arch with OS `dotnet`
  (`x86_64-unknown-dotnet`) would silently become a generic native ELF target, so clang and `TargetRegistry` refuse it.
- **clang.** A standalone `MSILPortableTargetInfo` (LP64 or ILP32, `long double` = `double`, 32-bit signed `wchar_t`, a
  `char*` `va_list`, `__int128` on msil64 only, `__MSIL__`, `__dotnet__`, `__msil64__`, no host macro, no `__ELF__`) and
  an `MSILPortableABIInfo`, matched slot for slot by a new `MSILPortable` class in M26's ExpandVariadics hook. A
  `DotNetToolChain` builds the link job: a sysroot, `-lc` and `clang_rt.builtins` by default, and `-static` refused with
  its own message.
- **Native-triple assumptions.** The backend, msil-ld, lib/CLI and the driver ask the triple "Windows or not?" in many
  places: 13 functions in the MSIL target, 30 functions (46 lines) in msil-ld, 4 functions in the driver and 4 lines in
  clang's MSIL target header. Each needs an explicit dotnet answer. One M26 site needs a change too: `MSILPrepareIR`'s
  `va_arg` check would refuse every portable `va_arg`.
- **Rebuild cost (measured from `.ninja_deps`, no build run).** The header edits rebuild 2,481 objects, 158.9
  job-minutes, which is 86% of a full build: about 40-45 minutes at 4 jobs on a quiet host and about 80 minutes at the
  2-process cap. `Triple.h` alone accounts for 2,477 objects. This is the R21 bend the user is asked about (Q10). P-0
  puts every header edit in P-1a's two root WPs, so the rebuild happens once.

### 1.3 The link-rule family (user: dotnet-specific, not ELF)

- **Why no `__ELF__`.** In llvm-libc, `__ELF__` without `__linux__` selects the baremetal OS layer
  (`src/__support/OSUtil/io.h`, `src/stdlib/abort_utils.h`, `include/llvm-libc-macros/time-macros.h`). Defining it
  would silently pull in baremetal I/O and abort hooks, which is the interim OS layer the user ruled out. Without
  `__ELF__`, those headers select nothing and an OS-dependent use fails loudly.
- **compiler-rt has exactly one blocker.** `int_lib.h`'s `COMPILER_RT_ALIAS` chain reaches `#error Unsupported target`.
  A one-token branch (`|| defined(__dotnet__)`, on the precedent of `__wasm__`) fixes it: 88 of 88 builtins sources
  compiled with it. Every other builtins chain has a generic fallback that is correct for a little-endian target without
  x87. `build-builtins.py` must drop its `-msil` suffix check, and `sources.txt` gives `int_util.c` and the `-ftrapv`
  helpers a dotnet glob.
- **Today's link rules are a two-way switch.** `cli::SectionRules` is compared with `==` at 13 sites and switched
  nowhere, and 8 more sites choose rules by `isOSWindows() ? COFF : ELF` selections or by ELF constants and defaults
  (11 enumerator uses outside `ImageLayout.cpp` in all, among them two member defaults in `ImageLayout.h`). A third
  enumerator would silently take COFF rules at some sites and ELF rules at others. P-1a
  turns every use into a named predicate or an exhaustive switch (inline in `ImageLayout.h`), plus a gate grep that
  fails on any enumerator outside `ImageLayout.cpp` other than a `case` label.
- **The family** (contract 5.18.5):
  - Constructors take no arguments (the wasm rule).
  - `__start_X`/`__stop_X` work as on today's non-Windows triples.
  - Exports of a `-shared` assembly are the default-visibility definitions plus forced exports (`--export`,
    `--export-if-defined`, `--export-list`). A forced export extracts its archive member, as `-u` does, and wins over
    `--exclude-libs`.
  - No native library input and no native binding of any kind: a native input is an error (T-P4), and an undefined C
    library name gets a new report line, N6, that names `c.dll`.
  - `-l` looks for the shared library first, on dotnet only.
  - The C runtime identity is `c`.
  - msil-ld refuses a static take of the build tree's `libc.a` until P-3 (T-P11), so a direct archive path cannot get
    around the `-static` refusal.
  - `.fini_array`, `.preinit_array`, `.ctors`, `.dtors` and `.CRT$X*` are named errors (T-P8). `.tdata`/`.tbss` keep
    Phase 2's `thread_local` error until M31.
  - The link-info record needs no change: dotnet-ness is in the triple key.
- **Destructors (P-2, not P-1a).** Upstream's `LowerGlobalDtorsPass` lowers `llvm.global_dtors` to `__cxa_atexit`, as
  WebAssembly does, and msil-ld defines a hidden per-assembly `__dso_handle`. Measured: the lowered IR compiles, links and
  runs on .NET 10 in ELF's destructor order with managed function pointers. C++ static destructors already need
  `__dso_handle` (today's link fails with `undefined hidden symbol: __dso_handle`). Both wait for P-2, because
  `__cxa_atexit` needs the heap. Until then `llvm.global_dtors` keeps today's diagnostic on dotnet. M29's exit table
  stays off on dotnet.

### 1.4 The libc: compile, link and run

- **Compile.** With the dotnet approximation, 546 of 561 candidate freestanding sources compile, and 835 of 894
  overall (adding fenv, complex, wctype, f16, f128 and the baremetal variants). `printf_core` and `scanf_core` compile.
  The reorder recommendation's claim that 726 of 815 sources "ran" is withdrawn: that probe compiled no stdio file and
  linked nothing. Every variadic stdio entry point stopped only at M26's missing variadic definitions, and M26 precedes
  P-1b, so `snprintf`, `sprintf` and `swprintf` ship in P-1b.
- **Link and run.** A `-shared` `c.dll` with 13 entry points plus the errno provider had 0 `pinvokeimpl` and 0
  `moduleref`, and referenced only the core library. Programs that call `strtod`, `strtol`, `qsort` with their own
  comparator (a cross-assembly managed `calli`), `sinf`, `exp`, `pow`, the string functions, `vsnprintf` with integer,
  string, float and hex-float formats, and errno (`ERANGE` across the assembly boundary) exited 42 on .NET Framework
  4.8, .NET 10 and WSL .NET 10, tiered and TC=0. `&strlen` compares equal in the program and in `c.dll`.
- **At full scale (feasibility review).** llvm-libc's own CMake works from this Windows host with the build's clang:
  configure in 60 seconds, then 437 compiles plus 26 header steps for 434 entry points in 8 minutes 17 seconds at 2 jobs
  on a loaded host. A full-scale `c.dll` from 515 objects linked in about a second: 1.1 MB, 1,354 methods, no ImplMap or
  ModuleRef row, and it passed `msil-verify.py` pass 1. Still unmeasured: CMake's compiler identification on msil64 CIL
  objects. P-1b's build WP checks that first and stops if it fails.
- **Two port defects, both resolved without upstream visibility patches.** (D1) `__llvm_libc_errno` is hidden
  (`LIBC_NAMESPACE_DECL` is `visibility("hidden")`), so `errno` could not cross into programs. A forced export through the
  checked-in `--export-list` fixes it. Every llvm-libc public name is hidden, so a plain `-shared` `c.dll` would export
  nothing. (D2) `vsscanf`, `sscanf` and `asprintf` need `malloc`, so they wait for P-2.
- **Absent, not trapping.** OS-dependent functions stay off the entry-point list. `OSUtil/dotnet` declares
  `write_to_stderr` and `internal::exit` but defines neither. A release (`-DNDEBUG`) build references neither (measured on
  `strfromd.cpp`), and P-1b's gate proves that `c.dll` neither defines nor references them. This replaces the
  recommendation's `__builtin_trap` stubs.
- **How the user's "link errors until P-2" shows up in practice.** llvm-libc's generated headers declare only the
  shipped entry points. So a C99 or C++ program that includes `<stdlib.h>` and calls `malloc` gets an undeclared-function
  compile error. A program that writes its own prototype gets an undefined-symbol link error with N6's line naming
  `c.dll`. Both are loud. P-1b's `absent` test pins both cases.
- **Withheld because the freestanding answer would be silently wrong:**
  - the `localtime` family, `mktime` and `strftime`, which would answer in UTC (Q18);
  - the fenv functions, whose dummy implementation silently succeeds (Q20);
  - `rand`/`srand`, whose `cpp::Atomic` calls the libatomic interface (until P-3, after M30);
  - `hcreate`/`hsearch`, which need the heap and a raw `thread_local` (P-2);
  - `strsignal` and `signal.h`, until a signal design (Q21).
- **P-1b's set.** About 510 entry points: ctype, inttypes, locale, math without f16 and f128, search without the hash
  table, stdbit, the stdlib numeric and sort functions, string, strings, the pure time functions and wchar, plus the
  buffer printf family.

### 1.5 MC-1: minimum managed calls from C and C++

- **The link side exists.** msil-ld's M21 binder already binds any public member by name and full signature once
  `--reference` is given. A hand-written CIL object with MC-1's stub shapes linked against the Framework 4.8 directory
  or the .NET 8 pack, verified, and ran on Framework 4.8.1, .NET 8, .NET 10 and WSL .NET 8/10. Without references, only
  members in the built-in `[mscorlib]` table bind.
- **The compiler side is what MC-1 builds** (contract 5.20):
  - **Declaration.** The attribute `msil_method("Ns.Type", "Member", "<CIL signature>"[, "assembly"])` names one public
    member. The repeatable `msil_catch("Ns.Exception", code)` maps listed exceptions to an error code written through a
    trailing `int*` parameter.
  - **IR and stubs.** clang records the call as IR attributes on a renamed declaration `__msil_mc.<symbol>`. The backend
    makes one internal stub per declaration (a new delivery kind, K5). The AsmPrinter writes its body: the conversions,
    then `call`, `callvirt`, `newobj`, `newarr`, `ldsfld` or `ldfld`, and, with `msil_catch`, a filter clause through
    M27's `emitExceptionClause`. No new clause kind is needed.
  - **Unlisted exceptions** escape with their stack trace.
  - **Strings.** C strings decode through *throwing* `UTF8Encoding`/`UTF32Encoding` instances, so invalid input becomes
    a mappable `DecoderFallbackException` rather than a silent U+FFFD. `Marshal.PtrToStringUTF8` is absent on 4.8, and
    `PtrToStringAnsi` uses the ANSI code page.
  - **Objects** are normal `GCHandle`s carried as `native int`.
  - **Address-of.** Taking the address of a managed declaration is an error on every triple until the fuller feature.
  - **Triples.** MC-1 works on the host triples and on msil64.
  - **What MC-1 leaves out:** generics, byrefs, spans and delegates (so `Monitor.Enter(object, ref bool)` is out, while
    `Monitor.Enter(object)` is in).
- **Two hazards found.** (a) `check-mscorlib-safe.py` proves rows with `ldtoken`, which skips the accessibility check:
  Framework 4.8.1's internal `Environment::get_TickCount64()` passes the probe, and a real call throws
  `MethodAccessException`. P-1a's harness WP fixes the script (call, `newobj` or reflection `IsPublic`), because P-1a's
  guard rows are the first mscorlib-safe rows after the finding. (b) The Framework 4.8 JIT silently accepts an `int32`
  where a struct parameter (`DateTime`) is expected, while .NET 8 throws `InvalidProgramException`. So msil-ld checks
  every enum pairing against the referenced TypeDef, and refuses a value-type pairing it cannot check without
  references.
- **BCL rows.** MC-1 appends one block of 15 mscorlib-safe rows: `GCHandle` and its members, the throwing encoders,
  `String::.ctor(char*)` and the type `DecoderFallbackException`. All resolve through `[mscorlib]` on Framework 4.8.1
  and .NET 8. MC-1's `String` type row collides with M32's planned row, which a section 12 row fixes (section 4).
  P-1a's `BitConverter` type row collides with M33's planned row; P-1r's row R18 fixes that.

### 1.6 The shared state layout (frozen at P-3)

- **Discovery.** Every copy of the managed libc, `c.dll` and every `-static` copy alike, finds the same state through
  five names that never change:
  - the process root, a never-freed unmanaged block published as a boxed `IntPtr` under the AppDomain-data key
    `<msil>v1:libc.root`, and created under a `Monitor` on the interned string `<msil>v1:libc.root.lock`;
  - a per-thread C block in the named thread data slot `<msil>v1:libc.thread`;
  - 64 wait buckets, `Monitor`s on the interned strings `<msil>v1:libc.wait:<i>`;
  - the atomics lock path's names `<msil>v1:atomic.lock:<i>` (already decided).

  Scope: one per process on modern .NET, across load contexts; one per AppDomain on Framework.
- **The root** is a header plus a 64-slot directory of versioned sub-blocks: stdio, descriptors, exit lists,
  environment cache, locale, misc (`rand`, `strtok`, the mbstates), signals, tss keys, threads, time zone, start-up and
  the heap scheme.
- **The per-thread C block** holds `errno` at offset 12, the locale, the tss values, the thread-exit list, the thread
  descriptor, the signal mask and an extension for the objects llvm-libc keeps in `LIBC_THREAD_LOCAL`.
- **Versioning rules.**
  - A name never changes, and a field is never reinterpreted.
  - After the freeze a sub-block never grows; a new field takes a new directory id.
  - An unknown major or a failed layout check is a loud failure naming both versions, never a private copy (the user's
    one-shared-state decision).
  - Major 0 is provisional from P-2; major 1 is frozen at P-3's commit as an append-only ABI between libc releases.
- **Staging follows the user's order.** P-1b keeps its few stateful globals in the single `c.dll` copy, with errno in one
  exported cell. P-2 creates the root at major 0. P-3 creates the per-thread block (errno moves into it with the same
  `__llvm_libc_errno` ABI) and freezes major 1.
- **Thread-locals.** In P-3's platform thread mode, llvm-libc's `LIBC_THREAD_LOCAL` becomes `thread_local`, which would
  need M31 (after P-3) and would give each copy its own object. So every shipped site (`strerror`'s buffer in
  `error_to_string.cpp`, `l64a.cpp`, `randomness.h`, and conditionally the signal and thread sources) gets a dotnet
  branch into the C block, and P-3's gate checks that no libc object has a TLS section.
- **The heap** is not an object but a frozen scheme. P-0 recommends one scheme on every runtime (Q29): a fixed header
  over `Marshal.AllocHGlobal`. `NativeMemory` is absent on Framework, and a block freed by the other scheme corrupts the
  heap.
- **Cost of a slot lookup:** 12-33 ns per `errno` access until a faster per-thread cache exists (about 1.5 ns with a
  thread-static). Correct, but slow for errno-heavy loops.

### 1.7 Still to reconcile before the plan commit (orchestrator work, no user decision)

The four drafts were revised in parallel after the reviews. These disagreements remain, and the orchestrator settles
them before the plan commit:
1. **MC-1's portable `catch-map` test.** The milestone text uses `System.IO.File::OpenRead(string)`, which is not in
   the built-in table, so it cannot link without `--reference`. Contract 5.20 adds the `DecoderFallbackException` type
   row instead (15 rows). Take 5.20's version. MC-1.a's row list must also say `String::.ctor(char*)`, as measured, not
   `String::.ctor(char*, int32, int32)`.
2. **The stub name.** The milestone text has `__msil_mc.<n>`; 5.20 has `__msil_mc.<symbol>`, which closes an LTO
   hazard. Take 5.20's.
3. **T-M texts.** T-M1 to T-M9 differ between the milestone text and 5.20, and the address-of error and the catch-all
   refusal have no T-M id. Adopt one wording.
4. **The host `static-call` test** binds `Math` through the built-in table, but `Math` is M33's row, which lands after
   MC-1. It needs `--reference` or another member.
5. **MC-1.b's Owned files** must list `SemaMSILManagedCall.h`.
6. **Q28's mechanism.** If the user picks the recommended option, 5.18 and the P-2 outline gain the trailer call
   `__llvm_libc_set_c_main()`, which neither has yet.
7. **The orchestration tooling.** The extractor and workflow id changes (PC-6) are git-ignored edits made before P-1a
   is launched. The `plan-check.py` change rides in the plan commit (an orchestrator call: a plan commit carrying one
   `msil/utils` file).
8. **P-1r runs as a read-only workflow.** It feeds one `[MSIL] Plan:` commit and is not a farm milestone, because its
   product is plan rows and the plan must not change during a farm run. This is an orchestrator choice.

---

## 2. Decisions still needed from the user

Each question has a recommendation, and "all as recommended" settles every one of them. The "Draft ids" column maps
each question to the drafts' own numbers (M = `draft-milestones.md` section 9; 5.18/5.19/5.20 = that contract's user
questions). Questions are grouped by the step that needs the answer.

### 2.1 Needed before the plan commit (P-1a)

| # | Question | Options | Recommendation | Draft ids |
|---|---|---|---|---|
| Q1 | How does an msil64 assembly fail in a 32-bit or big-endian process? | (a) throw `PlatformNotSupportedException` from `<Module>::.cctor`; (b) `Environment.FailFast` (silent on the Framework console); (c) print and `Environment.Exit` (kills a host that only loaded a library) | (a) | M U1, 5.18 U4 |
| Q2 | Should the guard also refuse big-endian runtimes? | (a) yes, with `BitConverter.GetBytes(1)[0]`: verifiable, two more BCL rows; (b) yes, with an `ldind.u1` read (fails the gates' verifier); (c) width only | (a): the trailer's image copy and every `ldind` assume little-endian, so s390x .NET would compute garbage | M U2, 5.18 U4 |
| Q3 | P/Invoke (explicit `import_module`/`import_name`) on the portable triples? | (a) refused (T-P2); (b) allowed as an opt-in that makes the output platform-dependent | (a): it can be added later without breaking anything, and `c.dll` keeps 0 `pinvokeimpl` either way | M U8, 5.18 U5 |
| Q4 | C type spellings | (a) wasm-style: `size_t` is `unsigned long` and `int64_t` is `long long` on both widths; (b) Linux-style (`unsigned int` `size_t` on msil32, `long` `int64_t` on msil64) | (a): identical C++ mangling across the two widths | M U10, 5.18 U1 |
| Q5 | `wint_t` and `char` | (a) `wint_t` `unsigned int`, `char` signed; (b) `wint_t` `int` (wasm) | (a): matches llvm-libc's `WEOF`, glibc and musl | M U9, 5.18 U2 |
| Q6 | `max_align_t` with `long double` = `double` | (a) clang's natural 8, with `malloc` still 16-aligned (P-2); (b) 16 through a target override | (a) | M U12, 5.18 U3 |
| Q7 | Do constructors receive arguments on the portable triples? | (a) none (the wasm rule); (b) `(argc, argv)` without envp | (a): .NET has no glibc-style constructor arguments | M U23, 5.18 U7 |
| Q8 | Shared-first `-l` (Design.md 5.2) on which triples? | (a) `*-unknown-dotnet` only; (b) every triple | (a): host links stay byte-identical | M U13, 5.18 U12 |
| Q9 | Where does the portable `clang_rt.builtins` live? | (a) the resource directory, like M25's archives; (b) the sysroot | (a) | M U11, 5.18 U10 |
| Q10 | **R21 bend:** the `Triple.h`, `TripleName.def` and `RuntimeLibcalls.td` edits land mid-phase in P-1a's root WPs (one rebuild of about 159 job-minutes, never during a post-commit run) | approve; decline (which blocks the portable target) | approve | M U20, 5.18 U11 |
| Q11 | The portable legs run only in post-commit runs (decided). Do the P-steps' and MC-1's **own gates** also run their **own** portable directories (with `msil-lit.ps1` on that directory, never through the standing legs)? | (a) yes, the P-steps' gates only (P-1a, P-1b, later P-2, P-3, P-4); (b) yes, MC-1's too; (c) no, post-commit only | (a): a P-step's exit criteria are portable behaviour, which only its own directory proves before its commit; MC-1's exit criteria are proved on the host triples; M27-M35's gates stay as written either way | M U24, 5.18 U14, 5.19 U14 |
| Q12 | The Design.md edits of section 3 | approve all; approve individually; reject | approve all (DM-7 follows Q29) | M U21, 5.18 U13 |
| Q13 | Portable LTO coverage before P-3 | (a) none: the portable legs run objects only, and LTO is added to them at P-3; (b) a separate post-commit leg `portable-lto` from P-1a | (a): it keeps to the user's one smoke leg, and the host LTO legs already cover the shared code | 5.18 O13 |
| Q14 | Ship an empty `libm.a` in the portable sysroot, so build systems that pass `-lm` link (wasi-libc's convention)? | (a) an empty `libm.a` only (math is in `c.dll`); (b) nothing: `-lm` is a loud "library not found" | (a): `-lm` asks for something `c.dll` provides, so an error there would be noise, not a caught mistake | 5.18 O7 |

### 2.2 Needed before P-1b (the C ABI that freezes at P-1b's commit)

| # | Question | Options | Recommendation | Draft ids |
|---|---|---|---|---|
| Q15 | errno numbering for the portable C ABI | (a) the full Linux asm-generic table in a new dotnet header family (`EOVERFLOW` 75); (b) llvm-libc's generic set extended (it changes baremetal users, and its `EOVERFLOW` 35 is Linux's `EDEADLK`); (c) UCRT or WASI numbers | (a) | M U3, 5.19 U2 |
| Q16 | **errno storage until P-3, an exception to fail-loudly.** P-1b's `errno` is one exported cell. A host that calls `c.dll` from several threads races silently on it (and on the libc's few hidden statics) until P-3's per-thread block. No loud check is possible before MC-1. Accept this exception, documented in Status.md? | (a) accept: EXTERNAL mode over one cell, with the same ABI and provider when P-3 moves the storage; (b) llvm-libc's SHARED mode (the same program ABI, a per-copy cell; no better for threads) | (a) | M U4, 5.19 O2 |
| Q17 | `c.dll`'s exports | (a) a generated, checked-in, append-only `--export-list`; libc objects stay hidden, so `libc.a` re-exports nothing; (b) default visibility plus upstream patches to `libc_errno.h` and the other `extern "C"` names | (a): no upstream patch, and the list is the reviewable C ABI | 5.18 U8, 5.19 U1 |
| Q18 | Ship the time-zone-dependent functions (`localtime`, `localtime_r`, `ctime`, `ctime_r`, `mktime`, `strftime`, `strftime_l`) in P-1b, where they would answer in UTC? | (a) absent until P-2's time-zone layer; (b) UTC-only | (a): fail loudly, no interim answer | M U5, 5.19 U4 |
| Q19 | `struct tm` with `tm_gmtoff` and `tm_zone`? | (a) yes (glibc, BSD, musl); (b) ISO C fields only | (a): adding them later changes `sizeof(struct tm)` | M U6, 5.19 U3 |
| Q20 | `<fenv.h>`? llvm-libc's dummy `FEnvImpl` silently succeeds | (a) no fenv functions, `math_errhandling` = `MATH_ERRNO`; (b) ship them as no-ops; (c) a software fenv from P-2 (flags in the C block, round-to-nearest only) | (a) now, (c) on request | M U7, 5.19 U5 |
| Q21 | Signals (`signal.h`, `strsignal`, `raise`) | (a) absent until a signal design, with Linux numbers then; (b) Linux numbering now, so `strsignal` links | (a) | 5.19 U6 |
| Q22 | Deploying `c.dll` next to a program | (a) the user's or the build system's job, as with `csc` references (a missing file fails loudly at run time); (b) the driver copies it beside an executable it links | (a): like Roslyn, the compiler writes only its output | 5.19 U8 |

### 2.3 Needed before MC-1

| # | Question | Options | Recommendation | Draft ids |
|---|---|---|---|---|
| Q23 | Managed-call attribute spelling | (a) `msil_method(type, member, signature[, assembly])` plus a repeatable `msil_catch(exception, code)` (GNU and `[[clang::...]]` spellings); (b) one attribute with a catch-list string; (c) a reserved prefix on `import_module`/`import_name` | (a) | M U14, 5.20 U1 |
| Q24 | Exceptions from managed calls | (A) listed exceptions map to codes through a per-stub filter, and unlisted ones escape loudly; MC-1 stays after M27; (B) everything escapes, and the mapping comes later | (A): P-2's `fopen` needs the mapping | 5.20 U2 |
| Q25 | Value types other than enums (`DateTime`, `TimeSpan`) | (a) as opaque boxed handles; (b) enums only until the fuller goal | (a): P-2's clocks and P-3's boxed `IntPtr` root need it, with no overlap with M33/M34's struct work | M U16, 5.20 U4 |
| Q26 | Decoding C strings in managed calls | (a) throwing UTF-8/UTF-32 encoders (invalid input becomes an exception that `msil_catch` maps, for example to `EILSEQ`); (b) `Encoding.UTF8` (silently replaces invalid bytes with U+FFFD) | (a) | M U17 |
| Q27 | Delegates over C functions | (a) none in MC-1: P-2 is designed without them, and they come at P-3 (MC-3's piece, over M28's `getDelegateConstructor`); (b) widen MC-1 with them (duplicates or pre-empts M28's work) | (a) | 5.20 U6 |

### 2.4 Needed before P-2 (can be answered later; P-0 recommends answering now)

| # | Question | Options | Recommendation | Draft ids |
|---|---|---|---|---|
| Q28 | Exit handlers and stream flushing when the host is **not** a C program (a C# program calling `c.dll`), before P-3 brings delegates | (a) handlers and the final flush run only through `exit()`; a non-C host that never calls `exit` loses unflushed output (an exception to fail-loudly, in Status.md); (b) as (a), but `c.dll`'s standard streams stay unbuffered until a C program's start-up says otherwise: the dotnet `'<msil>Main'` calls an exported `__llvm_libc_set_c_main()` before `main`, so a non-C host loses nothing (one more trailer item at P-2); (c) widen MC-1 with delegates so P-2 subscribes `AppDomain.ProcessExit` | (b): nothing is lost silently, for one trailer call; from P-3, `c.dll` subscribes `ProcessExit` in every option | 5.19 U10 |
| Q29 | The shared C heap. Option (a) **revises the user's mapping of 2026-10-31 23:09** ("the heap on NativeMemory (Marshal.AllocHGlobal on Framework)") | (a) a two-pointer header over `Marshal.AllocHGlobal` on every runtime; (b) `NativeMemory` on .NET 6+ and `AllocHGlobal` on Framework, as recorded | (a): `NativeMemory` is absent on Framework, shared `-static` state means copies built for different runtimes can free each other's blocks, and two schemes in one process corrupt the heap (Phase 2 measured `0xC0000374` for a cross free between two C runtimes) | M U19, 5.19 U15 |
| Q30 | How does `c.dll` bind its `System.*` members so it runs on Framework 4.8 and modern .NET? | (a) `--reference` to the Framework 4.8 implementation assemblies of the build host (works on any Windows host; Mono's 4.8-api references on Linux are unverified); (b) a reviewed built-in list extended with a `[System]` scope (a binder and D10 change, about 70-90 hot BCL rows, and enum checks lost without references); (c) the .NET 8 pack (drops Framework, against the standing rule) | (a), decided at P-2 | M U15, 5.20 U3 |
| Q31 | Global destructors on the portable triples | (a) upstream `LowerGlobalDtorsPass` to `__cxa_atexit` (wasm), with `.fini_array` an error; (b) FINI entries plus an M29-style runner | (a): measured working, and it needs no trailer exit table | M U22, 5.18 U6 |
| Q32 | When do the shared layouts freeze? | (a) provisional major 0 from P-2, major 1 at P-3's commit; (b) frozen at P-2's commit | (a): P-3's commit is the first at which two copies can share | 5.19 U13 |

### 2.5 Needed before P-3 (asked now for completeness)

| # | Question | Options | Recommendation | Draft ids |
|---|---|---|---|---|
| Q33 | How does a copy fail when it finds a shared root of an unknown major or a mismatched layout? (A private-instance fallback is excluded by the user's one-shared-state decision.) | (a) `Environment.FailFast` after writing both versions to stderr; (b) throw an exception naming both versions | (a): an exception could be caught, and the program would run on with no C state | M U18, 5.19 U12 |
| Q34 | `strtok` scope and tss keys (both freeze with P-3's layouts) | `strtok` per process (LLVM, glibc) or per thread (UCRT); keys as LLVM's 32-bit keys with the table in the root, or 64-bit GCHandle keys | per process; 32-bit keys | 5.19 U9 |
| Q35 | The `getopt` variables under `-static` | (a) per copy; (b) shared through a msil-ld process-data binding | (a) | 5.19 U11 |

Deferred to the P-2 and P-3 milestone drafts (not asked now): the spelling of `stdin`/`stdout`/`stderr` (5.19 U7,
recommended: lvalue macros over exported accessors; it freezes at P-2), the skip of Phase 2 decision Q21's "defined in X and exported
by Y" warning for WEAK/WEAK pairs on dotnet (5.18 U9, a C++-library question), `TZ` handling, a POSIX descriptor layer,
`environ`, and the strong-naming policy of `c.dll`.

---

## 3. Design.md edits proposed (need the user's approval, Q12)

Each edit gives the old text exactly as it stands in `llvm/docs/MSIL/Design.md` and the new text, and names its
driver. DM-1 to DM-17 extend PC-8's list of the milestone draft (section 1, 3, 4.5, 5.1, 5.2, 6 and the phase table)
with goal 6, 4.3, 4.4, 4.7, 5.2's resolution step and section 8, whose old texts the portable triples would otherwise
contradict. The heap edit (DM-7) has two versions, one per answer to Q29.

**DM-1. Section 1, goal 6.** Driver: the 2026-10-26 row (LLVM's in-tree libc) and the program order of 2026-11-01.

```text
6. The C library is a choice of target, as it is in C: glibc, musl, UCRT/MSVCRT, Darwin libSystem. A portable libc for .NET is a planned exploration after phase 3 (section 6).
```
```text
6. The C library is a choice of target, as it is in C: glibc, musl, UCRT/MSVCRT or Darwin libSystem on the host-libc triples, and LLVM's in-tree libc compiled to CIL on the portable triples `msil64-unknown-dotnet` and `msil32-unknown-dotnet` (section 6).
```

**DM-2. Section 1, non-goals: managed calls.** Driver: program order option A (MC-1 before P-2); contract 5.20.12
item 8.

```text
Two narrower features are later goals (user, 2026-10-22), after phase 3 at the earliest: C source calling managed (.NET) methods directly, and inline IL.
```
```text
Two narrower features are later goals (user, 2026-10-22): C source calling managed (.NET) methods directly, and inline IL. A first stage of the managed calls (an attribute that names one public .NET member, object handles, and listed exceptions mapped to error codes) ships during phase 3, because the portable C library's OS layer is written on it (user, 2026-11-01, program order option A); the fuller managed calls and inline IL come after phase 3 at the earliest.
```

**DM-3. Section 3, the triples paragraph.** Driver: the 2026-11-01 "Portable triples" row; finding 1.2 (MSIL is the
default object format of the new arches).

```text
so we add `msil` as a new `Triple::ObjectFormatType`.
```
```text
so we add `msil` as a new `Triple::ObjectFormatType`. The two portable triples at the end of the table are the exception: they name no CPU and no host platform, and their default object format is MSIL (below).
```

**DM-4. Section 3, the table's portable rows.** Driver: the 2026-11-01 decisions (msil64 first, msil32 in P-4 refused
loudly until then, `wchar_t` 32-bit signed, the three modes).

```text
| `msil64-unknown-dotnet` (planned exploration after phase 3) | Portable .NET libc (section 6) | LP64 |
| `msil32-unknown-dotnet` (planned exploration after phase 3) | Portable .NET libc (section 6) | ILP32 |
```
```text
| `msil64-unknown-dotnet` (plan steps P-1a to P-3, inside phase 3's sequence) | Managed LLVM libc: the assembly `c.dll`, or a static archive from P-3 (section 6) | LP64, 32-bit signed `wchar_t` |
| `msil32-unknown-dotnet` (plan step P-4, after phase 3; clang accepts it, the backend refuses it until then) | Managed LLVM libc (section 6) | ILP32, 32-bit signed `wchar_t` |
```

**DM-5. Section 3, a new bullet after the "Native `va_list` everywhere" bullet.** Driver: the 2026-11-01 decisions (no
`__ELF__`, a dotnet link family, managed function pointers) and Q3, Q4, Q5 and Q7 as recommended (the bullet's
parenthesis follows their answers).

Insert after the bullet beginning:
```text
- **Native `va_list` everywhere:**
```
the new bullet:
```text
- **The portable triples** (`msil32-unknown-dotnet`, `msil64-unknown-dotnet`) are the exception to the native `TargetInfo` rule. Clang has its own target and ABI lowering for them (ILP32 or LP64, `long double` = `double`, 32-bit signed `wchar_t`, unsigned `wint_t`, signed `char`, a `char*` `va_list` that `ExpandVariadics` matches, `__int128` on msil64 only), predefines `__MSIL__`, `__dotnet__` and `__msil64__` or `__msil32__` but no host macro and no `__ELF__`, and selects the MSIL object format by default. `msil-ld` links them with a link-rule family of their own, neither ELF's nor COFF's: constructors without arguments, `__start_`/`__stop_` symbols, exports forced by a list, and no native binding of any kind (P/Invoke is refused) (user, 2026-11-01).
```

**DM-6. Section 3, the "Pointer size is fixed" bullet.** Driver: loader findings 1.1 (stubless AnyCPU PE32 runs; a
wrong-width process computes silently wrong results); the user's "each refuses a process of the other width"; Q1, Q2.

```text
- Pointer size is fixed at compile time. Every triple names a CPU, so the PE's machine type is set (AMD64/ARM64/I386) and the runtime refuses to load the assembly on the wrong architecture, the same as a C# project built with `/platform:x64`. Option for later: a Windows x64 assembly that uses no arch-specific intrinsics could be marked AnyCPU-64, since x64 and ARM64 Windows share every layout.
```
```text
- Pointer size is fixed at compile time. Every host-libc triple names a CPU, so the PE's machine type is set (AMD64/ARM64/I386) and the runtime refuses to load the assembly on the wrong architecture, the same as a C# project built with `/platform:x64`. Option for later: a Windows x64 assembly that uses no arch-specific intrinsics could be marked AnyCPU-64, since x64 and ARM64 Windows share every layout. The portable triples name no CPU: their images and objects are IL-only AnyCPU (PE32, machine I386, only the `ILONLY` flag, no loader stub), which every 64-bit .NET runtime measured runs as a 64-bit process, and the module initializer starts with a guard that throws `PlatformNotSupportedException` in a process of the other pointer width or of big-endian byte order, where the image would otherwise run with silently wrong results.
```

**DM-7. Section 4.3, the heap.** Driver: Q29. It replaces the sentence "With a future portable libc ..." only.

```text
With a future portable libc it would be backed by `NativeMemory` or `Marshal.AllocHGlobal` (7.1).
```
Under Q29 (a) (recommended):
```text
On the portable triples it is the managed libc's `malloc` (from plan step P-2): a fixed header over `Marshal.AllocHGlobal` on every runtime, so that any copy of the libc in the process can free any block (section 6).
```
Under Q29 (b):
```text
On the portable triples it is the managed libc's `malloc` (from plan step P-2), over `NativeMemory` on .NET 6 and later and `Marshal.AllocHGlobal` on .NET Framework, with the scheme recorded in the shared state so that every copy of the libc in a process uses the same one (section 6).
```

**DM-8. Section 4.4, threads on the portable triples.** Driver: program order (threads in P-3); the 2026-10-31 23:09
mapping (threads on `System.Threading.Thread`); finding 1.6.

```text
Threads started by native code run no such list, and `pthread_key_create` destructors stay native on Linux.
```
```text
Threads started by native code run no such list, and `pthread_key_create` destructors stay native on Linux. On the portable triples (from plan step P-3), C threads are `System.Threading.Thread`s that the managed libc starts, and the libc keeps each thread's C state, `errno` among it, in a per-thread block found through a named thread data slot (section 6); `thread_local` uses the block described above.
```

**DM-9. Section 4.5, the representation bullet.** Driver: the 2026-11-01 decision (managed function pointers, M28's
thunks off on portable); Q27 (delegates at P-3).

```text
- **Representation:** a function pointer value is always a native-callable entry point, so pointers handed to native code are correct by construction.
```
```text
- **Representation:** on the host-libc triples a function pointer value is always a native-callable entry point, so pointers handed to native code are correct by construction. On the portable triples, where no native code exists, it is the managed method's address (`ldftn`, read through the per-function address field below), and every indirect call is a managed `calli`; the thunks, the thunk table and the fast-path lookup below are off there (user, 2026-11-01). A .NET delegate over a C function, for thread starts and exit callbacks, comes with plan step P-3.
```

**DM-10. Section 4.7, the last bullet.** Driver: the portable triples exist; finding 1.4 (the libc's variadic stdio is
our own code).

```text
- A future portable libc would avoid all of this: everything variadic is our own code.
```
```text
- None of this applies on the portable triples: the C library is our own code, so every variadic call is a call between our own functions, with a `char*` `va_list`.
```

**DM-11. Section 5.1, the object shape.** Driver: loader finding 1.1; PC-9.

```text
  - **Shape.** A PE32+ envelope with no Assembly row,
```
```text
  - **Shape.** A PE32+ envelope (on the portable triples a PE32 envelope with machine I386, like their images, section 3) with no Assembly row,
```

**DM-12. Section 5.2, the native-library step.** Driver: the dotnet family (no native binding); T-P4 and N6.

```text
The one exception is the lock path of atomics: its `__atomic_*` functions come from the platform's atomic library (4.8).
```
```text
The one exception is the lock path of atomics: its `__atomic_*` functions come from the platform's atomic library (4.8). On the portable triples this step does not exist: a native library input is an error, and an undefined C library name is reported as one that the managed C library does not provide.
```

**DM-13. Section 5.2, `-l` order.** Driver: Q8; the user's shared-first `-l` "once shared toolchain libraries arrive";
the critic's scoping note.

```text
This static-first order holds until the toolchain ships shared library assemblies of its own, such as the portable libc's (section 6); then `-l` looks for the shared library first, and `-Bstatic` or `-static` picks the archive, as GNU ld does.
```
```text
This static-first order holds on the host-libc triples. On the portable triples, whose C library ships as a shared library assembly (section 6), `-l` looks for the managed `foo.dll` first, and `-Bstatic` or `-static` picks the archive, as GNU ld does.
```

**DM-14. Section 6, the portable libc bullet.** Driver: the 2026-10-26 row (in-tree LLVM libc, OS layer over managed
calls), the 2026-11-01 choices (freestanding only before MC-1, absent functions fail loudly, no interim OS layer,
`-static` refused until P-3), the three modes of 2026-10-31, Q17, and finding 1.4 (compile error through the headers,
link error through a program's own prototype).

```text
- **Portable .NET libc (`-unknown-dotnet`), a planned exploration after phase 3 (user goal, 2026-10-22: a platform-independent build):** a real libc compiled for our target, with its OS layer over the BCL, which keeps C library state in .NET's own process-wide objects wherever they exist, such as the environment in `System.Environment`. As with a native C toolchain, the user chooses for each link how the libc is linked: as a shared library assembly by default (5.4), or statically from an archive with `-static`. The user owns the combination: the toolchain does not police what a native toolchain allows, such as several assemblies of one process that each link it statically, whose copies then share one C library state (below). Candidates are wasi-libc with a managed WASI preview1 layer (a small, documented interface), musl with a managed syscall shim, or picolibc. Revisit only once the host-libc targets are solid.
```
```text
- **Portable .NET libc (`-unknown-dotnet`; user goal 2026-10-22, LLVM's libc chosen 2026-10-25, program order 2026-11-01):** LLVM's in-tree libc (`libc/`) compiled for the portable triples. Its OS layer is C/C++ inside the libc that calls `System.*` through the managed calls (section 1), and it keeps C library state in .NET's own process-wide objects wherever they exist, such as the environment in `System.Environment`. It arrives in stages: first only the freestanding entry points (string, ctype, math, the numeric conversions, `snprintf` into a buffer), then a single-threaded OS layer (stdio, `malloc`, `exit`, clocks, the environment), then threads and `-static`. Until a function's stage, it is absent: the installed headers declare only what ships, so a program that calls it fails to compile, or, with its own prototype, to link. There is no interim OS layer in ILAsm, C# or compiler intrinsics. As with a native C toolchain, the user chooses for each link how the libc is linked: as the shared library assembly `c.dll` by default (5.4), whose exports are a checked-in, append-only list, or statically from an archive with `-static` (refused until the shared state exists). The user owns the combination: the toolchain does not police what a native toolchain allows, such as several assemblies of one process that each link it statically, whose copies then share one C library state (below).
```

**DM-15. Section 6, the "No state shared" bullet (two substitutions).** Driver: the lock-path freeze rule (T11) and
finding 1.6 (contract 5.19.11 names the objects).

```text
(`Monitor` locks on interned names are the recommended mechanism, confirmed when the portable triple is designed)
```
```text
(`Monitor` locks on the interned names `<msil>v1:atomic.lock:<i>`, a protocol frozen under T11 at plan step P-3's commit)
```
and
```text
the portable triple's design picks those objects.
```
```text
plan contract 5.19 names those objects: a per-thread block in a named thread data slot, a process root published in AppDomain data and created under a `Monitor` lock on an interned name, and interned-name `Monitor`s for the libc's waits.
```

**DM-16. Section 11, the phase table's phase 3 row.** Driver: program order option A.

```text
Real programs run: zlib, Lua, SQLite. |
```
```text
Real programs run: zlib, Lua, SQLite. Inside phase 3's sequence but outside its numbering and exit gate: the portable toolchain `msil64-unknown-dotnet`, the managed C library in three stages and the first stage of managed calls (plan steps P-1a to P-3); `msil32-unknown-dotnet` follows phase 3 (P-4). |
```

**DM-17. Section 8, the triple, clang and driver bullets (two substitutions).** Driver: finding 1.2.

```text
- `llvm/lib/TargetParser/Triple.cpp`: the `msil` object format. `llvm/lib/MC/TargetRegistry.cpp`: select the MSIL target by object format.
```
```text
- `llvm/lib/TargetParser/Triple.cpp`: the `msil` object format, and the arches `msil32`/`msil64` with the OS `dotnet`, whose default object format is MSIL. `llvm/lib/MC/TargetRegistry.cpp`: select the MSIL target by object format; a non-MSIL arch with the OS `dotnet` is refused.
```
and
```text
- No MSIL ABI lowering in clang (`clang/lib/CodeGen/Targets/`): the native target's serves, and P/Invoke signatures come from the IR (2.3).
```
```text
- No MSIL ABI lowering in clang for the host-libc triples (`clang/lib/CodeGen/Targets/`): the native target's serves, and P/Invoke signatures come from the IR (2.3). The portable triples have their own: `MSILPortableTargetInfo` in `clang/lib/Basic/Targets/MSIL.h`, `MSILPortableABIInfo` in `clang/lib/CodeGen/Targets/MSIL.cpp`, and a `DotNetToolChain` beside `MSILToolChain` (a sysroot with the managed libc's headers and `c.dll`, `-lc` and `clang_rt.builtins` by default, `-static` refused until plan step P-3).
```

The orchestrator applies the approved edits in the post-M26 plan commit (L8), recorded by the row "Design.md edits
(user-approved)" of section 4.

---

## 4. Section 12 rows proposed for the post-M26 plan commit

Format: the plan's `| Date | Milestone | Change | Reason |`. The date `2026-11-01` is a placeholder for the commit
date. Rows with `P-1a` or `MC-1` in the milestone cell rely on row 6's `plan-check.py` change (for fix-loop rows it is
required), which rides in the same commit. Not in this commit: the decisions already recorded at HEAD (program order, portable triples, portable
target choices); P-1r's rows R1-R21, which land in their own plan commit after P-1b (draft-milestones section 5); and
the answer row of section 2, which the orchestrator writes from the user's replies (row 20 gives its shape).

```text
| 2026-11-01 | - | Contract 5.18 'The portable target (msil32/msil64-unknown-dotnet)' added (P-1a; msil32 at P-4): 5.18.1 triples and object format, 5.18.2 clang TargetInfo and ABI, 5.18.3 predefines, 5.18.4 driver and sysroot, 5.18.5 the dotnet link-rule family, 5.18.6 msil-ld, 5.18.7 the PE envelope and the width guard, 5.18.8 the helper archive, 5.18.9 what stays loud, 5.18.10 legs and lit features, 5.18.11 msil32, 5.18.12 which step implements what, 5.18.13 upstream files, 5.18.14 interfaces | P-0 (recon reports `recon/p3/portable-{target,linkrules,loader}.md`); user decisions of 2026-11-01; decision brief answers |
| 2026-11-01 | - | Contract 5.19 'The managed C library (LLVM libc on the portable target)' added (P-1b, completed by P-2 and P-3): 5.19.1 rules (absent until real: an absent function is an undeclared-function error through the installed headers and a link error with N6's line through the program's own prototype), 5.19.2 stages, 5.19.3 tree and build, 5.19.4 entry points per stage, 5.19.5 errno, 5.19.6 the portable C ABI, 5.19.7 `c.dll` and its append-only export list `msil/lib/libc/msil64-unknown-dotnet.exports`, 5.19.8 the three modes and `-static`, 5.19.9-5.19.10 the P-2 OS layer, 5.19.11 shared state (names, layouts, versioning; major 0 from P-2, major 1 frozen at P-3), 5.19.12 threads and `-static`, 5.19.13 Framework and modern .NET, 5.19.14 upstream changes, 5.19.15 tests, 5.19.16 the loud table, 5.19.17 interfaces | P-0 (`recon/p3/portable-{libc,state}.md`); user decisions of 2026-10-22, 2026-10-25, 2026-10-31 and 2026-11-01; decision brief answers |
| 2026-11-01 | - | Contract 5.20 'Managed calls from C and C++, stage 1 (MC-1)' added: 5.20.1 model, 5.20.2 attribute grammar, 5.20.3 Sema, 5.20.4 IR, 5.20.5 lowering (a non-call use of a managed declaration is an error on every triple), 5.20.6 output forms, 5.20.7 msil-ld, 5.20.8 exceptions, 5.20.9 handles, 5.20.10 strings, 5.20.11 triples and tests, 5.20.12 amendments, 5.20.13 the BCL block (15 rows), 5.20.14 deferred, 5.20.15 files and roles, 5.20.16 interfaces | P-0 (`recon/p3/portable-mc1.md`); user goal of 2026-10-22 and program order of 2026-11-01; decision brief answers |
| 2026-11-01 | - | Section 6 gains the milestone texts `### P-1a.` (the msil64-unknown-dotnet toolchain without a C library) and `### P-1b.` (freestanding llvm-libc as the shared assembly `c.dll`) after M26, a P-1r paragraph (an orchestration pass after P-1b that lands the M27-M35 re-pin and gating rows; no heading, so plan-check and the extractor never treat it as a milestone), `### MC-1.` (managed calls, stage 1) after M27, and outlines of P-2 (after MC-1) and P-3 (after M30) whose milestone texts are drafted later; Phase 3's exit gate, numbering and M27-M35 gates are unchanged | program order (user, 2026-11-01, option A); P-0 drafts |
| 2026-11-01 | - | 5.13.5 exemptions (each with its hooks-manifest block, L6): P-1a: `llvm/include/llvm/TargetParser/Triple.h`, `TripleName.def`, `llvm/lib/TargetParser/Triple.cpp`, `TargetDataLayout.cpp`, `llvm/lib/MC/TargetRegistry.cpp`, `llvm/include/llvm/IR/RuntimeLibcalls.td`, `llvm/lib/Transforms/IPO/ExpandVariadics.cpp` (a portable case inside M26's block), `clang/lib/Basic/Targets.cpp`, `clang/lib/CodeGen/CodeGenModule.cpp`, `clang/lib/CodeGen/TargetInfo.h`, `clang/lib/CodeGen/CMakeLists.txt` (with `fork-only` for the new `clang/lib/CodeGen/Targets/MSIL.cpp`), `clang/include/clang/Basic/DiagnosticDriverKinds.td`, `compiler-rt/lib/builtins/int_lib.h` (`none` plus `fork msil/test/Portable/00-freestanding/helpers.test`), and the existing hook blocks of `llvm/unittests/TargetParser/TripleTest.cpp` and `llvm/unittests/MC/TargetRegistry.cpp`; P-1b: one `fork-only` line per new `libc/` file and, per changed upstream libc file, a hook block with `none llvm-libc's tests are not built by the fork` plus a `fork msil/test/Portable/01-libc/<test>.test` line; MC-1: `clang/include/clang/Basic/Attr.td`, `AttrDocs.td`, `DiagnosticSemaKinds.td`, `clang/lib/Sema/SemaDeclAttr.cpp`, `SemaDecl.cpp`, `clang/lib/Sema/CMakeLists.txt`, `CodeGenModule.cpp`. The closing paragraph of 5.13.5 ('compiler-rt is not edited in Phase 3'; 'No Phase 3 milestone edits `Attr.td`, ... `RuntimeLibcalls.td` ...') and the matching sentence of section 11's Phase 3 summary name these exceptions | the portable target and MC-1 need these upstream edits (P-0 findings: `int_lib.h`'s one blocker, the Triple recursion, RuntimeLibcalls' MSIL system library, the attribute records) |
| 2026-11-01 | - | Milestone ids P-<n>[a-z] and MC-<n>: 5.13.1-5.13.7 and 5.17 apply to them as to M24-M35; role names are `<id>/<role>`; a Depends-on line names WPs of its own milestone only. `msil/utils/plan-check.py` accepts `(M\d+|P-\d+[a-z]?|MC-\d+)` in MILESTONE_RE, WP_RE, AMENDMENT_RE's milestone cell and main()'s milestone argument; its malformed-heading guards become `^### (M\d|P-\d|MC-\d)` and `^#### (M|P-|MC-)` (section 7's `### P2-A.`/`### P3-A.` headings stay ignored), with self-test cases for a P-1a milestone, an MC-1 fix-loop row, `### P2-A.` and `scope P-1a`; this commit carries the change | plan-check rejects P-/MC- ids today (a P-1a fix-loop row would make every command exit 2) |
| 2026-11-01 | P-1a | R21 bend (user-approved): P-1a's root WPs P-1a.a and P-1a.b land the `Triple.h`, `TripleName.def`, `RuntimeLibcalls.td`, `DiagnosticDriverKinds.td` and `clang/lib/CodeGen/TargetInfo.h` edits mid-phase, isolated so the gate pays one rebuild (2,481 objects, about 159 job-minutes), never while a post-commit run is active | user decision (brief Q10); P-0 rebuild measurement from `.ninja_deps` |
| 2026-11-01 | - | Design.md edits (user-approved, brief section 3): goal 6; the non-goals' managed-calls sentence; section 3's triples paragraph, table rows, a portable-triples bullet and the pointer-size bullet; 4.3's heap sentence (per brief Q29); 4.4; 4.5's representation bullet; 4.7's last bullet; 5.1's shape; 5.2's native-library step and `-l` order; section 6's portable libc bullet and the 'No state shared' bullet; the phase table's phase 3 row; section 8's triple and ABI-lowering bullets | user approval (brief Q12) |
| 2026-11-01 | - | PE32 for the portable triples: 5.7.7 (the `PEOptions` comment 'Error until phase 5 (PE32, the mscoree stub, .reloc)' gains the measured reason that the portable triples write a stubless PE32/I386 ILONLY envelope; 'PE32+, with the classic 128-byte DOS header' names the exception), 5.7.10 (the canonical `Format: PE32+ machine=0x8664` line and 'llvm-cil-copy reads an IL-only PE32+ image'), 5.8.2 O1 ('PE32+ image. COFF Machine from the triple') and section 10 Q1/B2 ('netmodule (PE32+ envelope') each name the portable PE32 envelope; 5.18.7's rule: never `IMAGE_FILE_RELOCS_STRIPPED` on a DLL, never `IMAGE_FILE_32BIT_MACHINE`, never 32BITREQUIRED/32BITPREFERRED on msil64 | P-0 loader measurement: a stubless PE32/I386 ILONLY image runs as a 64-bit process on .NET 8, .NET 10, Framework 4.8 (direct start and LoadFrom) and WSL .NET 8/10 |
| 2026-11-01 | P-1a | 5.11.7 amended: `check-mscorlib-safe.py` proves each member public as well as present (a `call` or `newobj` probe, or reflection's `IsPublic` on Framework), with a self-test case for an internal member; P-1a's harness WP P-1a.i makes the change before P-1a's guard rows are proved, and P-1r's audit re-runs the fixed tool over every mscorlib-safe row planned for M27-M35 | P-0 (recon mc1 3.5): the `ldtoken` probe skips the access check, so Framework 4.8.1's internal `Environment::get_TickCount64()` passes it while a real call throws `MethodAccessException` |
| 2026-11-01 | - | 5.17.4 allowance: portable tests before P-2 have no output channel, so their transcripts are the status line of self-checking programs, hand-written under `%{msil_expected_rule}` with the reason 'portable: no stdout before P-2; the program checks itself'; `msil/test/README.md` lists every use | P-1a, P-1b and MC-1's portable tests run before the managed libc has stdout |
| 2026-11-01 | - | 5.17.6 legs `portable-smoke` (Windows: .NET 10, TC=0, a Framework 4.8 direct start, `msil_require_x86=1` for the guard test) and `portable-smoke-wsl` (WSL .NET 10, TC=0): the user's portable smoke leg as a Windows/WSL pair split by name, as `gc-churn`/`gc-churn-wsl`; defined by P-1a, extended by P-1b and MC-1; tag `Portable`, never `Exit`, run by `lit-legs.ps1 -Portable` and `lit-legs.sh --portable`. 5.17.11's post-commit invocation becomes `run-m1-gate.ps1 -SkipConfigure -SkipBuild -Exit -Portable -CorpusCache <cache>`; M35's gate keeps `-Exit` only, so M35's phase gate (default, `Wsl` and `Exit` legs), `exit-report.py` ('', `Wsl`, `Exit`) and `check-matrix.py` never select the pair. `msil/test/Portable/lit.local.cfg` marks its tests UNSUPPORTED unless the feature `msil-triple-portable` is set, net-core filters out `Portable/`, and `gate-select.py`'s SKIPPED_DIRS gains `Portable`. The portable archive and `c.dll` are built only by the legs' stamped setup and by the P-steps' explicit gate steps, never by `ci/build.ps1`/`build.sh` or the default `build-builtins.py` set, so no M27-M35 gate step changes. A P-step's own gate runs its own portable directory with `msil-lit.ps1` (per brief Q11); a post-commit failure becomes a fix WP of the next milestone | user decision (2026-11-01: portable smoke leg in post-commit runs only; M27-M35 gates as written); review finding: an `Exit` tag would put the leg into M35's gate |
| 2026-11-01 | - | 5.13.3 gains rows for the new files (`P-1a/target` `llvm/lib/Target/MSIL/MSILPortable.{h,cpp}`; `P-1a/cli` `llvm/lib/CLI/TrailerGuard.cpp`; `P-1a/clang` `clang/lib/CodeGen/Targets/MSIL.cpp`; `P-1b/build` `msil/utils/build/build-libc.py` and `msil/lib/libc/`; `MC-1/target` `llvm/lib/Target/MSIL/MSILManagedCalls.{h,cpp}`; `MC-1/lld` `lld/MSIL/ManagedCalls.{h,cpp}`; `MC-1/lists` `llvm/include/llvm/CLI/ManagedSignature.h`; `MC-1/clang` `clang/lib/Sema/SemaMSILManagedCall.{h,cpp}`); 5.13.4 gains P-1a, P-1b and MC-1 rows from their Roles paragraphs (hot files: P-1a/target, P-1a/lists, P-1a/cli, P-1a/lld, P-1a/builtins; MC-1/lists, MC-1/target); L7's milestone list gains P-1a, P-1b and MC-1 | L2-L4 and L7 rely on these tables |
| 2026-11-01 | - | 5.13.1 L2: a behaviour-preserving substitution of a `SectionRules` comparison, selection or default by a named predicate of `llvm/include/llvm/CLI/ImageLayout.h` (`cli::getSectionRules`, `cli::bindsNatively` and the link-rule predicates) is a third permitted non-hook edit kind in a hot file, proved by `il-snapshot.py --compare` and `det-manifest.py compare` in the milestone's gate; P-1a's gate also fails on any `SectionRules::` enumerator outside `ImageLayout.cpp` other than a `case` label | P-0: 13 `==` comparisons and 11 enumerator uses would give a third rule set COFF rules at some sites and ELF rules at others |
| 2026-11-01 | - | 5.13.7 B2: a block heading may name a portable or managed-calls step instead of `M<n>` (`// P-1a: <purpose> (plan 5.18)`, `// MC-1: managed calls from C and C++, stage 1 (plan 5.20)`); B7 gains P-1a's guard rows (5.18.7: `PlatformNotSupportedException` and its `.ctor(string)`, `BitConverter` and `GetBytes(int32)`) and MC-1's fifteen rows (5.20.13) | the guard and MC-1 append mscorlib-safe BCL rows |
| 2026-11-01 | MC-1 | 5.14.1 and 5.15.1 rule 4: a fifth delivery kind, K5 managed-call stubs (internal methods `__msil_mc.<symbol>` that P0 makes from attributed declarations, with AsmPrinter-written bodies that access one .NET member named by IR attributes); 5.14.1's last sentence becomes 'no IR declaration names a BCL member, except a managed-call declaration (K5); other code that needs a .NET API calls a K2 definition'; 5.14.2's N3 row adds `__msil_mc.*` (never a SYMTAB symbol, never bound by late binding); 5.13.6's P0 list gains `MSIL::createManagedCallStubs` between M24's and M33's calls (no slot added, `pipeline.ll` unchanged) | contract 5.20.4-5.20.5 |
| 2026-11-01 | - | 5.14.2: the class line N6 (an undefined C library name on `*-unknown-dotnet`: `>>> a C library function that the C library c.dll (msil64-unknown-dotnet) does not provide` when an input named `c.dll` takes part, else `>>> a C library function; link the managed C library c.dll (-lc)`) and N2's portable line (generic `__atomic_*` names undefined on dotnet until P-3); 5.14.4 T9's portable carve-out names 5.19.11 (the C library's state, found by the libc's own code; the toolchain's private state stays per output) | contracts 5.18.6 and 5.19.11 |
| 2026-11-01 | - | 5.15.11 gains the row 'Managed calls (MC-1)' (until MC-1, `msil_method` is an unknown attribute and the call an undefined-symbol link error; from MC-1, 5.20's rules, and taking the address is 5.20.5's error) and 5.15.12 a row for MC-1 covering 5.20; their portable column is P-1r's row R12 | contract 5.20.12 items 6-7 |
| 2026-11-01 | M32 | 5.15.10 and M32.a: the type row `CLI_BCL_TYPE(String, "System", "String", true)` exists from MC-1's block (5.20.13); M32.a drops it and appends only `String_Concat`, on MC-1's `String` type; M32's `--print-bcl-table` and B7 pins follow | a `CLI_BCL_TYPE` Id is an enumerator in `BCLMembers.h`, so a second `String` row would fail M32's gate build (dependency review) |
| 2026-11-01 | - | 5.17.7 G1b's milestone list gains P-1a (it changes `msil/lib/builtins/sources.txt`); 5.17.13's directory list gains `msil/test/M3/13-managed-calls` (MC-1, run by `p3-full` and `wsl-p3`) and `msil/test/Portable/{00-freestanding,01-libc,02-managed-calls,03-sjlj,90-loud}` (run only by the `Portable` legs and the P-steps' own gate steps); 5.17.4's hand-written list gains MC-1's managed-only results | P-1a's archive globs touch the host triples; new test directories |
| 2026-11-01 | - | P-0 recon copies: the six reports, each with its verifier section folded in, go to `llvm/docs/MSIL/recon/p3/portable-{target,linkrules,loader,libc,state,mc1}.md`, and the artifacts the milestone texts read to `llvm/docs/MSIL/recon/p3/portable-<key>-files/` (`portable-loader-files/guardB.il`, `dump.txt`, `wide.c`; `portable-libc-files/p1bset.py`, `overlay3/`, `run/{prog,fmt,fpid}.c`; `portable-mc1-files/mc1proto.il`, `bx.il`, `utf.il`, `enc.txt`, `probe-1.{il,out}`, `sec4.txt`), scrubbed of machine paths; every Reads line of P-1a, P-1b and MC-1 cites these copies | section 9's 'Lost inputs' rule: what a WP reads is in the repository |
| 2026-11-01 | - | FeatureIdeas.md item 3 (typed and delegate-accepting export overloads, 'scheduled with the managed-calls goal') notes that MC-1 does not include them and that they stay with the fuller managed calls (orchestrator edit, L8) | contract 5.20.12 item 9 |
| 2026-11-01 | - | Decision brief answers (user, <date>): <one clause per answered question Q1-Q35, for example 'Q1 (a): the width and byte-order guard throws PlatformNotSupportedException from <Module>::.cctor'; 'Q16 (a): errno over one exported cell until P-3, a documented exception to fail-loudly'; 'Q29 (a): the heap is a two-pointer header over Marshal.AllocHGlobal on every runtime, revising the 2026-10-31 mapping'> | user decisions |
```

---

## 5. Risks and estimates per milestone

Farm estimates are wall-clock hours including gate and review rounds. They are calibrated on M22 (10.2 h for 7,100
planned lines), M24 (20.8 h for 9,300) and M25 (14.4 h for 3,560). M24 and M22 ran to 2-3 times their planned line
counts. Each commit is followed by a 3-6 h post-commit run, which overlaps the next milestone's WP phase.

| Step | Estimate | Main risks |
|---|---|---|
| **P-1a** | 17-22 h (20-26 h if P-1a.i stays behind P-1a.h); one or two broad rebuilds of 2,481 objects (about 40-45 min at 4 jobs quiet, about 80 min at the 2-process cap); gate about 43-52 normalized minutes | Silent ELF or Linux fallthrough at an unconverted `isOSWindows()` or `SectionRules` site (mitigated by the predicates, the enumerator grep and the il-snapshot and det-manifest comparisons). Host output drift from the PE writer's per-format rewrite. The Triple recursion and RuntimeLibcalls gaps (crashes or silent expansions in legalization if missed). Size: about 30-40 WP hours; a split into P-1a and P-1a2 (the msil-ld family and its tests) is possible at the cost of a second post-commit run. The ABI pair (clang's `EmitVAArg` and ExpandVariadics' `MSILPortable` class) must agree slot for slot. Fifteen new upstream conflict surfaces. |
| **P-1b** | 11-16 h; llvm-libc build about 8-12 min at 4 jobs; no LLVM, clang or lld recompile; gate about 23-27 normalized minutes | CMake's compiler identification on msil64 CIL objects (unmeasured; checked first). The probe used the SysV host ABI with host macros removed, so the real portable ABI may change `div`/`ldiv`/`imaxdiv`/`localeconv` shapes (P-1b re-runs the probe's programs). The C ABI freezes at P-1b's commit: errno numbers, `struct tm`, `jmp_buf` (200 bytes, MSJ1 prefix), `mbstate_t`, `struct lconv` (Q15-Q19). About 12 small upstream libc conflict surfaces, re-checked at every upstream merge. `build-libc.py` is sized at about 1,000 lines (its model `build-builtins.py` is about 2,050). The errno race under a multi-threaded host until P-3 (Q16). |
| **P-1r** | 1-2 h as a read-only orchestration workflow (3-5 h if run as a farm milestone, whose gate could not check plan rows) | The as-built names after P-1a may differ from the rows' wording (the audit gives final wording). Planned BCL type collisions beyond `BitConverter` and `String` (the audit searches every planned block). The stricter `check-mscorlib-safe.py` may reject a planned M27-M35 row (each failure becomes a row before that milestone launches). |
| **MC-1** | 16-22 h (the recon said 22-32 h plus a 5 h post-commit run before the drafts narrowed it); a new `Attr.td` record rebuilds about 520 clang objects (3,500-4,000 CPU-seconds) | The attribute grammar and handle contract freeze early, because the libc is built on them. Framework's JIT accepts wrong scalars for struct parameters (every enum pairing is link-checked; `-S` text stays unchecked). Handle leaks are silent. `Attr.td` and `SemaDeclAttr.cpp` become permanent merge surfaces. `13-managed-calls` adds about 1-1.5 minutes to G3b and G4 of every later gate. |
| **P-2** | 16-22 h (reorder recommendation; not re-estimated by P-0, and the critic calls it optimistic: the generic `File` class needs a new `File/dotnet` platform half) | MC-1 must cover every BCL member P-2's root needs (`String.Intern`, `AppDomain.GetData`/`SetData`, boxed `IntPtr`, `object[]`, `Monitor.Enter(object)`); 5.20 lists them as within MC-1 under Q25 (a). Q30 (how `c.dll` binds `System.*`) must be answered. `TargetLibraryInfo` treats OS dotnet as having every function and must be re-audited when stdio arrives (the `printf` to `puts` fold). The P-2 re-pin rows (M29.a/b/d cite P-2's exit field and `__dso_handle`). A public release between P-2 and P-3 would ship a pre-freeze layout (the version rules make a mix fail loudly). |
| **P-3** | 30-45 h for two milestones (critic's figure) | The frozen protocols: lock names (T11), the root, the C block and the heap scheme become an append-only ABI between libc releases. Every `LIBC_THREAD_LOCAL` site must move into the C block. The finalizable holder `'<msil>CBlock'` is new trailer machinery. `Thread.Interrupt` and Framework thread aborts can orphan interned locks. Per-AppDomain scope on Framework. Isolation between load contexts is lost (accepted by the user). |
| **P-4** | Not estimated | The most invasive change (about 100 I64 sites in the backend, the D2 mirror, 4-byte trailer and link-info pointers, an x86 leg). msil32's layout, ABI column and predefines freeze at P-4's commit. |

**Schedule.** P-1a through P-3 add about 91-129 farm hours before M35, about 110-165 hours with their post-commit runs.
The critic's pessimistic bound is 160-220 hours. In exchange, the freestanding libc runs 2 milestones after M26 (P-1a,
P-1b), and the first portable `printf` runs after P-2, 6 steps after M26. The original order put both behind M35 plus
the managed-calls goal, 12 or more milestones out.

**Cross-cutting risks.**
- A portable break is found one milestone late, because the legs run only after a commit. The fix becomes a fix WP of
  the next milestone.
- Unmeasured hosts: big-endian .NET, Windows on ARM64 without Framework 4.8.1, 32-bit ARM and browser .NET, and
  Windows versions older than 11 for the stubless Framework start (Roslyn's stubless x64 images have always run there,
  so the risk is small).
- Determinism of the "one image" claim across the Windows and WSL legs rests on reproducible builds; a byte comparison
  between the two legs' images would need a harness feature that P-1a does not include.

---

## 6. What changed versus the reorder recommendation

1. **Envelope.** The recommendation's risk that Roslyn's mscoree stub and `.reloc` are needed is retired: a stubless
   PE32/I386 ILONLY image runs everywhere measured, so P-1a's PE writer change is small. The stack reserve stays 4 MB.
2. **Guard.** It sits in `<Module>::.cctor`, not in the trailer's `Main`, and throws `PlatformNotSupportedException`
   instead of failing fast. Byte order comes from `BitConverter.GetBytes(1)[0]`. The recommendation's BCL rows
   (`IntPtr.get_Size`, `BitConverter.IsLittleEndian`, `Environment.FailFast` moved earlier from M28) are not needed.
3. **Link family.** The recommendation proposed ELF-style rules with `__ELF__`; the user chose a dotnet family without
   `__ELF__`. P-0 confirmed the reason (llvm-libc's `__ELF__` selects baremetal hooks), designed the family, and found
   compiler-rt's one blocker. New upstream edits the recommendation did not list: `Triple::getDefaultFormat`, the
   `RuntimeLibcalls.td` MSIL system library, and the refusal of a non-MSIL arch with OS `dotnet`. Every two-way
   `isOSWindows()` rule site (25 by the linkrules recon's count) gets an explicit dotnet answer.
4. **libc evidence.** The "726/815 ran" claim is withdrawn (the critic's MUST-FIX). Measured instead: 546 of 561
   candidate and 835 of 894 sources compile, stdio's cores compile, a `c.dll` links and runs with 0 P/Invokes on three
   runtimes, and a full-scale `c.dll` links.
5. **P-1b's content.**
   - **Absent, not trapping.** The `__builtin_trap` stubs are replaced by absent functions: compile errors through the
     headers, N6 link errors otherwise.
   - **errno** is EXTERNAL with an exported provider, which settles the critic's EXTERNAL/SHARED inconsistency.
   - **Exports** come from a forced `--export-list`, because every llvm-libc name is hidden.
   - **Keys** are `__dotnet__` and `__msil64__`, not `__MSIL__`.
   - **Size.** About 510 entry points, not 800-850. `snprintf` ships at P-1b. `sscanf`/`asprintf` wait for the heap,
     `rand` for M30's atomics (at P-3), and the localtime family and fenv for P-2 or later.
   - **`libc.a`** is not installed before P-3, and msil-ld refuses a static take of the build-tree archive (T-P11).
6. **Destructors.** The recommendation kept destructors as M29's loud error and had M29 bind `c.dll`'s `__cxa_atexit`
   on portable. Now P-2 lowers `llvm.global_dtors` with upstream's `LowerGlobalDtorsPass` and defines `__dso_handle`.
   M29's exit table, runner and Framework fallback stay off on dotnet, so the generalized ExitTable lookup is dropped.
7. **MC-1.**
   - **Exceptions** need no new clause kind (a per-stub filter through M27's `emitExceptionClause`), which answers the
     critic's ownership worry.
   - **Binding.** The binder already binds arbitrary members with `--reference`.
   - **Strings** decode through throwing encoders, not `PtrToStringAnsi` (the critic's MUST-FIX) and not `Encoding.UTF8`.
   - **Address-of** a managed declaration is an error. A 15th BCL row, `DecoderFallbackException`, was added.
   - **Plan amendments.** MC-1 needs a delivery-kind amendment (K5) and Design.md's non-goal edit.
   - **Estimate:** 16-22 h, against 20-30 h.
8. **`check-mscorlib-safe.py` defect.** New: the tool passes internal members. P-1a fixes it.
9. **The portable leg.** "Tagged non-Exit" becomes a concrete `Portable` tag on a Windows/WSL pair of legs, kept out
   of M35's gate, the exit report and the CI build. Whether P-steps' own gates run their own directories is a new user
   question (Q11).
10. **P-1r** is an orchestration workflow, not a 3-5 h farm milestone, and its rows grew from about 7 to 21:
    - new rows: the dotnet condition for M27's mask save, `ThunkKind::Managed` for M28, the `BitConverter` collision
      with M33, M31's TLS on dotnet, R20's predicate rule for M28-M31, and the `mscorlib-safe.test` re-pin ownership;
    - its loud-failure tests moved into P-1b (P-1b.h).
11. **P-2 and P-3 boundaries.**
    - P-2 has no delegates, so the `ProcessExit` subscription moves to P-3 (Q28 covers non-C hosts until then).
    - The per-thread C block and its finalizable holder move from P-2 to P-3, as the user's order puts per-thread state
      there.
    - P-3 also carries the `LIBC_THREAD_LOCAL` relocation, `rand`/`srand`, and `FileStream.Seek` instead of
      `RandomAccess` (absent on Framework).
12. **The critic's alternative OS layers** (ILAsm, a C# netmodule, compiler intrinsics) are not offered: the user ruled
    out any interim OS layer on 2026-11-01.
13. **Tooling.** New: `plan-check.py`, the extractor and the workflow must accept P- and MC- ids (PC-6). The milestone
    texts cite the repository copies of the recon reports, not git-ignored notes or scratch files.
14. **Estimates.** P-1a is 17-22 h (recommendation 16-22 h) plus a measured 159 job-minute rebuild. P-1b is 11-16 h
    (10-16 h). P-1r is 1-2 h (3-5 h). MC-1 is 16-22 h (20-30 h). The total delay to M35 is closer to the critic's
    figure than to the recommendation's 90-130 h once post-commit runs are counted.
15. **Unchanged:** the program order, msil32 as P-4 after M35, managed function pointers with M28's thunks off on
    portable, `-static` refused until P-3, the in-tree llvm-libc with its OS layer over managed calls, MC-1 after M27,
    and threads after M28's generic pieces and M30's inline atomics.

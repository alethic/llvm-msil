# Phase 3 recon: tests, corpus and gates (`tests-gates-p3`)

Status: recon report for Phase 3 planning, 2026-10-28, at `cc4c79787d6a` (M23, the Phase 2 exit) on `msil`.
Written in two sessions: the first was stopped before it returned; the second reviewed it, re-ran the three
measurements the stop interrupted (E12-E14: the WSL composite, the asm composite, the WSL configuration), labelled
E11 as an upper bound, added appendix A (the 70 excluded c-testsuite tests one by one) and folded in the tests the
sibling feature recons propose (4.3, 4.6, 4.8).
Scope: today's harness and gates; the measured cost structure of the gates; a standard milestone gate for
Phase 3 that fits Q32's 20-30 minutes on a quiet host; the generalized tool-timeout rule; the test plan of the
Phase 3 features (what flips, c-testsuite's excluded tests, the `msil/test/M3` corpus, threads, hosted-libc
programs); Linux-triple coverage and CI; concrete lit parameters, substitutions, legs, gate steps per
milestone and the Phase 3 exit gate.

Experiments ran in `C:\llvm-msil-scratch\p3\tests-gates-p3\`; the scripts, sources and trimmed results that back
the claims are in [tests-gates-p3-files/](tests-gates-p3-files/). Nothing tracked was modified; nothing was
installed. **Measurement caveat:** the host was at 100% CPU load from other work during every experiment
(`Win32_Processor.LoadPercentage` = 100, other recon agents and other projects), so wall times below are loaded
times; CPU times (user + kernel of a whole process tree, read from a Windows job object) and process counts are
the load-independent numbers this report reasons with.

---

## 0. Summary

1. **Where the gate time goes (measured).** M23's standard step G3 (`lit-legs.ps1 -Leg net8`) took 60.5, 86.9,
   51.6 and 43.9 minutes in the four phase-gate runs (`C:\llvm-msil-scratch\milestones\M23\gate-1..4\16.log`).
   Gate 4's xunit file (`build/lit-results/net8.xml`, 2026-10-28 07:10-07:54) has 856 test entries (169
   Programs excluded, 670 passed, 17 unsupported) and 16,749 test-seconds in 2,629 s of wall (6.4 workers busy):
   `msil/test/M1` 41.9%, `msil/test/M2` 28.9%, `msil/test/harness` 20.9%, every FileCheck directory together
   8.3%, `M2/00-link/spill-limits.test` alone 637 s (3.8%). One net8 composite (`%{msil_c}` at -O0 and -O2)
   starts **about 44 Windows processes per level, 88 per test** (15 commands that lit starts per level, each a
   Python wrapper around one or more tools), not 8, and costs 13 CPU-s per test; the lit configuration costs
   4.2 CPU-s, 11 processes and 13-17 s of loaded wall per lit invocation, and is evaluated **once** per
   invocation (audit-hook measurement; section 2.5), but `msil/test/harness` runs **132 nested lit
   invocations** that each pay it again.
2. **What each check costs (CPU per test, 4-test sample, section 2.4).** obj on .NET 8 only: 4.75; + Framework run:
   5.4 (`lean`); + native oracle: +1.6; + O3/O6 equivalence: +2.05 per single-TU test (0 for multi-TU tests
   under obj); + `msil_also_run=10,8+tc0`: +2.26; all of them (today's net8): 10.8 CPU-s and 77 processes per
   test, **2.0 times the lean composite**. `msil_also_run` and the native oracle are 36% of a net8 composite and
   duplicate checks that other legs make (`expected` checks every `.expected` against the native build; the
   phase gate's net8 keeps `msil_also_run`).
3. **WSL legs are launch-bound.** A `wsl-net10` composite level launches `wsl.exe` 7 times (jitall, run, two
   `msil_also_run` entries of jitall + run, the native run; counted in lit's `-a` log, E12); 385 tests make about
   5,400 launches. Measured `wsl.exe -d Ubuntu --exec true`: 0.97 s per launch alone, 0.28 s per launch (3.6/s)
   at concurrency 8, latency 1.86 s; so launches alone set a floor of about 25 minutes for today's `wsl-net10`
   (it took 36-109 minutes). A lean WSL composite (no oracle, no equivalence, no `msil_also_run`: 2 launches per
   level) costs 33% of the full one's Windows CPU and took 13.6 s against 37.4 s back to back (E12).
4. **Proposed standard milestone gate (section 3).** G3 becomes `net8-core`: the FileCheck directories plus
   `msil/test/M1` and `M2` through objects on .NET 8 and Framework 4.8, with O3/O6 on single-TU tests, without
   the native oracle and without `msil_also_run`, without `spill-limits` and without the harness (the harness
   runs when the milestone changes harness files); plus `p3-full`: `msil/test/M3` (the Phase 3 feature tests)
   with every parameter of today's net8; plus `wsl-p3`; plus `expected --check` (both OSes, both levels) on the
   test directories the milestone touched. Projected from the measured ratios: about 50% of today's net8
   test-time for `net8-core`, so the standard gate is about 19-25 minutes on a quiet host (M23.d's quiet
   composite: 19.5 s) and about 55-75 minutes under the loads of M23's runs. Because the host is shared, record
   a **load factor** with every gate (one calibration composite at the start, quiet reference 19.5 s) and judge
   the budget on load-normalized minutes. Everything that leaves the standard step runs in a **post-commit run**
   after every Phase 3 milestone (net8 with `msil_also_run`, the harness, `spill-limits`, `wsl-net10`, the
   corpus legs), whose failures become fix WPs of the next milestone, as 5.12.10 does.
5. **Tool timeouts (section 3.5).** Carry M23 step 16's re-run rule into every gate step that runs lit, and
   automate it in `lit-legs.ps1`/`lit-legs.sh`: after a leg, re-run alone (`-j1`, the leg's parameters) every
   test whose only failures are `<tool> did not finish within <T> s` / `[timeout]`, at most three per leg; the row
   reads `PASS (retried after timeout: <tests>)`; a second timeout or any other failure fails the leg. Add a
   per-test `%{msil_limits}` placeholder for large programs (SQLite) instead of raising `msil_timeout` for all.
6. **What flips in Phase 3 (section 4.1).** The seven P3 rows of `matrix.txt` (`p5-tls`, `p6-dtors`,
   `fini-array`, `crt-xt`, `atexit-order`, `native-callback`, `variadic-def-xtu`) become transcripts; 44 tests
   pin a Phase 3 diagnostic today (26 in `llvm/test/CodeGen/MSIL`, 9 in `lld/test/MSIL`, the seven P3
   tests of `08-semantics` and two `09-hosted` tests); c-testsuite's `00140` becomes a plain entry. Two native oracles
   cannot give the P3 transcripts yet: the E7 `start.c` runs no `.fini_array`, and the E7 sysroot has no
   `atexit`. **Measured fix: an `E7h` hosted oracle** (the glibc sysroot's `crt1.o`, `crti.o`, `crtn.o` and
   `-lc` = `libc.so.6` + `libc_nonshared.a`, `-nostartfiles -nodefaultlibs`, `ld.lld`) links and runs
   `atexit` + destructor programs and pthreads in WSL with glibc's order.
7. **c-testsuite's 70 excluded tests (section 4.2).** By the list's rule: 68 call `printf` (62 only `printf`),
   2 call other C library functions (00025 `strlen`, 00040 `calloc`); 4 have an empty `.c.expected` (00025,
   00040, 00208, 00218). IR scan of all 220 for the Linux triple: native variadic calls in 69, variadic
   definitions in 2 (00140, 00204), dynamic alloca in 1 (00207, -O0 only); no TLS, atomics, setjmp, complex or
   f16. With the variadics recon's prototype run (its section 8.4) these need a **second list** with its own rule
   and count, comparing with `<name>.c.expected` (or with the native transcript where `.c.expected` assumes LP64:
   00212, 00216, 00218 on Windows), with named per-triple exclusions (00204's `long double` on Linux, 00214 on
   Windows) instead of XFAIL, and a `ctest-wsl` leg.
8. **Threads can be tested deterministically (section 4.6, measured).** Shapes whose output is independent of
   the interleaving: N threads x M atomic increments (exact total), per-thread TLS results joined and printed by
   main, a seq_cst ping-pong; all exact on native Windows/Linux, Framework 4.8 and .NET 8/10 (Windows and WSL).
   The same shapes are sensitive: a non-atomic increment lost 0.7-2.4 M of 4 M updates on every runtime and
   lost updates in 15 of 20 runs even at 2 x 10,000. A TLS address-distinctness check **without a barrier is
   flaky** (1 of 20 runs and the first -O0 run failed, because an exited thread's TLS block is reused); with a
   barrier 0 of 80+ failed.
9. **Linux-triple coverage and CI (section 5).** Add `ctest-wsl` (obj, WSL .NET 10, E7, `msil_also_run=8`) and
   widen `lto-wsl` by `01-archive`, `03-native`, `04-shared`, `05-refs`, `07-equiv` (about +85% of lto-wsl's
   time, +5-8 quiet minutes) in the phase gate; do not wait for CI, whose first dispatch is a user action (Q38)
   and whose corpus licences are open (item 11). Teach `check-matrix.py` the legs (`lit-legs.ps1 -Describe`,
   `lit-legs.sh --describe`), one FileCheck per level, and one row list.

Decisions D1-D14 are in section 7. Four need the user: the gate budget (D1), how to judge it on a shared host
(D2, decided with D1), CI licences (D11) and the CI first run (D12). Appendix A lists the 70 excluded c-testsuite
tests one by one.

---

## 1. Today's harness and gates (inventory at `cc4c79787d6a`)

### 1.1 `msil/test/lit.cfg.py` (2,952 lines)

- Parameters (all read through `params.get`, `bool_param` or `number_param`): `msil_also_run`,
  `msil_cil_dump`, `msil_corpus_dir`, `msil_corpus_skip`, `msil_dotnet_env`, `msil_dotnet_runtime`,
  `msil_equiv`, `msil_glibc_sysroot`, `msil_ilasm`, `msil_jitall`, `msil_link_output`, `msil_linux_sysroot`,
  `msil_mdcanon`, `msil_mem_limit_mb`, `msil_native_oracle`, `msil_native_rtlib`, `msil_opt_levels`,
  `msil_pipeline`, `msil_references`, `msil_require_corpus`, `msil_require_csc`, `msil_require_exec`,
  `msil_require_link`, `msil_require_verify`, `msil_roundtrip`, `msil_run_netfx`, `msil_runner`,
  `msil_scale_tests`, `msil_timeout` (line 296, default 60 s), `msil_triple`, `msil_wsl_distro`,
  `msil_wsl_dotnet`, `msil_wsl_ilasm`.
- Features: `msil-also-run`, `msil-cil-dump`, `msil-corpus` (and `msil-corpus-<name>`), `msil-csc`,
  `msil-dotnet`, `msil-equiv`, `msil-exec`, `msil-glibc-sysroot`, `msil-ilasm`, `msil-ildasm`, `msil-ilverify`,
  `msil-jitall`, `msil-ld-lld`, `msil-linux-oracle`, `msil-linux-sysroot`, `msil-lld-link`, `msil-mdcanon`,
  `msil-native-oracle`, `msil-native-rtlib`, `msil-netfx`, `msil-peverify`, `msil-roundtrip`, `msil-run-netfx`,
  `msil-scale-tests`, `msil-triple-linux`, `msil-triple-windows`, `msil-ucrt-headers`, `msil-wsl`,
  `msil-wsl-ilasm`, plus `msil-pipeline=<p>`, `msil-prog-pipeline=<p>`, `msil-link-output=<x>`, `msil-ref=<n>`,
  `msil-refpack=<n>`.
- Composites (`substitutions`, from line 2785): `%{msil_c}`, `%{msil_ll}`, `%{msil_prog}`, per-level
  `%{msil_c_<L>}`, `%{msil_prog_<L>}`, `%{msil_prog_fail_<L>}`, `%{msil_det}`. Under `obj` every one-source
  composite is `prog_pipeline(level, ONE_SOURCE, single='c'|'ll')` (line 2701): `%msil_prog_build` (msil-prog.py;
  with `msil_equiv=1` and a single source also the `--no-gc-sections` equivalence link), O3's reference
  (`asm_reference`, line 2681: `clang -S` + ilasm), `%msil_verify` (with O6's `--md-baseline`), `%msil_jitall`,
  `%msil_exec`, `also_run_legs` (line 2423: jitall + run per `msil_also_run` entry), the round trip
  (`msil_roundtrip`), `%msil_equiv --oracle O3`, the native oracle (`msil-prog.py --mode native` + run;
  `native_leg`, line 2388, for `.ll`), and the Framework run.
- `msil_also_run` (lines 730-794, 2239-2262): after the main run, jitall and the run repeat once per entry with
  that runtime's runtimeconfig (and `DOTNET_TieredCompilation=0` for `+tc0`) against the same `.expected`.
- Every child runs under msil-run.py's timeout and memory cap (`limits`, line 2148: `--timeout %g
  --mem-limit-mb %d`, baked into every command when lit.cfg.py is evaluated). Timeout messages:
  `msil-run: <tool> did not finish within <T> s` (msil-run.py:817; `wsl.exe ...` with `WSL_GRACE = 120` s,
  msil-run.py:137, 799), `<tool> did not finish within <T> s` (msil-verify.py:410), `[timeout]` lines
  (msil-jitall.py:201, 284).

### 1.2 Corpora

| Corpus | Tests | Format |
|---|---|---|
| `msil/test/M1` (12 directories, `00-smoke` ... `11-programs`) | 143: 99 C, 43 IR, 1 `.test` | freestanding C (`msil_test.h`, `puts` only), `%{msil_c}`/`%{msil_ll}`, `.expected` from the Windows oracle, `.linux.expected` overrides from E7 |
| `msil/test/M2` (`00-link` ... `09-hosted`) | 127 (`08-semantics` 47) | split-file `.test` + `%{msil_prog}`; `09-hosted` compile-only |
| `msil/test/Programs` | 169: nine programs, nine `det/` tests, `det/check-recipe.test`, 150 c-testsuite tests | corpus cache (`fetch.py`, `manifest.json` v1), `REQUIRES: msil-corpus-<name>` |
| `msil/test/harness` | 112 in `Status.md` (115 lit entries in gate 4's xunit): `prog/` 16, `legs/` 13, `pipeline/` 3 (+3 more in the xunit), `pipeline-obj/` 5, `equiv/` 11, 64 top-level | self-tests; **34 files run `%{harness_lit}`, 132 nested lit runs in all** (`ref-flags.test` 21, `prog/lit-config.test` 20, `also-run.test` 10, `corpus-param.test` 9, `lit-config-errors.test` 7, `ctest-lists.test` 7) |
| FileCheck suites | `llvm/test/CodeGen/MSIL` 139, `lld/test/MSIL` 111, `clang/test/CodeGen/MSIL` 22 + 6 driver tests + `init-msil.c`, `llvm/test/tools/llvm-cil-*` 14, `llvm/test/MC/MSIL` 9 | many RUN lines per file (`call-errors.ll` 215) |

c-testsuite: `msil/test/Programs/c-testsuite/libc-free.txt` (162 lines: header with `# rule:` and `# count: 150`,
150 entries, one `diag` entry: `00140 diag error: variadic function definitions are not supported for MSIL yet  #
P3-B ...`); `lit.local.cfg` recomputes the list from the cached tree by the rule and stops on any difference;
`msil_ctest_format.py` yields one test per name running `ctest.template` (`%{msil_prog}` against
`exit0.expected`, or per level `%{msil_prog_fail_<L>}` piped into FileCheck of `diag.check` for a `diag` entry,
which also `REQUIRES: msil-prog-pipeline=obj`).

### 1.3 The link-semantics matrix and its checker

`msil/test/M2/08-semantics/matrix.txt`: 98 lines, 55 rows of seven columns; P3 lines (milestones column naming `P3`, `P3-A`
or `P3-B`): `p5-tls`, `p6-dtors`, `native-callback` (rows of 5.9.16) and `fini-array` (linux-gnu),
`crt-xt` (windows-msvc), `atexit-order`, `variadic-def-xtu`; each is `diag:` and its test pins today's text
(read in full for this report). `Inputs/check-matrix.py` (978 lines; `ROWS_5916` line 103, `M20_ROWS` line 118,
`check_diag` line 566, `check` line 682) checks matrix.txt against the tests only. Its three gaps (plan row
'Phase 3 input (link-semantics matrix checker)'), all confirmed by reading the code:
(1) it never reads the leg lists, so a line can need a (pipeline, triple) pair that no leg runs for that test;
(2) a `diag:` line needs failing builds at -O0 and -O2 but only one FileCheck of the text per pipeline;
(3) the 5.9.16 row lists are its own constants.

### 1.4 Legs and their measured time (M23's four phase-gate runs)

`msil/utils/ci/lit-legs.ps1` (565 lines: header 1-189 with gate-1/gate-2 minutes, `$checkMsil` line 201, legs
`$legs` 260-324, `$removedLegs` 326, `Invoke-LitLeg` 372, `Invoke-ExpectedLeg` 411, `Invoke-UtcLeg` 440,
`Invoke-EncoderLeg` 460, `Invoke-ObjTextLeg` 487) and `lit-legs.sh` (379 lines; natively on Linux; `net8` there
has `msil_also_run=10,8+tc0,10+tc0`). Minutes from the step 16 summary tables (`16.log`) of gate rounds 1-4
(2026-10-27/28; all on the shared, loaded host):

| Leg | g1 | g2 | g3 | g4 | Leg | g1 | g2 | g3 | g4 |
|---|---|---|---|---|---|---|---|---|---|
| net8 | 60.5 | 86.9 | 51.6 | 43.9 | ctest | 8.7 | 17.8 | 6.8 | 10.7 |
| more-levels | 15.5 | 15.7 | 13.4 | 12.1 | wsl-net10 | 56.5 | 103.2 | 35.6 | 109.2 |
| asm-oracle | 19.0 | 22.5 | 14.2 | 15.7 | wsl-ilasm | 2.3 | 1.2 | 1.3 | 4.0 |
| modern-ilasm | 1.4 | 3.6 | 1.4 | 2.0 | expected-linux | 2.4 | 1.5 | 1.4 | 4.0 |
| roundtrip | 4.9 | 5.6 | 2.5 | 4.4 | obj-text-wsl | 0.7 | 0.6 | 0.5 | 1.6 |
| expected | 4.5 | 5.4 | 2.4 | 3.5 | lto-wsl | 9.2 | 6.0 | 6.3 | 10.4 |
| utc | 0.4 | 0.5 | 0.2 | 0.2 | programs-wsl | 3.2 | 1.8 | 1.7 | 3.1 |
| obj-text | 3.2 | 5.5 | 2.8 | 3.3 | programs-lto-wsl | 3.6 | 1.8 | 2.7 | 4.2 |
| lto | 10.7 | 22.5 | 17.3 | 18.7 | det-wsl | 0.8 | 0.6 | 0.6 | 0.9 |
| det | 0.6 | 1.1 | 1.3 | 0.7 | hosted | 0.3 | 0.2 | 0.2 | 0.3 |
| refs-net48 | 2.8 | 6.9 | 5.8 | 3.4 | determinism | 1.1 | 0.6 | 0.5 | 0.8 |
| refs-net8 | 3.1 | 8.1 | 2.5 | 4.6 | upstream-hooks (5 rows) | 2.8 | 2.6 | 2.2 | 2.8 |
| refs-net10 | 2.6 | 4.4 | 2.4 | 2.4 | step 16 total | 228 | 339 | 184 | 275 |
| programs | 2.1 | 4.4 | 2.0 | 2.3 | | | | | |
| programs-lto | 3.9 | 6.4 | 2.9 | 3.9 | | | | | |

Gate 1 failed one net8 test and gate 4 one `wsl-net10` test (`M1/09-structs/struct-byval.c`: PEVerify /md
timeout, passed alone; plan row 'M23 gate step 16 amended (a tool timeout under host load)').

Older xunit files in `build/lit-results/` give quieter references: `net10.xml` and `net8-tc0.xml` (2026-10-26
05:36-05:40; asm, `msil/test/M1`, 143 tests) took 141.4 s and 140.7 s (7.6 test-s per test); `obj-wsl.xml`
(2026-10-26 06:11; obj, E7, `msil_equiv=1`, `M1/11-programs` + M2, 112 tests) 182.8 s; tests-p2 1.3's M11 net8
(2026-10-20; asm, 327 tests) 513 s for 4,006 test-s (7.8 workers busy).

### 1.5 `run-m1-gate.ps1`, `msil-ci.yml`, `det-manifest.py`

- `run-m1-gate.ps1`: configure, build, unit tests, host-setup (3 rows), corpus (`fetch.py --check`, never
  `--skip`), every leg of `lit-legs.ps1 -List` except `-Quick`/`-Full`, determinism (`det-manifest.py build`
  twice + `compare`), upstream-hooks (5 rows), ci-sh. One row per step; exit 0 only if every row is PASS.
- `.github/workflows/msil-ci.yml` (491 lines): `workflow_dispatch` only; `MSIL_CORPUS_SKIP: coremark,c-testsuite`
  (line 127); jobs `linux` (ubuntu-24.04, `timeout-minutes: 330`, line 134), `windows` (windows-2022, 330, line
  295), `determinism` (15, line 467; compares the two `det-manifest.py` manifests of objects and archives). Never
  dispatched since M23 (plan row 'CI input (first run of msil-ci.yml after M23)').

### 1.6 The 'Phase 3 input' rows this report answers

Linux-triple coverage (ctest, lto-wsl paths) -> 5.1; c-testsuite after P3-B -> 4.2; the seven P3 matrix rows
and the oracle gaps -> 4.1, 4.5; the matrix checker -> 5.3; gate time -> 2, 3; tool timeouts -> 3.5; CI first
run -> 5.2.

---

## 2. Cost structure of the gates (measured)

### 2.1 Tools of this report

- `tests-gates-p3-files/scripts/jobrun.py`: runs a command in a fresh Windows job object (the script assigns itself,
  so every descendant is in the job) and reports wall, CPU (`TotalUserTime + TotalKernelTime`) and
  `TotalProcesses`. The job counts every Windows process, not processes inside the WSL VM.
- `replay.py`: replays the `# executed command:` lines of a `lit -a` log one at a time, each in its own job.
- `xunit_cost.py`: per-directory sums of lit's xunit test times.
- `audit-sitecustomize.py`: a `sitecustomize` (through `PYTHONPATH`) with `sys.addaudithook` that logs every
  `open` of a lit config file and every `subprocess.Popen` of the lit processes (lit does not pass `PYTHONPATH`
  to the tests' own Python scripts, so those are not seen).

### 2.2 The net8 leg (gate 4)

`results/xunit-gate4-net8.txt`: 16,749 test-seconds in 2,629 s. By top directory: M1 7,013 s (143 tests, 49.0 s
each), M2 4,837 s (127, 38.1), harness 3,496 s (115, 30.4), `lld/test/MSIL` 647 s (111, 5.8),
`llvm/test/CodeGen/MSIL` 568 s (139, 4.1), clang 138 s (29), tools and MC 49 s (23). Largest single tests:
`M2/00-link/spill-limits.test` 637 s, `harness/ref-flags.test` 200 s, `harness/prog/pipelines.test` 132 s,
`harness/prog/roundtrip.test` 114 s, `harness/also-run.test` 112 s. The same composites took 7.6 test-s each in
the quiet 2026-10-26 `net10` leg (asm, no oracle, no Framework, no equivalence, no `msil_also_run`); measured
alone at `-j1` today (E13, host at 76%), such an asm .NET 10 composite costs 28 processes and 4.2 CPU-s per test
and took 13.3 s.

FileCheck files are not free either (E11, `results/profile-sets.txt`): the 20 slowest files of
`llvm/test/CodeGen/MSIL` (lit's default `--order=smart` runs previously slow tests first, and `--max-tests=20`
takes those) started 2,360 processes for 102 CPU-s, the 20 slowest of `lld/test/MSIL` 1,783 processes for 102
CPU-s: up to 118 and 89 processes per file. That is an upper bound; the whole directories record 683 s (141
files) and 1,062 s (113 files) in lit's time caches. They stay in every gate: they are 8.3% of net8's test time
and need no .NET.

### 2.3 One composite, step by step

`msil/test/M1/03-control/loops.c` with net8's parameters (`msil_pipeline=obj msil_dotnet_runtime=8
msil_native_oracle=required msil_run_netfx=1 msil_equiv=1 msil_also_run=10,8+tc0` + the three `msil_require_*`),
lit `-j1` inside one job: **99 processes, 18.0 CPU-s**, 96.9 and 132.5 s loaded wall (two runs). The config alone
(same parameters, no test selected): 11 processes, 4.2 CPU-s, 13.3 s. So the test (two levels) is about 88
processes and 13.8 CPU-s. The audit hook's Popen log of the same run (main process vs worker) shows the config
spends 17 s of loaded wall in `cmd.exe`, `clang -print-file-name`, three `dotnet` probes, `ilverify --version`,
`build-jitall.py` (3.8 s) and `build-mdtools.py` (1.7 s), and the worker then starts **15 commands per level**.

`results/composite-replay-net8-loops.txt` replays those 30 commands one at a time (each command's job also holds
the replay's `bash` and its `conhost`, 2 processes more than lit itself starts; loaded wall):

| Step (one level) | Lit commands | Processes (without bash/conhost) | CPU-s O0 / O2 | Loaded wall O0 / O2 |
|---|---|---|---|---|
| build: clang -c, link, `--no-gc-sections` equivalence link (msil-prog.py) | 1 | 6 | 0.56 / 0.58 | 2.2 / 1.7 |
| O3 reference: mkdir, clang -S, ilasm (msil-run.py --tool) | 3 | 1+1+2 | 0.53 / 0.64 | 5.7 / 5.4 |
| verify (ILVerify, PEVerify, neutralized ildasm/ilasm pass, O6 /md) | 1 | 9 | 1.11 / 0.97 | 14.2 / 11.2 |
| jitall + run, .NET 8 | 2 | 2+2 | 0.89 / 0.72 | 5.5 / 3.5 |
| jitall + run, .NET 10 (`msil_also_run`) | 2 | 2+2 | 0.76 / 0.68 | 4.7 / 4.1 |
| jitall + run, .NET 8 `TieredCompilation=0` (`msil_also_run`) | 2 | 2+2 | 0.72 / 0.77 | 3.0 / 2.4 |
| O3: msil-equiv.py (two llvm-cil-dump listings) | 1 | 5 | 0.83 / 0.58 | 13.8 / 4.6 |
| native oracle: msil-prog.py --mode native + run | 2 | 4+2 | 0.80 / 0.81 | 9.9 / 5.6 |
| Framework run | 1 | 2 | 0.42 / 0.27 | 2.6 / 1.4 |
| **level total** | **15** | **~44** | **6.62 / 6.02** | |

Shares of the test's CPU: verify 16%, O3 (reference + comparison) 20%, `msil_also_run` 23%, native oracle 13%,
the main .NET 8 jitall + run 12%, build 9%, Framework 5%. M23.d's quiet-host profile of the same kind of
composite (19.5 s at `-j1`, plan row 'Phase 3 input (gate time)') gave verify 20%, native 17%, O3 18%, compile
and links 9%, .NET 8 run 11%, each `msil_also_run` entry 11-12%, Framework 5%: the same picture.

Process start-up dominates small tests: `python -c pass` 0.03-0.06 CPU-s (0.2-0.6 s loaded wall); `python
msil-run.py --help` 0.14-0.22 CPU-s (0.9-1.4 s) because of its imports; `dotnet --version` 0.25-0.33 CPU-s
(0.63-0.93 s). A level's 15 Python wrappers cost about 2-3 CPU-s of its 6-6.6.

### 2.4 What each parameter costs

Four tests (`M1/03-control/loops.c`, `M1/01-int/many-args.ll`, `M2/00-link/two-tu.test`,
`M2/08-semantics/zoo.test`), one lit run each at `-j1` inside a job (`results/profile-sets.txt`); per test after
subtracting the config (4.2 CPU-s, 11 processes):

| Parameter set | CPU-s / test | Processes / test | vs full |
|---|---|---|---|
| `minimal`: obj, .NET 8 | 4.75 | 32 | 0.44 |
| `lean`: + `msil_run_netfx=1` | 5.4 | 36 | 0.50 |
| `lean` + `msil_equiv=1` | 6.45 (single-TU tests +2.05, multi-TU +0) | 48 | 0.60 |
| `lean` + `msil_native_oracle=required` | 7.0 | 49 | 0.65 |
| `lean` + `msil_also_run=10,8+tc0` | 7.7 | 52 | 0.71 |
| `full` (today's net8 parameters) | 10.8 | 77 | 1 |

The increments add up (5.4 + 1.6 + 1.03 + 2.26 = 10.3 against 10.8 measured). The loaded walls of these runs
(460.7 s full, 194.5 s lean) give a 2.4 ratio, consistent with the CPU ratio of 2.0 but too noisy to use.

### 2.5 The lit configuration, nested lit and the "evaluated twice" question

With the audit hook, a lit run of one msil/test test opens `build/tools/msil/test/lit.site.cfg.py`,
`msil/test/lit.cfg.py` and the directory's `lit.local.cfg` **once, in the main process**; the worker process
(lit always uses a `ProcessPoolExecutor`, `llvm/utils/lit/lit/run.py` 141-180, also at `-j1`) never re-reads them
(`lit/worker.py`: the worker gets the pickled configuration). So M23.d's 'lit.cfg.py evaluated twice' is not a
per-invocation cost; the measurable costs are the config itself (4.2 CPU-s, 11 processes, 13-17 s loaded, about
7 s more for the WSL probe per M23.d) once per leg, and the harness's 132 nested lit runs (`%{harness_lit}`,
`msil/test/harness/lit.local.cfg`), each of which re-runs every probe, `build-jitall.py` and `build-mdtools.py`:
about 550 CPU-s of the harness's cost. A probe cache (section 3.4) removes most of it.

### 2.6 WSL

- `wsl.exe -d Ubuntu --exec true` (`scripts/wsl_concurrency.py`): concurrency 1: 0.97 s per launch (latency 1.00
  s); 2: 0.54 s; 4: 0.39 s; 8: **0.28 s per launch (3.6 launches/s), latency 1.86 s median, 2.40 max**.
  The Windows side of one launch costs 0.09-0.17 CPU-s and 4 processes; the Linux side is in the VM.
- A `wsl-net10` composite level launches `wsl.exe` for jitall, the run, `msil_also_run`'s two jitall + run pairs
  and the E7 native run: 7 per level, 14 per test, about 5,400 for 385 tests, i.e. **about 25 minutes of launch
  throughput** before any work; `wsl-net10` took 35.6-109.2 minutes. Without `msil_also_run`, 3 per level (2,300
  launches, about 11 minutes); without the oracle either, 2; with one `wsl.exe` per level that runs every runtime
  step of the level inside WSL, 1 per level.
- **Measured (E12, `results/wsl-composite.txt`; `M1/03-control/loops.c`, lit `-j1 -a` in a job object).** With
  `wsl-net10`'s parameters a level runs 14 commands, 7 of them through `wsl.exe` (msil-jitall + msil-run for .NET
  10, .NET 8 and .NET 10 TC=0, and the E7 native run) and 7 on Windows (the msil-prog.py build, the O3 reference's
  mkdir + `clang -S` + ilasm, msil-verify.py, msil-equiv.py, `msil-prog.py --mode native`). Per test, after the
  WSL configuration (E14: 15 processes, 4.05 CPU-s, 18.2 s loaded; the Windows-triple configuration is 11
  processes, 4.16 CPU-s): **123 Windows processes and 13.2 CPU-s**, plus the Linux side in the VM, which the job
  does not see. The lean WSL composite (no oracle, no equivalence, no `msil_also_run`) runs 4 commands per level,
  2 of them through `wsl.exe`: **43 processes, 4.3 CPU-s** (33%). Back to back, lit's testing time was 37.4 s
  (full) and 13.6 s (lean), a ratio of 2.76. G4's `wsl-p3` (3.2) therefore keeps `msil_also_run` only on
  `msil/test/M3`.
- Per-directory test-seconds of gate 4's `wsl-net10` (`results/xunit-gate4-wsl-net10.txt`): M1 28,718 (55.5%),
  M2 15,658, harness 7,336; average 201 s per M1 test under that load, against 11.4 s per test in the quieter
  2026-10-26 `obj-wsl` run without `msil_also_run`.

### 2.7 A model for quiet-host minutes

The legs are latency-bound, not CPU-bound: the quiet 2026-10-26 `net10` leg ran 143 composites in 141 s with
7.7 workers busy, at about the composites' `-j1` time each. So **quiet leg wall = sum of quiet `-j1` test times /
7.8**, and a parameter set's quiet test time scales with its CPU per test (both are dominated by process
launches). Anchors: M23.d's quiet net8 composite, 19.5 s at `-j1`; the 2026-10-26 `net10` leg, 7.6 test-s per
asm composite. Today's net8 leg then is about 270 composites x 19.5 s + harness 115 x ~12 s + FileCheck 302 x
~2 s + `spill-limits` ~270 s, about 7,500 test-s / 7.8 = **about 16 minutes on a quiet host**; M23's gate runs
measured 2.7-5.4 times that (43.9-86.9 minutes), the 'load factor' of the shared host. The plan's 9-11 minute
figure for G3 dates from asm composites without O3, oracles across both runtimes and `msil_also_run`.
E13 shows how strongly even partial load counts: the same asm .NET 10 composite took 13.3 s alone at `-j1` with
the host at 76% load, against 7.6 test-s per composite at `-j8` in the quiet 2026-10-26 leg, a factor of about
1.75 before any contention among lit's own workers. This is why D2 normalizes by a calibration composite rather
than by `LoadPercentage`.

---

## 3. A standard milestone gate for Phase 3

### 3.1 What a Phase 3 milestone gate must keep

- D21 verification on everything that runs (MSILStackCheck, jitall, ILVerify, PEVerify): never dropped.
- Every Phase 1 and Phase 2 test through the default pipeline on .NET 8 and Framework 4.8 (Framework 4.8 is the
  default-compatibility check that every Phase 3 default must pass: 'defaults must run on .NET Framework 4.8 and
  modern .NET alike').
- O3/O6 on single-TU tests: Phase 3 changes `MSILPrepareIR`, the AsmPrinter and the trailer, which the `-S`
  self-link and msil-ld share; O3 is the check that the two stay equal.
- The Phase 3 feature tests on **every** runtime (.NET 8, .NET 10, TC=0, Framework, WSL .NET 8/10): TLS
  (Tier-0 and optimized code take different thread-static paths, frame-tls E2b), thunks (delegates vs
  `UnmanagedCallersOnly`), ProcessExit (Framework's 2 s limit, lifecycle recon), Interlocked/Volatile, the
  exception filters of setjmp.
- Both triples for the feature tests (glibc vs UCRT is the substance of P3-B).
- The milestone's new or changed `.expected` files checked against the native build at -O0 and -O2.

What can move to the post-commit run without losing a check that the milestone itself needs: `msil_also_run`
on M1/M2 (runtime-independent code paths already verified and run on .NET 8 and Framework), the composites'
native oracle on unchanged tests (the `expected` leg checks every `.expected` against the native build; the
native toolchain does not change in a milestone), the harness self-tests when no harness file changed,
`spill-limits` when neither trailer, spill classes nor the type-member limit changed, and the WSL run of
unchanged M1 tests.

### 3.2 Proposed standard steps

| Step | Command (shape) | Quiet min (model of 2.7) | Loaded min (x2.7-5.4) |
|---|---|---|---|
| G0 | host setup (unchanged; plus `make-linux-sysroot.py --with-nonshared`, 4.5) | <0.5 | <1 |
| G1 | build (not counted) | | |
| G2 | unit tests, CLITests floor | ~1 | 1-3 |
| G2c | **calibration**: `msil-lit.ps1 -LitArgs "-sv -j1 <net8 params>" msil/test/M1/03-control/loops.c`, wall / 19.5 s = the gate's load factor, printed in the gate report | 0.3 | 1-2 |
| G3 | `lit-legs.ps1 -Leg net8-core`: `$checkMsil` FileCheck paths + `msil/test/M1` + `msil/test/M2` (`--filter-out=MSIL :: (Programs|harness|M3)/`, `--filter-out=spill-limits`), `msil_pipeline=obj msil_dotnet_runtime=8 msil_run_netfx=1 msil_equiv=1 msil_require_csc=1`, no oracle, no `msil_also_run` | 7-8 | 20-40 |
| G3b | `lit-legs.ps1 -Leg p3-full`: `msil/test/M3` with today's net8 parameters (oracle, Framework, O3/O6, `msil_also_run=10,8+tc0`) | 2-4 (60-100 tests) | 6-20 |
| G3c | `lit-legs.ps1 -Leg harness` **when the milestone changes** `msil/utils/**`, `msil/test/lit.cfg.py`, any `lit.local.cfg` or `msil/test/harness/**` (G6 decides; with the probe cache of 3.4) | 2-3 | 6-15 |
| G4 | `lit-legs.ps1 -Wsl -Leg wsl-p3`: `msil/test/M3`, `msil/test/M2/08-semantics`, `msil/test/M1/11-programs` on WSL .NET 10, E7/E7h, `msil_also_run=8` on M3 only | 4-6 | 11-30 |
| G5 | upstream-hooks manifest, pins (full when an upstream file changes) | 0.3 (3) | 1 (8) |
| G6 | test presence; harness-change decision for G3c; `check-matrix.py` with legs (5.3) | <0.5 | <1 |
| G7 | `update-expected.py --check` at `--opt=-O0` and `-O2`, `--os windows` and `--os linux`, on the test directories the milestone adds or changes (`git diff --name-only HEAD` under `msil/test`) | 1-2 | 3-8 |
| G8, G9 | format check; `plan-check.py scope M<n>` | ~1 | 1-2 |
| | **total (without G3c)** | **17-23** | **45-115** |
| | with G3c | 19-26 | 51-130 |

`net8-core`'s test time is projected from gate 4's per-directory test-seconds and the measured parameter ratios:
FileCheck 1,397 s (unchanged) + M1 7,013 x 0.66 (single-TU: lean + O3 against full) + M2 without `spill-limits`
4,200 x 0.58 (multi-TU: lean against full) = 8,462 test-s, **50.5% of today's net8** (43.9 min in gate 4 -> about
22 min under that load; 16 min quiet -> about 8 min). Without O3 on M1 it would be 43%.

**Budget rule.** Q32's 20-30 minutes holds for load-normalized time: the gate report prints each step's raw
minutes, the calibration factor of G2c and raw / factor. A gate whose normalized time exceeds 30 minutes moves
steps out in this order, each through a section 12 row: G3c to the post-commit run (when the harness change is
only additive), G4's `M1/11-programs`, G3b's `msil_also_run` on directories the milestone did not touch.

### 3.3 Post-commit and checkpoint runs

- **After every Phase 3 milestone commit**, in the background (the orchestrator, not a gate step), the legs the
  standard gate leaves out: today's `net8` (oracle, `msil_also_run`, `spill-limits`, harness), `wsl-net10`,
  `lto`, `lto-wsl`, `programs*`, `ctest*`, `expected`, `expected-linux`, `asm-oracle`, `roundtrip`: about 3-6
  hours on the loaded host, about 1.5-2 hours quiet. A failure becomes a fix WP of the next milestone through a
  section 12 row, as 5.12.10 did for M12, M15, M18 and M20. The next milestone's gate may not pass while such a
  fix is open.
- **P3-A checkpoint** (after the last P3-A milestone): the full `run-m1-gate.ps1 -CorpusCache` as a gate step,
  recorded at its measured time (Q32 amended).
- **Phase 3 exit**: section 6.4.

### 3.4 Harness work that cuts the cost without dropping a check

| Change | Saves (measured basis) | Size |
|---|---|---|
| Probe cache: lit.cfg.py writes the results of its probes (dotnet runtimes, ilverify, peverify, ilasm, the jitall and mdtools up-to-date checks, the WSL probe) to `<build>/tools/msil/test/probe-cache.json` keyed by the tools' paths, sizes and mtimes and the parameters that select them; `%{harness_lit}` and every leg reuse it | 4.2 CPU-s and 11 processes per lit invocation; about 550 CPU-s of the harness's 132 nested runs; about 7 s of WSL probe per WSL leg | ~200 lines (lit.cfg.py, harness/lit.local.cfg) |
| One Python process per composite level (`msil-steps.py`): verify, jitall, run, `msil_also_run`, the Framework run and the native run as functions of one process (msil-run.py, msil-verify.py and msil-jitall.py become importable modules; each step still prints its command line and output for lit's log) | 10-12 Python starts per level: about 2 CPU-s of 6-6.6 per level, 30-40% of a lean composite | ~400 lines (msil/utils, lit.cfg.py composites) |
| One `wsl.exe` per WSL composite level: msil-run.py `--runner wsl --steps <file>` runs the level's jitall and runs inside one WSL Python process | 6 of 7 launches per level: about 20 of today's 25 minutes of `wsl-net10`'s launch floor | ~200 lines (msil-run.py, msil-jitall.py, lit.cfg.py) |
| ILVerify once per image pair (the image and its neutralized copy in one ILVerify process) | 1 dotnet start per level | small (msil-verify.py) |
| Native-oracle transcript cache keyed by the source, flags and the native clang's hash | the oracle's 13% where it stays (phase gate) | ~100 lines (msil-prog.py) |

None of these changes a check; each needs its own harness self-tests. The first two make the M1/M2 composites
roughly 30-40% cheaper, which would let `msil_also_run` return to the standard step if the user prefers that.

### 3.5 The tool-timeout rule for every gate

M23's rule (plan section 12, 'M23 gate step 16 amended (a tool timeout under host load)'): a lit-leg row that
fails only through tests whose every failure is a tool timeout counts as PASS when each such test, re-run once
alone with exactly the leg's parameters while no other lit run is going, passes; more than three such tests
fail the step. Evidence that it is load, not code: PEVerify /md takes 0.08-0.24 s on `struct-byval.c`'s image and
hit 60 s under load; `wsl.exe` latency doubles at concurrency 8 (2.6).

Generalize it in three parts:
1. **Plan text (5.12.10 replacement):** the rule applies to every gate step that runs lit, to the post-commit runs
   and to `run-m1-gate.ps1`.
2. **Automate it in `lit-legs.ps1` and `lit-legs.sh`** (the scripts every gate and CI run): after a leg's lit run,
   read the xunit file; a failed test whose failure output contains only timeout failures (`did not finish
   within`, `[timeout]`) and no other failing check is re-run once with `-j1 --param` the leg's parameters, into
   `<leg>-retry-<stem>.log`; the row is `PASS (retried after timeout: <tests>)` when every re-run passes and at
   most three tests were re-run, else FAIL. `run-m1-gate.ps1` and CI then need no agent judgement, the summary
   names every re-run, and a deterministic hang still fails (it times out twice).
3. **Per-test limits instead of a global raise:** a `%{msil_limits}` placeholder (default `--timeout
   <msil_timeout> --mem-limit-mb <msil_mem_limit_mb>`) inside every composite and tool substitution, so a test of
   a large program (SQLite's amalgamation, Lua's test suite) can `REDEFINE: %{msil_limits} = --timeout 300
   --mem-limit-mb 8192` for its own commands; `msil_timeout` stays 60 s everywhere else, so real hangs are still
   found quickly.

Not recommended: lit's `ALLOW_RETRIES`/`--max-retries-per-test` (retries any failure and hides real flakiness),
or raising `msil_timeout` for every leg (delays the detection of every hang).

---

## 4. Test plan for the Phase 3 features

### 4.1 What flips

**Matrix P3 rows** (each becomes a `%{msil_prog}` transcript on obj and lto with `.expected` from
update-expected.py on both OSes, and its line `transcript`; check-matrix.py then requires the composite and the
file):

| Row | Native transcript source | Oracle gap today | Feature |
|---|---|---|---|
| `p5-tls` | Windows and E7 (12) | none | P3-A TLS |
| `p6-dtors` | Linux `BCDMcb`, Windows `BCDMbc` (lifecycle recon 7.1) | E7 runs no `.fini_array`; Windows native build faults at exit (J9): hand-written Windows transcript (lifecycle L11) | P3-A destructors |
| `fini-array` (linux-gnu) | glibc | E7 runs no `.fini_array` | P3-A |
| `crt-xt` (windows-msvc) | MSVC CRT `.CRT$XT` | the static-CRT oracle; lifecycle recon's hand-written transcript | P3-A |
| `atexit-order` | Windows /MT and /MD; glibc | E7 sysroot has no `atexit` (`libc_nonshared.a`) | P3-A lifecycle |
| `native-callback` | sorted array | none | P3-B callbacks |
| `variadic-def-xtu` | 42 | none | P3-B variadics |

**Tests that pin a Phase 3 diagnostic today** (44 files; each changes in the milestone of its feature, like
5.12.13's table): `llvm/test/CodeGen/MSIL`: `add-prototypes.ll`, `alias.ll` (TLS), `atomic-errors.ll`,
`call-errors.ll`, `call-indirect.ll`, `dynamic-alloca-error.ll`, `ext-loads.ll`, `extern-weak.ll`,
`fp-expand.ll`, `globals.ll`, `i128-divrem.ll`, `imports-linked.ll`, `libcall-policy.ll`, `object-calls.ll`,
`object-errors.ll`, `pinvoke-errors.ll`, `section-errors.ll`, `sections-whole-program.ll`,
`target-intrinsic-errors.ll`, `trunc-stores.ll`, `tti-hooks.ll`, `unsupported-ir-errors.ll`,
`va-arg-aggregate.ll`, `va-intrinsics.ll`, `varargs-call-error.ll` (26); `lld/test/MSIL`: `libcall.test`,
`native-aggregate.test`, `native-callback.test`, `native-variadic.test`, `obj-gc-sections.test`, `obj-pinvoke.test`,
`obj-sections-unsupported.test`, `obj-signatures.test`, `shared-export-selection.test` (TLS export) (9); M2:
the seven P3 08-semantics tests, `09-hosted/glibc-atexit.test`, `09-hosted/glibc-callback-redirect.test`
(found with `grep -rlE "not supported for MSIL yet|phase 3|not supported yet|needs phase 3"` and read one by one;
`byval.ll` and `native-coff-importlib.test` matched only historical comments and are left out; the plan's Phase 3
milestones should list them per WP as 5.12.13 did, after the feature recons fix the new texts).

**c-testsuite:** `00140` (libc-free.txt line 146) becomes a plain entry when variadic definitions compile; the
`diag` machinery stays for P3 work in progress.

### 4.2 c-testsuite's 70 excluded tests

Measured (`results/ctest-classify.txt`, `results/ctest-irscan-linux.txt`; the rule and tree of libc-free.txt):

- By the list's rule (`libc-free.txt` line 8, `# rule:`; the regular expression of tests-p2 2.4's `ctest.py`):
  68 call `printf` (62 only `printf`; with others: 00179 `memcmp, strcat, strcmp, strcpy, strlen, strncmp,
  strncpy`, 00180 `strcpy`, 00182 `fprintf`, 00186 `sprintf`, 00187 `fopen`, 00204 `va_start`); 00025 `strlen`,
  00040 `calloc`. Empty `.c.expected`: 00025, 00040, 00208, 00218 (66 non-empty). The variadics recon's 8.4 says
  "63 call only `printf`; 5 call `printf` and other functions": the same 68 tests, with `fprintf` (00182) counted
  as `printf`. **Appendix A lists all 70 tests**: what the rule matched, the `.c.expected` size, the Phase 3
  constructs in their IR and the entry each gets in the proposed second list.
- IR for `x86_64-unknown-linux-gnu-msil` (glibc sysroot headers, -O0 and -O2, all 220): native variadic calls in
  69 tests (66 have nothing else), variadic definitions in 00140 and 00204, `va_start` in 00204, dynamic alloca
  and `stacksave` in 00207 at -O0 only; no `thread_local`, atomics, `setjmp`, `_Complex` helpers or `half`.
- The variadics recon (its 8.4, prototype build of all 70 at -O0/-O2 on both triples, .NET 8) found: Linux 68/69
  equal to `.c.expected`; Windows 60/61, plus 00212, 00216, 00218 equal to the native build but not to
  `.c.expected` (which assumes LP64 and GCC bit-fields); 00204's `long double` printed through glibc's `vprintf`
  (`nan` in 20 of 174 lines; Design.md 3's documented limitation); 00207's VLA at -O0 (P3-A); six tests that
  declare `printf` themselves fail to link on windows-msvc (UCRT exports no `printf`; decision V10 of the
  variadics recon); 00214 does not compile for Windows natively either.

**Proposed second list** `msil/test/Programs/c-testsuite/libc.txt` (same header shape: `# rule:` = the printing
tests, i.e. everything libc-free.txt's rule matches, `# count: 70`), one entry per test:
- `<name>`: build through the leg's pipeline, run, compare with an expected file the format writes into `%t`
  from the cached `<name>.c.expected` plus `[exit 0]` (msil_ctest_format.py reads it; nothing is vendored).
- `<name> native <triple>  # <reason>`: on that triple compare with the native build's transcript instead (a
  run-time expected file from the oracle; for 00212, 00216, 00218 on windows-msvc).
- `<name> diag <triple> <text>  # P3-x ...`: the named diagnostic of a Phase 3 feature not built yet (00207 at -O0
  until P3-A's dynamic alloca; the six `printf`-declaring tests on windows-msvc until V10 is implemented).
- `<name> exclude <triple>  # <reason>`: a documented deviation or a non-portable test (00204 linux-gnu:
  `long double` is `double`, Design.md 3; 00214 windows-msvc: `typedef unsigned long size_t` is not LLP64-clean);
  the vocabulary is closed (`long-double`, `not-portable`), each exclusion is listed in Status.md, and the
  format reports them UNSUPPORTED with the reason, never XFAIL.
- The `lit.local.cfg` check recomputes both lists by their rules (the union must be the whole tree) and stops on
  a difference, as today.
Legs: `ctest` runs both lists through objects; `ctest-wsl` the same for the Linux triple; the second list joins
when variadic calls land (P3-B variadics milestone), not before (a capability rule, 4.4). Costs (gate 4 load):
`ctest` 150 tests, 10.7 minutes (4,897 test-s, 32.6 s each); +70 tests about +47%.

### 4.3 The Phase 3 corpus: `msil/test/M3`

One directory per feature area, created by the milestone that implements it (layout as M2: split-file `.test`
or single `.c` with `%{msil_prog}`; `.expected` from update-expected.py, `.linux.expected`/`.windows.expected`
overrides with a comment; every test at -O0 and -O2, both triples, obj and lto; D37's exit-status rule):

| Directory | Feature (recon) | Contents (from the feature recons, consolidated) |
|---|---|---|
| `M3/00-frames` | dynamic alloca, VLAs, stacksave/restore, `__builtin_frame_address` (frame-tls) | `vla_cases.c` (native transcript identical on both OSes: `vla_loop 128493856`, `alloca_loop 780`, `vla_align 1`, `vla_rec 1000`), alloca in loops, over-aligned VLAs, setjmp interplay once sjlj lands |
| `M3/01-tls` | `thread_local` (frame-tls) | single-thread TLS (definition, extern across objects, address initializers, `.tbss`, alignment 64, weak undefined), TLS exports from `-shared` (`tls-shared-export`); threads (`tls_threads.c`) after callbacks |
| `M3/02-lifecycle` | destructors, `atexit`, `exit` family, `.fini_array`, `.CRT$XT` (lifecycle) | `order.c` modes (`ret`, `exit`, `nested`, `exitinhandler`, `exitindtor`, `atexitindtor`, `exitinctor`), `quick_exit`, `_Exit`, exit status as explicit per-OS checks (not transcripts), deadlock regressions with timeouts, a C# host of a library with destructors (`msil_require_csc`) |
| `M3/03-helpers` | private helpers, compiler-rt builtins (helpers-switches, fp-helpers) | i128 helper vs inline, `_Float16`/`__bf16` conversions and arithmetic, `_Complex` multiply/divide with Annex G cases (inf/NaN), constrained FP <-> i128, each against native |
| `M3/04-switches` | codegen switches and BCL intrinsics (helpers-switches) | each switch off (default; Framework-compatible) and on (`%{msil_switches}`, references net8/net10), the reference check's error when a reference lacks the member; bit ops, rounding, min/max, bitcasts |
| `M3/05-variadics` | variadic definitions and calls (variadics) | `va.c`, `regs.c` (linux), `natives.c`, `winfp.c` (msvcrt via `import_module`), `va_copy`, structs and i128 in `...`, zero variadic arguments, the `msil_fixed_arity` list |
| `M3/06-callbacks` | native-callable thunks (callbacks) | `bsearch`, `qsort_r`/`qsort_s`, `lfind`/`tsearch`/`twalk`, callbacks stored by a test-built native library (`%{msil_deps}`), struct-by-value callbacks, identity (`&f` vs the thunk), the fast path across objects and libraries, `GetProcAddress`/`dlsym` pointers called from C; `--uco-thunks` variants |
| `M3/07-sjlj` | setjmp/longjmp (sjlj) | `sjlj-semantics.c` (14 cases), cross-assembly (`-shared`) longjmp, `longjmp` out of a `qsort` comparator (Windows; Linux pinned to its diagnostic) |
| `M3/08-atomics` | C11 atomics (atomics-threads) | single-thread semantics of every operation and size (1, 2, 4, 8, 16 bytes), memory orders compile, `atomic_is_lock_free` answers, adjacent-byte CAS correctness |
| `M3/09-threads` | threads (callbacks + TLS + atomics) | the deterministic shapes of 4.6 (`threads.c` in tests-gates-p3-files/threads), `pthread_create`/`CreateThread`/`_beginthreadex`/`thrd_create`, `pthread_once`, `pthread_key_create` destructors, TLS per thread, the spilled-`'<msil>Data'` first-use race (plan row 2026-10-27) |
| `M3/10-hosted` | real libc headers and stdio (hosted-libc) | `printf`/`fprintf(stderr)`/`sprintf` families, `stdin`/`stdout`/`stderr` data imports, `errno`, `FILE*` I/O in the private run directory, `time.h`, `getenv`, through glibc's and the UCRT's real headers |

Matrix additions (08-semantics, check-matrix.py-listed): `tls-xtu-init`, `tls-shared-export`, `tls-weak-undef`,
`tls-threads` (frame-tls 9), `exit-order`, `p2c-exit-order` (lifecycle 7.2), `sjlj-xasm` (sjlj 10), a
variadic export across assemblies (variadics 8.2), a library function pointer passed to native `qsort`
(callbacks 5.6 item 4).

**From the sibling recons' test sections** (read again in the second session; each recon owns the details, this
list fixes where the tests live and which legs run them):

- **atomics-threads 12:** FileCheck `atomics.ll` (replaces `atomic-errors.ll`; three triples including
  `aarch64-unknown-linux-gnu-msil`), `atomics-partword.ll`, `atomics-libcalls.ll`, `fence.ll` (D36's
  `'<msil>compiler_barrier'`), `clang/test/CodeGen/MSIL/atomic-lock-free.c`; an msil-verify unit test for the
  `Interlocked` allowance (Q-AT1). Execution in `M3/08-atomics`: every C11/GNU/`__sync` operation single-threaded,
  the MSVC `_Interlocked*` family on windows-msvc, `atomic_is_lock_free` answers; **16- and 24-byte atomics get a
  hand-checked `.expected`** (native Windows cannot link them, and neither E7 nor E7h has libatomic: it ships
  with GCC, and the glibc sysroot has no `libatomic*`, checked; adding Ubuntu's `libatomic1` package to
  `make-glibc-sysroot.sh` would give E7h one, *unverified*). In `M3/09-threads`:
  `counter.c` (exact count), `sb.c` (store buffering, seq_cst: zero forbidden outcomes in a fixed number of rounds;
  the wrong mapping showed 6,463-80,852 per 2 M rounds, atomics-threads 5.1), `subword.c` (adjacent bytes),
  a relaxed-load spin and a compiler-barrier spin that must terminate (bounded, 4.6 rule 5), `texit.c`, `c11thr.c`
  (glibc; Windows only with `-lvcruntime`), a 16-byte atomic shared by a program and its `-shared` library
  (Q-AT6's process-wide lock table). All of them on `TieredCompilation=0` as well (D38).
- **lifecycle 7:** `exit-status` is an explicit per-OS status check, not a transcript (`exit(300)` gives 300 on
  Windows and 44 on Linux; D37 limits transcripts to 0-255). **`abort` runs on Linux only** (status 134):
  msil-run.py already calls `SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
  SEM_NOOPENFILEERRORBOX)` before it starts a child (`msil/utils/msil-run.py` line 308), but the UCRT's `abort`
  ends in `__fastfail` (0xC0000409), which bypasses the error mode, and the lifecycle recon measured a WER delay of
  minutes; Windows gets a link-level check of the `abort` binding instead. Deadlock regressions run on all five
  runtimes, each with its own `%{msil_limits}` timeout (3.5). FileCheck `dtors-*.ll`; lld `obj-dtors.test`,
  `obj-exit-synth.test`, `shared-fini.test`.
- **sjlj 10:** `sjlj-lower.ll` (`llc -stop-after=msil-lower-sjlj`), `sjlj-diag.ll`, `sjlj-eh.ll` (with an `%if
  ilasm` round trip and the clause table through `llvm-cil-dump`); `M3/07-sjlj/sjlj-semantics.c` with
  `.linux.expected` and `.windows.expected` from the native builds; the cross-assembly cases; the S10 guard.
  Programs: zlib `contrib/puff` and `contrib/blast` (in the cached zlib tree; once variadics exist), Lua's
  error-heavy scripts in the P3-B exit, libpng/libjpeg-turbo after Phase 3.
- **callbacks 5.6:** the GC-churn runner (a small C# tool in `msil/utils`, built like jitall), `--uco-thunks`
  variants on .NET 8 and 10, Windows and WSL (never Framework: the attribute does not exist there).
- **frame-tls 9:** the frame and TLS execution tests need the Framework and `TieredCompilation=0` runs (D22, D38;
  Tier-0 and optimized code take different thread-static paths, frame-tls E2b). G3b (`p3-full`) keeps both.
- **variadics 8:** FileCheck `variadic-def.ll`, `variadic-call.ll`, `variadic-fixed-arity.ll`,
  `variadic-errors.ll` (both triples, -O0 and -O2); lld `native-variadic.test` rewritten, `obj-variadic.test`,
  `shared-variadic.test`; the `M3/05-variadics` corpus; `msil_also_run` keeps .NET 10 and TC=0 on it (the AL hazard of variadics 2.2 is a
  TC=0/warm-up effect).

### 4.4 Capability rules for Phase 3 tests (5.5's table, continued)

A test written before its capability exists fails for the wrong reason. The test WPs of each Phase 3 milestone
respect:

| Until | Not available | Write tests like this |
|---|---|---|
| hosted harness mode (first Phase 3 milestone) | real libc headers in execution tests | freestanding `msil_test.h` as M1/M2 |
| variadics | `printf` and friends, variadic definitions | `put_str`/`put_i64`/`put_kv` of `msil_test.h`, or `puts` through real `<stdio.h>` |
| callbacks | `qsort`, `bsearch`, `signal`, threads, any C function passed to native code | callbacks only within the program |
| TLS + callbacks + atomics | threads | single-thread TLS and atomics |
| lifecycle | `atexit`, destructors, transcripts after `main` returns | output only before `main` returns |
| sjlj | `setjmp` | |
| helpers milestone | `_Complex` mul/div, `_Float16` arithmetic, `__bf16` arguments | |

A test of a feature that a later milestone implements pins today's diagnostic (as the P3 rows do) and is never
XFAIL; the plan's milestone that implements the feature turns it into a transcript.

### 4.5 Native oracles for Phase 3

- **E7h (hosted Linux oracle), measured:** `clang --target=x86_64-unknown-linux-gnu --sysroot=<glibc sysroot>
  -fuse-ld=lld -nostartfiles -nodefaultlibs <sysroot>/usr/lib/x86_64-linux-gnu/crt1.o crti.o <sources> -lc
  crtn.o`; `-lc` is the sysroot's `libc.so` script (`GROUP ( libc.so.6 libc_nonshared.a AS_NEEDED (
  ld-linux-x86-64.so.2 ) )`). `threads/atexit.c` printed `main returns 42`, `second registered, runs first`,
  `first registered, runs last`, `destructor` (glibc's order: handlers, then `.fini_array`), and `threads.c tls`
  passed, in WSL. No `crtbeginS.o` or libgcc is needed for C (the plain driver link fails on them). E7h
  replaces E7 for M3 and for the P3 matrix rows; E7 stays for M1/M2 (freestanding, `start.c`). The glibc
  sysroot (`make-glibc-sysroot.sh`, G0 since M15) already has `crt1.o`, `crti.o`, `crtn.o`, `libc_nonshared.a`
  (`atexit`, `at_quick_exit`), `libc.so`, `libm.so.6` and `libpthread.so.0`.
- **Windows:** the harness's native oracle links clang's default static CRT (/MT), while MSIL output binds the
  DLL CRT (`ucrtbase.dll`); exit-time behaviour differs between them (lifecycle recon 2.2-2.3). Hosted and
  lifecycle tests use a `/MD` native oracle (`-fms-runtime-lib=dll`, *unverified flag spelling for this clang*)
  or per-OS `.windows.expected` files written from the /MD build, as the lifecycle recon proposes.
- `update-expected.py` needs the same two modes (`--hosted`: no `-ffreestanding`, `%{msil_hosted_cflags}`, E7h on
  Linux, /MD on Windows).

### 4.6 Threads: deterministic tests (measured)

`tests-gates-p3-files/threads/threads.c` (C11 atomics, pthreads or `_beginthreadex`) and `Threads.cs` (the CLR
counterpart, built once by the Framework csc and run on Framework 4.8 and through `dotnet exec --runtimeconfig`
on .NET 8/10, Windows and WSL); results in `results/threads-results.txt`:

| Shape | Native Windows -O0/-O2 | Native Linux (E7) -O0/-O2 | CLR Framework, .NET 8, .NET 10, WSL 8/10 |
|---|---|---|---|
| 4 threads x 1e6 atomic increments, main prints the total | exact | exact | exact (Interlocked on an `AllocHGlobal` cell) |
| the same, non-atomic | lost 15,767-2,403,986 of 4e6 in 20 of 20 runs; at 2 x 1e4 lost in 15 of 20 | lost 2.0 M | lost 0.69-2.39 M (Volatile.Read/Write, not atomic) |
| N threads bump a TLS counter, main checks each result, distinct addresses, its own copy | exact with a barrier (0 of 80+ failed); **without the barrier 1 of 20 (-O2) and the first -O0 run failed** | exact (0 of 40) | exact |
| two threads pass a token 1e6 times through a seq_cst atomic | exact | exact | (not run) |

Rules that follow for `M3/08-atomics` and `M3/09-threads`:
1. Threads never print; main prints after joining, so the transcript is the native one on every runtime.
2. The result is a function of the input only: totals, per-thread results stored in an array and printed in
   index order, counts of rounds; never timing or order of completion.
3. Every cross-thread comparison of addresses or identities happens while all threads are alive (a barrier on an
   atomic counter before exit): the runtime reuses an exited thread's TLS block (measured above).
4. Sensitivity is part of the test: sizes such that a wrong lowering (a plain load-add-store instead of
   `atomicrmw`) fails with near certainty (4 x 1e6: lost updates in every run on every runtime). Memory-order
   litmus tests (atomics-threads' `sb.c`) have the same shape: a correct lowering prints zero forbidden outcomes on
   every run, and the round count is chosen so that the wrong mapping (`volatile. stind` without the trailing
   fence: 6,463-80,852 forbidden outcomes per 2 M rounds, atomics-threads 5.1) fails every run; the test prints
   only the count of forbidden outcomes, never the distribution of allowed ones.
5. Spin loops only on atomics, with an iteration bound that turns a hang into a failure (`exit(3)` after, for
   example, 1e9 spins), well under `msil_timeout`.
6. A `msil_stress=<n>` parameter (feature `msil-stress`) repeats the run of tests that `REQUIRES: msil-threads`
   n times in a `stress` leg of the phase gate (n = 20), so rare interleavings get a chance outside the standard
   gate; the standard gate runs each once.
7. The callbacks recon's GC-churn leg (a C# runner that allocates on another thread while the program runs)
   joins the phase gate for `M3/06-callbacks` and `M3/09-threads` (`msil_gc_churn=1`).

### 4.7 Hosted-libc programs (coordinates with the hosted-libc recon)

P3-B's exit names zlib (full), Lua and SQLite. The hosted-libc recon (key `hosted-libc`, in progress when this
report was written) fetches them, catalogues every failure and recommends pins; this report fixes how they enter
the tests:
- Each becomes a `manifest.json` v1 entry (pinned by commit or archive hash and tree hash, with licence: zlib
  licence, MIT for Lua, public domain for SQLite) fetched by `fetch.py` into the cache; never vendored.
- `msil/test/Programs/<name>/` with a driver and a hosted `.test` (`%{msil_hosted_cflags}`, real headers, no
  shim), built as archives + program like the Phase 2 programs; exit criteria per program: zlib's `example` and
  `minigzip` round trips; Lua's own test scripts that need no `os`/`io` beyond files in the private directory;
  SQLite's shell or a driver running a fixed SQL script (`SQLITE_THREADSAFE=0` first, threadsafe after the
  threads milestone).
- Legs: `programs`/`programs-lto` and the WSL twins (both triples, every runtime); `REDEFINE: %{msil_limits}` for
  SQLite (3.5); `det` tests for each.
- CI: zlib, Lua and SQLite licences permit CI use, unlike CoreMark and c-testsuite (section 10 item 11 is not
  needed for them).

### 4.8 The Rust experiment (HANDOFF issue 15) as a Phase 3 measure, not a gate leg

`C:\llvm-msil-scratch\rust-exp\std\RUN.sh --bs` (reproduced independently by `std-repro\REPRO.sh`; `RUN.sh --bs`
took 16.5 minutes on the loaded host) builds nine Rust `std` programs (`v0_empty` ... `v8_exit`) through the S
and B pipelines on both triples, runs them on Framework 4.8, .NET 8 and WSL, verifies them, and writes a table
whose last columns count the Phase 3 stand-ins and diagnostics per program: for example `S-win v6_thread`:
`inline:15, thread-local:6, intrinsic:2`, `undefined:1, function:1` (no PE); `S-lin v8_exit`: `variadic:25,
intrinsic:3, thread-local:2`. Those counts are exactly what P3-A (TLS, the x86 scalar fallbacks of Design.md 3.2)
and P3-B (variadics, thunks, atomics) remove.

- It cannot be a lit leg: the Rust toolchain (`rust-exp\toolchain`) is not a project tool, and the scope rules
  keep the repository to the compiler tools.
- Recommended use: re-run `RUN.sh --bs` at the P3-A checkpoint and at the P3-B exit, record the table's
  diagnostic columns in Status.md next to the corpus results, and treat a count that grows as a finding. The
  callbacks recon's stretch goal (`thread::spawn` + `join` on both triples, 5.6 item 10) and the atomics recon's
  "`v6_thread` without `lower-atomic`" are the P3-B exit's Rust lines.
- In-tree FileCheck tests take the shapes it found, without Rust: `llvm.x86.sse2.pause` (Rust's `spin_loop`), the
  MSVC `__fastfail` inline asm (`int 0x29`), and x86 intrinsics in dead dispatch paths, each in
  `llvm/test/CodeGen/MSIL/` once helpers-switches fixes their lowering (scalar fallback, diagnostic or
  `Environment.FailFast`).

---

## 5. Linux-triple coverage and CI

### 5.1 Coverage the phase gate leaves to CI today, and the fix

| Gap (plan row 'Phase 3 input (Linux-triple coverage ...)') | Proposal | Cost (gate 4 load / quiet) |
|---|---|---|
| c-testsuite never runs for the Linux triple in the phase gate | `ctest-wsl` leg: c-testsuite through objects, WSL .NET 10, E7 required, `msil_also_run=8`, corpus parameters; from the variadics milestone also the second list (E7h) | `ctest` is 10.7 min (4,897 test-s); WSL composites cost more launches: 12-20 min loaded, 5-8 quiet (with one `wsl.exe` per level, 3.4, about half) |
| `lto-wsl` lacks `M2/01-archive`, `03-native`, `04-shared` (struct-abi.test), `05-refs`, `07-equiv` | add them | those directories are 4,002 of the Windows `lto` leg's 8,846 test-s: `lto-wsl` +85% (10.4 -> about 19 min loaded; +5-8 quiet) |
| no leg runs c-testsuite under lto, on .NET 10, TC=0 or Framework | `ctest` gets `msil_run_netfx=1 msil_also_run=10` in the phase gate; a `ctest-lto` leg only at the Phase 3 exit | +40% of `ctest` |

Both belong in the phase gate (and the post-commit run), not CI only: CI's first dispatch is a user action (Q38)
and c-testsuite's CI licence is open (item 11), so CI cannot be the record.

### 5.2 CI for Phase 3

- **First dispatch** (Q38, user): needed before Phase 3 adds legs, because both jobs are already estimated at
  255-350 minutes cold against `timeout-minutes: 330` (plan row 'CI input'). Record each job's time per step.
  The first half of Q38 is already done: `git ls-remote --symref origin HEAD` gives `ref: refs/heads/msil`
  (checked in this session, as the backlog recon also found), so only the dispatch remains, a user action.
- **If a job nears 330 minutes**, split by artifact, not by rebuilding: a `build-*` job uploads `build/bin` plus
  the files lit needs (lit configs, `tools/msil`) as an artifact; test jobs download it and run disjoint legs
  (`lit-legs.ps1 -Leg ...`); `det-manifest.py build` runs right after the build. lit's `--num-shards/--run-shard`
  can split one leg (net8) over two jobs. Phase 3 adds `M3` (in `net8`), the second ctest list and the hosted
  programs: about +30-40% of today's test time by the Phase 3 exit (section 6.4).
- **Licences:** while `MSIL_CORPUS_SKIP` names c-testsuite, `ctest`, `ctest-wsl` and the second list are SKIP
  rows in CI; zlib, Lua and SQLite need no skip.
- The Linux job runs the hosted tests natively (system glibc, apt clang as the oracle; `msil_glibc_sysroot=/`),
  the most faithful glibc run Phase 3 can have; the Windows job runs the UCRT side.

### 5.3 The matrix checker

1. `lit-legs.ps1 -Describe` and `lit-legs.sh --describe` print every leg as JSON (name, tag, paths, the
   resolved pipeline, triple, runner and `msil_also_run`); `check-matrix.py --legs <json>` requires, for each
   line, every (pipeline, triple) pair to be run by some leg of the default `-Wsl` set whose paths contain the
   test. `matrix-coverage.test` runs it with the describe output of both scripts.
2. A `diag:` line requires one FileCheck of the text per pipeline **and level**.
3. The 5.9.16 row list moves into `matrix.txt` itself (a `# row:` header block, or `rows.txt` next to it) and
   the plan's 5.9.16 table points to it; `ROWS_5916`/`M20_ROWS` go.
4. Phase 3 vocabulary: a milestones column naming the implementing Phase 3 milestone (`M2x`) after the flip; a
   `native` column kind `transcript (hand-written)` for rows whose native build cannot give the transcript
   (p6-dtors on Windows, crt-xt), each with a comment naming the source.

---

## 6. Concrete proposals

### 6.1 lit parameters, features and substitutions

| Name | Values | Feature | Meaning |
|---|---|---|---|
| `msil_hosted` (derived: the triple's headers are found) | | `msil-hosted` | Linux: `msil_glibc_sysroot` has `usr/include/stdio.h`; Windows: `INCLUDE` has `corecrt.h` (today's `msil-ucrt-headers`) |
| `%{msil_hosted_cflags}` | Linux `--sysroot=<glibc sysroot>`; Windows empty | | hosted compile flags for both builds; M3 directories set `%{msil_cflags}` to `-std=gnu17 -Werror=return-type -I<Inputs> %{msil_hosted_cflags}` (no `-ffreestanding`) |
| E7h oracle | | `msil-linux-hosted-oracle` | 4.5; `msil-prog.py --mode native --hosted` |
| `msil_switches` | comma list of codegen switch names | `msil-switch=<name>` | added to every compile and link (`-mmsil-<switch>` / `-Wl,--<switch>`, spelling from helpers-switches), with `msil_references` set to a pack that has the members |
| `msil_stress` | 0 / n | `msil-stress` | repeat the runs of `REQUIRES: msil-threads` tests n times |
| `msil_gc_churn` | 0 / 1 | `msil-gc-churn` | run programs under the GC-churn runner (callbacks recon 5.6 item 8) |
| `%{msil_limits}` | default from `msil_timeout`, `msil_mem_limit_mb` | | per-test override (3.5) |
| `msil_probe_cache` | path / empty | | 3.4 |
| `msil-threads` (feature) | | `msil-threads` | the triple and runner can run threaded C (always true from the threads milestone; lets stress and churn legs select tests) |

`update-expected.py`: `--hosted`, `--opt` both levels in one run (`--levels O0,O2`), the E7h link.
`msil_ctest_format.py`: the second list's entry kinds (4.2).

### 6.2 Legs (`lit-legs.ps1` and `lit-legs.sh`)

| Leg | Tag | Paths | Parameters | Purpose |
|---|---|---|---|---|
| `net8-core` | Quick (standard gate) | `$checkMsil` minus harness/M3/Programs/spill-limits | obj, .NET 8, Framework, `msil_equiv=1`, csc | G3 |
| `p3-full` | Quick | `msil/test/M3` | net8's full set | G3b |
| `harness` | Quick | `msil/test/harness` | lean | G3c |
| `wsl-p3` | Quick (WSL) | `M3`, `M2/08-semantics`, `M1/11-programs` | WSL .NET 10, E7/E7h, `msil_also_run=8` (M3) | G4 |
| `net8` | default | unchanged (M3 joins through `msil/test`) | unchanged | phase gate, post-commit |
| `wsl-net10` | Wsl | + `msil/test/M3` | unchanged | phase gate |
| `ctest-wsl` | Wsl | c-testsuite (both lists from variadics) | obj, WSL .NET 10, E7(h), `msil_also_run=8` | 5.1 |
| `lto-wsl` | Wsl | + `01-archive`, `03-native`, `04-shared`, `05-refs`, `07-equiv`, `M3` | unchanged | 5.1 |
| `switches`, `switches-wsl` | default / Wsl | `M3`, `M1/11-programs`, `M2/04-shared` | `msil_switches=<all modern switches>`, `msil_references=net8`, .NET 8 and `msil_also_run=10` (no Framework) | switch-on code paths |
| `stress` | Full + phase exit | tests with `msil-threads` | `msil_stress=20`, `msil_also_run=10,8+tc0` | 4.6 |
| `gc-churn` | Full + phase exit | `M3/06-callbacks`, `M3/09-threads` | `msil_gc_churn=1` | callbacks 5.6 |
| `programs-hosted` | (programs legs) | zlib full, Lua, SQLite | as `programs` | 4.7 |

### 6.3 Gate steps per Phase 3 milestone area

The brief orders the milestones; the gate additions per area (on top of section 3.2's standard steps):

| Area | Extra gate steps (in the budget) | After the commit |
|---|---|---|
| First Phase 3 milestone (shared plumbing, upstream hooks, harness for Phase 3) | full upstream-hooks; the harness leg (G3c); the new harness self-tests (hosted mode, E7h, probe cache, `%{msil_limits}`, the timeout retry in lit-legs, `-Describe`, the second ctest list's format on a synthetic tree); `il-snapshot.py --compare` over `msil/test/M1` and `llvm/test/CodeGen/MSIL` (the hooks must not change IL) | full post-commit run (3.3) |
| Helpers and switches | `M3/03-helpers`, `M3/04-switches` in G3b; `switches` leg on `M3/04-switches` only; `check-mscorlib-safe.py`; `il-snapshot.py --compare` with every switch off (defaults must not change IL) | `switches`, `switches-wsl` |
| Frames and TLS | G3b with `M3/00-frames`, `M3/01-tls`; `p5-tls` and the TLS matrix rows in G4; `lld/test/MSIL` with `RUN_LLD_MAIN_TWICE=1` (new linker state); `spill-limits` when the trailer changes | `stress` (TLS threads come later) |
| Lifecycle | `M3/02-lifecycle`, `p6-dtors`, `fini-array`, `crt-xt`, `atexit-order`, `09-hosted/glibc-atexit.test` on both triples; the deadlock regressions (each with its own timeout) | Framework ProcessExit runs on every runtime |
| Variadics | `M3/05-variadics`; `variadic-def-xtu`; `ctest` with 00140 plain (about 11 min loaded; obj, .NET 8 only, -O2 only for the gate if over budget) | `ctest`, `ctest-wsl`, the second list |
| Callbacks | `M3/06-callbacks`, `native-callback`; `--uco-thunks` variants on .NET 8 only | `gc-churn`, `switches` |
| Atomics and threads | `M3/08-atomics`, `M3/09-threads`, `tls-threads`; each threads test once | `stress` (n = 20) |
| setjmp/longjmp | `M3/07-sjlj`, `sjlj-xasm` | |
| Hosted-libc programs | `programs` + `programs-wsl` for the program the milestone enables (zlib full, Lua, SQLite), -O2 only in the gate | both levels, LTO, every runtime |

### 6.4 The Phase 3 exit gate

`run-m1-gate.ps1 -CorpusCache` with every default and `-Wsl` leg (M3 inside `net8`, `wsl-net10`, `lto`,
`lto-wsl`), `ctest` and `ctest-wsl` with both lists, `programs*` with the hosted programs, `switches` and
`switches-wsl`, `stress`, `gc-churn`, determinism (det-manifest.py over M1 and M3), every upstream-hooks leg and
ci-sh; the matrix complete with every P3 row a transcript; the plan's exit criteria per program. Its recorded time
is the measured one (Q32 amended): M23's 184-339 minutes plus roughly 30-40% (M3 at about 80-100 composites
across the full legs, the second ctest list, three hosted programs, the new legs), about 4-8 hours on the loaded
host, about 1.5-2.5 hours quiet; with section 3.4's harness work about 25-35% less.

---

## 7. Decisions

| # | Question | Options | Recommendation | Evidence | User? |
|---|---|---|---|---|---|
| D1 | Q32 for Phase 3 milestone gates: narrow the standard step or raise the budget? | (a) narrow G3 to `net8-core` + `p3-full` + conditional harness, rest post-commit; (b) keep today's net8 and raise the budget to 60-90 min; (c) keep net8 and run the standard gate only on a quiet host | (a), with the budget judged on load-normalized minutes (G2c) | net8 is 2.0x a lean composite in CPU (2.4); `net8-core` projects to 50% of net8's test time; M23's runs were 2.7-5.4x quiet | yes |
| D2 | How to judge gate time on a shared host | (a) raw minutes; (b) raw / calibration factor from one composite at gate start; (c) re-run on a quiet host | (b) | M23 legs varied 2-3x between runs of the same tree (net8 43.9-86.9, wsl-net10 35.6-109.2) | yes (with D1) |
| D3 | Tool timeouts in every gate | (a) the agent rule in plan text only; (b) automated retry in lit-legs (alone, `-j1`, at most 3) plus the plan text; (c) raise `msil_timeout` | (b), plus `%{msil_limits}` per test | PEVerify 0.08-0.24 s vs a 60 s timeout under load; `wsl.exe` latency 1.86 s at concurrency 8 | no |
| D4 | Where `msil_also_run` and the native oracle run | (a) every gate; (b) post-commit and phase gate for M1/M2, every gate for M3 | (b) | together 36% of a net8 composite; `expected` checks the oracle side; M3 keeps every runtime | no |
| D5 | Harness self-tests in milestone gates | (a) always; (b) when harness files change (G6 decides) | (b) | 20.9% of net8's test time; 132 nested lit runs | no |
| D6 | Harness cost work (probe cache, one Python per level, one `wsl.exe` per level) | (a) in the first Phase 3 milestone; (b) later; (c) never | (a): it pays for every later gate and the 4-8 hour exit gate | 2.3, 2.5, 2.6 | no |
| D7 | c-testsuite's 70 printing tests | (a) second list with `.c.expected`, native-transcript and closed-vocabulary exclusions; (b) per-OS expected files in the repo; (c) leave out | (a) | 4.2; nothing vendored, no XFAIL | no |
| D8 | Linux-triple coverage | (a) `ctest-wsl` and wider `lto-wsl` in the phase gate; (b) CI's Linux job as the record | (a) | CI not dispatched since M23, c-testsuite skipped in CI | no |
| D9 | Hosted Linux oracle | (a) E7h (real crt1 + `libc_nonshared.a` from the glibc sysroot); (b) extend `start.c` (fini_array, link libc_nonshared.a); (c) hand-written transcripts | (a) for M3 and the P3 rows; E7 stays for M1/M2 | 4.5, measured in WSL | no |
| D10 | Thread tests | (a) deterministic shapes + barrier rule + `msil_stress` leg in the phase gate; (b) stress in every gate; (c) no thread tests until Phase 4 | (a) | 4.6 | no |
| D11 | CI licences (section 10 item 11) | allow CoreMark/c-testsuite in CI, or not | the user's; without it c-testsuite has no CI record and `ctest-wsl` is the only Linux run | | yes |
| D12 | CI first dispatch (Q38) | dispatch once now; or after Phase 3 adds legs | now, before Phase 3's legs; split jobs by artifact if near 330 min | plan row 'CI input'; the default branch is already `msil` (5.2) | yes |
| D13 | The Rust std experiment in Phase 3 | (a) a recorded measure at the P3-A checkpoint and the P3-B exit (4.8); (b) a lit leg; (c) ignore | (a) | the toolchain is outside the scope rules; its table counts exactly the Phase 3 stand-ins (4.8) | no |
| D14 | Execution tests of `abort` and of 16/24-byte atomics | (a) `abort` on Linux only with a Windows link-level check; 16/24-byte atomics against a hand-checked `.expected`; (b) Windows `abort` runs with a long timeout; (c) skip | (a) | `__fastfail` bypasses msil-run.py's `SetErrorMode` (WER delay of minutes, lifecycle recon); no native oracle links 16-byte atomics (no libatomic in either sysroot, 4.3) | no |

---

## 8. Effects on the later goals

- **Portable triple** (`msil64-unknown-dotnet`): the harness assumes one triple per lit run and a native
  oracle per triple; the portable triple has no native oracle, so its transcripts must come from the host-libc
  triples' `.expected` files (the same C semantics with LP64). Keep `.expected` triple-neutral where possible
  (the M3 tests print no `long`/`size_t` sizes), and keep `msil-prog.py --mode native` optional per leg (the
  oracle is already a parameter): eases it. The E7h oracle and hosted mode are host-libc-specific and do not
  preclude it.
- **Managed calls / inline IL:** tests of those need C# consumers and providers; `%msil_csc` and `%{msil_shlibs}`
  exist; nothing here precludes them.
- **FeatureIdeas** (multi-triple assemblies): a per-triple leg structure is a harness assumption only.
- **Final cleanup:** leg and parameter names here carry no milestone numbers (`net8-core`, `p3-full` is the only
  phase-named leg; rename it `m3` at cleanup).

## 9. Risks

1. The quiet-host model rests on two anchors (M23.d's 19.5 s composite, 2026-10-26's quiet legs) and CPU ratios
   measured under load; HT contention may inflate CPU-s under load by up to ~1.3x uniformly, which keeps ratios
   but not absolute CPU. Check the model with one quiet run of `net8-core` before the first Phase 3 gate.
2. Thread tests can hang under load (spin loops); every spin needs a bound (4.6 rule 5) and the leg's timeout
   retry must never mask a deterministic hang (it times out twice).
3. Hosted tests depend on the WSL distribution's glibc (2.43 today): an `apt upgrade` changes the oracle; pin the
   glibc sysroot packages (make-glibc-sysroot.sh already records versions) and report the version in the lit
   note.
4. The post-commit run takes 3-6 hours on the shared host; overlapping it with the next milestone's builds slows
   both. Run it while the next milestone's WPs write code (no builds), as M12-M20 did.
5. The second ctest list depends on the variadics recon's decision V10 (definitions of the UCRT's inline stdio
   functions) for six Windows entries.

## 10. Work packages (disjoint files; rough sizes)

1. **Gate tooling** (~500 lines; `msil/utils/ci/lit-legs.ps1`, `lit-legs.sh`, `ci-common.*`): legs `net8-core`,
   `p3-full`, `harness`, `wsl-p3`, `ctest-wsl`, widened `lto-wsl`, `switches*`, `stress`, `gc-churn`; the
   automatic timeout retry; `-Describe`/`--describe`; the calibration step; harness self-tests under
   `msil/test/harness/legs/`.
2. **Harness cost** (~800; `msil/test/lit.cfg.py`, `msil/test/harness/lit.local.cfg`, `msil/utils/msil-run.py`,
   `msil-verify.py`, `msil-jitall.py`, new `msil-steps.py`): probe cache, one Python per level, one `wsl.exe` per
   level, `%{msil_limits}`.
3. **Hosted mode and oracles** (~600; `lit.cfg.py` hosted block, `msil-prog.py`, `update-expected.py`,
   `make-linux-sysroot.py` or a new `make-e7h-sysroot` step): `%{msil_hosted_cflags}`, E7h, the Windows /MD
   oracle, `--hosted` in update-expected.py.
4. **c-testsuite second list** (~400; `msil/test/Programs/c-testsuite/*`, `harness/ctest-lists.test`).
5. **Matrix checker** (~300; `msil/test/M2/08-semantics/Inputs/check-matrix.py`, `matrix.txt`,
   `matrix-coverage.test`).
6. **M3 skeleton and per-feature tests**: one test WP per feature milestone (`msil/test/M3/<NN>-<area>/`), from
   the feature recons' test sections.
7. **Corpus additions** (~300; `msil/utils/corpus/manifest.json`, `msil/test/Programs/<zlib-full|lua|sqlite>/`),
   after the hosted-libc recon's pins.
8. **CI** (~150; `.github/workflows/msil-ci.yml`): the new legs, the corpus skip semantics for the second list,
   the artifact split if the first dispatch shows it is needed.

## 11. Experiments index (`C:\llvm-msil-scratch\p3\tests-gates-p3\`; copies in tests-gates-p3-files/)

| # | What | Result file |
|---|---|---|
| E1 | per-directory test-seconds of gate 4's legs and of 2026-10-26's quieter legs (xunit) | `results/xunit-*.txt` |
| E2 | one net8 composite in a job object: 99 processes, 18.0 CPU-s; config alone 11 / 4.2 | `results/profile-sets.txt` |
| E3 | the composite's 30 commands replayed one by one | `results/composite-replay-net8-loops.txt` |
| E4 | parameter sets on a 4-test sample (minimal, lean, +equiv, +oracle, +also_run, full) | `results/profile-sets.txt` |
| E5 | lit config files opened per process (audit hook): once, main process only | section 2.5 |
| E6 | process start-up: python, msil-run.py import, dotnet, wsl.exe | section 2.3, 2.6 |
| E7 | wsl.exe launch scaling at concurrency 1-8 | `scripts/wsl_concurrency.py`, section 2.6 |
| E8 | c-testsuite classification (rule, calls, `.expected`) and IR scan of all 220 tests | `results/ctest-classify.txt`, `results/ctest-irscan-linux.txt` |
| E9 | deterministic thread shapes, native (Windows, Linux) and CLR (Framework, .NET 8/10, WSL 8/10) | `threads/`, `results/threads-results.txt` |
| E10 | E7h hosted Linux oracle (real crt1, libc_nonshared.a) with atexit, a destructor and pthreads | `threads/atexit.c`, `results/threads-results.txt` |
| E11 | FileCheck directory cost, the 20 slowest files of each directory at `-j1` (an upper bound: CodeGen 2,360 processes, 102 CPU-s; lld 1,783, 102) | `results/profile-sets.txt` |
| E12 | one WSL composite, full (`wsl-net10`'s parameters) and lean: commands, `wsl.exe` launches, processes, CPU | `results/wsl-composite.txt`, `scripts/batch4.sh` |
| E13 | one asm .NET 10 composite (the shape of the quiet 2026-10-26 legs) | `results/wsl-composite.txt` |
| E14 | the WSL lit configuration alone | `results/wsl-composite.txt` |
| E15 | appendix A's table, generated from E8's data and the variadics recon's 8.4 | `scripts/ctest_appendix.py` |

---

## Appendix A. c-testsuite's 70 excluded tests, one by one

Why each test is excluded today: `msil/test/Programs/c-testsuite/libc-free.txt` keeps the tests of
`tests/single-exec` (tree `c-testsuite-5c7275656d75`) whose source matches none of the C library calls of its
`# rule:` line (tests-p2 2.4's `ctest.py` rule); the 70 below each match at least one (column 2). Column 3 is the
line count of the cached `<name>.c.expected` (0 = the test prints nothing and only its exit status counts).
Column 4 lists the Phase 3 constructs in the test's IR for `x86_64-unknown-linux-gnu-msil` (glibc sysroot
headers; `vararg-call` = a call of a native variadic function, `vararg-def` = a variadic definition; a level
missing means the optimizer removed the construct, for example `printf("...
")` becoming `puts` at -O2).
Column 5 is the entry proposed for the second list (4.2) with the result the variadics recon's prototype measured
(its 8.4, .NET 8, -O0 and -O2, both triples): "pass" means equal to `.c.expected` on both triples.

| Test | Rule matches (why excluded today) | `.c.expected` lines | IR, linux-gnu (-O0/-O2) | Phase 3 entry (prototype result) | Note |
|---|---|---|---|---|---|
| 00025 | `strlen` | 0 | none | pass (no variadics; empty expected) | libc call `strlen`; binds since M15 |
| 00040 | `calloc` | 0 | none | pass (no variadics; empty expected) | libc call `calloc`; binds since M15 |
| 00056 | `printf` | 3 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00125 | `printf` | 1 | vararg-call O0 | pass (both triples, equal to `.c.expected`) |  |
| 00131 | `printf` | 5 | vararg-call O0 | pass (both triples, equal to `.c.expected`) |  |
| 00132 | `printf` | 15 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00154 | `printf` | 6 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00156 | `printf` | 10 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00157 | `printf` | 10 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00158 | `printf` | 8 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00159 | `printf` | 4 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00160 | `printf` | 11 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00161 | `printf` | 11 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00163 | `printf` | 8 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00164 | `printf` | 15 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00165 | `printf` | 2 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00166 | `printf` | 4 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00167 | `printf` | 2 | vararg-call O0 | pass (both triples, equal to `.c.expected`) |  |
| 00168 | `printf` | 10 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00169 | `printf` | 18 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00170 | `printf` | 4 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00171 | `printf` | 3 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00172 | `printf` | 6 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00173 | `printf` | 7 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00174 | `printf` | 16 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00175 | `printf` | 12 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00176 | `printf` | 2 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00177 | `printf` | 8 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00178 | `printf` | 4 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00179 | `memcmp`, `printf`, `strcat`, `strcmp`, `strcpy`, `strlen`, `strncmp`, `strncpy` | 19 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00180 | `printf`, `strcpy` | 1 | vararg-call O0 | pass (both triples, equal to `.c.expected`) |  |
| 00181 | `printf` | 71 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00182 | `fprintf`, `printf` | 4 | vararg-call O0 | pass (both triples, equal to `.c.expected`) |  |
| 00183 | `printf` | 10 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00184 | `printf` | 2 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00185 | `printf` | 20 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00186 | `printf`, `sprintf` | 20 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00187 | `fopen`, `printf` | 27 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00188 | `printf` | 6 | vararg-call O0 | pass (both triples, equal to `.c.expected`) |  |
| 00189 | `printf` | 2 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00190 | `printf` | 1 | vararg-call O0 | pass (both triples, equal to `.c.expected`) |  |
| 00191 | `printf` | 1 | vararg-call O0 | pass (both triples, equal to `.c.expected`) |  |
| 00192 | `printf` | 10 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00193 | `printf` | 4 | vararg-call O0 | pass (both triples, equal to `.c.expected`) |  |
| 00194 | `printf` | 1 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00195 | `printf` | 1 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00196 | `printf` | 20 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00197 | `printf` | 8 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00198 | `printf` | 9 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00199 | `printf` | 8 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00200 | `printf` | 1 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00201 | `printf` | 1 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00202 | `printf` | 2 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00203 | `printf` | 1 | vararg-call O0 | pass (both triples, equal to `.c.expected`) |  |
| 00204 | `printf`, `va_start` | 174 | va_start O0/O2, vararg-call O0/O2, vararg-def O0/O2 | linux-gnu: `exclude linux-gnu long-double`; windows: pass | defines variadic functions, `va_start`; `%Lf` through glibc `vprintf` prints `nan` in 20 of 174 lines |
| 00205 | `printf` | 72 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00206 | `printf` | 5 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00207 | `printf` | 6 | dyn-alloca O0, stacksave O0, vararg-call O0/O2 | `diag` at -O0 until P3-A dynamic alloca (both triples); -O2 pass | VLA (`dynamic alloca` + `stacksave` at -O0) |
| 00208 | `printf` | 0 | vararg-call O0 | pass (empty `.c.expected`) |  |
| 00210 | `printf` | 2 | vararg-call O0/O2 | windows: `diag` until V10 (UCRT exports no `printf`); linux pass | declares `printf` itself |
| 00211 | `printf` | 1 | vararg-call O0/O2 | windows: `diag` until V10; linux pass | declares `printf` itself |
| 00212 | `printf` | 1 | vararg-call O0 | windows: `native windows-msvc` (expected assumes LP64); linux pass |  |
| 00213 | `printf` | 23 | vararg-call O0/O2 | windows: `diag` until V10; linux pass | declares `printf` itself |
| 00214 | `printf` | 2 | vararg-call O0 | windows: `exclude windows-msvc not-portable`; linux pass | `typedef unsigned long size_t` (no native Windows build either) |
| 00215 | `printf` | 14 | vararg-call O0/O2 | windows: `diag` until V10; linux pass | declares `printf` itself |
| 00216 | `printf` | 43 | vararg-call O0/O2 | windows: `native windows-msvc` (GCC bit-field layout); linux pass |  |
| 00217 | `printf` | 1 | vararg-call O0/O2 | windows: `diag` until V10; linux pass | declares `printf` itself |
| 00218 | `printf` | 0 | vararg-call O0 | windows: `native windows-msvc`, and `diag` at -O0 until V10; linux pass | declares `printf` itself; empty `.c.expected` |
| 00219 | `printf` | 14 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |
| 00220 | `printf` | 1 | vararg-call O0/O2 | pass (both triples, equal to `.c.expected`) |  |

Totals (prototype, from the variadics recon's 8.4): linux-gnu 68 of 70 equal to `.c.expected` at -O0 and 69 at
-O2 (00204's `long double`; 00207's VLA at -O0); windows-msvc 60 and 61, plus 00212, 00216 (and 00218 at -O2) equal
to the native build, six tests that declare `printf` themselves waiting for decision V10 (five at -O2), 00207 at
-O0 and 00214 not portable. After P3-A's dynamic alloca and V10, 138 of the 140 (test, triple) pairs are
expected to pass: 135 against `.c.expected` and 3 (00212, 00216, 00218 on windows-msvc) against the native
transcript; the other 2 are the named exclusions (00204 on linux-gnu, 00214 on windows-msvc).

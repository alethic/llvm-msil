# lit-legs.ps1 - the test legs of the MSIL CI (Windows job), of the phase gate and of the
# milestone gates: the test-leg step of msil-ci.yml and of msil/utils/run-m1-gate.ps1, and the
# lit steps of the standard milestone gate (plan 5.17.7).
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1             # the Windows CI legs
#   pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1 -Wsl        # and the WSL legs (dev host)
#   pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1 -Wsl -Exit  # and the -Exit legs
#   pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1 -Full       # and the nightly legs
#   pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1 -Wsl -Exit -Portable  # and the portable legs
#   pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1 -Leg programs,ctest
#   pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1 -Wsl -Leg wsl-p3 -Run 1
#   pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1 -List
#   pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1 -Describe   # every leg as JSON; runs nothing
#
# The leg set of the Phase 2 exit (plan 5.12.10, 5.12.11; M23) with Phase 3's changes (plan
# 5.17.6; M24). CIL objects are the default pipeline (msil_pipeline=obj, msil_link_output=pe;
# Q31), so every Phase 1 leg runs through clang -c and msil-ld, and -S plus ilasm is a test oracle
# only (asm-oracle, modern-ilasm, wsl-ilasm, the reference side of O3 and O4, the round trip). One
# verified build serves several runtimes: msil_also_run repeats jitall and the run of every
# composite on further .NET runtimes against the same expected file. Tags: none (the default set),
# -Wsl, -Quick (runs only when -Leg names it), -Full and, from M24, -Exit (the legs that
# 'lit-legs.ps1 -Exit' adds: every post-commit run and the Phase 3 exit gate run them, plan
# 5.17.11; an -Exit leg whose name ends in '-wsl' also needs -Wsl). M26 adds the first, ctest-lto;
# M28 adds gc-churn and gc-churn-wsl, M30 stress and stress-wsl. P-1a (plan 5.18.10 PX4) adds the
# tag -Portable: the legs that 'lit-legs.ps1 -Portable' adds, portable-smoke and
# portable-smoke-wsl, which the post-commit runs run (run-m1-gate.ps1 -Portable); like an -Exit
# leg, the one whose name ends in '-wsl' also needs -Wsl. A portable leg is never tagged -Exit, so
# -Exit alone selects neither, and M35's gate, exit-report.py and check-matrix.py (whose phase tags
# are '', Wsl and Exit) see no change.
#
# Gate steps. The standard milestone gate of Phase 3 (plan 5.17.7) runs '-Leg net-core' (G3),
# '-Leg p3-full' (G3b), '-Leg harness' (G3c, only when 'python msil/utils/ci/gate-select.py
# harness' prints 'harness: run'), '-Wsl -Leg wsl-p3' (G4; '-Run 1' once its run 2 has moved to the
# post-commit run) and, in G6, check-matrix.py --legs on the output of -Describe; a milestone that
# changes the trailer, the spill classes or a type's member limit adds '-Leg scale'. run-m1-gate.ps1
# (the post-commit run after every Phase 3 milestone and the phase gates, plan 5.17.11) runs every
# leg below except the -Quick and -Full ones, -Wsl ones included, and with -Exit the -Exit legs;
# M23 gate step 12 and M24 gate step 26 check this list.
#
# Legs, in order, with the minutes the two phase-gate runs of M23 took on the development host
# (gate-1 and gate-2 of 2026-10-27, first and second figure; the 4-core host was shared with other
# work, so the spread is load, not the legs), before M24 added msil/test/M3 and the other changes
# that the descriptions name; for the legs that M24 adds, plan 5.17.7's quiet estimate. Together
# the Windows legs took 144 and 217 minutes and the -Wsl legs 79 and 117, against plan M23's
# estimate of 53 and 22 (Rebuild cost): the composites under obj with the equivalence oracles and
# the msil_also_run runtimes cost several times the pre-M23 legs' asm composites per test. The user
# accepted the measured time in place of section 10's Q32 budget of 70-90 minutes for the whole
# gate (plan section 12, row 'Q32 amended').
#   net8           every MSIL FileCheck directory (llvm/test/CodeGen/MSIL, clang/test/CodeGen/MSIL,
#                  the clang driver tests, lld/test/MSIL, llvm/test/MC/MSIL, the llvm-cil-dump and
#                  llvm-cil-copy tests, and from M26 clang/test/Sema/msil-long-double-format.c and
#                  llvm/test/Transforms/ExpandVariadics/msil-abi.ll) and msil/test (M1, M2, M3
#                  and the harness; msil/test/Programs
#                  is filtered out: the corpus legs run it) under obj on .NET 8 (tiered) and on the
#                  .NET Framework 4.8, -O0 and -O2, native oracle required (with the VS clang's
#                  compiler-rt builtins), msil_equiv=1 (O3 on the --no-gc-sections equivalence link
#                  and O6), msil_also_run=10,8+tc0 (.NET 10, and .NET 8 with
#                  DOTNET_TieredCompilation=0, on the same verified images), msil_scale_tests=1
#                  (msil/test/M2/00-link/spill-limits.test at the real CLR limits; besides net8 only
#                  the -Quick leg scale sets it, plan 5.12.2, 5.17.6) and msil_require_csc=1 (the C#
#                  consumer of msil/test/M2/04-shared and harness/csc.test)       (60, 87)
#   more-levels    msil/test/M1 under obj on .NET 8 at O1, O3, Os and Oz          (15.5, 15.5)
#   asm-oracle     msil/test/M1 and msil/test/harness/pipeline under asm (clang -S or llc text,
#                  the .NET Framework ilasm), .NET 8, native oracle required, msil_roundtrip=1
#                  (ildasm -> ilasm of every module, Design.md 5.5); the FileCheck directories do
#                  not depend on the pipeline, and M2 links objects under asm too (19, 22.5)
#   modern-ilasm   msil/test/M1/11-programs under asm, assembled by the .NET ilasm (win-x64) at
#                  MsilILAsmVersion, not the Framework's                          (1.5, 3.5)
#   roundtrip      msil/test/M1/11-programs, msil/test/M2/07-equiv and
#                  msil/test/M2/04-shared/roundtrip-library.test under obj, msil_roundtrip=1: O2 on
#                  msil-ld's outputs (ildasm -> ilasm -> ildasm, listings equal, rerun; a -shared
#                  library and a program relinked against its reassembly)         (5, 5.5)
#   expected       update-expected.py --check --levels O0,O2 msil/test/M1 msil/test/M2
#                  msil/test/M3: .expected vs the Windows native oracle at -O0 and -O2 (M23 checked
#                  -O0 alone), with the compiler-rt builtins (M2's %{msil_prog} tests built by
#                  msil-prog.py --mode native), hosted mode as lit decides it (the DLL CRT oracle
#                  in msil/test/M3)                                               (4.5, 5.5)
#   utc            run-utc-tests.py with six tests of llvm/test/tools/UpdateTestChecks/
#                  update_llc_test_checks, which lit reports UNSUPPORTED on Windows (it looks
#                  for a file named 'llc'; plan M5.b and section 12)              (0.5, 0.5)
#   obj-text       the object oracle of CIL objects (plan M19.f, 5.12.6):
#                  msil/utils/mdtools/cli-corpus-check.py --mode object over msil/test/M1 at -O0
#                  and -O2, then over msil/test/M1/11-programs for x86_64-unknown-linux-gnu-msil:
#                  canon(clang -c) == canon(ilasm(object-form text)) with the dumper options
#                  --params=drop --initlocals=ignore-without-locals --snan=quiet, the --link-info
#                  lines equal, two clang -c runs from different directories byte-identical,
#                  mdcanon and both ildasms read every object, the envelope rules and the COFF
#                  view's llvm-nm letters and llvm-ar index. mdcanon is built or refreshed first
#                  (build-mdtools.py). Windows only (the Framework ilasm)           (3, 5.5)
#   lto            msil/test/M1/11-programs, msil/test/M2 and msil/test/M3 under lto (clang -flto -c
#                  and the LTO link of msil-ld into its image) on .NET 8 and on the .NET Framework
#                  4.8, native oracle required, msil_equiv=1 (O4: the --no-gc-sections LTO image
#                  against ilasm's image of the same link's --lto-emit-asm text) and
#                  msil_also_run=10; .ll tests are filtered out (lto runs them on llc text) (11, 22.5)
#   det            msil/test/M2/06-det and msil/test/Programs/det: %{msil_det} builds each
#                  program twice from relative paths and compares the objects, archives and
#                  msil-ld's image byte for byte (corpus leg, see below)            (0.5, 1)
#   refs-net48     msil/test/M1/11-programs, msil/test/M2/00-link and msil/test/M2/05-refs under
#                  obj with msil_references=net48: every link binds the BCL against the .NET
#                  Framework 4.8 reference assemblies (lit.cfg.py finds them: $env:MSIL_REF_NET48,
#                  the Reference Assemblies directory, or the NuGet package that
#                  msil/utils/tools/msil-tools.proj pins), with msil_equiv=1 (O3 with the scopes
#                  entry of equiv-allowed.txt, O6) and msil_run_netfx=1, on .NET 8 and on the .NET
#                  Framework 4.8                                                      (3, 7)
#   refs-net8      the same paths, msil/test/M2/04-shared and the P2-C rows p2c-bsymbolic,
#                  p2c-cycle and p2c-weak-def of msil/test/M2/08-semantics with
#                  msil_references=net8 (the newest Microsoft.NETCore.App.Ref 8.x pack) on .NET 8
#                  and, msil_also_run=10, on .NET 10; msil-verify.py runs ILVerify against the pack
#                  and skips PEVerify, which cannot load such an image                (3, 8)
#   refs-net10     the refs-net48 paths with msil_references=net10 (the newest 10.x pack) on .NET
#                  10                                                               (2.5, 4.5)
#   programs       the nine exit-corpus programs (msil/test/Programs/<name>: zlib, tinf,
#                  monocypher, coremark, lz4, blake2, tweetnacl, kissfft, jsmn) and, as they
#                  appear, the hosted programs of $hostedPrograms (zlib-full from M26, lua from
#                  M27, sqlite from M30; plan 5.17.6, 5.17.15) under obj, -O0 and -O2, on .NET 8
#                  and the .NET Framework 4.8, msil_also_run=10,10+tc0 (.NET 10, and from M26
#                  .NET 10 with DOTNET_TieredCompilation=0), native oracle required (corpus leg)
#                                                                                   (2, 4.5)
#   programs-lto   the same under lto, with msil_equiv=1 (O4) (corpus leg)          (4, 6.5)
#   ctest          msil/test/Programs/c-testsuite: c-testsuite's two lists (plan 5.17.14; from
#                  M26 all 220 tests: libc-free.txt's 150 and libc.txt's 70, the hosted ones
#                  that call the C library, 'Programs/c-testsuite/libc/<name>') under obj, -O0
#                  and -O2, on .NET 10 (M23: .NET 8), the .NET Framework 4.8 and,
#                  msil_also_run=8, .NET 8, native oracle required (corpus leg). While
#                  $env:MSIL_CORPUS_SKIP names c-testsuite the leg prints 'ctest: skipped
#                  (c-testsuite is in MSIL_CORPUS_SKIP)', runs no lit and is reported as SKIP,
#                  not PASS                                                          (9, 18)
#   ctest-lto      -Exit (M26; plan 5.17.6): msil/test/Programs/c-testsuite, both lists, under
#                  lto on .NET 10, native oracle required (corpus leg; skipped like ctest while
#                  $env:MSIL_CORPUS_SKIP names c-testsuite)
#   wsl-net10      -Wsl: msil/test/M1, msil/test/M2, msil/test/M3 and msil/test/harness on
#                  x86_64-unknown-linux-gnu-msil under obj, WSL .NET 10, E7 Linux oracle required
#                  (E7h, with msil_glibc_sysroot, for the hosted tests), msil_equiv=1 (O3; O6 and
#                  ilasm on the host), msil_also_run=8,10+tc0 (WSL .NET 8, and .NET 10 with
#                  DOTNET_TieredCompilation=0)                                    (56, 103)
#   wsl-ilasm      -Wsl: msil/test/M1/11-programs under asm, assembled by the Linux ilasm inside
#                  WSL                                                              (2, 1)
#   expected-linux -Wsl: update-expected.py --check --levels O0,O2 --os linux msil/test/M1
#                  msil/test/M2 msil/test/M3: the E7 Linux oracle (E7h with the glibc sysroot
#                  where hosted) vs <stem>.linux.expected, else <stem>.expected  (2.5, 1.5)
#   obj-text-wsl   -Wsl: cli-corpus-check.py --mode gnu-tools over msil/test/M1 at -O0 and -O2,
#                  then over msil/test/M1/11-programs for x86_64-unknown-linux-gnu-msil: GNU nm
#                  inside the WSL distribution prints llvm-nm's letters for every clang -c object,
#                  and GNU ar t and nm -s list the members and the index of an archive of each
#                  level's objects                                                (0.5, 0.5)
#   lto-wsl        -Wsl: msil/test/M1/11-programs, msil/test/M2/00-link (the six 00-link rows of
#                  matrix.txt under LTO on the Linux triple), 01-archive, 02-lto, 03-native,
#                  04-shared, 05-refs, 07-equiv and 08-semantics, and msil/test/M3 (M24 added
#                  01-archive, 03-native, 04-shared, 05-refs, 07-equiv and M3: plan 5.17.6, bl T10)
#                  under lto, WSL .NET 10, E7 (E7h) required with msil_glibc_sysroot,
#                  msil_also_run=8; O4 over these paths runs in lto               (9, 6)
#   programs-wsl   -Wsl: the nine programs and the hosted ones under obj, WSL .NET 10, E7 (E7h)
#                  required with msil_glibc_sysroot, msil_also_run=8,10+tc0 (from M26) (corpus
#                  leg)                                                             (3, 2)
#   programs-lto-wsl  -Wsl: the same under lto, with msil_equiv=1 (O4) (corpus leg)  (3.5, 2)
#   ctest-wsl      -Wsl (M24; plan 5.17.6, bl T10): msil/test/Programs/c-testsuite for
#                  x86_64-unknown-linux-gnu-msil under obj, WSL .NET 10 and, msil_also_run=8, WSL
#                  .NET 8, E7 (E7h) required with msil_glibc_sysroot (corpus leg; skipped like
#                  ctest while $env:MSIL_CORPUS_SKIP names c-testsuite)
#   det-wsl        -Wsl: msil/test/M2/06-det and msil/test/Programs/det for the Linux triple,
#                  with msil_glibc_sysroot for the hosted programs' det tests (from M26) (corpus
#                  leg)                                                             (1, 0.5)
#   hosted         -Wsl, optional (plan Q16, 5.12.11): msil/test/M2/09-hosted, the hosted
#                  compile-only tests (glibc headers and libc.so, UCRT headers), with
#                  msil_glibc_sysroot given: $env:MSIL_GLIBC_SYSROOT, else
#                  C:\llvm-msil-scratch\glibc-sysroot, which msil/utils/make-glibc-sysroot.sh
#                  builds inside WSL (step G0 of the gates from M15), so a missing sysroot is a
#                  configuration error, never a leg of UNSUPPORTED tests; the CI never runs it
#   net-core       -Quick, step G3 of the standard milestone gate (M24; plan 5.17.6, 5.17.7): the
#                  paths of net8 without msil/test/Programs, msil/test/harness, msil/test/M3,
#                  spill-limits.test and $p3Tests ('--filter-out=MSIL :: (Programs|harness|M3)/|
#                  spill-limits|<$p3Out>'), under obj on .NET 10 and the .NET Framework 4.8 with
#                  msil_equiv=1 (O3 and O6 in every composite, and the explicit O3 and O4 tests of
#                  msil/test/M2/07-equiv) and msil_require_csc=1; no native oracle, no
#                  msil_also_run, no msil_scale_tests                                 (7-8)
#   p3-full        -Quick, G3b (M24): msil/test/M3 and $p3Tests in one run under obj on .NET 10,
#                  .NET 10 with DOTNET_TieredCompilation=0 (msil_also_run=10+tc0) and the .NET
#                  Framework 4.8, native oracle required, msil_equiv=1, msil_require_csc=1: the
#                  Windows half of the gate's first condition (plan 5.17.7), which
#                  check-matrix.py --legs proves in G6 (the gate rule)                (2-4)
#   harness        -Quick, G3c (M24): msil/test/harness under obj on .NET 10 and the .NET
#                  Framework 4.8, native oracle required, msil_require_csc=1           (2-3)
#   wsl-p3         -Quick, G4 (M24): two lit runs on x86_64-unknown-linux-gnu-msil under obj on
#                  WSL .NET 10, E7 (E7h) required with msil_glibc_sysroot. Run 1: msil/test/M3 and
#                  $p3Tests with msil_also_run=10+tc0 (the WSL half of the gate's first condition).
#                  Run 2: msil/test/M2/08-semantics and msil/test/M1/11-programs without $p3Tests
#                  ('--filter-out=<$p3Out>'), the run a gate moves first           (4-6)
#   scale          -Quick (M24): msil/test/M2/00-link/spill-limits.test at the real CLR limits
#                  (msil_scale_tests=1) under obj on .NET 10, for the gates of milestones that
#                  change the trailer, the spill classes or a type's member limit
#   programs-hosted  -Quick (M26; plan 5.17.6, 5.17.15): the hosted programs of $hostedPrograms
#                  that exist (zlib-full from M26, lua from M27, sqlite from M30) under obj at
#                  -O2 (msil_opt_levels=O2) on .NET 10, .NET 10 with DOTNET_TieredCompilation=0
#                  (msil_also_run=10+tc0) and the .NET Framework 4.8, native oracle required (the
#                  DLL CRT oracle of hosted mode; corpus leg): an exit check of the gates of M26,
#                  M27, M28 and M30
#   programs-hosted-wsl  -Quick (M26): the same programs for x86_64-unknown-linux-gnu-msil on
#                  WSL .NET 10 and, msil_also_run=10+tc0, with DOTNET_TieredCompilation=0, E7h
#                  required with msil_glibc_sysroot (zlib-full's fortify test included)
#   wsl-quick      -Quick: msil/test/M1/11-programs, msil/test/harness and msil/test/M2 on WSL .NET
#                  10, E7 required (plan 5.12.10 G4, the WSL step of the Phase 2 milestone gates;
#                  Phase 3's G4 is wsl-p3)
#   all-levels     -Full: msil/test at O0,O1,O2,O3,Os,Oz (nightly)
#   lto-m1         -Full: all of msil/test/M1 under lto (11-programs runs in lto)
#   encoder-parity -Full: msil/utils/mdtools/cli-corpus-check.py --mode encoder (plan M18.f):
#                  every msil/test/M1 program at -O0 and -O2 through clang -S -emit-llvm and llc
#                  -show-mc-encoding, each method's bytes rebuilt from the encoding comments and
#                  compared with the .NET Framework ilasm's image of the same text, at least 2,600
#                  methods; once obj is the default, net8's O3 compares every M1 method's encoding
#                  with ilasm's anyway. Windows only (the Framework ilasm)
#   portable-smoke -Portable (P-1a; plan 5.18.10 PX4): msil/test/Portable (00-freestanding at P-1a,
#                  the later P-steps' directories as they appear) on msil64-unknown-dotnet under
#                  obj on .NET 10, .NET 10 with DOTNET_TieredCompilation=0 (msil_also_run=10+tc0)
#                  and the .NET Framework 4.8 (msil_run_netfx=1), msil_require_portable=1 (the
#                  portable toolchain, helper archive and sysroot) and msil_require_x86=1 (the 32-bit
#                  dotnet.exe with .NET 8 and 10 x86 and the 32-bit Framework, so guard-x86 is
#                  never silently UNSUPPORTED); no native oracle. Setup, first, in order (each
#                  step stamped: an up-to-date product is not rebuilt): 'build-builtins.py
#                  --build-dir <build> --triple msil64-unknown-dotnet' (the portable helper
#                  archive, P-1a), then 'build-libc.py --build-dir <build>' (P-1b, plan 5.19.3:
#                  llvm-libc, c.dll, the empty libm.a and the C headers in the portable sysroot);
#                  the first step that fails ends the setup and fails the row ('setup failed:
#                  <script>[ <arguments>] (exit <n>; <log>)'), and no lit runs
#   portable-smoke-wsl  -Portable, needs -Wsl (P-1a): the same directory on the same triple under
#                  WSL .NET 10 and, msil_also_run=10+tc0, with DOTNET_TieredCompilation=0 (the
#                  WSL legs' parameters with msil_triple=msil64-unknown-dotnet in place of the
#                  Linux triple), msil_require_portable=1; no native oracle, no E7h. The same setup
# Removed in M23, and -Leg <name> fails naming the leg that covers them: obj (net8), obj-wsl
# (wsl-net10), net10 (net8's msil_also_run=10), net8-tc0 (net8's 8+tc0), wsl-net10-tc0
# (wsl-net10's 10+tc0).
#
# $p3Tests and $p3Out (plan 5.17.6, 5.17.12 item 3). $p3Tests is the list of the Phase 3 feature
# tests outside msil/test/M3 that 'python msil/test/M2/08-semantics/Inputs/check-matrix.py
# --p3-tests <repo>' prints (repository-relative paths, which lit takes as test paths; M24:
# msil/test/M1/02-fp/strict-i128.c). The script reads it every time it builds its legs, that is
# for -Describe and before running any leg (not for -List); when the checker fails, the script
# stops with the checker's message and runs nothing. $p3Out is the regular expression that matches
# exactly those tests' full lit names ('MSIL :: <path below msil/test>', metacharacters escaped,
# each alternative ending in '$', joined with '|'); it joins the --filter-out of net-core and of
# wsl-p3's run 2, so that p3-full and wsl-p3's run 1 alone run those tests in a gate. With an empty
# $p3Tests there is no $p3Out (an empty --filter-out would match every test).
#
# The corpus legs (programs, programs-lto, ctest, det and their -Wsl twins) need the exit corpus
# of msil/utils/corpus/manifest.json, which msil/utils/corpus/fetch.py downloads into a cache
# outside the repository: they pass msil_corpus_dir=$env:MSIL_CORPUS_CACHE (when it is set; else
# lit's default, C:\llvm-msil-scratch\corpus-cache), msil_corpus_skip=$env:MSIL_CORPUS_SKIP and
# msil_require_corpus=1, so a missing or stale cache is a configuration error and an entry left out
# on purpose is UNSUPPORTED with a note. msil-ci.yml sets MSIL_CORPUS_SKIP to '' since M24 (R23,
# plan 5.17.17: the CI fetches and runs every entry; M23's CI left out coremark and c-testsuite,
# plan section 10 item 11); the variable stays for local opt-outs. run-m1-gate.ps1 removes it from
# the legs' environment, so the phase gate runs every entry.
#
# The WSL legs need a WSL distribution (only the development host has one; the Linux CI job
# covers the Linux triple natively) with the private .NET runtimes of
# msil/utils/msil-wsl-dotnet.sh: every WSL lit leg passes msil_wsl_dotnet=private (plan 5.12.8,
# Q39), so a missing private runtime is a configuration error, never a run on the distribution's
# own dotnet. The WSL legs that run programs also require the E7 Linux oracle
# (msil_native_oracle=required), which, like expected-linux, needs the sysroot of
# msil/utils/make-linux-sysroot.py ($env:MSIL_LINUX_SYSROOT, else C:\llvm-msil-scratch\linux-sysroot,
# passed as msil_linux_sysroot) and an ld.lld (the tree's); the ones that run msil/test/M3
# (wsl-net10, lto-wsl, ctest-wsl, wsl-p3; $e7h below) also pass the glibc sysroot of
# make-glibc-sysroot.sh as msil_glibc_sysroot, the headers and the E7h oracle of hosted mode (plan
# 5.17.3). run-m1-gate.ps1's host-setup step makes all three (step G0 of the gates). modern-ilasm
# and wsl-ilasm take the .NET ilasm, and roundtrip and asm-oracle the .NET ildasm and ilasm
# (lit.cfg.py reads the version the same way), at the version that msil/utils/tools/msil-tools.proj
# pins (MsilILAsmVersion), from the NuGet global-packages folder ('dotnet restore
# msil/utils/tools/msil-tools.proj -p:MsilToolsRid=win-x64', and linux-x64 for wsl-ilasm); a newer
# version in that folder is never used, and a missing one fails the leg; nothing here downloads.
# obj-text takes the NuGet ildasm the same way, and the .NET Framework ildasm from the Windows SDK's
# NETFX 4.x Tools; obj-text-wsl needs only GNU nm and ar (binutils) in the distribution Ubuntu, the
# one cli-corpus-check.py and lit.cfg.py use by default.
#
# Every lit leg consists of one lit run or more (paths, parameters and lit arguments each; wsl-p3
# has two), and its row passes only when every run passes. Every run goes through
# msil/utils/build/msil-lit.ps1 (shared build-dir lock, the command of the gate steps) with
# --param msil_require_verify=1 (plan D38), --param msil_require_exec=1 and --param
# msil_require_link=1 (plan 5.12.2, M14) and --param msil_require_hosted=1 (plan 5.17.2, 5.17.6;
# M24), so a missing verifier, jitall, compiler, runtime, msil-ld, llvm-ar, hosted header set or
# hosted oracle is a configuration error instead of a leg of UNSUPPORTED tests. A run writes
# <build>/lit-results/<file>.xml (xunit) and <file>.log, where <file> is the leg's name, or
# <leg>-run<k> for run k of a leg of several runs. Its lit arguments go one per line
# ('--param=<k>=<v>', a run's '--filter-out=<regex>', '--xunit-xml-output=<file>') into the
# response file <build>/lit-results/<file>.params.rsp, which msil-lit.ps1 passes to lit as
# '-sv @<file>' (lit reads response files, as upstream-hooks.ps1 uses them), so a parameter value
# may contain blanks (a NuGet ilasm under a profile directory with a space; backlog 3.5); the build
# directory itself may not, because msil-lit.ps1 splits -LitArgs at blanks, and the leg fails
# naming it. A run passes when lit exits 0 and at least one test passed.
#
# The timeout retry (plan 5.17.9; M24). After a lit run that failed, lit-retry.py select reads its
# xunit file. When it exits 0, lit exited 1 (failed tests only) and lit ran every test (no testcase
# in the xunit file has <skipped message="Skipped"/>), each test it lists (a failed test whose
# failure text, after the last command lit's shell executed, carries a tool timeout marker) is
# re-run once, alone, with exactly the run's parameters and lit arguments, '-j1' and
# '--filter=^<escaped name>$' over the run's paths (c-testsuite's virtual tests included), into
# <build>/lit-results/<leg>-retry-<stem>.log and .xml (response file <leg>-retry-<stem>.params.rsp),
# where <stem> is the full lit name with every run of characters other than letters, digits, '.' and
# '-' written '_' (a second test with the same stem gets '-2', and so on), preceded by 'run<k>-' in
# a leg of several runs. The row is PASS, with a detail that starts 'retried after timeout: <names>
# (<logs>)', when every re-run passes (lit exits 0 and one test passed). It is FAIL when a re-run
# fails in any way (a hang that times out twice still fails: 're-run after timeout failed: <name>
# (<log>; lit exit <n>)'), when more than three tests timed out ('<n> tests timed out; re-run the
# leg on a quieter host'), when a failure is not timeout-only (no re-run then), when lit failed
# without an xunit file or without a failed test in it, when lit-retry.py fails, and, when select
# exited 0 and listed tests, when lit exited otherwise than 1 ('lit reported an error, so no
# retry': lit exits 2 after an error in tests, such as 'a worker process crashed'; plan section 12,
# 'Plan 5.17.9 amended (a lit error stops the retry)') or did not run every test ('lit did not run
# <k> test(s), so no retry': --max-failures or --max-time, which LIT_OPTS can pass, stops lit early
# without an error). Either way lit writes every test it had not run as skipped, and nothing is
# re-run, so a retry never passes a run whose other tests did not run. A run's xunit file and retry
# files are deleted before it starts, so no earlier run's file can pass for its own. lit's own
# retries (ALLOW_RETRIES, --max-retries-per-test) are never used, and msil_timeout stays
# lit.cfg.py's 60 s.
#
# -Run <k>[,<k>...], with exactly one -Leg, runs only those runs of the leg (1-based, in the
# order above) and names its row '<leg> (run <k>[,<k>...])'; an index that the leg does not have,
# -Run without exactly one -Leg, or -Run on a leg without lit runs is a usage error:
# 'lit-legs.ps1: usage error: ...' on stderr, exit status 2, nothing run. -Describe prints every
# leg as plan 5.17.6's JSON (version 1: script 'lit-legs.ps1', and for each leg its name, tag (''
# for the default set; Wsl, Quick, Full, Exit or Portable), kind (lit, expected, utc, encoder or obj-text) and lit runs, each with its
# paths, lit arguments and every parameter it passes, the msil_require_* ones included, and the
# derived pipeline, prog_pipeline, triple (default x86_64-pc-windows-msvc-msil), runner (default
# local) and also_run) and runs nothing; a NuGet tool path prints as '@nuget:<rid>/<tool>', the
# corpus parameters print as the environment resolves them, and $p3Tests and $p3Out appear
# resolved. G6 and run-m1-gate.ps1's matrix-legs row hand it to check-matrix.py --legs.
#
# expected, expected-linux, utc, encoder-parity, obj-text and obj-text-wsl are not lit legs (no
# retry): each runs its script under the shared build-dir lock, writes <build>/lit-results/<leg>.log
# and passes when the script exits 0 (and, for the expected legs, at least one test matched, 'ok:'
# or 'rule:'; for encoder-parity, obj-text and obj-text-wsl, every run printed its summary line).
# Every selected leg runs even after a failure; the exit status is 1 if any leg failed (a SKIP row,
# the ctest legs', is no failure), 2 on a usage error of -Run.
[CmdletBinding()]
param(
  [string]$BuildDir = '',         # default: $env:MSIL_BUILD_DIR, else <repo>\build
  [switch]$Wsl,
  [switch]$Full,
  [switch]$Exit,                  # also the -Exit legs (post-commit run, phase-exit gate)
  [switch]$Portable,              # also the -Portable legs (post-commit run; P-1a)
  [string[]]$Leg = @(),           # run only these legs (names as in -List; comma-separated works)
  [string[]]$Run = @(),           # with one -Leg: only these runs of it (1-based; comma-separated)
  [switch]$List,
  [switch]$Describe,              # print every leg as JSON (plan 5.17.6) and run nothing
  [int]$Jobs = 0                  # lit -j (0: lit's default)
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\build\msil-env.ps1')
. (Join-Path $PSScriptRoot '..\build\msil-lock.ps1')
. (Join-Path $PSScriptRoot 'ci-common.ps1')
if (-not $BuildDir) { $BuildDir = $MsilBuild }
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
# True while -Describe resolves the legs: NuGet tools print as '@nuget:<rid>/<tool>'.
$script:Describing = $false

# check-msil's paths (msil/test/CMakeLists.txt); lit-legs.sh's check_msil is the same list.
$checkMsil = @('llvm/test/CodeGen/MSIL', 'clang/test/CodeGen/MSIL', 'clang/test/Preprocessor/init-msil.c',
               'clang/test/Preprocessor/init-msil-portable.c', 'clang/test/Driver/msil-dotnet.c',
               'clang/test/Driver/msil-toolchain.c', 'clang/test/Driver/msil-link.c',
               'clang/test/Driver/msil-lto.c', 'clang/test/Driver/msil-header-search.c',
               'clang/test/Driver/msil-object.c', 'clang/test/Driver/msil-shared.c',
               'clang/test/Sema/msil-long-double-format.c',
               'llvm/test/Transforms/ExpandVariadics/msil-abi.ll', 'lld/test/MSIL',
               'llvm/test/tools/llvm-cil-dump', 'llvm/test/tools/llvm-cil-copy', 'llvm/test/MC/MSIL',
               'msil/test')
$m1 = @('msil/test/M1')
$m2 = @('msil/test/M2')
$m3 = @('msil/test/M3')
$programs = @('msil/test/M1/11-programs')
# The paths of the roundtrip leg: O2 on msil-ld's outputs (plan 5.12.6, 5.12.11; section 12, 'M23.d
# extended (roundtrip leg)'); lit-legs.sh's roundtrip is the same list.
$roundtrip = @('msil/test/M1/11-programs', 'msil/test/M2/07-equiv',
               'msil/test/M2/04-shared/roundtrip-library.test')
# The paths of the reference legs (plan 5.12.11, M21); lit-legs.sh's refs is the same list.
$refs = @('msil/test/M1/11-programs', 'msil/test/M2/00-link', 'msil/test/M2/05-refs')
# refs-net8 also runs the libraries bound to the net8 pack (section 12, 'M23.d extended (libraries
# bound to the net8 pack)'); lit-legs.sh's refs_net8 is the same list.
$refsNet8 = $refs + @('msil/test/M2/04-shared', 'msil/test/M2/08-semantics/p2c-bsymbolic.test',
                      'msil/test/M2/08-semantics/p2c-cycle.test',
                      'msil/test/M2/08-semantics/p2c-weak-def.test')
# lto-wsl's paths (plan 5.17.6, bl T10: since M24 the Linux triple's LTO links of the M2
# directories that link programs, and msil/test/M3).
$ltoWsl = $programs + @('msil/test/M2/00-link', 'msil/test/M2/01-archive', 'msil/test/M2/02-lto',
                        'msil/test/M2/03-native', 'msil/test/M2/04-shared', 'msil/test/M2/05-refs',
                        'msil/test/M2/07-equiv', 'msil/test/M2/08-semantics') + $m3
# The exit corpus (plan 5.12.9): the nine programs, their determinism tests and c-testsuite;
# lit-legs.sh has the same lists.
$corpusPrograms = @('zlib', 'tinf', 'monocypher', 'coremark', 'lz4', 'blake2', 'tweetnacl', 'kissfft',
                    'jsmn') | ForEach-Object { "msil/test/Programs/$_" }
# The hosted programs of Phase 3 (plan 5.17.15: zlib-full from M26, lua from M27, sqlite from M30)
# that exist: programs-hosted and programs-hosted-wsl run them at -O2, and the programs legs run
# them with the nine (their det tests are in msil/test/Programs/det); lit-legs.sh has the same
# list.
$hostedPrograms = @(@('zlib-full', 'lua', 'sqlite') | ForEach-Object { "msil/test/Programs/$_" } |
                    Where-Object { Test-Path -LiteralPath (Join-Path $MsilRepo $_) -PathType Container })
$det = @('msil/test/M2/06-det', 'msil/test/Programs/det')
$ctest = @('msil/test/Programs/c-testsuite')
# net8 runs msil/test without its corpus tests, which the corpus legs run.
$noCorpus = '--filter-out=MSIL :: Programs/'
# net-core leaves out the corpus tests, the harness (G3c), msil/test/M3 (G3b), the scale test
# (plan 5.17.6) and msil/test/Portable (the portable legs', plan 5.18.10 PX4); $p3Out joins it.
$netCoreOut = 'MSIL :: (Programs|harness|M3|Portable)/|spill-limits'
$linux = 'msil_triple=x86_64-unknown-linux-gnu-msil'
# The E7 Linux oracle's sysroot (msil/utils/make-linux-sysroot.py), defaulted as lit.cfg.py and
# run-m1-gate.ps1's host-setup step default it.
$linuxSysroot = if ($env:MSIL_LINUX_SYSROOT) { $env:MSIL_LINUX_SYSROOT } else { 'C:\llvm-msil-scratch\linux-sysroot' }
# The glibc sysroot of hosted mode and of the hosted leg (msil/utils/make-glibc-sysroot.sh),
# defaulted as lit.cfg.py defaults it on Windows.
$glibcSysroot = if ($env:MSIL_GLIBC_SYSROOT) { $env:MSIL_GLIBC_SYSROOT } else { 'C:\llvm-msil-scratch\glibc-sysroot' }
# Every WSL lit leg: the Linux triple on the private runtime of msil-wsl-dotnet.sh (plan 5.12.8).
$wslRuntime = @('msil_runner=wsl', $linux, 'msil_wsl_dotnet=private', 'msil_dotnet_runtime=10')
# The portable legs (plan 5.18.10 PX4): the portable triple, and the setup steps that run before
# their lit runs, in order and each stamped: the helper archive (P-1a), then llvm-libc, c.dll and
# the portable sysroot (P-1b, plan 5.19.3).
$portableTriple = 'msil_triple=msil64-unknown-dotnet'
$portableSetup = @(
  @{ Script = 'msil/utils/build/build-builtins.py'; Args = @('--triple', 'msil64-unknown-dotnet') }
  @{ Script = 'msil/utils/build/build-libc.py'; Args = @() }
)
# The legs that require the E7 Linux oracle ...
$e7 = @('msil_native_oracle=required', "msil_linux_sysroot=$($linuxSysroot.Replace('\', '/'))")
# ... and, where they run hosted tests (msil/test/M3), the glibc sysroot of E7h (plan 5.17.3, 5.17.6).
$e7h = $e7 + @("msil_glibc_sysroot=$($glibcSysroot.Replace('\', '/'))")
# The Windows legs that run programs: .NET 8, the .NET Framework and the native oracle.
$winRun = @('msil_dotnet_runtime=8', 'msil_native_oracle=required', 'msil_run_netfx=1')
# Every lit run's requirements (plan D38, 5.12.2, 5.12.11, 5.17.6).
$requireParams = @('msil_require_verify=1', 'msil_require_exec=1', 'msil_require_link=1',
                   'msil_require_hosted=1')
# -Describe's triple of a run without msil_triple (plan 5.17.6).
$defaultTriple = 'x86_64-pc-windows-msvc-msil'
# Set when the legs are built (see the header): $p3Tests, and $p3Out ('' when $p3Tests is empty).
$p3Tests = @()
$p3Out = ''

# The entries of $env:MSIL_CORPUS_SKIP (comma-separated; blanks dropped).
function Get-CorpusSkip {
  @("$env:MSIL_CORPUS_SKIP" -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
}

# The lit parameters of a corpus leg (plan 5.12.11, 5.12.12).
function Get-CorpusParams {
  $p = @('msil_require_corpus=1')
  if ($env:MSIL_CORPUS_CACHE) { $p += "msil_corpus_dir=$($env:MSIL_CORPUS_CACHE.Replace('\', '/'))" }
  $skip = @(Get-CorpusSkip)
  if ($skip.Count -gt 0) { $p += "msil_corpus_skip=$($skip -join ',')" }
  return $p
}

# A NuGet tool's path for a leg's parameters, or '@nuget:<rid>/<tool>' under -Describe.
function Get-LegNuGetTool([string]$Rid, [string]$Tool) {
  if ($script:Describing) { return "@nuget:$Rid/$Tool" }
  return Get-MsilNuGetTool -Repo $MsilRepo -Rid $Rid -Tool $Tool
}

# The alternatives of a lit filter joined with '|', empty ones left out.
function Join-LitFilter([string[]]$Parts) { @($Parts | Where-Object { $_ }) -join '|' }

# Params, Paths and Lit are script blocks (Lit may also be an array): tool paths, the corpus
# variables and $p3Tests are resolved when the leg runs, so a missing package fails that leg only.
# A leg of several lit runs has Runs, a list of @{ Paths; Params; Lit }. Tag: '' (the default set),
# 'Wsl', 'Quick' (only when -Leg names it), 'Full', 'Exit' or 'Portable' (see the header). Setup
# (the portable legs): a list of steps @{ Script; Args }, which $MsilPython runs in order with
# '--build-dir <build>' and the step's arguments before the leg's lit runs; the first failure ends
# the setup, fails the row and runs no lit.
$legs = @(
  @{ Name = 'net8'; Paths = { $checkMsil }; Lit = @($noCorpus)
     Params = { @('msil_pipeline=obj') + $winRun +
                @('msil_equiv=1', 'msil_also_run=10,8+tc0', 'msil_scale_tests=1', 'msil_require_csc=1') } }
  @{ Name = 'more-levels'; Paths = { $m1 }
     Params = { 'msil_pipeline=obj', 'msil_dotnet_runtime=8', 'msil_opt_levels=O1,O3,Os,Oz' } }
  @{ Name = 'asm-oracle'; Paths = { $m1 + @('msil/test/harness/pipeline') }
     Params = { 'msil_pipeline=asm', 'msil_dotnet_runtime=8', 'msil_native_oracle=required',
                'msil_roundtrip=1' } }
  @{ Name = 'modern-ilasm'; Paths = { $programs }
     Params = { 'msil_pipeline=asm', "msil_ilasm=$(Get-LegNuGetTool -Rid win-x64 -Tool ilasm)" } }
  @{ Name = 'roundtrip'; Paths = { $roundtrip }; Params = { 'msil_pipeline=obj', 'msil_roundtrip=1' } }
  @{ Name = 'expected'; Expected = 'windows' }
  @{ Name = 'utc'; Utc = $true }
  @{ Name = 'obj-text'; ObjText = 'object' }
  @{ Name = 'lto'; Paths = { $programs + $m2 + $m3 }; Lit = @('--filter-out=\.ll$')
     Params = { @('msil_pipeline=lto') + $winRun + @('msil_equiv=1', 'msil_also_run=10') } }
  @{ Name = 'det'; Paths = { $det }; Params = { @('msil_dotnet_runtime=8') + @(Get-CorpusParams) } }
  @{ Name = 'refs-net48'; Paths = { $refs }
     Params = { 'msil_pipeline=obj', 'msil_references=net48', 'msil_equiv=1', 'msil_run_netfx=1' } }
  @{ Name = 'refs-net8'; Paths = { $refsNet8 }
     Params = { 'msil_pipeline=obj', 'msil_references=net8', 'msil_dotnet_runtime=8', 'msil_also_run=10' } }
  @{ Name = 'refs-net10'; Paths = { $refs }
     Params = { 'msil_pipeline=obj', 'msil_references=net10', 'msil_dotnet_runtime=10' } }
  # The programs legs take the hosted programs as they appear, with msil_also_run's 10+tc0 (plan
  # 5.17.6; M26).
  @{ Name = 'programs'; Paths = { $corpusPrograms + $hostedPrograms }
     Params = { @('msil_pipeline=obj') + $winRun + @('msil_also_run=10,10+tc0') + @(Get-CorpusParams) } }
  @{ Name = 'programs-lto'; Paths = { $corpusPrograms + $hostedPrograms }
     Params = { @('msil_pipeline=lto') + $winRun + @('msil_equiv=1', 'msil_also_run=10,10+tc0') +
                @(Get-CorpusParams) } }
  @{ Name = 'ctest'; Paths = { $ctest }; CorpusEntry = 'c-testsuite'
     Params = { @('msil_pipeline=obj', 'msil_dotnet_runtime=10', 'msil_native_oracle=required',
                  'msil_run_netfx=1', 'msil_also_run=8') + @(Get-CorpusParams) } }
  # c-testsuite's two lists under LTO (plan 5.17.6; M26): the post-commit runs and the phase gate.
  @{ Name = 'ctest-lto'; Paths = { $ctest }; Tag = 'Exit'; CorpusEntry = 'c-testsuite'
     Params = { @('msil_pipeline=lto', 'msil_dotnet_runtime=10', 'msil_native_oracle=required') +
                @(Get-CorpusParams) } }
  @{ Name = 'wsl-net10'; Paths = { $m1 + $m2 + $m3 + @('msil/test/harness') }; Tag = 'Wsl'
     Params = { @('msil_pipeline=obj') + $wslRuntime + $e7h + @('msil_equiv=1', 'msil_also_run=8,10+tc0') } }
  @{ Name = 'wsl-ilasm'; Paths = { $programs }; Tag = 'Wsl'
     Params = { @('msil_pipeline=asm') + $wslRuntime +
                "msil_wsl_ilasm=$(Get-LegNuGetTool -Rid linux-x64 -Tool ilasm)" } }
  @{ Name = 'expected-linux'; Expected = 'linux'; Tag = 'Wsl' }
  @{ Name = 'obj-text-wsl'; ObjText = 'gnu-tools'; Tag = 'Wsl' }
  # lto-wsl's msil/test/M2/00-link (section 12, '5.12.11 amended (lto-wsl paths)'): the six matrix
  # rows there run under LTO on the Linux triple only here; M24 widened it ($ltoWsl).
  @{ Name = 'lto-wsl'; Tag = 'Wsl'; Paths = { $ltoWsl }
     Params = { @('msil_pipeline=lto') + $wslRuntime + $e7h + @('msil_also_run=8') } }
  @{ Name = 'programs-wsl'; Paths = { $corpusPrograms + $hostedPrograms }; Tag = 'Wsl'
     Params = { @('msil_pipeline=obj') + $wslRuntime + $e7h + @('msil_also_run=8,10+tc0') +
                @(Get-CorpusParams) } }
  # O4 on the Linux triple's LTO links of the programs (section 12, '5.12.11 amended (O4 for the
  # Linux triple's programs)').
  @{ Name = 'programs-lto-wsl'; Paths = { $corpusPrograms + $hostedPrograms }; Tag = 'Wsl'
     Params = { @('msil_pipeline=lto') + $wslRuntime + $e7h + @('msil_equiv=1', 'msil_also_run=8,10+tc0') +
                @(Get-CorpusParams) } }
  @{ Name = 'ctest-wsl'; Paths = { $ctest }; Tag = 'Wsl'; CorpusEntry = 'c-testsuite'
     Params = { @('msil_pipeline=obj') + $wslRuntime + $e7h + @('msil_also_run=8') + @(Get-CorpusParams) } }
  # det-wsl builds the hosted programs' det tests with the glibc sysroot's headers (plan 5.17.6).
  @{ Name = 'det-wsl'; Paths = { $det }; Tag = 'Wsl'
     Params = { $wslRuntime + @("msil_glibc_sysroot=$($glibcSysroot.Replace('\', '/'))") + @(Get-CorpusParams) } }
  @{ Name = 'hosted'; Paths = { 'msil/test/M2/09-hosted' }; Tag = 'Wsl'
     Params = { "msil_glibc_sysroot=$($glibcSysroot.Replace('\', '/'))" } }
  @{ Name = 'net-core'; Tag = 'Quick'; Paths = { $checkMsil }
     Lit = { "--filter-out=$(Join-LitFilter @($netCoreOut, $p3Out))" }
     Params = { 'msil_pipeline=obj', 'msil_dotnet_runtime=10', 'msil_run_netfx=1', 'msil_equiv=1',
                'msil_require_csc=1' } }
  @{ Name = 'p3-full'; Tag = 'Quick'; Paths = { $m3 + $p3Tests }
     Params = { 'msil_pipeline=obj', 'msil_dotnet_runtime=10', 'msil_native_oracle=required',
                'msil_run_netfx=1', 'msil_equiv=1', 'msil_also_run=10+tc0', 'msil_require_csc=1' } }
  @{ Name = 'harness'; Tag = 'Quick'; Paths = { 'msil/test/harness' }
     Params = { 'msil_pipeline=obj', 'msil_dotnet_runtime=10', 'msil_native_oracle=required',
                'msil_run_netfx=1', 'msil_require_csc=1' } }
  @{ Name = 'wsl-p3'; Tag = 'Quick'
     Runs = @(
       @{ Paths = { $m3 + $p3Tests }
          Params = { @('msil_pipeline=obj') + $wslRuntime + $e7h + @('msil_also_run=10+tc0') } }
       @{ Paths = { 'msil/test/M2/08-semantics', 'msil/test/M1/11-programs' }
          Lit = { if ($p3Out) { "--filter-out=$p3Out" } }
          Params = { @('msil_pipeline=obj') + $wslRuntime + $e7h } }
     ) }
  @{ Name = 'scale'; Tag = 'Quick'; Paths = { 'msil/test/M2/00-link/spill-limits.test' }
     Params = { 'msil_pipeline=obj', 'msil_dotnet_runtime=10', 'msil_scale_tests=1' } }
  # The hosted programs at -O2 (plan 5.17.6, 5.17.15; M26): the exit checks of the gates of M26,
  # M27, M28 and M30.
  @{ Name = 'programs-hosted'; Tag = 'Quick'; Paths = { $hostedPrograms }
     Params = { @('msil_pipeline=obj', 'msil_opt_levels=O2', 'msil_dotnet_runtime=10',
                  'msil_native_oracle=required', 'msil_run_netfx=1', 'msil_also_run=10+tc0') +
                @(Get-CorpusParams) } }
  @{ Name = 'programs-hosted-wsl'; Tag = 'Quick'; Paths = { $hostedPrograms }
     Params = { @('msil_pipeline=obj', 'msil_opt_levels=O2') + $wslRuntime + $e7h +
                @('msil_also_run=10+tc0') + @(Get-CorpusParams) } }
  @{ Name = 'wsl-quick'; Tag = 'Quick'; Params = { @('msil_pipeline=obj') + $wslRuntime + $e7 }
     Paths = { 'msil/test/M1/11-programs', 'msil/test/harness', 'msil/test/M2' } }
  @{ Name = 'all-levels'; Paths = { 'msil/test' }; Tag = 'Full'
     Params = { 'msil_opt_levels=O0,O1,O2,O3,Os,Oz' } }
  @{ Name = 'lto-m1'; Paths = { $m1 }; Tag = 'Full'; Lit = @('--filter-out=\.ll$')
     Params = { @('msil_pipeline=lto') + $winRun } }
  @{ Name = 'encoder-parity'; Encoder = $true; Tag = 'Full' }
  # The portable smoke legs (plan 5.18.10 PX4; P-1a): post-commit runs only, never -Exit.
  @{ Name = 'portable-smoke'; Tag = 'Portable'; Paths = { 'msil/test/Portable' }; Setup = $portableSetup
     Params = { $portableTriple, 'msil_pipeline=obj', 'msil_dotnet_runtime=10', 'msil_also_run=10+tc0',
                'msil_run_netfx=1', 'msil_require_portable=1', 'msil_require_x86=1' } }
  @{ Name = 'portable-smoke-wsl'; Tag = 'Portable'; Paths = { 'msil/test/Portable' }; Setup = $portableSetup
     Params = { @($wslRuntime | ForEach-Object { if ($_ -eq $linux) { $portableTriple } else { $_ } }) +
                @('msil_pipeline=obj', 'msil_also_run=10+tc0', 'msil_require_portable=1') } }
)

# The legs that M23 removed, and the leg that runs their tests and runtimes now (plan 5.12.11).
$removedLegs = [ordered]@{
  'obj'           = 'net8 (objects are the default pipeline; net8 passes msil_equiv=1 and msil_scale_tests=1)'
  'obj-wsl'       = 'wsl-net10 (objects are the default pipeline; wsl-net10 passes msil_equiv=1)'
  'net10'         = 'net8 (msil_also_run=10)'
  'net8-tc0'      = 'net8 (msil_also_run=8+tc0)'
  'wsl-net10-tc0' = 'wsl-net10 (msil_also_run=10+tc0)'
}

# The tests of the utc leg: the update_llc_test_checks tests that run-utc-tests.py can run and
# whose targets every build of msil/utils/ci has (MSIL and X86). wasm-function-name needs
# WebAssembly, which msil/cmake/caches/MSIL.cmake does not build, and run-utc-tests.py fails a
# test whose target is missing instead of skipping it. lit-legs.sh has the same list.
$utcTests = @('msil-basic', 'x86-basic', 'x86-mir-only', 'x86-asm-mir-mixed', 'x86-constant-pool',
              'asm-show-inst')

# The encoder-parity leg's floor (plan M18.f): msil/test/M1 at -O0 and -O2 had 2,670 method
# bodies in llvm/docs/MSIL/recon/p2/mc-streamer.md (E2, 93 C programs) and has 2,819 at M18
# (99 C programs).
$encoderMinMethods = 2600

# A leg's kind in -Describe (plan 5.17.6).
function Get-LegKind($l) {
  if ($l.Expected) { 'expected' } elseif ($l.Utc) { 'utc' } elseif ($l.Encoder) { 'encoder' }
  elseif ($l.ObjText) { 'obj-text' } else { 'lit' }
}

# The number of lit runs of a leg (0 for a leg that is not a lit leg), without resolving them.
function Get-LegRunCount($l) {
  if ((Get-LegKind $l) -ne 'lit') { return 0 }
  if ($l.Runs) { return @($l.Runs).Count }
  return 1
}

# True for a WSL leg (tag Wsl, or a name ending in '-wsl'; plan 5.17.6).
function Test-WslLeg($l) { $l.Tag -eq 'Wsl' -or $l.Name -like '*-wsl' }

# The lit runs of a lit leg, resolved: one @{ Paths; Params; Lit } per run, Params with the
# msil_require_* parameters of every lit run.
function Get-LegRuns($l) {
  $specs = if ($l.Runs) { @($l.Runs) } else { @(@{ Paths = $l.Paths; Params = $l.Params; Lit = $l.Lit }) }
  $out = @()
  foreach ($s in $specs) {
    $lit = if ($s.Lit -is [scriptblock]) { @(& $s.Lit) } else { @($s.Lit) }
    $out += @{ Paths = @(& $s.Paths); Params = @(& $s.Params) + $requireParams
               Lit = @($lit | Where-Object { $_ }) }
  }
  return $out
}

# A command's exit status, stdout lines and stderr lines (under 2>&1, a native command's stderr
# lines arrive as ErrorRecords).
function Invoke-Captured([string]$Exe, [string[]]$Arguments) {
  $outLines = [System.Collections.Generic.List[string]]::new()
  $errLines = [System.Collections.Generic.List[string]]::new()
  & $Exe @Arguments 2>&1 | ForEach-Object {
    if ($_ -is [System.Management.Automation.ErrorRecord]) { $errLines.Add("$_") } else { $outLines.Add("$_") }
  }
  return @{ Rc = $LASTEXITCODE; Out = $outLines.ToArray(); Err = $errLines.ToArray() }
}

# A text as a regular expression of Python's re module (lit's --filter and --filter-out) that
# matches it literally.
function ConvertTo-LitRegex([string]$Text) { [regex]::Replace($Text, '[\\.^$|?*+()\[\]{}]', '\$0') }

# $p3Tests (see the header): the lines of 'check-matrix.py --p3-tests'; the script stops with the
# checker's message when it fails or prints something that is not a test path under msil/test.
function Get-P3Tests {
  $checker = Join-Path $MsilRepo 'msil/test/M2/08-semantics/Inputs/check-matrix.py'
  $r = Invoke-Captured $MsilPython @($checker, '--p3-tests', $MsilRepo)
  if ($r.Rc -ne 0) {
    $msg = @(@($r.Err) + @($r.Out) | ForEach-Object { "$_".Trim() } | Where-Object { $_ }) -join '; '
    throw ("check-matrix.py --p3-tests exited $($r.Rc), so the legs cannot take `$p3Tests (plan " +
           "5.17.6): $(if ($msg) { $msg } else { 'no message' })")
  }
  $tests = @()
  foreach ($line in $r.Out) {
    $p = "$line".Trim()
    if (-not $p) { continue }
    if ($p -notmatch '^msil/test/[^\\]+$' -or $p -match '(^|/)\.\.?(/|$)') {
      throw "check-matrix.py --p3-tests printed '$p', which is not a repository-relative test path under msil/test"
    }
    $tests += $p
  }
  return $tests
}

# $p3Out (see the header): 'MSIL :: <path below msil/test>$' for each test, escaped, joined.
function ConvertTo-P3Out([string[]]$Tests) {
  @($Tests | ForEach-Object { 'MSIL :: ' + (ConvertTo-LitRegex $_.Substring('msil/test/'.Length)) + '$' }) -join '|'
}

# A run in -Describe's JSON (plan 5.17.6).
function ConvertTo-RunDescription($r) {
  $params = [ordered]@{}
  foreach ($p in $r.Params) {
    $i = "$p".IndexOf('=')
    if ($i -lt 1) { throw "a lit parameter without a name or '=': '$p'" }
    $params[$p.Substring(0, $i)] = $p.Substring($i + 1)
  }
  $pipeline = if ($params.Contains('msil_pipeline')) { $params['msil_pipeline'] } else { 'obj' }
  return [ordered]@{
    paths = @($r.Paths)
    lit = @($r.Lit)
    params = $params
    pipeline = $pipeline
    prog_pipeline = $(if ($pipeline -eq 'lto') { 'lto' } else { 'obj' })
    triple = $(if ($params.Contains('msil_triple')) { $params['msil_triple'] } else { $defaultTriple })
    runner = $(if ($params.Contains('msil_runner')) { $params['msil_runner'] } else { 'local' })
    also_run = @("$($params['msil_also_run'])" -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
  }
}

# -Run's usage error: the message on stderr, exit status 2, nothing run.
function Exit-Usage([string]$Message) {
  [Console]::Error.WriteLine("lit-legs.ps1: usage error: $Message")
  exit 2
}

$selected = @($Leg | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$known = $legs | ForEach-Object { $_.Name }
foreach ($s in $selected) {
  if ($removedLegs.Contains($s)) {
    throw "leg '$s' was removed in M23 (plan 5.12.11): $($removedLegs[$s]) runs its tests and runtimes now"
  }
  if ($known -notcontains $s) { throw "unknown leg '$s' (legs: $($known -join ', '))" }
}
if ($List) {
  foreach ($l in $legs) {
    $tag = if ($l.Tag) { " (-$($l.Tag))" } else { '' }
    Write-Output "$($l.Name)$tag"
  }
  exit 0
}
# -Run (see the header): the 1-based run numbers, sorted, each once.
$runIdx = @()
if ($PSBoundParameters.ContainsKey('Run')) {
  $runText = @($Run | ForEach-Object { "$_" -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
  if ($selected.Count -ne 1) {
    Exit-Usage "-Run needs exactly one -Leg (got $(if ($selected.Count) { $selected -join ', ' } else { 'none' }))"
  }
  if ($runText.Count -eq 0) { Exit-Usage '-Run names no run' }
  $target = $legs | Where-Object { $_.Name -eq $selected[0] } | Select-Object -First 1
  $count = Get-LegRunCount $target
  foreach ($t in $runText) {
    $k = 0
    if ($count -eq 0) { Exit-Usage "-Run $t`: leg '$($target.Name)' has no lit runs" }
    if (-not [int]::TryParse($t, [ref]$k) -or $k -lt 1 -or $k -gt $count -or "$k" -ne $t) {
      Exit-Usage "-Run $t`: leg '$($target.Name)' has $count lit run(s), numbered 1 to $count"
    }
    $runIdx += $k
  }
  $runIdx = @($runIdx | Sort-Object -Unique)
}

# The legs are built: $p3Tests and $p3Out (the script stops here when the checker fails).
$p3Tests = @(Get-P3Tests)
$p3Out = ConvertTo-P3Out $p3Tests

if ($Describe) {
  $script:Describing = $true
  $described = @()
  foreach ($l in $legs) {
    $kind = Get-LegKind $l
    $runs = @()
    if ($kind -eq 'lit') { $runs = @(Get-LegRuns $l | ForEach-Object { ConvertTo-RunDescription $_ }) }
    $described += [ordered]@{ name = $l.Name; tag = "$($l.Tag)"; kind = $kind; runs = $runs }
  }
  $doc = [ordered]@{ version = 1; script = 'lit-legs.ps1'; legs = $described }
  Write-Output ($doc | ConvertTo-Json -Depth 16)
  exit 0
}

$toRun = @($legs | Where-Object {
  $l = $_
  if ($selected.Count -gt 0) { $selected -contains $l.Name }
  else {
    switch ($l.Tag) {
      'Wsl' { [bool]$Wsl }
      'Full' { [bool]$Full }
      'Exit' { [bool]$Exit -and ([bool]$Wsl -or -not (Test-WslLeg $l)) }
      'Portable' { [bool]$Portable -and ([bool]$Wsl -or -not (Test-WslLeg $l)) }
      'Quick' { $false }
      default { $true }
    }
  }
})

$results = Join-Path $BuildDir 'lit-results'
New-Item -ItemType Directory -Force -Path $results | Out-Null
$msilLit = Join-Path $MsilRepo 'msil/utils/build/msil-lit.ps1'
$retryScript = Join-Path $PSScriptRoot 'lit-retry.py'

# The response-file lines of a lit run: its parameters, its lit arguments, more arguments and the
# xunit file. One argument per line, the way lit's argument parser reads '@<file>'
# (fromfile_prefix_chars in lit/cl_arguments.py): a blank inside a value stays inside it.
function Get-RspLines($r, [string[]]$More, [string]$Xml) {
  @($r.Params | ForEach-Object { "--param=$_" }) + @($r.Lit) + @($More | Where-Object { $_ }) +
    "--xunit-xml-output=$($Xml.Replace('\', '/'))"
}

# One msil-lit.ps1 run: writes the response file, runs lit over the paths with '-sv' (and the
# extra lit arguments) and the response file, tees the output into the log and returns lit's exit
# status. -NoJobs leaves out -Jobs (the retry runs with -j1).
function Invoke-MsilLit([string]$Rsp, [string[]]$RspLines, [string[]]$Paths, [string]$Log,
                        [string]$Label, [string[]]$ExtraLitArgs = @(), [switch]$NoJobs) {
  foreach ($a in $RspLines) {
    if ($a -match '[\r\n]') { throw "lit argument with a line break: '$a'" }
  }
  Set-Content -LiteralPath $Rsp -Value $RspLines -Encoding utf8
  # '-LitArgs:<value>', not '-LitArgs <value>': 'pwsh -File' takes an argument that starts with
  # '-' and contains ':' (a path such as D:/...) for '-<name>:<value>', so the value would be
  # missing.
  $litArgsParam = "-LitArgs:$((@('-sv') + $ExtraLitArgs + "@$($Rsp.Replace('\', '/'))") -join ' ')"
  $jobsArgs = if ($Jobs -gt 0 -and -not $NoJobs) { @('-Jobs', "$Jobs") } else { @() }
  Write-Host "[msil-ci] leg $Label`: msil-lit.ps1 `"$litArgsParam`" $($Paths -join ' ')"
  Write-Host "[msil-ci] $Rsp`: $($RspLines -join ' ')"
  & pwsh -NoProfile -File $msilLit -BuildDir $BuildDir @jobsArgs $litArgsParam @Paths 2>&1 |
    ForEach-Object { "$_" } | Tee-Object -FilePath $Log | Out-Host
  return $LASTEXITCODE
}

# A retry file's stem (see the header): the full lit name with every run of characters other
# than letters, digits, '.' and '-' written '_', made unique within its run.
function Get-RetryStem([string]$Name, $Used) {
  $s = [regex]::Replace($Name, '[^A-Za-z0-9.\-]+', '_').Trim('_')
  if (-not $s) { $s = 'test' }
  $stem = $s
  for ($n = 2; -not $Used.Add($stem); $n++) { $stem = "$s-$n" }
  return $stem
}

# One lit run of a leg and, after a failure, the timeout retry (see the header). Returns
# @{ Ok; Detail; Retried (the names re-run, when every re-run passed); Logs (their logs) }.
function Invoke-LitRun($r, [string]$Stem, [string]$RetryPrefix, [string]$Label) {
  $rsp = Join-Path $results "$Stem.params.rsp"
  $xml = Join-Path $results "$Stem.xml"
  $log = Join-Path $results "$Stem.log"
  Remove-Item -LiteralPath $xml -Force -ErrorAction SilentlyContinue
  Get-ChildItem -LiteralPath $results -File |
    Where-Object { $_.Name.StartsWith($RetryPrefix, [StringComparison]::OrdinalIgnoreCase) } |
    Remove-Item -Force
  $rc = Invoke-MsilLit -Rsp $rsp -RspLines (Get-RspLines $r @() $xml) -Paths $r.Paths -Log $log -Label $Label
  $counts = Get-MsilLitCounts -Lines (Get-Content -LiteralPath $log)
  $passed = [int]$counts['Passed']
  $parts = @($counts.Keys | Sort-Object | ForEach-Object { "$($_.ToLower()) $($counts[$_])" })
  $detail = "$(if ($parts) { $parts -join ', ' } else { 'no summary' }); lit exit $rc"
  $none = @{ Ok = $false; Detail = $detail; Retried = @(); Logs = @() }
  if ($rc -eq 0) {
    if ($passed -ge 1) { $none.Ok = $true } else { $none.Detail += '; no test passed' }
    return $none
  }
  # A configuration error (lit_config.fatal, e.g. a missing tool under msil_require_verify=1).
  $fatal = @(Select-String -LiteralPath $log -Pattern ': fatal: (.*)$' | Select-Object -First 1)
  if ($fatal.Count -gt 0) { $none.Detail += "; fatal: $($fatal[0].Matches[0].Groups[1].Value)" }
  if (-not (Test-Path -LiteralPath $xml -PathType Leaf)) {
    $none.Detail += '; no xunit file, so no retry'
    return $none
  }
  $sel = Invoke-Captured $MsilPython @($retryScript, 'select', $xml, '--max', '3')
  $names = @($sel.Out | ForEach-Object { "$_".Trim() } | Where-Object { $_ })
  $why = @($sel.Err | ForEach-Object { "$_".Trim() } | Where-Object { $_ }) -join '; '
  if ($sel.Rc -eq 3) {
    $none.Detail += "; $($names.Count) tests timed out; re-run the leg on a quieter host"
    return $none
  }
  if ($sel.Rc -eq 4) {
    $m = [regex]::Match($why, 'lit-retry: (\d+) failed test')
    $n = if ($m.Success) { $m.Groups[1].Value } else { 'a' }
    $none.Detail += "; $n failed test(s) not timeout-only, so nothing was re-run"
    return $none
  }
  if ($sel.Rc -ne 0) {
    $none.Detail += "; lit-retry.py select exited $($sel.Rc)$(if ($why) { ": $why" })"
    return $none
  }
  if ($names.Count -eq 0) {
    $none.Detail += '; the xunit file lists no failed test, so no retry'
    return $none
  }
  # Only a run that exited 1 (failed tests only) and ran every test is retried (see the header), so
  # a re-run never passes a run whose other tests did not run: lit exits 2 after an error in tests,
  # such as 'a worker process crashed', and 1 after --max-failures or --max-time (which LIT_OPTS
  # can pass), and either way writes every test it had not run as <skipped message="Skipped"/>
  # (an excluded or unsupported test has another message), on a line of its own after two spaces;
  # only such lines count, so a failure's text that quotes the element does not.
  if ($rc -ne 1) {
    $none.Detail += '; lit reported an error, so no retry'
    return $none
  }
  $notRun = @(Select-String -LiteralPath $xml -CaseSensitive `
                -Pattern '^  <skipped message="Skipped"/>').Count
  if ($notRun -gt 0) {
    $none.Detail += "; lit did not run $notRun test(s), so no retry"
    return $none
  }
  $used = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
  $ok = @(); $okLogs = @(); $bad = @()
  foreach ($name in $names) {
    $file = $RetryPrefix + (Get-RetryStem $name $used)
    $rLog = Join-Path $results "$file.log"
    $rXml = Join-Path $results "$file.xml"
    $filter = '--filter=^' + (ConvertTo-LitRegex $name) + '$'
    $rrc = Invoke-MsilLit -Rsp (Join-Path $results "$file.params.rsp") -RspLines (Get-RspLines $r @($filter) $rXml) `
             -Paths $r.Paths -Log $rLog -Label "$Label (re-run after timeout: $name)" -ExtraLitArgs @('-j1') -NoJobs
    $rPassed = [int](Get-MsilLitCounts -Lines (Get-Content -LiteralPath $rLog))['Passed']
    if ($rrc -eq 0 -and $rPassed -ge 1) { $ok += $name; $okLogs += $rLog.Replace('\', '/') }
    else { $bad += "$name ($($rLog.Replace('\', '/')); lit exit $rrc)" }
  }
  if ($bad.Count -gt 0) {
    $none.Detail += "; re-run after timeout failed: $($bad -join ', ')"
    if ($ok.Count -gt 0) { $none.Detail += "; re-run after timeout passed: $($ok -join ', ') ($($okLogs -join ', '))" }
    return $none
  }
  return @{ Ok = $true; Detail = $detail; Retried = $ok; Logs = $okLogs }
}

# A lit leg: its runs (or the -Run ones), one row; run k of a leg of several runs writes
# <leg>-run<k>.* and retries into <leg>-retry-run<k>-<stem>.*.
function Invoke-LitLeg($l, [int[]]$Only) {
  # msil-lit.ps1 splits -LitArgs at blanks, and the response file lives in the build directory.
  if ($BuildDir -match '\s') {
    throw "the build directory '$BuildDir' contains a blank, which msil-lit.ps1 would split inside -LitArgs"
  }
  $runs = @(Get-LegRuns $l)
  $multi = $runs.Count -gt 1
  $done = @()
  for ($k = 1; $k -le $runs.Count; $k++) {
    if ($Only.Count -gt 0 -and $Only -notcontains $k) { continue }
    if ($multi) {
      $x = Invoke-LitRun $runs[$k - 1] "$($l.Name)-run$k" "$($l.Name)-retry-run$k-" "$($l.Name) run $k"
    } else {
      $x = Invoke-LitRun $runs[$k - 1] $l.Name "$($l.Name)-retry-" $l.Name
    }
    $x.K = $k
    $done += $x
  }
  $ok = $done.Count -gt 0 -and -not ($done | Where-Object { -not $_.Ok })
  $retried = @($done | ForEach-Object { $_.Retried })
  $logs = @($done | ForEach-Object { $_.Logs })
  $parts = foreach ($x in $done) {
    $d = $x.Detail
    # In a failed leg, a run whose re-runs passed says so in its own part.
    if (-not $ok -and @($x.Retried).Count -gt 0) {
      $d += "; re-run after timeout passed: $($x.Retried -join ', ') ($($x.Logs -join ', '))"
    }
    if ($multi) { "run $($x.K): $d" } else { $d }
  }
  $detail = @($parts) -join '; '
  if ($ok -and $retried.Count -gt 0) {
    $detail = "retried after timeout: $($retried -join ', ') ($($logs -join ', ')); $detail"
  }
  return @{ Ok = [bool]$ok; Detail = $detail }
}

function Invoke-ExpectedLeg($l) {
  # update-expected.py builds natively with the just-built clang and compares at -O0 and -O2:
  # 'expected' with the Windows oracle (LIB from the VS environment, which msil-env.ps1 or
  # ilammy/msvc-dev-cmd provides, and the VS clang's compiler-rt builtins; gate step 12 of M10
  # and M11; the DLL CRT oracle in hosted directories), 'expected-linux' with the E7 Linux oracle
  # under WSL (M12 gate step 14; E7h with the glibc sysroot in hosted directories). Both check
  # M2's %{msil_prog} tests too (from M14; msil-prog.py --mode native builds them) and
  # msil/test/M3 (M24; plan 5.17.6, hosted mode as lit decides it).
  $log = Join-Path $results "$($l.Name).log"
  $clang = Join-Path $BuildDir 'bin/clang.exe'
  $testPaths = $m1 + $m2 + $m3
  $osArgs = @()
  $what = "update-expected.py --check --levels O0,O2 $($testPaths -join ' ')"
  if ($l.Expected -eq 'linux') {
    $osArgs = @('--os', 'linux', '--linux-sysroot', $linuxSysroot, '--glibc-sysroot', $glibcSysroot)
    $what = "update-expected.py --check --levels O0,O2 --os linux $($testPaths -join ' ')"
  }
  $lock = Enter-MsilBuildLock -BuildDir $BuildDir -Shared -What $what
  try {
    & $MsilPython (Join-Path $MsilRepo 'msil/utils/update-expected.py') --clang $clang --check `
      --levels O0,O2 @osArgs @($testPaths | ForEach-Object { Join-Path $MsilRepo $_ }) 2>&1 |
      ForEach-Object { "$_" } | Tee-Object -FilePath $log | Out-Host
    $rc = $LASTEXITCODE
  } finally { $lock.Dispose() }
  # 'rule: <file> (<reason>)' counts as a match (a hand-written expected file under
  # %{msil_expected_rule}, plan 5.17.4).
  $ok = @(Select-String -LiteralPath $log -Pattern '^(ok|rule): ').Count
  $bad = @(Select-String -LiteralPath $log -Pattern '^(MISMATCH|MISSING|FAIL|REFUSED)\b').Count
  $detail = "$what`: $ok ok, $bad failed (exit $rc)"
  if ($rc -eq 0 -and $ok -lt 1) { $detail += '; nothing matched' }
  return @{ Ok = ($rc -eq 0 -and $ok -ge 1); Detail = $detail }
}

function Invoke-UtcLeg($l) {
  # lit cannot run these tests on Windows (llvm/test/tools/UpdateTestChecks/lit.local.cfg sets
  # its llc-binary feature only when a file named 'llc' exists); run-utc-tests.py runs their RUN
  # lines without lit (plan M5.b). It needs llc and llvm-config from the build.
  $log = Join-Path $results "$($l.Name).log"
  $llc = Join-Path $BuildDir 'bin/llc.exe'
  $lock = Enter-MsilBuildLock -BuildDir $BuildDir -Shared -What 'run-utc-tests'
  try {
    & $MsilPython (Join-Path $MsilRepo 'msil/utils/run-utc-tests.py') --llc $llc @utcTests 2>&1 |
      ForEach-Object { "$_" } | Tee-Object -FilePath $log | Out-Host
    $rc = $LASTEXITCODE
  } finally { $lock.Dispose() }
  $summary = @(if (Test-Path -LiteralPath $log) {
                 Select-String -LiteralPath $log -Pattern '^\d+ of \d+ tests passed$' |
                   Select-Object -Last 1
               })
  $what = if ($summary.Count -gt 0) { $summary[0].Line } else { 'no summary' }
  return @{ Ok = ($rc -eq 0); Detail = "run-utc-tests.py: $what (exit $rc)" }
}

function Invoke-EncoderLeg($l) {
  # The encoder parity check (plan M18.f): cli-corpus-check.py --mode encoder over msil/test/M1
  # at -O0 and -O2 with this build's clang, llc and llvm-cil-dump, under the shared build-dir
  # lock. The script fails when a method differs from ilasm's, a program cannot be compiled or
  # assembled, or fewer than $encoderMinMethods methods were compared.
  if (-not $IsWindows) { throw 'encoder-parity runs only on Windows (it needs the .NET Framework ilasm)' }
  $log = Join-Path $results "$($l.Name).log"
  $bin = Join-Path $BuildDir 'bin'
  $what = 'cli-corpus-check.py --mode encoder msil/test/M1'
  $lock = Enter-MsilBuildLock -BuildDir $BuildDir -Shared -What $what
  try {
    & $MsilPython (Join-Path $MsilRepo 'msil/utils/mdtools/cli-corpus-check.py') --mode encoder `
      --levels O0,O2 --min-methods $encoderMinMethods --clang (Join-Path $bin 'clang.exe') `
      --llc (Join-Path $bin 'llc.exe') --cil-dump (Join-Path $bin 'llvm-cil-dump.exe') `
      (Join-Path $MsilRepo 'msil/test/M1') 2>&1 | ForEach-Object { "$_" } |
      Tee-Object -FilePath $log | Out-Host
    $rc = $LASTEXITCODE
  } finally { $lock.Dispose() }
  $summary = @(if (Test-Path -LiteralPath $log) {
                 Select-String -LiteralPath $log -Pattern '^cli-corpus-check\.py: encoder: \d+ of \d+ methods' |
                   Select-Object -Last 1
               })
  $detail = if ($summary.Count -gt 0) { $summary[0].Line -replace '^cli-corpus-check\.py: ', '' }
            else { 'no summary' }
  return @{ Ok = ($rc -eq 0 -and $summary.Count -gt 0); Detail = "$detail (exit $rc)" }
}

function Invoke-ObjTextLeg($l) {
  # The object checks (plan M19.f): obj-text runs cli-corpus-check.py --mode object over
  # msil/test/M1 at -O0 and -O2, then over msil/test/M1/11-programs for the Linux triple;
  # obj-text-wsl runs --mode gnu-tools, with GNU nm and ar inside WSL, over the same two inputs
  # (exit criterion 4 of M19). Every run uses this build's clang, llvm-cil-dump, llvm-nm and
  # llvm-ar under the shared build-dir lock, and every run must exit 0 and print its summary
  # line, so the leg needs both summary lines of its mode. --mode object needs mdcanon,
  # which build-mdtools.py builds or refreshes first, as lit does (nothing when it is up to date).
  if (-not $IsWindows) {
    throw "$($l.Name) runs only on Windows (the .NET Framework ilasm, or WSL for GNU nm and ar)"
  }
  $log = Join-Path $results "$($l.Name).log"
  [System.IO.File]::WriteAllText($log, '')
  $bin = Join-Path $BuildDir 'bin'
  $mdtools = Join-Path $BuildDir 'tools/msil/mdtools'
  $m1Dir = Join-Path $MsilRepo 'msil/test/M1'
  $tools = @('--clang', (Join-Path $bin 'clang.exe'), '--cil-dump', (Join-Path $bin 'llvm-cil-dump.exe'),
             '--llvm-nm', (Join-Path $bin 'llvm-nm.exe'))
  $mode = $l.ObjText
  $modeArgs = @('--mode', $mode, '--levels', 'O0,O2') + $tools
  if ($mode -eq 'object') {
    $modeArgs += @('--llvm-ar', (Join-Path $bin 'llvm-ar.exe'), '--mdtools', $mdtools)
  }
  $runs = @(
    @{ What = "cli-corpus-check.py --mode $mode msil/test/M1"; Args = $modeArgs + @($m1Dir) }
    @{ What = "cli-corpus-check.py --mode $mode --triple x86_64-unknown-linux-gnu-msil msil/test/M1/11-programs"
       Args = $modeArgs + @('--triple', 'x86_64-unknown-linux-gnu-msil',
                            (Join-Path $MsilRepo 'msil/test/M1/11-programs')) }
  )
  $script = Join-Path $MsilRepo 'msil/utils/mdtools/cli-corpus-check.py'
  $codes = @()
  $lock = Enter-MsilBuildLock -BuildDir $BuildDir -Shared -What "lit-legs $($l.Name)"
  try {
    if ($mode -eq 'object') {
      & $MsilPython (Join-Path $MsilRepo 'msil/utils/mdtools/build-mdtools.py') --out $mdtools 2>&1 |
        ForEach-Object { "$_" } | Tee-Object -FilePath $log -Append | Out-Host
      if ($LASTEXITCODE -ne 0) {
        return @{ Ok = $false; Detail = "build-mdtools.py --out $mdtools exited $LASTEXITCODE" }
      }
    }
    foreach ($r in $runs) {
      Write-Host "[msil-ci] leg $($l.Name): $($r.What)"
      & $MsilPython $script @($r.Args) 2>&1 | ForEach-Object { "$_" } |
        Tee-Object -FilePath $log -Append | Out-Host
      $codes += $LASTEXITCODE
    }
  } finally { $lock.Dispose() }
  $summaries = @(Select-String -LiteralPath $log -Pattern "^cli-corpus-check\.py: $mode`: \d+ of \d+ " |
                 ForEach-Object { $_.Line -replace '^cli-corpus-check\.py: ', '' -replace '\): .*$', ')' })
  $failed = @($codes | Where-Object { $_ -ne 0 })
  $detail = if ($summaries.Count -gt 0) { $summaries -join ' | ' } else { 'no summary' }
  return @{ Ok = ($failed.Count -eq 0 -and $summaries.Count -eq $runs.Count)
            Detail = "$detail (exit $($codes -join ', '))" }
}

# A leg's Setup (the portable legs): each step's script with '--build-dir <build>' and its
# arguments, in order, logged one after the other to <build>/lit-results/<leg>.setup.log. Returns
# $null when every step passed, else the row's detail, which names the first step that failed.
function Invoke-LegSetup($l) {
  $log = Join-Path $results "$($l.Name).setup.log"
  Remove-Item -LiteralPath $log -Force -ErrorAction SilentlyContinue
  foreach ($step in @($l.Setup)) {
    $script = Join-Path $MsilRepo $step.Script
    $stepArgs = @($step.Args)
    $setupArgs = @('--build-dir', $BuildDir) + $stepArgs
    $what = "$(Split-Path -Leaf $script) $($stepArgs -join ' ')".TrimEnd()
    Write-Host "[msil-ci] leg $($l.Name): setup: $what"
    & $MsilPython $script @setupArgs 2>&1 | ForEach-Object { "$_" } |
      Tee-Object -FilePath $log -Append | Out-Host
    $rc = $LASTEXITCODE
    if ($rc -ne 0) { return "setup failed: $what (exit $rc; $($log.Replace('\', '/')))" }
  }
  return $null
}

foreach ($l in $toRun) {
  $rowName = if ($runIdx.Count -gt 0) { "$($l.Name) (run $($runIdx -join ','))" } else { $l.Name }
  # The ctest legs while MSIL_CORPUS_SKIP names c-testsuite: a SKIP row (ci-common.ps1), in the
  # summary table, $env:MSIL_CI_RESULTS and the GitHub step summary, but no failure.
  if ($l.CorpusEntry -and (@(Get-CorpusSkip) -contains $l.CorpusEntry)) {
    Write-Host "$($l.Name): skipped ($($l.CorpusEntry) is in MSIL_CORPUS_SKIP)"
    Add-MsilCiResult -Step lit-legs -Name $rowName -Status SKIP -Seconds 0 `
      -Detail "$($l.CorpusEntry) is in MSIL_CORPUS_SKIP (plan section 10 item 11); no lit run"
    continue
  }
  $sw = [Diagnostics.Stopwatch]::StartNew()
  try {
    $setupFailed = if ($l.Setup) { Invoke-LegSetup $l } else { $null }
    $r = if ($setupFailed) { @{ Ok = $false; Detail = $setupFailed } }
         elseif ($l.Expected) { Invoke-ExpectedLeg $l } elseif ($l.Utc) { Invoke-UtcLeg $l }
         elseif ($l.Encoder) { Invoke-EncoderLeg $l } elseif ($l.ObjText) { Invoke-ObjTextLeg $l }
         else { Invoke-LitLeg $l $runIdx }
  } catch {
    $r = @{ Ok = $false; Detail = "$_" }
  }
  Add-MsilCiResult -Step lit-legs -Name $rowName -Status $(if ($r.Ok) { 'PASS' } else { 'FAIL' }) `
    -Seconds $sw.Elapsed.TotalSeconds -Detail $r.Detail
}

Write-MsilCiSummary -Title 'MSIL CI: test legs'
# Test-MsilCiFailed counts FAIL rows only: the ctest legs' SKIP rows are no failure.
exit $(if (Test-MsilCiFailed) { 1 } else { 0 })

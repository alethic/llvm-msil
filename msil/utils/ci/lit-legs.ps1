# lit-legs.ps1 - the test legs of the MSIL CI (Windows job) and of the phase gate: the test-leg
# step of msil-ci.yml and of msil/utils/run-m1-gate.ps1.
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1             # the Windows CI legs
#   pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1 -Wsl        # and the WSL legs (dev host)
#   pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1 -Full       # and the nightly legs
#   pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1 -Leg programs,ctest
#   pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1 -List
#
# The leg set of the Phase 2 exit (plan 5.12.10, 5.12.11; M23). CIL objects are the default
# pipeline (msil_pipeline=obj, msil_link_output=pe; Q31), so every Phase 1 leg runs through
# clang -c and msil-ld, and -S plus ilasm is a test oracle only (asm-oracle, modern-ilasm,
# wsl-ilasm, the reference side of O3 and O4, the round trip). One verified build serves several
# runtimes: msil_also_run repeats jitall and the run of every composite on further .NET runtimes
# against the same expected file. M23 gate step 16 (run-m1-gate.ps1) runs every leg below except
# the -Quick and -Full ones; step 12 checks this list. Legs, in order, with the minutes the two
# phase-gate runs of M23 took on the development host (gate-1 and gate-2 of 2026-10-27, first
# and second figure; the 4-core host was shared with other work, so the spread is load, not the
# legs). Together the Windows legs took 144 and 217 minutes and the -Wsl legs 79 and 117, against
# plan M23's estimate of 53 and 22 (Rebuild cost): the composites under obj with the equivalence
# oracles and the msil_also_run runtimes cost several times the pre-M23 legs' asm composites per
# test. The user accepted the measured time in place of section 10's Q32 budget of 70-90 minutes
# for the whole gate (plan section 12, row 'Q32 amended').
#   net8           every MSIL FileCheck directory (llvm/test/CodeGen/MSIL, clang/test/CodeGen/MSIL,
#                  the clang driver tests, lld/test/MSIL, llvm/test/MC/MSIL, the llvm-cil-dump and
#                  llvm-cil-copy tests) and msil/test (M1, M2 and the harness; msil/test/Programs
#                  is filtered out: the corpus legs run it) under obj on .NET 8 (tiered) and on the
#                  .NET Framework 4.8, -O0 and -O2, native oracle required (with the VS clang's
#                  compiler-rt builtins), msil_equiv=1 (O3 on the --no-gc-sections equivalence link
#                  and O6), msil_also_run=10,8+tc0 (.NET 10, and .NET 8 with
#                  DOTNET_TieredCompilation=0, on the same verified images), msil_scale_tests=1
#                  (msil/test/M2/00-link/spill-limits.test at the real CLR limits; no other leg
#                  sets it, plan 5.12.2) and msil_require_csc=1 (the C# consumer of
#                  msil/test/M2/04-shared and harness/csc.test)                  (60, 87)
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
#   expected       update-expected.py --check msil/test/M1 msil/test/M2: .expected vs the Windows
#                  native oracle, with the compiler-rt builtins (M2's %{msil_prog} tests built by
#                  msil-prog.py --mode native)                                    (4.5, 5.5)
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
#   lto            msil/test/M1/11-programs and msil/test/M2 under lto (clang -flto -c and the LTO
#                  link of msil-ld into its image) on .NET 8 and on the .NET Framework 4.8, native
#                  oracle required, msil_equiv=1 (O4: the --no-gc-sections LTO image against
#                  ilasm's image of the same link's --lto-emit-asm text) and msil_also_run=10;
#                  .ll tests are filtered out (lto runs them on llc text)          (11, 22.5)
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
#                  monocypher, coremark, lz4, blake2, tweetnacl, kissfft, jsmn) under obj, -O0 and
#                  -O2, on .NET 8 and the .NET Framework 4.8, msil_also_run=10, native oracle
#                  required (corpus leg)                                            (2, 4.5)
#   programs-lto   the same under lto, with msil_equiv=1 (O4) (corpus leg)          (4, 6.5)
#   ctest          msil/test/Programs/c-testsuite: c-testsuite's 150 libc-free tests (and their
#                  diag entries) under obj, -O0 and -O2, on .NET 8, native oracle required (corpus
#                  leg). While $env:MSIL_CORPUS_SKIP names c-testsuite the leg prints
#                  'ctest: skipped (c-testsuite is in MSIL_CORPUS_SKIP)', runs no lit and is
#                  reported as SKIP, not PASS                                        (9, 18)
#   wsl-net10      -Wsl: msil/test/M1, msil/test/M2 and msil/test/harness on
#                  x86_64-unknown-linux-gnu-msil under obj, WSL .NET 10, E7 Linux oracle required,
#                  msil_equiv=1 (O3; O6 and ilasm on the host), msil_also_run=8,10+tc0 (WSL .NET 8,
#                  and .NET 10 with DOTNET_TieredCompilation=0)                  (56, 103)
#   wsl-ilasm      -Wsl: msil/test/M1/11-programs under asm, assembled by the Linux ilasm inside
#                  WSL                                                              (2, 1)
#   expected-linux -Wsl: update-expected.py --check --os linux msil/test/M1 msil/test/M2: the E7
#                  Linux oracle vs <stem>.linux.expected, else <stem>.expected    (2.5, 1.5)
#   obj-text-wsl   -Wsl: cli-corpus-check.py --mode gnu-tools over msil/test/M1 at -O0 and -O2,
#                  then over msil/test/M1/11-programs for x86_64-unknown-linux-gnu-msil: GNU nm
#                  inside the WSL distribution prints llvm-nm's letters for every clang -c object,
#                  and GNU ar t and nm -s list the members and the index of an archive of each
#                  level's objects                                                (0.5, 0.5)
#   lto-wsl        -Wsl: msil/test/M1/11-programs, msil/test/M2/00-link (the six 00-link rows of
#                  matrix.txt under LTO on the Linux triple; added after gate-2, about +2),
#                  msil/test/M2/02-lto and msil/test/M2/08-semantics under lto, WSL .NET 10, E7
#                  required, msil_also_run=8; O4 over these paths runs in lto     (9, 6)
#   programs-wsl   -Wsl: the nine programs under obj, WSL .NET 10, E7 required, msil_also_run=8
#                  (corpus leg)                                                     (3, 2)
#   programs-lto-wsl  -Wsl: the same under lto, with msil_equiv=1 (O4; added after gate-2,
#                  about +1 to +3) (corpus leg)                                  (3.5, 2)
#   det-wsl        -Wsl: msil/test/M2/06-det and msil/test/Programs/det for the Linux triple
#                  (corpus leg)                                                     (1, 0.5)
#   hosted         -Wsl, optional (plan Q16, 5.12.11): msil/test/M2/09-hosted, the hosted
#                  compile-only tests (glibc headers and libc.so, UCRT headers), with
#                  msil_glibc_sysroot given: $env:MSIL_GLIBC_SYSROOT, else
#                  C:\llvm-msil-scratch\glibc-sysroot, which msil/utils/make-glibc-sysroot.sh
#                  builds inside WSL (step G0 of the gates from M15), so a missing sysroot is a
#                  configuration error, never a leg of UNSUPPORTED tests; the CI never runs it
#   wsl-quick      -Quick (runs only when -Leg names it): msil/test/M1/11-programs,
#                  msil/test/harness and msil/test/M2 on WSL .NET 10, E7 required (plan 5.12.10 G4,
#                  the WSL step of the milestone gates)
#   all-levels     -Full: msil/test at O0,O1,O2,O3,Os,Oz (nightly)
#   lto-m1         -Full: all of msil/test/M1 under lto (11-programs runs in lto)
#   encoder-parity -Full: msil/utils/mdtools/cli-corpus-check.py --mode encoder (plan M18.f):
#                  every msil/test/M1 program at -O0 and -O2 through clang -S -emit-llvm and llc
#                  -show-mc-encoding, each method's bytes rebuilt from the encoding comments and
#                  compared with the .NET Framework ilasm's image of the same text, at least 2,600
#                  methods; once obj is the default, net8's O3 compares every M1 method's encoding
#                  with ilasm's anyway. Windows only (the Framework ilasm)
# Removed in M23, and -Leg <name> fails naming the leg that covers them: obj (net8), obj-wsl
# (wsl-net10), net10 (net8's msil_also_run=10), net8-tc0 (net8's 8+tc0), wsl-net10-tc0
# (wsl-net10's 10+tc0).
#
# The corpus legs (programs, programs-lto, ctest, det and their -Wsl twins) need the exit corpus
# of msil/utils/corpus/manifest.json, which msil/utils/corpus/fetch.py downloads into a cache
# outside the repository: they pass msil_corpus_dir=$env:MSIL_CORPUS_CACHE (when it is set; else
# lit's default, C:\llvm-msil-scratch\corpus-cache), msil_corpus_skip=$env:MSIL_CORPUS_SKIP and
# msil_require_corpus=1, so a missing or stale cache is a configuration error and an entry left
# out on purpose (the CI's MSIL_CORPUS_SKIP: coremark,c-testsuite, plan section 10 item 11 and
# 5.12.12) is UNSUPPORTED with a note. run-m1-gate.ps1 removes MSIL_CORPUS_SKIP from the legs'
# environment, so the phase gate runs all ten entries.
#
# The WSL legs need a WSL distribution (only the development host has one; the Linux CI job
# covers the Linux triple natively) with the private .NET runtimes of
# msil/utils/msil-wsl-dotnet.sh: every WSL lit leg passes msil_wsl_dotnet=private (plan 5.12.8,
# Q39), so a missing private runtime is a configuration error, never a run on the distribution's
# own dotnet. The WSL legs that run programs also require the E7 Linux oracle
# (msil_native_oracle=required), which, like expected-linux, needs the sysroot of
# msil/utils/make-linux-sysroot.py ($env:MSIL_LINUX_SYSROOT, else C:\llvm-msil-scratch\linux-sysroot,
# passed as msil_linux_sysroot) and an ld.lld (the tree's). run-m1-gate.ps1's host-setup step makes
# both (step G0 of the Phase 2 gates). modern-ilasm and wsl-ilasm take the .NET ilasm, and roundtrip
# and asm-oracle the .NET ildasm and ilasm (lit.cfg.py reads the version the same way), at the
# version that msil/utils/tools/msil-tools.proj pins (MsilILAsmVersion), from the NuGet
# global-packages folder ('dotnet restore msil/utils/tools/msil-tools.proj -p:MsilToolsRid=win-x64',
# and linux-x64 for wsl-ilasm); a newer version in that folder is never used, and a missing one fails
# the leg; nothing here downloads. obj-text takes the NuGet ildasm the same way, and the .NET
# Framework ildasm from the Windows SDK's NETFX 4.x Tools; obj-text-wsl needs only GNU nm and ar
# (binutils) in the distribution Ubuntu, the one cli-corpus-check.py and lit.cfg.py use by default.
#
# Every lit leg runs msil/utils/build/msil-lit.ps1 (shared build-dir lock, the command of the gate
# steps) with --param msil_require_verify=1 (plan D38), --param msil_require_exec=1 and --param
# msil_require_link=1 (plan 5.12.2, M14), so a missing verifier, jitall, compiler, runtime,
# msil-ld or llvm-ar is a configuration error instead of a leg of UNSUPPORTED tests, and writes
# <build>/lit-results/<leg>.xml (xunit) and <leg>.log. Its lit arguments go one per line
# ('--param=<k>=<v>', a leg's '--filter-out=<regex>', '--xunit-xml-output=<file>') into the
# response file <build>/lit-results/<leg>.params.rsp, which msil-lit.ps1 passes to lit as
# '-sv @<file>' (lit reads response files, as upstream-hooks.ps1 uses them), so a parameter value
# may contain blanks (a NuGet ilasm under a profile directory with a space; backlog 3.5); the
# build directory itself may not, because msil-lit.ps1 splits -LitArgs at blanks, and the leg
# fails naming it. A leg passes when lit exits 0 and at least one test passed. expected,
# expected-linux, utc, encoder-parity, obj-text and obj-text-wsl are not lit legs: each runs its
# script under the shared build-dir lock, writes <build>/lit-results/<leg>.log and passes when
# the script exits 0 (and, for the expected legs, at least one test matched; for encoder-parity,
# obj-text and obj-text-wsl, every run printed its summary line). Every selected leg runs even
# after a failure; the exit status is 1 if any leg failed (a SKIP row, the ctest leg's, is no
# failure).
[CmdletBinding()]
param(
  [string]$BuildDir = '',         # default: $env:MSIL_BUILD_DIR, else <repo>\build
  [switch]$Wsl,
  [switch]$Full,
  [string[]]$Leg = @(),           # run only these legs (names as in -List; comma-separated works)
  [switch]$List,
  [int]$Jobs = 0                  # lit -j (0: lit's default)
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\build\msil-env.ps1')
. (Join-Path $PSScriptRoot '..\build\msil-lock.ps1')
. (Join-Path $PSScriptRoot 'ci-common.ps1')
if (-not $BuildDir) { $BuildDir = $MsilBuild }
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

# check-msil's paths (msil/test/CMakeLists.txt); lit-legs.sh's check_msil is the same list.
$checkMsil = @('llvm/test/CodeGen/MSIL', 'clang/test/CodeGen/MSIL', 'clang/test/Preprocessor/init-msil.c',
               'clang/test/Driver/msil-toolchain.c', 'clang/test/Driver/msil-link.c',
               'clang/test/Driver/msil-lto.c', 'clang/test/Driver/msil-header-search.c',
               'clang/test/Driver/msil-object.c', 'clang/test/Driver/msil-shared.c', 'lld/test/MSIL',
               'llvm/test/tools/llvm-cil-dump', 'llvm/test/tools/llvm-cil-copy', 'llvm/test/MC/MSIL',
               'msil/test')
$m1 = @('msil/test/M1')
$m2 = @('msil/test/M2')
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
# The exit corpus (plan 5.12.9): the nine programs, their determinism tests and c-testsuite;
# lit-legs.sh has the same lists.
$corpusPrograms = @('zlib', 'tinf', 'monocypher', 'coremark', 'lz4', 'blake2', 'tweetnacl', 'kissfft',
                    'jsmn') | ForEach-Object { "msil/test/Programs/$_" }
$det = @('msil/test/M2/06-det', 'msil/test/Programs/det')
$ctest = @('msil/test/Programs/c-testsuite')
# net8 runs msil/test without its corpus tests, which the corpus legs run.
$noCorpus = '--filter-out=MSIL :: Programs/'
$linux = 'msil_triple=x86_64-unknown-linux-gnu-msil'
# The E7 Linux oracle's sysroot (msil/utils/make-linux-sysroot.py), defaulted as lit.cfg.py and
# run-m1-gate.ps1's host-setup step default it.
$linuxSysroot = if ($env:MSIL_LINUX_SYSROOT) { $env:MSIL_LINUX_SYSROOT } else { 'C:\llvm-msil-scratch\linux-sysroot' }
# Every WSL lit leg: the Linux triple on the private runtime of msil-wsl-dotnet.sh (plan 5.12.8).
$wslRuntime = @('msil_runner=wsl', $linux, 'msil_wsl_dotnet=private', 'msil_dotnet_runtime=10')
# The legs that require the E7 Linux oracle.
$e7 = @('msil_native_oracle=required', "msil_linux_sysroot=$($linuxSysroot.Replace('\', '/'))")
# The glibc sysroot of the hosted leg (msil/utils/make-glibc-sysroot.sh), defaulted as lit.cfg.py
# defaults it on Windows.
$glibcSysroot = if ($env:MSIL_GLIBC_SYSROOT) { $env:MSIL_GLIBC_SYSROOT } else { 'C:\llvm-msil-scratch\glibc-sysroot' }
# The Windows legs that run programs: .NET 8, the .NET Framework and the native oracle.
$winRun = @('msil_dotnet_runtime=8', 'msil_native_oracle=required', 'msil_run_netfx=1')

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

# Params and Paths are script blocks: tool paths and the corpus variables are resolved when the
# leg runs, so a missing package fails that leg only. Tag: '' (the default set), 'Wsl', 'Quick'
# (only when -Leg names it) or 'Full'.
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
     Params = { 'msil_pipeline=asm',
                "msil_ilasm=$(Get-MsilNuGetTool -Repo $MsilRepo -Rid win-x64 -Tool ilasm)" } }
  @{ Name = 'roundtrip'; Paths = { $roundtrip }; Params = { 'msil_pipeline=obj', 'msil_roundtrip=1' } }
  @{ Name = 'expected'; Expected = 'windows' }
  @{ Name = 'utc'; Utc = $true }
  @{ Name = 'obj-text'; ObjText = 'object' }
  @{ Name = 'lto'; Paths = { $programs + $m2 }; Lit = @('--filter-out=\.ll$')
     Params = { @('msil_pipeline=lto') + $winRun + @('msil_equiv=1', 'msil_also_run=10') } }
  @{ Name = 'det'; Paths = { $det }; Params = { @('msil_dotnet_runtime=8') + @(Get-CorpusParams) } }
  @{ Name = 'refs-net48'; Paths = { $refs }
     Params = { 'msil_pipeline=obj', 'msil_references=net48', 'msil_equiv=1', 'msil_run_netfx=1' } }
  @{ Name = 'refs-net8'; Paths = { $refsNet8 }
     Params = { 'msil_pipeline=obj', 'msil_references=net8', 'msil_dotnet_runtime=8', 'msil_also_run=10' } }
  @{ Name = 'refs-net10'; Paths = { $refs }
     Params = { 'msil_pipeline=obj', 'msil_references=net10', 'msil_dotnet_runtime=10' } }
  @{ Name = 'programs'; Paths = { $corpusPrograms }
     Params = { @('msil_pipeline=obj') + $winRun + @('msil_also_run=10') + @(Get-CorpusParams) } }
  @{ Name = 'programs-lto'; Paths = { $corpusPrograms }
     Params = { @('msil_pipeline=lto') + $winRun + @('msil_equiv=1', 'msil_also_run=10') +
                @(Get-CorpusParams) } }
  @{ Name = 'ctest'; Paths = { $ctest }; CorpusEntry = 'c-testsuite'
     Params = { @('msil_pipeline=obj', 'msil_dotnet_runtime=8', 'msil_native_oracle=required') +
                @(Get-CorpusParams) } }
  @{ Name = 'wsl-net10'; Paths = { $m1 + $m2 + @('msil/test/harness') }; Tag = 'Wsl'
     Params = { @('msil_pipeline=obj') + $wslRuntime + $e7 + @('msil_equiv=1', 'msil_also_run=8,10+tc0') } }
  @{ Name = 'wsl-ilasm'; Paths = { $programs }; Tag = 'Wsl'
     Params = { @('msil_pipeline=asm') + $wslRuntime +
                "msil_wsl_ilasm=$(Get-MsilNuGetTool -Repo $MsilRepo -Rid linux-x64 -Tool ilasm)" } }
  @{ Name = 'expected-linux'; Expected = 'linux'; Tag = 'Wsl' }
  @{ Name = 'obj-text-wsl'; ObjText = 'gnu-tools'; Tag = 'Wsl' }
  # lto-wsl also runs msil/test/M2/00-link (section 12, '5.12.11 amended (lto-wsl paths)'): the
  # six matrix rows there run under LTO on the Linux triple only here.
  @{ Name = 'lto-wsl'; Tag = 'Wsl'
     Paths = { $programs + @('msil/test/M2/00-link', 'msil/test/M2/02-lto',
                             'msil/test/M2/08-semantics') }
     Params = { @('msil_pipeline=lto') + $wslRuntime + $e7 + @('msil_also_run=8') } }
  @{ Name = 'programs-wsl'; Paths = { $corpusPrograms }; Tag = 'Wsl'
     Params = { @('msil_pipeline=obj') + $wslRuntime + $e7 + @('msil_also_run=8') + @(Get-CorpusParams) } }
  # O4 on the Linux triple's LTO links of the programs (section 12, '5.12.11 amended (O4 for the
  # Linux triple's programs)').
  @{ Name = 'programs-lto-wsl'; Paths = { $corpusPrograms }; Tag = 'Wsl'
     Params = { @('msil_pipeline=lto') + $wslRuntime + $e7 + @('msil_equiv=1', 'msil_also_run=8') +
                @(Get-CorpusParams) } }
  @{ Name = 'det-wsl'; Paths = { $det }; Tag = 'Wsl'; Params = { $wslRuntime + @(Get-CorpusParams) } }
  @{ Name = 'hosted'; Paths = { 'msil/test/M2/09-hosted' }; Tag = 'Wsl'
     Params = { "msil_glibc_sysroot=$($glibcSysroot.Replace('\', '/'))" } }
  @{ Name = 'wsl-quick'; Tag = 'Quick'; Params = { @('msil_pipeline=obj') + $wslRuntime + $e7 }
     Paths = { 'msil/test/M1/11-programs', 'msil/test/harness', 'msil/test/M2' } }
  @{ Name = 'all-levels'; Paths = { 'msil/test' }; Tag = 'Full'
     Params = { 'msil_opt_levels=O0,O1,O2,O3,Os,Oz' } }
  @{ Name = 'lto-m1'; Paths = { $m1 }; Tag = 'Full'; Lit = @('--filter-out=\.ll$')
     Params = { @('msil_pipeline=lto') + $winRun } }
  @{ Name = 'encoder-parity'; Encoder = $true; Tag = 'Full' }
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
$run = @($legs | Where-Object {
  if ($selected.Count -gt 0) { $selected -contains $_.Name }
  else {
    switch ($_.Tag) { 'Wsl' { [bool]$Wsl } 'Full' { [bool]$Full } 'Quick' { $false } default { $true } }
  }
})

$results = Join-Path $BuildDir 'lit-results'
New-Item -ItemType Directory -Force -Path $results | Out-Null
$msilLit = Join-Path $MsilRepo 'msil/utils/build/msil-lit.ps1'

function Invoke-LitLeg($l) {
  # msil-lit.ps1 splits -LitArgs at blanks, and the response file lives in the build directory.
  if ($BuildDir -match '\s') {
    throw "the build directory '$BuildDir' contains a blank, which msil-lit.ps1 would split inside -LitArgs"
  }
  $params = @(& $l.Params) + @('msil_require_verify=1', 'msil_require_exec=1', 'msil_require_link=1')
  $paths = @(& $l.Paths)
  $rsp = Join-Path $results "$($l.Name).params.rsp"
  $xml = (Join-Path $results "$($l.Name).xml").Replace('\', '/')
  # One argument per line, the way lit's argument parser reads '@<file>' (fromfile_prefix_chars
  # in lit/cl_arguments.py): a blank inside a value stays inside it.
  $rspLines = @($params | ForEach-Object { "--param=$_" }) + @($l.Lit | Where-Object { $_ }) +
              "--xunit-xml-output=$xml"
  foreach ($a in $rspLines) {
    if ($a -match '[\r\n]') { throw "lit argument with a line break: '$a'" }
  }
  Set-Content -LiteralPath $rsp -Value $rspLines -Encoding utf8
  $log = Join-Path $results "$($l.Name).log"
  # '-LitArgs:<value>', not '-LitArgs <value>': 'pwsh -File' takes an argument that starts with
  # '-' and contains ':' (a path such as D:/...) for '-<name>:<value>', so the value would be
  # missing.
  $litArgsParam = "-LitArgs:-sv @$($rsp.Replace('\', '/'))"
  $jobsArgs = if ($Jobs -gt 0) { @('-Jobs', "$Jobs") } else { @() }
  Write-Host "[msil-ci] leg $($l.Name): msil-lit.ps1 `"$litArgsParam`" $($paths -join ' ')"
  Write-Host "[msil-ci] $rsp`: $($rspLines -join ' ')"
  & pwsh -NoProfile -File $msilLit -BuildDir $BuildDir @jobsArgs $litArgsParam @paths 2>&1 |
    ForEach-Object { "$_" } | Tee-Object -FilePath $log | Out-Host
  $rc = $LASTEXITCODE
  $counts = Get-MsilLitCounts -Lines (Get-Content -LiteralPath $log)
  $passed = [int]$counts['Passed']
  $parts = @($counts.Keys | Sort-Object | ForEach-Object { "$($_.ToLower()) $($counts[$_])" })
  $detail = "$(if ($parts) { $parts -join ', ' } else { 'no summary' }); lit exit $rc"
  if ($rc -eq 0 -and $passed -lt 1) { $detail += '; no test passed' }
  # A configuration error (lit_config.fatal, e.g. a missing tool under msil_require_verify=1).
  $fatal = @(Select-String -LiteralPath $log -Pattern ': fatal: (.*)$' | Select-Object -First 1)
  if ($rc -ne 0 -and $fatal.Count -gt 0) { $detail += "; fatal: $($fatal[0].Matches[0].Groups[1].Value)" }
  return @{ Ok = ($rc -eq 0 -and $passed -ge 1); Detail = $detail }
}

function Invoke-ExpectedLeg($l) {
  # update-expected.py builds natively with the just-built clang and compares: 'expected' with
  # the Windows oracle (LIB from the VS environment, which msil-env.ps1 or ilammy/msvc-dev-cmd
  # provides, and the VS clang's compiler-rt builtins; gate step 12 of M10 and M11),
  # 'expected-linux' with the E7 Linux oracle under WSL (M12 gate step 14). Both check M2's
  # %{msil_prog} tests too (from M14; msil-prog.py --mode native builds them).
  $log = Join-Path $results "$($l.Name).log"
  $clang = Join-Path $BuildDir 'bin/clang.exe'
  $osArgs = @()
  $testPaths = @('msil/test/M1') + $m2
  $what = "update-expected.py --check $($testPaths -join ' ')"
  if ($l.Expected -eq 'linux') {
    $osArgs = @('--os', 'linux', '--linux-sysroot', $linuxSysroot)
    $what = "update-expected.py --check --os linux $($testPaths -join ' ')"
  }
  $lock = Enter-MsilBuildLock -BuildDir $BuildDir -Shared -What $what
  try {
    & $MsilPython (Join-Path $MsilRepo 'msil/utils/update-expected.py') --clang $clang --check @osArgs `
      @($testPaths | ForEach-Object { Join-Path $MsilRepo $_ }) 2>&1 | ForEach-Object { "$_" } |
      Tee-Object -FilePath $log | Out-Host
    $rc = $LASTEXITCODE
  } finally { $lock.Dispose() }
  $ok = @(Select-String -LiteralPath $log -Pattern '^ok: ').Count
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

foreach ($l in $run) {
  # The ctest leg while MSIL_CORPUS_SKIP names c-testsuite: a SKIP row (ci-common.ps1), in the
  # summary table, $env:MSIL_CI_RESULTS and the GitHub step summary, but no failure.
  if ($l.CorpusEntry -and (@(Get-CorpusSkip) -contains $l.CorpusEntry)) {
    Write-Host "$($l.Name): skipped ($($l.CorpusEntry) is in MSIL_CORPUS_SKIP)"
    Add-MsilCiResult -Step lit-legs -Name $l.Name -Status SKIP -Seconds 0 `
      -Detail "$($l.CorpusEntry) is in MSIL_CORPUS_SKIP (plan section 10 item 11); no lit run"
    continue
  }
  $sw = [Diagnostics.Stopwatch]::StartNew()
  try {
    $r = if ($l.Expected) { Invoke-ExpectedLeg $l } elseif ($l.Utc) { Invoke-UtcLeg $l }
         elseif ($l.Encoder) { Invoke-EncoderLeg $l } elseif ($l.ObjText) { Invoke-ObjTextLeg $l }
         else { Invoke-LitLeg $l }
  } catch {
    $r = @{ Ok = $false; Detail = "$_" }
  }
  Add-MsilCiResult -Step lit-legs -Name $l.Name -Status $(if ($r.Ok) { 'PASS' } else { 'FAIL' }) `
    -Seconds $sw.Elapsed.TotalSeconds -Detail $r.Detail
}

Write-MsilCiSummary -Title 'MSIL CI: test legs'
# Test-MsilCiFailed counts FAIL rows only: the ctest leg's SKIP row is no failure.
exit $(if (Test-MsilCiFailed) { 1 } else { 0 })

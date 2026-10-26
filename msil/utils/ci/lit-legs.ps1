# lit-legs.ps1 - the test legs of the MSIL CI (Windows job) and of the milestone-1 gate: step 4
# of msil-ci.yml and of msil/utils/run-m1-gate.ps1.
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1             # the Windows CI legs
#   pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1 -Wsl        # and the WSL legs (dev host)
#   pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1 -Full       # and every optimization level
#   pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1 -Leg net10,roundtrip
#   pwsh -NoProfile -File msil/utils/ci/lit-legs.ps1 -List
#
# Legs, in order. The numbers are the gate steps of plan section 6 that run the same command:
# 'M21 n' for the M21 gate, 'M20 n' for the M20 gate, 'M19 n' for the M19 gate, 'M18 n' for the
# M18 gate, 'M14 n' for the M14 gate, 'M12 n' for the M12 gate, plain numbers for the M10 and
# M11 gates (steps 5-12 the same in both, 20 and 21 M11's); lit-legs.sh has the Linux job's legs:
#   net8           llc/clang FileCheck tests (lld/test/MSIL, the clang driver tests, from M17
#                  the llvm-cil-dump and llvm-cil-copy tests, from M18 llvm/test/MC/MSIL and
#                  from M19 clang/test/Driver/msil-object.c included), harness, msil/test/M1 and
#                  msil/test/M2 on .NET 8 (tiered) and on the .NET Framework 4.8, -O0 and -O2,
#                  native oracle required (with the VS clang's compiler-rt builtins, lit's
#                  msil_native_rtlib=auto); M1 through -S and ilasm, M2 through CIL objects and
#                  msil-ld's image (from M20 %{msil_prog} links objects under the default
#                  pipeline asm)           (M21 13; M20 17; M19 13; M18 15; M14 11; M12 9; step 5)
#   net10          msil/test/M1 on .NET 10                                       (M12 10; step 6)
#   net8-tc0       msil/test/M1 on .NET 8, DOTNET_TieredCompilation=0 (optimizing JIT)  (step 7)
#   more-levels    msil/test/M1 on .NET 8 at O1,O3,Os,Oz (O0 and O2 run in the other legs; .ll
#                  tests run at O1 and O3 only, lit.cfg.py)                            (step 21)
#   lto            msil/test/M1 and msil/test/M2 with msil_pipeline=lto (plan 5.12.11): every
#                  C test of M1 as a one-source program through clang -flto -c and msil-ld
#                  (its image from M20, msil_link_output=pe; --lto-emit-asm and ilasm -exe
#                  before), and M2, on .NET 8 at -O0 and -O2, native oracle required; M1's .ll
#                  tests are filtered out (lto runs them on llc text, as net8 does)  (M14 12)
#   det            msil/test/M2/06-det: %{msil_det}, objects, archives and msil-ld's image (M20;
#                  M14: --lto-emit-asm text) built twice and compared          (M20 20; M14 13)
#   obj            msil/test/M1 and msil/test/M2 with msil_pipeline=obj (plan 5.12.11, M20): every
#                  C test of M1 as a one-source program through clang -c and msil-ld, every .ll
#                  test through llc -filetype=obj and msil-ld, and M2, on .NET 8 and on the .NET
#                  Framework 4.8 at -O0 and -O2, native oracle required, with msil_equiv=1 (O3:
#                  the --no-gc-sections image against ilasm's image of the -S text, canonically,
#                  and O6: the PEVerify /md messages of the image that runs among those of the
#                  ilasm image) and msil_scale_tests=1 (msil/test/M2/00-link/spill-limits.test,
#                  65,600 functions and globals; only this leg sets it, plan 5.12.2); M20's gate
#                  runs its parameters on M1 alone (step 18), the post-commit run-m1-gate.ps1
#                  and M21's gate the whole leg                             (M21 17; M20 18)
#   refs-net48     msil/test/M1/11-programs, msil/test/M2/00-link and msil/test/M2/05-refs with
#                  msil_pipeline=obj and msil_references=net48 (plan 5.11.10, 5.12.11, M21): every
#                  link binds the BCL against the .NET Framework 4.8 reference assemblies (lit.cfg.py
#                  finds them: $env:MSIL_REF_NET48, the Reference Assemblies directory, or the NuGet
#                  package that msil/utils/tools/msil-tools.proj pins), with msil_equiv=1 (O3 with
#                  the scopes entry of equiv-allowed.txt, O6) and msil_run_netfx=1, on .NET 8 and on
#                  the .NET Framework 4.8                                            (M21 15)
#   refs-net8      the same paths with msil_references=net8 (the newest Microsoft.NETCore.App.Ref
#                  8.x pack) on .NET 8; msil-verify.py runs ILVerify against the pack and skips
#                  PEVerify, which cannot load such an image ('bound to .NET 8 references')
#                                                                                    (M21 16)
#   refs-net10     the same with msil_references=net10 (the newest 10.x pack) on .NET 10
#                                                                                    (M21 17)
#   wsl-net10      -Wsl: msil/test/M1 and msil/test/harness (the WSL runner's own checks),
#                  x86_64-unknown-linux-gnu-msil, WSL .NET 10, E7 Linux oracle required (M18 16;
#                  M12 11, and M12 12 checks its response file and log; step 8)
#   wsl-net10-tc0  -Wsl: msil/test/M1 with DOTNET_TieredCompilation=0                   (step 9)
#   wsl-ilasm      -Wsl: 11-programs assembled by the Linux ilasm inside WSL           (step 10)
#   wsl-quick      -Wsl: msil/test/M1/11-programs, msil/test/harness and msil/test/M2 (once it
#                  exists) on WSL .NET 10, E7 Linux oracle required (plan 5.12.10 G4: the WSL
#                  step of every Phase 2 gate from M13 on; the M12 gate runs wsl-net10 instead,
#                  the M20 gate obj-wsl)                                    (M21 14; M19 14)
#   obj-wsl        -Wsl: msil/test/M1/11-programs and msil/test/M2 with msil_pipeline=obj and
#                  msil_equiv=1 (O3 and O6; PEVerify and ilasm run on the host) on WSL .NET 10,
#                  E7 Linux oracle required (plan 5.12.11, M20; no .NET Framework run and no
#                  msil_scale_tests)                                                 (M20 19)
#   hosted         -Wsl, optional (plan Q16, 5.12.11): msil/test/M2/09-hosted, the hosted
#                  compile-only tests (glibc headers and libc.so, UCRT headers), with
#                  msil_glibc_sysroot given: $env:MSIL_GLIBC_SYSROOT, else
#                  C:\llvm-msil-scratch\glibc-sysroot, which msil/utils/make-glibc-sysroot.sh
#                  builds inside WSL (step G0 of the gates from M15), so a missing sysroot is a
#                  configuration error, never a leg of UNSUPPORTED tests. It is not in the
#                  default set: like the WSL legs it needs what only the development host has (a
#                  sysroot made through the WSL distribution); the CI never runs it   (M15 20)
#   modern-ilasm   11-programs assembled by the .NET ilasm (win-x64), not Framework's
#                                                                         (M18 17; step 11)
#   roundtrip      11-programs through ildasm -> ilasm, then run (Design.md 5.5)
#                                                                         (M18 17; M12 13)
#   expected       update-expected.py --check msil/test/M1 msil/test/M2: .expected vs the Windows
#                  native oracle, with the compiler-rt builtins (M2's %{msil_prog} tests built by
#                  msil-prog.py --mode native)                              (M14 15; step 12)
#   expected-linux -Wsl: update-expected.py --check --os linux msil/test/M1 msil/test/M2: the E7
#                  Linux oracle vs <stem>.linux.expected, else <stem>.expected        (M12 14)
#   utc            run-utc-tests.py with six tests of llvm/test/tools/UpdateTestChecks/
#                  update_llc_test_checks, which lit reports UNSUPPORTED on Windows (it looks
#                  for a file named 'llc'; plan M5.b and section 12)          (M18 18; step 20)
#   encoder-parity msil/utils/mdtools/cli-corpus-check.py --mode encoder (plan M18.f, 5.12.11):
#                  every msil/test/M1 program at -O0 and -O2 through clang -S -emit-llvm and
#                  llc -show-mc-encoding, the bytes of every method rebuilt from the encoding
#                  comments (PC-relative fixups resolved from the label offsets) and compared
#                  with the .NET Framework ilasm's image of the same text: equal code bytes
#                  (a float32 signaling NaN that ilasm quiets counts apart), token kinds and
#                  names matching each fixup, and equal .maxstack, which only confirms that
#                  ilasm kept the listing's directive: the Framework ilasm writes it into a
#                  fat header without checking it against the code (a '.maxstack 1' over a
#                  body of stack depth 3 assembles silently), so it is no independent
#                  evidence of the stack depth, which rests on MSILStackCheck (plan D20),
#                  jitall and M18's gate step 14; at least 2,600 methods
#                  (2,819 in the 99 C programs of M18's msil/test/M1). Windows only (the
#                  Framework ilasm); not in lit-legs.sh                            (M18 19)
#   obj-text       the object oracle of CIL objects (plan M19.f, 5.12.6, 5.12.11):
#                  msil/utils/mdtools/cli-corpus-check.py --mode object over msil/test/M1 at -O0
#                  and -O2, then over msil/test/M1/11-programs for the Linux triple
#                  x86_64-unknown-linux-gnu-msil (also -O0 and -O2). For every program and
#                  level, canon(clang -c) == canon(ilasm(object-form text)): the
#                  'llvm-cil-dump --canonical --params=drop --initlocals=ignore-without-locals
#                  --snan=quiet' listings of the clang -c object and of the .NET Framework
#                  ilasm's object of the -mllvm -msil-object-form text are equal (the link-info
#                  record's bytes included), and so are their --link-info lines; two clang -c
#                  runs from different directories give byte-identical objects; mdcanon (one
#                  System.Reflection.Metadata batch) lists every object as llvm-cil-dump does,
#                  and both ildasms open every object; the envelope (machine,
#                  characteristics 0x2022, no entry point, the .text section and the COFF
#                  view's marker sections, the module name, no InitLocals) and C1-C5 and C7 hold
#                  (for ilasm's object too); and the COFF view gives plan 5.8.6's llvm-nm letters
#                  and an llvm-ar index of exactly the defined non-local symbols and the
#                  commons. mdcanon is built or refreshed first with
#                  msil/utils/mdtools/build-mdtools.py (nothing happens when it is up to date).
#                  Windows only (the Framework ilasm); not in lit-legs.sh          (M19 15)
#   obj-text-wsl   -Wsl: cli-corpus-check.py --mode gnu-tools over msil/test/M1 at -O0 and -O2,
#                  then over msil/test/M1/11-programs for the Linux triple
#                  x86_64-unknown-linux-gnu-msil (also -O0 and -O2), as obj-text does (plan
#                  M19.f, judge-dotnet-value J2): GNU nm inside the WSL distribution prints
#                  llvm-nm's letters for every clang -c object, and GNU ar t and nm -s list
#                  the members and the index of an archive of each level's objects, as their
#                  link-info records give them; not in lit-legs.sh                 (M19 16)
#   all-levels     -Full: msil/test at O0,O1,O2,O3,Os,Oz (nightly)
# The WSL legs need a WSL distribution (only the development host has one; the Linux CI job
# covers the Linux triple natively) with the private .NET runtimes of
# msil/utils/msil-wsl-dotnet.sh: every WSL lit leg passes msil_wsl_dotnet=private (plan 5.12.8,
# Q39), so a missing private runtime is a configuration error, never a run on the distribution's
# own dotnet. wsl-net10 and wsl-quick also require the E7 Linux oracle (msil_native_oracle=
# required), which, like expected-linux, needs the sysroot of msil/utils/make-linux-sysroot.py
# ($env:MSIL_LINUX_SYSROOT, else C:\llvm-msil-scratch\linux-sysroot, passed as
# msil_linux_sysroot) and an ld.lld (the tree's from M13 on, else the Visual Studio LLVM's).
# run-m1-gate.ps1's host-setup step makes both (step G0 of the Phase 2 gates). modern-ilasm and
# wsl-ilasm take the .NET ilasm, and roundtrip the .NET ildasm and ilasm (lit.cfg.py reads the
# version the same way), at the version that msil/utils/tools/msil-tools.proj pins
# (MsilILAsmVersion), from the NuGet global-packages folder ('dotnet restore
# msil/utils/tools/msil-tools.proj -p:MsilToolsRid=win-x64', and linux-x64 for wsl-ilasm); a newer
# version in that folder is never used, and a missing one fails the leg; nothing here downloads.
# obj-text takes the NuGet ildasm the same way, and the .NET Framework ildasm from the Windows
# SDK's NETFX 4.x Tools; obj-text-wsl needs only GNU nm and ar (binutils) in the distribution
# Ubuntu, the one cli-corpus-check.py and lit.cfg.py use by default.
#
# Every lit leg runs msil/utils/build/msil-lit.ps1 (shared build-dir lock, the command of the gate
# steps) with --param msil_require_verify=1 (plan D38), --param msil_require_exec=1 and --param
# msil_require_link=1 (plan 5.12.2, M14), so a missing verifier, jitall, compiler, runtime,
# msil-ld or llvm-ar is a configuration error instead of a leg of UNSUPPORTED tests, and writes
# <build>/lit-results/<leg>.xml (xunit) and <leg>.log. Its lit arguments go one per line
# ('--param=<k>=<v>', the lto leg's '--filter-out=<regex>', '--xunit-xml-output=<file>') into
# the response file <build>/lit-results/<leg>.params.rsp, which msil-lit.ps1 passes to lit as
# '-sv @<file>' (lit reads response files, as upstream-hooks.ps1 uses them), so a parameter value
# may contain blanks (a NuGet ilasm under a profile directory with a space; backlog 3.5); the
# build directory itself may not, because msil-lit.ps1 splits -LitArgs at blanks, and the leg
# fails naming it. A leg passes when lit exits 0 and at least one test passed. expected,
# expected-linux, utc, encoder-parity, obj-text and obj-text-wsl are not lit legs: each runs its
# script under the shared build-dir lock, writes <build>/lit-results/<leg>.log and passes when
# the script exits 0 (and, for the expected legs, at least one test matched; for encoder-parity,
# obj-text and obj-text-wsl, every run printed its summary line). Every selected leg runs even
# after a failure; the exit status is 1 if any failed.
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
               'clang/test/Driver/msil-object.c', 'lld/test/MSIL', 'llvm/test/tools/llvm-cil-dump',
               'llvm/test/tools/llvm-cil-copy', 'llvm/test/MC/MSIL', 'msil/test')
$m1 = @('msil/test/M1')
# msil/test/M2 (plan 5.12.5, from M14) where it exists.
function Get-M2Paths {
  if (Test-Path -LiteralPath (Join-Path $MsilRepo 'msil/test/M2')) { 'msil/test/M2' }
}
$programs = @('msil/test/M1/11-programs')
# The paths of the reference legs (plan 5.12.11, M21); lit-legs.sh's refs is the same list.
$refs = @('msil/test/M1/11-programs', 'msil/test/M2/00-link', 'msil/test/M2/05-refs')
$linux = 'msil_triple=x86_64-unknown-linux-gnu-msil'
$tc0 = 'msil_dotnet_env=DOTNET_TieredCompilation=0'
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

# Params and Paths are script blocks: tool paths are resolved when the leg runs, so a missing
# package fails that leg only, and msil/test/M2 joins wsl-quick once it exists.
$legs = @(
  @{ Name = 'net8'; Paths = { $checkMsil }
     Params = { 'msil_dotnet_runtime=8', 'msil_native_oracle=required', 'msil_run_netfx=1' } }
  @{ Name = 'net10'; Paths = { $m1 }; Params = { 'msil_dotnet_runtime=10' } }
  @{ Name = 'net8-tc0'; Paths = { $m1 }; Params = { 'msil_dotnet_runtime=8', $tc0 } }
  @{ Name = 'more-levels'; Paths = { $m1 }
     Params = { 'msil_dotnet_runtime=8', 'msil_opt_levels=O1,O3,Os,Oz' } }
  @{ Name = 'lto'; Paths = { $m1 + @(Get-M2Paths) }; Lit = @('--filter-out=\.ll$')
     Params = { 'msil_pipeline=lto', 'msil_dotnet_runtime=8', 'msil_native_oracle=required' } }
  @{ Name = 'det'; Paths = { 'msil/test/M2/06-det' }; Params = { 'msil_dotnet_runtime=8' } }
  @{ Name = 'obj'; Paths = { $m1 + @(Get-M2Paths) }
     Params = { 'msil_pipeline=obj', 'msil_equiv=1', 'msil_dotnet_runtime=8', 'msil_native_oracle=required',
                'msil_run_netfx=1', 'msil_scale_tests=1' } }
  @{ Name = 'refs-net48'; Paths = { $refs }
     Params = { 'msil_pipeline=obj', 'msil_references=net48', 'msil_equiv=1', 'msil_run_netfx=1' } }
  @{ Name = 'refs-net8'; Paths = { $refs }
     Params = { 'msil_pipeline=obj', 'msil_references=net8', 'msil_dotnet_runtime=8' } }
  @{ Name = 'refs-net10'; Paths = { $refs }
     Params = { 'msil_pipeline=obj', 'msil_references=net10', 'msil_dotnet_runtime=10' } }
  @{ Name = 'wsl-net10'; Paths = { 'msil/test/M1', 'msil/test/harness' }; Wsl = $true
     Params = { $wslRuntime + $e7 } }
  @{ Name = 'wsl-net10-tc0'; Paths = { $m1 }; Wsl = $true; Params = { $wslRuntime + $tc0 } }
  @{ Name = 'wsl-ilasm'; Paths = { $programs }; Wsl = $true
     Params = { $wslRuntime +
                "msil_wsl_ilasm=$(Get-MsilNuGetTool -Repo $MsilRepo -Rid linux-x64 -Tool ilasm)" } }
  @{ Name = 'wsl-quick'; Wsl = $true; Params = { $wslRuntime + $e7 }
     Paths = { 'msil/test/M1/11-programs', 'msil/test/harness'
               if (Test-Path -LiteralPath (Join-Path $MsilRepo 'msil/test/M2')) { 'msil/test/M2' } } }
  @{ Name = 'obj-wsl'; Wsl = $true; Paths = { $programs + @(Get-M2Paths) }
     Params = { $wslRuntime + $e7 + @('msil_pipeline=obj', 'msil_equiv=1') } }
  @{ Name = 'hosted'; Paths = { 'msil/test/M2/09-hosted' }; Wsl = $true
     Params = { "msil_glibc_sysroot=$($glibcSysroot.Replace('\', '/'))" } }
  @{ Name = 'modern-ilasm'; Paths = { $programs }
     Params = { "msil_ilasm=$(Get-MsilNuGetTool -Repo $MsilRepo -Rid win-x64 -Tool ilasm)" } }
  @{ Name = 'roundtrip'; Paths = { $programs }; Params = { 'msil_roundtrip=1' } }
  @{ Name = 'expected'; Expected = 'windows' }
  @{ Name = 'expected-linux'; Expected = 'linux'; Wsl = $true }
  @{ Name = 'utc'; Utc = $true }
  @{ Name = 'encoder-parity'; Encoder = $true }
  @{ Name = 'obj-text'; ObjText = 'object' }
  @{ Name = 'obj-text-wsl'; ObjText = 'gnu-tools'; Wsl = $true }
  @{ Name = 'all-levels'; Paths = { 'msil/test' }; Full = $true
     Params = { 'msil_opt_levels=O0,O1,O2,O3,Os,Oz' } }
)

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
foreach ($s in $selected) { if ($known -notcontains $s) { throw "unknown leg '$s' (legs: $($known -join ', '))" } }
if ($List) {
  foreach ($l in $legs) {
    $tag = if ($l.Wsl) { ' (-Wsl)' } elseif ($l.Full) { ' (-Full)' } else { '' }
    Write-Output "$($l.Name)$tag"
  }
  exit 0
}
$run = @($legs | Where-Object {
  if ($selected.Count -gt 0) { $selected -contains $_.Name }
  else { (-not $_.Wsl -or $Wsl) -and (-not $_.Full -or $Full) }
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
  $testPaths = @('msil/test/M1') + @(Get-M2Paths)
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
exit $(if (Test-MsilCiFailed) { 1 } else { 0 })

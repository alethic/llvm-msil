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
# 'M12 n' for the M12 gate, plain numbers for the M10 and M11 gates (steps 5-12 the same in both,
# 20 and 21 M11's); lit-legs.sh has the Linux job's legs:
#   net8           llc/clang FileCheck tests, harness, msil/test/M1 on .NET 8 (tiered) and on the
#                  .NET Framework 4.8, -O0 and -O2, native oracle required (with the VS clang's
#                  compiler-rt builtins, lit's msil_native_rtlib=auto)          (M12 9; step 5)
#   net10          msil/test/M1 on .NET 10                                       (M12 10; step 6)
#   net8-tc0       msil/test/M1 on .NET 8, DOTNET_TieredCompilation=0 (optimizing JIT)  (step 7)
#   more-levels    msil/test/M1 on .NET 8 at O1,O3,Os,Oz (O0 and O2 run in the other legs; .ll
#                  tests run at O1 and O3 only, lit.cfg.py)                            (step 21)
#   wsl-net10      -Wsl: msil/test/M1 and msil/test/harness (the WSL runner's own checks),
#                  x86_64-unknown-linux-gnu-msil, WSL .NET 10, E7 Linux oracle required (M12 11,
#                  and M12 12 checks its response file and log; step 8)
#   wsl-net10-tc0  -Wsl: msil/test/M1 with DOTNET_TieredCompilation=0                   (step 9)
#   wsl-ilasm      -Wsl: 11-programs assembled by the Linux ilasm inside WSL           (step 10)
#   wsl-quick      -Wsl: msil/test/M1/11-programs, msil/test/harness and msil/test/M2 (once it
#                  exists) on WSL .NET 10, E7 Linux oracle required (plan 5.12.10 G4: the WSL
#                  step of every Phase 2 gate from M13 on; the M12 gate runs wsl-net10 instead)
#   modern-ilasm   11-programs assembled by the .NET ilasm (win-x64), not Framework's  (step 11)
#   roundtrip      11-programs through ildasm -> ilasm, then run (Design.md 5.5)       (M12 13)
#   expected       update-expected.py --check msil/test/M1: .expected vs the Windows native
#                  oracle, with the compiler-rt builtins                               (step 12)
#   expected-linux -Wsl: update-expected.py --check --os linux msil/test/M1: the E7 Linux
#                  oracle vs <stem>.linux.expected, else <stem>.expected               (M12 14)
#   utc            run-utc-tests.py with six tests of llvm/test/tools/UpdateTestChecks/
#                  update_llc_test_checks, which lit reports UNSUPPORTED on Windows (it looks
#                  for a file named 'llc'; plan M5.b and section 12)                   (step 20)
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
#
# Every lit leg runs msil/utils/build/msil-lit.ps1 (shared build-dir lock, the command of the gate
# steps) with --param msil_require_verify=1 (plan D38) and --param msil_require_exec=1, so a
# missing verifier, jitall, compiler or runtime is a configuration error instead of a leg of
# UNSUPPORTED tests, and writes <build>/lit-results/<leg>.xml (xunit) and <leg>.log. Its lit
# arguments go one per line ('--param=<k>=<v>', '--xunit-xml-output=<file>') into the response
# file <build>/lit-results/<leg>.params.rsp, which msil-lit.ps1 passes to lit as '-sv @<file>'
# (lit reads response files, as upstream-hooks.ps1 uses them), so a parameter value may contain
# blanks (a NuGet ilasm under a profile directory with a space; backlog 3.5); the build directory
# itself may not, because msil-lit.ps1 splits -LitArgs at blanks, and the leg fails naming it. A
# leg passes when lit exits 0 and at least one test passed. expected, expected-linux and utc are
# not lit legs: each runs its script under the shared build-dir lock, writes
# <build>/lit-results/<leg>.log and passes when the script exits 0 (and, for the expected legs,
# at least one test matched). Every selected leg runs even after a failure; the exit status is 1
# if any failed.
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

$checkMsil = @('llvm/test/CodeGen/MSIL', 'clang/test/CodeGen/MSIL', 'clang/test/Preprocessor/init-msil.c',
               'clang/test/Driver/msil-toolchain.c', 'msil/test')
$m1 = @('msil/test/M1')
$programs = @('msil/test/M1/11-programs')
$linux = 'msil_triple=x86_64-unknown-linux-gnu-msil'
$tc0 = 'msil_dotnet_env=DOTNET_TieredCompilation=0'
# The E7 Linux oracle's sysroot (msil/utils/make-linux-sysroot.py), defaulted as lit.cfg.py and
# run-m1-gate.ps1's host-setup step default it.
$linuxSysroot = if ($env:MSIL_LINUX_SYSROOT) { $env:MSIL_LINUX_SYSROOT } else { 'C:\llvm-msil-scratch\linux-sysroot' }
# Every WSL lit leg: the Linux triple on the private runtime of msil-wsl-dotnet.sh (plan 5.12.8).
$wslRuntime = @('msil_runner=wsl', $linux, 'msil_wsl_dotnet=private', 'msil_dotnet_runtime=10')
# The legs that require the E7 Linux oracle.
$e7 = @('msil_native_oracle=required', "msil_linux_sysroot=$($linuxSysroot.Replace('\', '/'))")

# Params and Paths are script blocks: tool paths are resolved when the leg runs, so a missing
# package fails that leg only, and msil/test/M2 joins wsl-quick once it exists.
$legs = @(
  @{ Name = 'net8'; Paths = { $checkMsil }
     Params = { 'msil_dotnet_runtime=8', 'msil_native_oracle=required', 'msil_run_netfx=1' } }
  @{ Name = 'net10'; Paths = { $m1 }; Params = { 'msil_dotnet_runtime=10' } }
  @{ Name = 'net8-tc0'; Paths = { $m1 }; Params = { 'msil_dotnet_runtime=8', $tc0 } }
  @{ Name = 'more-levels'; Paths = { $m1 }
     Params = { 'msil_dotnet_runtime=8', 'msil_opt_levels=O1,O3,Os,Oz' } }
  @{ Name = 'wsl-net10'; Paths = { 'msil/test/M1', 'msil/test/harness' }; Wsl = $true
     Params = { $wslRuntime + $e7 } }
  @{ Name = 'wsl-net10-tc0'; Paths = { $m1 }; Wsl = $true; Params = { $wslRuntime + $tc0 } }
  @{ Name = 'wsl-ilasm'; Paths = { $programs }; Wsl = $true
     Params = { $wslRuntime +
                "msil_wsl_ilasm=$(Get-MsilNuGetTool -Repo $MsilRepo -Rid linux-x64 -Tool ilasm)" } }
  @{ Name = 'wsl-quick'; Wsl = $true; Params = { $wslRuntime + $e7 }
     Paths = { 'msil/test/M1/11-programs', 'msil/test/harness'
               if (Test-Path -LiteralPath (Join-Path $MsilRepo 'msil/test/M2')) { 'msil/test/M2' } } }
  @{ Name = 'modern-ilasm'; Paths = { $programs }
     Params = { "msil_ilasm=$(Get-MsilNuGetTool -Repo $MsilRepo -Rid win-x64 -Tool ilasm)" } }
  @{ Name = 'roundtrip'; Paths = { $programs }; Params = { 'msil_roundtrip=1' } }
  @{ Name = 'expected'; Expected = 'windows' }
  @{ Name = 'expected-linux'; Expected = 'linux'; Wsl = $true }
  @{ Name = 'utc'; Utc = $true }
  @{ Name = 'all-levels'; Paths = { 'msil/test' }; Full = $true
     Params = { 'msil_opt_levels=O0,O1,O2,O3,Os,Oz' } }
)

# The tests of the utc leg: the update_llc_test_checks tests that run-utc-tests.py can run and
# whose targets every build of msil/utils/ci has (MSIL and X86). wasm-function-name needs
# WebAssembly, which msil/cmake/caches/MSIL.cmake does not build, and run-utc-tests.py fails a
# test whose target is missing instead of skipping it. lit-legs.sh has the same list.
$utcTests = @('msil-basic', 'x86-basic', 'x86-mir-only', 'x86-asm-mir-mixed', 'x86-constant-pool',
              'asm-show-inst')

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
  $params = @(& $l.Params) + @('msil_require_verify=1', 'msil_require_exec=1')
  $paths = @(& $l.Paths)
  $rsp = Join-Path $results "$($l.Name).params.rsp"
  $xml = (Join-Path $results "$($l.Name).xml").Replace('\', '/')
  # One argument per line, the way lit's argument parser reads '@<file>' (fromfile_prefix_chars
  # in lit/cl_arguments.py): a blank inside a value stays inside it.
  $rspLines = @($params | ForEach-Object { "--param=$_" }) + "--xunit-xml-output=$xml"
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
  # 'expected-linux' with the E7 Linux oracle under WSL (M12 gate step 14).
  $log = Join-Path $results "$($l.Name).log"
  $clang = Join-Path $BuildDir 'bin/clang.exe'
  $osArgs = @()
  $what = 'update-expected.py --check msil/test/M1'
  if ($l.Expected -eq 'linux') {
    $osArgs = @('--os', 'linux', '--linux-sysroot', $linuxSysroot)
    $what = 'update-expected.py --check --os linux msil/test/M1'
  }
  $lock = Enter-MsilBuildLock -BuildDir $BuildDir -Shared -What $what
  try {
    & $MsilPython (Join-Path $MsilRepo 'msil/utils/update-expected.py') --clang $clang --check @osArgs `
      (Join-Path $MsilRepo 'msil/test/M1') 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $log | Out-Host
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

foreach ($l in $run) {
  $sw = [Diagnostics.Stopwatch]::StartNew()
  try {
    $r = if ($l.Expected) { Invoke-ExpectedLeg $l } elseif ($l.Utc) { Invoke-UtcLeg $l }
         else { Invoke-LitLeg $l }
  } catch {
    $r = @{ Ok = $false; Detail = "$_" }
  }
  Add-MsilCiResult -Step lit-legs -Name $l.Name -Status $(if ($r.Ok) { 'PASS' } else { 'FAIL' }) `
    -Seconds $sw.Elapsed.TotalSeconds -Detail $r.Detail
}

Write-MsilCiSummary -Title 'MSIL CI: test legs'
exit $(if (Test-MsilCiFailed) { 1 } else { 0 })

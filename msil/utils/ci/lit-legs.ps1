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
# Legs, in order (the numbers are the M10 gate steps of plan section 6; lit-legs.sh has the
# Linux job's legs):
#   net8           llc/clang FileCheck tests, harness, msil/test/M1 on .NET 8 (tiered) and on the
#                  .NET Framework 4.8, -O0 and -O2, native oracle required             (step 5)
#   net10          msil/test/M1 on .NET 10                                             (step 6)
#   net8-tc0       msil/test/M1 on .NET 8, DOTNET_TieredCompilation=0 (optimizing JIT)  (step 7)
#   wsl-net10      -Wsl: msil/test/M1 and msil/test/harness (the WSL runner's own checks),
#                  x86_64-unknown-linux-gnu-msil, WSL .NET 10                          (step 8)
#   wsl-net10-tc0  -Wsl: the same with DOTNET_TieredCompilation=0                      (step 9)
#   wsl-ilasm      -Wsl: 11-programs assembled by the Linux ilasm inside WSL           (step 10)
#   modern-ilasm   11-programs assembled by the .NET ilasm (win-x64), not Framework's  (step 11)
#   roundtrip      11-programs through ildasm -> ilasm, then run (Design.md 5.5)
#   expected       update-expected.py --check msil/test/M1: .expected vs native      (step 12)
#   all-levels     -Full: msil/test at O0,O1,O2,O3,Os,Oz (nightly)
# The WSL legs need a WSL distribution with .NET 10 (only the development host has one; the Linux
# CI job covers the Linux triple natively). modern-ilasm and wsl-ilasm take the .NET ilasm, and
# roundtrip the .NET ildasm and ilasm (lit.cfg.py reads the version the same way), at the
# version that msil/utils/tools/msil-tools.proj pins (MsilILAsmVersion), from the NuGet
# global-packages folder ('dotnet restore msil/utils/tools/msil-tools.proj
# -p:MsilToolsRid=win-x64', and linux-x64 for wsl-ilasm); a newer version in that folder is
# never used, and a missing one fails the leg; nothing here downloads.
#
# Every lit leg runs msil/utils/build/msil-lit.ps1 (shared build-dir lock, the command of the gate
# steps) with --param msil_require_verify=1 (plan D38) and --param msil_require_exec=1, so a
# missing verifier, jitall, compiler or runtime is a configuration error instead of a leg of
# UNSUPPORTED tests, and writes <build>/lit-results/<leg>.xml (xunit) and <leg>.log. A leg passes
# when lit exits 0 and at least one test passed. Every selected leg runs even after a failure;
# the exit status is 1 if any failed.
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

# Params is a script block: tool paths are resolved when the leg runs, so a missing package
# fails that leg only.
$legs = @(
  @{ Name = 'net8'; Paths = $checkMsil
     Params = { 'msil_dotnet_runtime=8', 'msil_native_oracle=required', 'msil_run_netfx=1' } }
  @{ Name = 'net10'; Paths = $m1; Params = { 'msil_dotnet_runtime=10' } }
  @{ Name = 'net8-tc0'; Paths = $m1; Params = { 'msil_dotnet_runtime=8', $tc0 } }
  @{ Name = 'wsl-net10'; Paths = @('msil/test/M1', 'msil/test/harness'); Wsl = $true
     Params = { 'msil_runner=wsl', $linux, 'msil_dotnet_runtime=10' } }
  @{ Name = 'wsl-net10-tc0'; Paths = $m1; Wsl = $true
     Params = { 'msil_runner=wsl', $linux, 'msil_dotnet_runtime=10', $tc0 } }
  @{ Name = 'wsl-ilasm'; Paths = $programs; Wsl = $true
     Params = { 'msil_runner=wsl', $linux, 'msil_dotnet_runtime=10',
                "msil_wsl_ilasm=$(Get-MsilNuGetTool -Repo $MsilRepo -Rid linux-x64 -Tool ilasm)" } }
  @{ Name = 'modern-ilasm'; Paths = $programs
     Params = { "msil_ilasm=$(Get-MsilNuGetTool -Repo $MsilRepo -Rid win-x64 -Tool ilasm)" } }
  @{ Name = 'roundtrip'; Paths = $programs; Params = { 'msil_roundtrip=1' } }
  @{ Name = 'expected'; Expected = $true }
  @{ Name = 'all-levels'; Paths = @('msil/test'); Full = $true
     Params = { 'msil_opt_levels=O0,O1,O2,O3,Os,Oz' } }
)

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
  $params = @(& $l.Params) + @('msil_require_verify=1', 'msil_require_exec=1')
  $litArgs = @('-sv') + @($params | ForEach-Object { "--param $_" }) +
             "--xunit-xml-output=$((Join-Path $results "$($l.Name).xml").Replace('\', '/'))"
  if ($Jobs -gt 0) { $litArgs += "-j$Jobs" }
  # msil-lit.ps1 splits -LitArgs at blanks.
  foreach ($a in $litArgs) {
    if (($a -replace '^--param ', '') -match '\s') { throw "lit argument with a blank: '$a'" }
  }
  $log = Join-Path $results "$($l.Name).log"
  # '-LitArgs:<value>', not '-LitArgs <value>': 'pwsh -File' takes an argument that starts with
  # '-' and contains ':' (a path such as D:/...) for '-<name>:<value>', so the value would be
  # missing.
  $litArgsParam = "-LitArgs:$($litArgs -join ' ')"
  Write-Host "[msil-ci] leg $($l.Name): msil-lit.ps1 `"$litArgsParam`" $($l.Paths -join ' ')"
  & pwsh -NoProfile -File $msilLit -BuildDir $BuildDir $litArgsParam @($l.Paths) 2>&1 |
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
  # update-expected.py builds natively with the just-built clang (LIB from the VS environment,
  # which msil-env.ps1 or ilammy/msvc-dev-cmd provides) and compares; plan gate step 12.
  $log = Join-Path $results "$($l.Name).log"
  $clang = Join-Path $BuildDir 'bin/clang.exe'
  $lock = Enter-MsilBuildLock -BuildDir $BuildDir -Shared -What 'update-expected --check'
  try {
    & $MsilPython (Join-Path $MsilRepo 'msil/utils/update-expected.py') --clang $clang --check `
      (Join-Path $MsilRepo 'msil/test/M1') 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $log | Out-Host
    $rc = $LASTEXITCODE
  } finally { $lock.Dispose() }
  return @{ Ok = ($rc -eq 0); Detail = "update-expected.py --check msil/test/M1 (exit $rc)" }
}

foreach ($l in $run) {
  $sw = [Diagnostics.Stopwatch]::StartNew()
  try {
    $r = if ($l.Expected) { Invoke-ExpectedLeg $l } else { Invoke-LitLeg $l }
  } catch {
    $r = @{ Ok = $false; Detail = "$_" }
  }
  Add-MsilCiResult -Step lit-legs -Name $l.Name -Status $(if ($r.Ok) { 'PASS' } else { 'FAIL' }) `
    -Seconds $sw.Elapsed.TotalSeconds -Detail $r.Detail
}

Write-MsilCiSummary -Title 'MSIL CI: test legs'
exit $(if (Test-MsilCiFailed) { 1 } else { 0 })

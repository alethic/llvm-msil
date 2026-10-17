# unit-tests.ps1 - the MSIL unit tests: step 3 of the MSIL CI (Windows job) and of
# msil/utils/run-m1-gate.ps1.
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   pwsh -NoProfile -File msil/utils/ci/unit-tests.ps1 [-BuildDir <dir>]
#
# Runs TargetParserTests, MCTests and MSILTests with the gtest filter below through
# msil/utils/build/msil-gtest.ps1 -NoSkip -MinTests 1 (shared build-dir lock): a binary fails on a
# non-zero exit, on a '[  SKIPPED ]' line (GTEST_SKIP exits 0, so a skipped test would look like
# a pass) and when no test passed. The filter selects every MSIL test (every gtest name of the
# MSIL work contains MSIL) and the registry tests, except TargetRegistry.MSILLookupWithoutTarget,
# which M1.e requires to skip whenever the msil target is registered, which it always is from M2
# on (plan section 12, the M2 gate step 7 amendment and the msil-ci.yml fix-5 row). Every binary
# runs even after a failure; exit 1 if any failed. unit-tests.sh runs the same on Linux.
[CmdletBinding()]
param(
  [string]$BuildDir = ''          # default: $env:MSIL_BUILD_DIR, else <repo>\build
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\build\msil-env.ps1')
. (Join-Path $PSScriptRoot 'ci-common.ps1')
if (-not $BuildDir) { $BuildDir = $MsilBuild }
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

$filter = '*MSIL*:TargetRegistry.*:-TargetRegistry.MSILLookupWithoutTarget'
$binaries = @('unittests/TargetParser/TargetParserTests.exe', 'unittests/MC/MCTests.exe',
              'unittests/Target/MSIL/MSILTests.exe')

foreach ($b in $binaries) {
  $exe = Join-Path $BuildDir $b
  $name = [System.IO.Path]::GetFileNameWithoutExtension($b)
  $sw = [Diagnostics.Stopwatch]::StartNew()
  & pwsh -NoProfile -File (Join-Path $MsilRepo 'msil/utils/build/msil-gtest.ps1') `
    -Exe $exe -Filter $filter -NoSkip -MinTests 1 -BuildDir $BuildDir 2>&1 |
    ForEach-Object { "$_" } | Tee-Object -Variable captured | Out-Host
  $rc = $LASTEXITCODE
  $verdict = @($captured | Where-Object { $_ -match '^\[msil-gtest\] (PASS|FAIL):' }) | Select-Object -Last 1
  # '[msil-gtest] PASS: 57 test(s) passed, 0 skipped line(s) (<exe> --gtest_filter=...)'
  $detail = if ($verdict) { $verdict -replace '^\[msil-gtest\] (PASS|FAIL): ', '' -replace ' \([^()]*\)$', '' }
            else { "msil-gtest.ps1 exit $rc" }
  Add-MsilCiResult -Step unit-tests -Name $name -Status $(if ($rc -eq 0) { 'PASS' } else { 'FAIL' }) `
    -Seconds $sw.Elapsed.TotalSeconds -Detail $detail
}

Write-MsilCiSummary -Title 'MSIL CI: unit tests'
exit $(if (Test-MsilCiFailed) { 1 } else { 0 })

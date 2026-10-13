# msil-gtest.ps1 - run a gtest binary under the SHARED build lock and fail unless the selected
# tests really ran and passed.
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   pwsh -NoProfile -File msil-gtest.ps1 -Exe D:/llvm-msil/build/unittests/Target/MSIL/MSILTests.exe -Filter '*MSIL*' -NoSkip -MinTests 3
#   pwsh -NoProfile -File msil-gtest.ps1 -Exe unittests/TargetParser/TargetParserTests.exe -Filter 'TripleTest.*'
# A relative -Exe is looked up in the current directory, then in the build directory.
# GTEST_SKIP exits 0 and a filter that matches nothing prints '[  PASSED  ] 0 tests.', so a
# plain run cannot tell a skipped or missing test from a passing one. This script fails (exit 1)
# when the binary exits non-zero, when there is no '[  PASSED  ] N test(s).' summary line, when
# N is below -MinTests, or (with -NoSkip) when any '[  SKIPPED ]' line is printed.
[CmdletBinding(PositionalBinding = $false)]
param(
  [string]$Exe = '',
  [string]$Filter = '',
  [switch]$NoSkip,
  [int]$MinTests = 0,
  [string]$BuildDir = '',         # default: $env:MSIL_BUILD_DIR, else <repo>\build
  [double]$WaitMinutes = 240
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'msil-env.ps1')
. (Join-Path $PSScriptRoot 'msil-lock.ps1')
if (-not $BuildDir) { $BuildDir = $MsilBuild }
if (-not $Exe -or -not $Filter) {
  Write-Host 'usage: msil-gtest.ps1 -Exe <gtest binary> -Filter <gtest filter> [-NoSkip] [-MinTests N] [-BuildDir <dir>]'
  exit 2
}

$candidates = @($Exe)
if (-not [System.IO.Path]::IsPathRooted($Exe)) { $candidates += (Join-Path $BuildDir $Exe) }
$candidates = @($candidates | ForEach-Object { $_; if (-not $_.EndsWith('.exe')) { "$_.exe" } })
$path = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $path) {
  Write-Host "[msil-gtest] FAIL: $Exe not found (tried: $($candidates -join ', ')); build it first"
  exit 1
}
$path = [System.IO.Path]::GetFullPath($path)

$lines = [System.Collections.Generic.List[string]]::new()
$lock = Enter-MsilBuildLock -BuildDir $BuildDir -Shared -What "gtest $([IO.Path]::GetFileName($path))" -WaitMinutes $WaitMinutes
try {
  & $path "--gtest_filter=$Filter" '--gtest_color=no' 2>&1 | ForEach-Object {
    $s = "$_"; $lines.Add($s); Write-Host $s
  }
  $rc = $LASTEXITCODE
} finally { $lock.Dispose() }

$problems = [System.Collections.Generic.List[string]]::new()
if ($rc -ne 0) { $problems.Add("exit code $rc") }
$passed = $null
foreach ($l in $lines) {
  $m = [regex]::Match($l, '^\[  PASSED  \] (\d+) tests?\.')
  if ($m.Success) { $passed = [int]$m.Groups[1].Value }
}
if ($null -eq $passed) { $problems.Add("no '[  PASSED  ] N tests.' summary line") }
elseif ($passed -lt $MinTests) { $problems.Add("only $passed test(s) passed, -MinTests $MinTests") }
$skipped = @($lines | Where-Object { $_.StartsWith('[  SKIPPED ]') })
if ($NoSkip -and $skipped.Count -gt 0) { $problems.Add("$($skipped.Count) '[  SKIPPED ]' line(s) with -NoSkip") }

if ($problems.Count -gt 0) {
  Write-Host "[msil-gtest] FAIL: $($problems -join '; ') ($path --gtest_filter=$Filter)"
  exit 1
}
Write-Host "[msil-gtest] PASS: $passed test(s) passed, $($skipped.Count) skipped line(s) ($path --gtest_filter=$Filter)"
exit 0

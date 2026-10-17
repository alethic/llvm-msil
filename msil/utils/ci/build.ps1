# build.ps1 - build what the MSIL tests need, then jitall: step 2 of the MSIL CI (Windows job)
# and of msil/utils/run-m1-gate.ps1.
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   pwsh -NoProfile -File msil/utils/ci/build.ps1 [-BuildDir <dir>] [-KeepGoing]
#
# The ninja targets are the M10 build targets (plan M10) plus TargetParserTests for
# unit-tests.ps1: clang llc opt FileCheck not count split-file llvm-config TargetParserTests
# MCTests MSILTests. They are built through msil/utils/build/msil-build.ps1 (exclusive build-dir
# lock, -j throttled when RAM is low) on a GitHub runner and on the development host alike.
# jitall (msil/utils/jitall/build-jitall.py) is then compiled offline into
# <build>/tools/msil/jitall; it does nothing when jitall.dll is up to date.
# Long: on the development host start it with run_in_background and wait for it.
[CmdletBinding()]
param(
  [string]$BuildDir = '',         # default: $env:MSIL_BUILD_DIR, else <repo>\build
  [switch]$KeepGoing
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\build\msil-env.ps1')
. (Join-Path $PSScriptRoot 'ci-common.ps1')
if (-not $BuildDir) { $BuildDir = $MsilBuild }
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

$targets = @('clang', 'llc', 'opt', 'FileCheck', 'not', 'count', 'split-file', 'llvm-config',
             'TargetParserTests', 'MCTests', 'MSILTests')

$sw = [Diagnostics.Stopwatch]::StartNew()
$buildArgs = @('-NoProfile', '-File', (Join-Path $MsilRepo 'msil/utils/build/msil-build.ps1'),
               '-BuildDir', $BuildDir)
if ($KeepGoing) { $buildArgs += '-KeepGoing' }
& pwsh @buildArgs @targets
$rc = $LASTEXITCODE
Add-MsilCiResult -Step build -Name ninja -Status $(if ($rc -eq 0) { 'PASS' } else { 'FAIL' }) `
  -Seconds $sw.Elapsed.TotalSeconds -Detail "$($targets -join ' ') (exit $rc)"

if ($rc -eq 0) {
  $sw.Restart()
  & $MsilPython (Join-Path $MsilRepo 'msil/utils/jitall/build-jitall.py') --build-dir $BuildDir
  $jrc = $LASTEXITCODE
  Add-MsilCiResult -Step build -Name jitall -Status $(if ($jrc -eq 0) { 'PASS' } else { 'FAIL' }) `
    -Seconds $sw.Elapsed.TotalSeconds -Detail "build-jitall.py (exit $jrc)"
}

Write-MsilCiSummary -Title 'MSIL CI: build'
exit $(if (Test-MsilCiFailed) { 1 } else { 0 })

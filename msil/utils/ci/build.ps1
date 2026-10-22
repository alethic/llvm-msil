# build.ps1 - build what the MSIL tests need, then jitall: step 2 of the MSIL CI (Windows job)
# and of msil/utils/run-m1-gate.ps1.
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   pwsh -NoProfile -File msil/utils/ci/build.ps1 [-BuildDir <dir>] [-KeepGoing]
#
# The ninja targets are those of msil/utils/ci/build-targets.txt, the one build-target list
# (plan 5.6.11; its comments say why each target is there): from M13 the M11 set, then lld and
# its copies, llvm-ar and its three copies, the binary tools, CLITests and LLDAsLibAllTests.
# build.sh builds the same file's targets, and this script builds them as gate step G1 does,
# with 'msil/utils/build/msil-build.ps1 -Standard' (exclusive build-dir lock, -j throttled when
# RAM is low), on a GitHub runner and on the development host alike; it reads the file itself
# only to fail early on a malformed one and to name the targets in its row.
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

# The format of build-targets.txt, as msil-build.ps1 -Standard reads it: one target per line;
# '#' starts a comment that runs to the end of the line; blank lines are skipped.
$listFile = Join-Path $PSScriptRoot 'build-targets.txt'
$targets = @()
$problem = ''
try {
  $n = 0
  foreach ($line in [System.IO.File]::ReadAllLines($listFile)) {
    $n++
    $t = ($line -replace '#.*$', '').Trim()
    if (-not $t) { continue }
    if ($t -match '\s') { $problem = "${listFile}:${n}: one target per line, not '$t'"; break }
    $targets += $t
  }
  if (-not $problem -and $targets.Count -eq 0) { $problem = "$listFile names no target" }
} catch {
  $problem = "cannot read ${listFile}: $($_.Exception.Message)"
}

$sw = [Diagnostics.Stopwatch]::StartNew()
if ($problem) {
  $rc = 1
  Add-MsilCiResult -Step build -Name ninja -Status FAIL -Seconds $sw.Elapsed.TotalSeconds -Detail $problem
} else {
  $buildArgs = @('-NoProfile', '-File', (Join-Path $MsilRepo 'msil/utils/build/msil-build.ps1'),
                 '-Standard', '-BuildDir', $BuildDir)
  if ($KeepGoing) { $buildArgs += '-KeepGoing' }
  & pwsh @buildArgs
  $rc = $LASTEXITCODE
  Add-MsilCiResult -Step build -Name ninja -Status $(if ($rc -eq 0) { 'PASS' } else { 'FAIL' }) `
    -Seconds $sw.Elapsed.TotalSeconds -Detail "build-targets.txt: $($targets -join ' ') (exit $rc)"
}

if ($rc -eq 0) {
  $sw.Restart()
  & $MsilPython (Join-Path $MsilRepo 'msil/utils/jitall/build-jitall.py') --build-dir $BuildDir
  $jrc = $LASTEXITCODE
  Add-MsilCiResult -Step build -Name jitall -Status $(if ($jrc -eq 0) { 'PASS' } else { 'FAIL' }) `
    -Seconds $sw.Elapsed.TotalSeconds -Detail "build-jitall.py (exit $jrc)"
}

Write-MsilCiSummary -Title 'MSIL CI: build'
exit $(if (Test-MsilCiFailed) { 1 } else { 0 })

# msil-build.ps1 - build ninja targets under the build-dir lock (one ninja per build dir).
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   pwsh -NoProfile -File msil-build.ps1 llc FileCheck not count
#   pwsh -NoProfile -File msil-build.ps1 -Jobs 4 clang
#   pwsh -NoProfile -File msil-build.ps1 -Standard            # the one build-target list
#   pwsh -NoProfile -File msil-build.ps1 -Standard llvm-mc    # that list, then llvm-mc
# -Standard builds every target of msil/utils/ci/build-targets.txt (plan 5.6.11; gate step G1
# from M13 on), in the file's order, then the targets named on the command line (a target
# already on the list is not named twice); with it the target list may be empty. Without
# -Standard the targets are required, as they always were.
# Long builds: start it with run_in_background and poll the log; never in a foreground
# tool call (those time out after 2-10 minutes). A background job dies with the agent's
# turn, so the agent that started a build waits for it.
# Build agent only: takes the EXCLUSIVE build-dir lock (msil-lock.ps1).
[CmdletBinding(PositionalBinding = $false, DefaultParameterSetName = 'Named')]
param(
  [Parameter(Mandatory, ValueFromRemainingArguments, ParameterSetName = 'Named')]
  [Parameter(ValueFromRemainingArguments, ParameterSetName = 'Standard')]
  [string[]]$Targets = @(),
  [Parameter(Mandatory, ParameterSetName = 'Standard')]
  [switch]$Standard,              # build-targets.txt first (see above)
  [string]$BuildDir = '',         # default: $env:MSIL_BUILD_DIR, else <repo>\build
  [int]$Jobs = 0,                 # 0 = ninja default (logical CPUs + 2), capped by the CMake job pools
  [double]$WaitMinutes = 240,     # how long to queue behind another agent's build
  [switch]$KeepGoing              # ninja -k 0: report every failing TU, not just the first
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'msil-env.ps1')
. (Join-Path $PSScriptRoot 'msil-lock.ps1')
if (-not $BuildDir) { $BuildDir = $MsilBuild }

if ($Standard) {
  # The format of build-targets.txt: one target per line; '#' starts a comment that runs to the
  # end of the line; blank lines are skipped (build.ps1 and build.sh read it the same way).
  $listFile = Join-Path $MsilRepo 'msil/utils/ci/build-targets.txt'
  $listed = [System.Collections.Generic.List[string]]::new()
  $n = 0
  foreach ($line in [System.IO.File]::ReadAllLines($listFile)) {
    $n++
    $t = ($line -replace '#.*$', '').Trim()
    if (-not $t) { continue }
    if ($t -match '\s') { throw "${listFile}:${n}: one target per line, not '$t'" }
    $listed.Add($t)
  }
  if ($listed.Count -eq 0) { throw "$listFile names no target" }
  $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
  $Targets = @(@($listed) + @($Targets) | Where-Object { $seen.Add($_) })
  Write-Host "[msil-build] -Standard: the $($listed.Count) target(s) of $listFile, then $($Targets.Count - $listed.Count) named one(s)"
}

$ninjaArgs = @('-C', $BuildDir)
if ($Jobs -gt 0) { $ninjaArgs += @('-j', "$Jobs") }
if ($KeepGoing)  { $ninjaArgs += @('-k', '0') }
$ninjaArgs += $Targets

$lock = Enter-MsilBuildLock -BuildDir $BuildDir -What ("ninja " + ($Targets -join ' ')) -WaitMinutes $WaitMinutes
try {
  if ($Jobs -eq 0) {
    # Other agents' experiments have been seen holding 40 GB of the 64 GB. A clang-cl job peaks
    # at ~1-2 GB, so throttle instead of pushing the machine into paging.
    $freeGB = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1MB
    $j = if ($freeGB -lt 6) { 2 } elseif ($freeGB -lt 16) { 4 } else { 0 }
    if ($j) { $ninjaArgs = @('-j', "$j") + $ninjaArgs; Write-Host "[msil-build] only $([int]$freeGB) GB free RAM -> -j $j" }
  }
  $sw = [Diagnostics.Stopwatch]::StartNew()
  Write-Host "[msil-build] $(Get-Date -Format s) ninja $($ninjaArgs -join ' ')"
  & ninja @ninjaArgs
  $rc = $LASTEXITCODE
  Write-Host "[msil-build] $(Get-Date -Format s) exit=$rc elapsed=$([math]::Round($sw.Elapsed.TotalMinutes,1))min"
} finally { $lock.Dispose() }
exit $rc

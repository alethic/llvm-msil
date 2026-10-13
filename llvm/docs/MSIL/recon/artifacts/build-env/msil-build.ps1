# msil-build.ps1 - build ninja targets under the build-dir lock (one ninja per build dir).
#   pwsh -NoProfile -File msil-build.ps1 llc FileCheck not count
#   pwsh -NoProfile -File msil-build.ps1 -Jobs 4 clang
# Long builds: start it with run_in_background and poll the log; never in a foreground
# tool call (those time out after 2-10 minutes).
[CmdletBinding(PositionalBinding = $false)]
param(
  [Parameter(Mandatory, ValueFromRemainingArguments)][string[]]$Targets,
  [string]$BuildDir = 'D:\llvm-msil\build',
  [int]$Jobs = 0,                 # 0 = ninja default (logical CPUs + 2), capped by the CMake job pools
  [double]$WaitMinutes = 240,     # how long to queue behind another agent's build
  [switch]$KeepGoing              # ninja -k 0: report every failing TU, not just the first
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'msil-env.ps1')
. (Join-Path $PSScriptRoot 'msil-lock.ps1')

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

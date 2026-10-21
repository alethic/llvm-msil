# Recon: build one object in the private hooks tree and time it.
param(
  [Parameter(Mandatory)][string]$Target,
  [string]$BuildDir = 'C:\llvm-msil-hooks-build',
  [int]$Jobs = 8
)
$ErrorActionPreference = 'Stop'
. D:\llvm-msil\msil\utils\build\msil-env.ps1
$ninja = 'ninja.exe'
$sw = [Diagnostics.Stopwatch]::StartNew()
"start $(Get-Date -Format s) target=$Target"
& $ninja -C $BuildDir "-j$Jobs" $Target
$rc = $LASTEXITCODE
"build exit=$rc elapsed=$([math]::Round($sw.Elapsed.TotalSeconds,1))s end=$(Get-Date -Format s)"
exit $rc

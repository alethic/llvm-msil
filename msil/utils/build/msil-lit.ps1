# msil-lit.ps1 - run llvm-lit on source-tree test paths, holding the SHARED build lock
# (many lit runs may overlap; none overlaps a ninja/cmake run, so tools are never half-linked).
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   pwsh -NoProfile -File msil-lit.ps1 llvm\test\CodeGen\MSIL
#   pwsh -NoProfile -File msil-lit.ps1 -LitArgs '-v --filter=fib' llvm\test\CodeGen\MSIL clang\test\CodeGen\MSIL
#   pwsh -NoProfile -File msil-lit.ps1 -LitArgs "-sv --param msil_require_verify=1" msil/test/harness
# Relative paths are relative to the repository root. The tools the tests use must already be
# built (msil-build.ps1 llc FileCheck not count llvm-config ...).
[CmdletBinding(PositionalBinding = $false)]
param(
  [Parameter(Mandatory, ValueFromRemainingArguments)][string[]]$Paths,
  [string]$BuildDir = '',         # default: $env:MSIL_BUILD_DIR, else <repo>\build
  [string]$LitArgs = '-sv',
  [int]$Jobs = 0,
  [double]$WaitMinutes = 240
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'msil-env.ps1')
. (Join-Path $PSScriptRoot 'msil-lock.ps1')
if (-not $BuildDir) { $BuildDir = $MsilBuild }

$lit = Join-Path $BuildDir 'bin\llvm-lit.py'
if (-not (Test-Path $lit)) { throw "$lit missing - configure first" }
$full = foreach ($p in $Paths) {
  if ([System.IO.Path]::IsPathRooted($p)) { $p } else { Join-Path $MsilRepo $p }
}
$argv = @($lit) + ($LitArgs -split '\s+' | Where-Object { $_ })
if ($Jobs -gt 0) { $argv += "-j$Jobs" }
$argv += $full

$lock = Enter-MsilBuildLock -BuildDir $BuildDir -Shared -What 'lit' -WaitMinutes $WaitMinutes
try {
  & $MsilPython @argv
  $rc = $LASTEXITCODE
} finally { $lock.Dispose() }
exit $rc

# msil-run.ps1 - run one tool from the build's bin\ under the SHARED build lock.
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# On Windows a running .exe cannot be overwritten, so an llc/clang started by hand while
# ninja relinks it makes the link fail; the shared lock prevents that.
#   pwsh -NoProfile -File msil-run.ps1 llc -mtriple=x86_64-unknown-linux-gnu-msil -O2 fib.ll -o -
#   pwsh -NoProfile -File msil-run.ps1 clang --target=x86_64-unknown-linux-gnu-msil -S fib.c -o fib.il
# Deliberately a plain script ($args, no param block): every argument after the tool name is
# passed through verbatim, including ones like -o / -S that PowerShell would otherwise try to
# bind. Build dir: $env:MSIL_BUILD_DIR if set, else <repo>\build (repo = three levels up).
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'msil-lock.ps1')
$BuildDir = if ($env:MSIL_BUILD_DIR) { $env:MSIL_BUILD_DIR }
            else { Join-Path ([System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))) 'build' }
if ($args.Count -lt 1) { throw 'usage: msil-run.ps1 <tool> [args...]' }
$tool = $args[0]; $toolArgs = @($args | Select-Object -Skip 1)
$exe = Join-Path $BuildDir "bin\$tool.exe"
if (-not (Test-Path $exe)) { throw "$exe not built (msil-build.ps1 $tool)" }
$lock = Enter-MsilBuildLock -BuildDir $BuildDir -Shared -What "run $tool"
try { & $exe @toolArgs; $rc = $LASTEXITCODE } finally { $lock.Dispose() }
exit $rc

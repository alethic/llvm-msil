# msil-configure.ps1 - configure the llvm-msil build tree (Ninja, Release+Assertions).
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   pwsh -NoProfile -File msil-configure.ps1                       # M0, M1: no MSIL target yet
#   pwsh -NoProfile -File msil-configure.ps1 -Experimental MSIL    # M2 on (llvm/lib/Target/MSIL exists)
#   pwsh -NoProfile -File msil-configure.ps1 -NoMsilProject        # without the msil/ external project
#   pwsh -NoProfile -File msil-configure.ps1 -Compiler msvc -BuildDir <dir>   # cl.exe + link.exe
# The msil/ directory (execution tests, check-msil*) is configured as an LLVM external project
# (-DLLVM_EXTERNAL_PROJECTS=msil -DLLVM_EXTERNAL_MSIL_SOURCE_DIR=<repo>/msil) unless
# -NoMsilProject is given, which clears LLVM_EXTERNAL_PROJECTS in the cache.
# Build agent only: takes the EXCLUSIVE build-dir lock (msil-lock.ps1).
param(
  [string]$BuildDir = '',         # default: $env:MSIL_BUILD_DIR, else <repo>\build
  [ValidateSet('clangcl','msvc')][string]$Compiler = 'clangcl',
  [string]$Targets = 'X86;WebAssembly',
  [string]$Experimental = '',
  [int]$CompileJobs = 8,
  [int]$LinkJobs = 2,
  [switch]$NoMsilProject,
  [switch]$Fresh
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'msil-env.ps1')
. (Join-Path $PSScriptRoot 'msil-lock.ps1')
if (-not $BuildDir) { $BuildDir = $MsilBuild }

$cmakeArgs = @(
  '-G', 'Ninja',
  '-S', (Join-Path $MsilRepo 'llvm'),
  '-B', $BuildDir,
  '-DCMAKE_BUILD_TYPE=Release',
  '-DLLVM_ENABLE_ASSERTIONS=ON',
  '-DLLVM_ENABLE_PROJECTS=clang;lld',
  "-DLLVM_TARGETS_TO_BUILD=$Targets",
  "-DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=$Experimental",
  '-DLLVM_OPTIMIZED_TABLEGEN=OFF',
  "-DLLVM_PARALLEL_COMPILE_JOBS=$CompileJobs",
  "-DLLVM_PARALLEL_LINK_JOBS=$LinkJobs",
  '-DLLVM_ENABLE_PDB=OFF',
  '-DLLVM_INCLUDE_BENCHMARKS=OFF',
  '-DLLVM_INCLUDE_EXAMPLES=OFF',
  '-DLLVM_INCLUDE_TESTS=ON',
  '-DLLVM_BUILD_TESTS=OFF',
  '-DLLVM_APPEND_VC_REV=OFF',
  '-DLLVM_ENABLE_ZLIB=OFF',
  '-DLLVM_ENABLE_ZSTD=OFF',
  '-DLLVM_ENABLE_LIBXML2=OFF',
  '-DLLVM_ENABLE_DIA_SDK=OFF',
  '-DLLVM_ENABLE_TELEMETRY=OFF',
  '-DCLANG_ENABLE_STATIC_ANALYZER=OFF',
  '-DCLANG_ENABLE_OBJC_REWRITER=OFF',
  '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
  "-DPython3_EXECUTABLE=$MsilPython"
)
if ($NoMsilProject) {
  $cmakeArgs += '-DLLVM_EXTERNAL_PROJECTS='
} else {
  # Forward slashes, like Python3_EXECUTABLE: CMake pastes source paths into lit.site.cfg.py.
  $cmakeArgs += @('-DLLVM_EXTERNAL_PROJECTS=msil',
                  "-DLLVM_EXTERNAL_MSIL_SOURCE_DIR=$($MsilRepo.Replace('\', '/').TrimEnd('/'))/msil")
}
if ($Compiler -eq 'clangcl') {
  $cmakeArgs += @(
    "-DCMAKE_C_COMPILER=$MsilLlvmBin\clang-cl.exe",
    "-DCMAKE_CXX_COMPILER=$MsilLlvmBin\clang-cl.exe",
    "-DCMAKE_LINKER=$MsilLlvmBin\lld-link.exe",
    "-DCMAKE_AR=$MsilLlvmBin\llvm-lib.exe",
    '-DLLVM_ENABLE_LLD=ON'
  )
} else {
  $cl = (Get-Command cl.exe).Source
  $cmakeArgs += @("-DCMAKE_C_COMPILER=$cl", "-DCMAKE_CXX_COMPILER=$cl",
                  "-DCMAKE_LINKER=$((Get-Command link.exe).Source)")
}
if ($Fresh) { $cmakeArgs += '--fresh' }

$lock = Enter-MsilBuildLock -BuildDir $BuildDir -What "configure $Experimental"
try {
  $sw = [Diagnostics.Stopwatch]::StartNew()
  & cmake @cmakeArgs
  $rc = $LASTEXITCODE
} finally { $lock.Dispose() }
"configure exit=$rc elapsed=$([math]::Round($sw.Elapsed.TotalSeconds,1))s"
exit $rc

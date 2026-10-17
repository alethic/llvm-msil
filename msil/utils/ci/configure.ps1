# configure.ps1 - configure the build tree: step 1 of the MSIL CI (Windows job) and of
# msil/utils/run-m1-gate.ps1.
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   pwsh -NoProfile -File msil/utils/ci/configure.ps1 -Ci      # GitHub runner (msil-ci.yml)
#   pwsh -NoProfile -File msil/utils/ci/configure.ps1          # development host
#
# The only CI step that differs between a GitHub runner and the development host, because the
# two compile differently:
#   -Ci    cmake -C msil/cmake/caches/MSIL.cmake with clang-cl from the MSVC developer
#          environment (ilammy/msvc-dev-cmd), PCH off and, when sccache is on PATH, sccache
#          launchers with embedded debug information (sccache cannot cache PDBs or PCHs).
#   else   msil/utils/build/msil-configure.ps1 -Experimental MSIL, the tracked configuration of
#          the development host (plan section 4: clang-cl and lld-link from VS, X86 and
#          WebAssembly, the msil/ external project). It takes the exclusive build-dir lock, and
#          re-running it on a configured tree with the same options changes nothing that ninja
#          has to rebuild.
# Every later step (build.ps1, unit-tests.ps1, lit-legs.ps1) runs the same commands in both
# places.
[CmdletBinding()]
param(
  [switch]$Ci,
  [string]$BuildDir = ''          # default: $env:MSIL_BUILD_DIR, else <repo>\build
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\build\msil-env.ps1')
. (Join-Path $PSScriptRoot 'ci-common.ps1')
if (-not $BuildDir) { $BuildDir = $MsilBuild }
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

$sw = [Diagnostics.Stopwatch]::StartNew()
if ($Ci) {
  $cmakeArgs = @(
    '-G', 'Ninja',
    '-S', (Join-Path $MsilRepo 'llvm'),
    '-B', $BuildDir,
    '-C', (Join-Path $MsilRepo 'msil/cmake/caches/MSIL.cmake'),
    '-DCMAKE_C_COMPILER=clang-cl', '-DCMAKE_CXX_COMPILER=clang-cl',
    '-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON'
  )
  if (Get-Command sccache -ErrorAction SilentlyContinue) {
    $cmakeArgs += @(
      '-DCMAKE_C_COMPILER_LAUNCHER=sccache', '-DCMAKE_CXX_COMPILER_LAUNCHER=sccache',
      # sccache needs embedded debug information (upstream libc-overlay-tests.yml).
      '-DCMAKE_POLICY_DEFAULT_CMP0141=NEW', '-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded'
    )
  }
  Write-Host "[msil-ci] cmake $($cmakeArgs -join ' ')"
  & cmake @cmakeArgs
  $rc = $LASTEXITCODE
  $how = 'cmake -C msil/cmake/caches/MSIL.cmake'
} else {
  & pwsh -NoProfile -File (Join-Path $MsilRepo 'msil/utils/build/msil-configure.ps1') `
    -Experimental MSIL -BuildDir $BuildDir
  $rc = $LASTEXITCODE
  $how = 'msil-configure.ps1 -Experimental MSIL'
}
$status = if ($rc -eq 0) { 'PASS' } else { 'FAIL' }
Add-MsilCiResult -Step configure -Name $(if ($Ci) { 'ci' } else { 'local' }) -Status $status `
  -Seconds $sw.Elapsed.TotalSeconds -Detail "$how (exit $rc)"
Write-MsilCiSummary -Title 'MSIL CI: configure'
exit $(if ($rc -eq 0) { 0 } else { 1 })

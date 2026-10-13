# msil-env.ps1 - dot-source me:  . .\msil-env.ps1
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Puts the VS 2026 x64 developer environment (cl, clang-cl, lld-link, cmake, INCLUDE/LIB...)
# into the current PowerShell session. Enter-VsDevShell costs ~5 s, so the variables it
# changes are cached in %TEMP%\msil-vsenv-x64.json and re-applied in ~0.1 s afterwards.
# Delete that file after a Visual Studio update.
#
# Globals for the other scripts and for interactive use:
#   $MsilRepo     repository root (three levels above this script: msil\utils\build)
#   $MsilBuild    build directory: $env:MSIL_BUILD_DIR if set, else <repo>\build
#   $MsilPython   Python 3, always with forward slashes: $env:MSIL_PYTHON if set, else the
#                 host's pythoncore-3.14 (falling back to python.exe on PATH)
#   $MsilLlvmBin  the VS-bundled LLVM tools (clang-cl, lld-link, llvm-lib, clang-format)
#   $MsilIlasm    .NET Framework ilasm 4.8;  $MsilIldasm  NETFX 4.8 ildasm
$script:VsRoot = 'C:\Program Files\Microsoft Visual Studio\18\Professional'
if (-not ($env:VSCMD_ARG_TGT_ARCH -eq 'x64' -and (Get-Command cl.exe -ErrorAction SilentlyContinue))) {
  $cache = Join-Path ([System.IO.Path]::GetTempPath()) 'msil-vsenv-x64.json'
  $loaded = $false
  if (Test-Path $cache) {
    try {
      $c = Get-Content -Raw $cache | ConvertFrom-Json
      foreach ($p in $c.vars.PSObject.Properties) { Set-Item -Path "env:$($p.Name)" -Value $p.Value }
      $env:PATH = $c.pathPrefix + $env:PATH + $c.pathSuffix   # same placement as vcvars
      $loaded = [bool](Get-Command cl.exe -ErrorAction SilentlyContinue)
    } catch { $loaded = $false }
  }
  if (-not $loaded) {
    $before = @{}; Get-ChildItem env: | ForEach-Object { $before[$_.Name] = $_.Value }
    Import-Module (Join-Path $script:VsRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    # "'vswhere.exe' is not recognized" may be printed: harmless. VsDevCmd.bat runs vswhere by
    # bare name after pushd, which NoDefaultCurrentDirectoryInExePath=1 (set by Claude Code) blocks.
    Enter-VsDevShell -VsInstallPath $script:VsRoot -SkipAutomaticLocation `
      -DevCmdArguments '-arch=x64 -host_arch=x64 -no_logo' | Out-Null
    $vars = [ordered]@{}
    Get-ChildItem env: | Where-Object { $_.Name -ne 'Path' -and $before[$_.Name] -ne $_.Value } |
      ForEach-Object { $vars[$_.Name] = $_.Value }
    # vcvars puts MSVC/SDK dirs in front of PATH and VC\Tools\Llvm\x64\bin, CMake, Ninja at the end.
    $oldPath = $before['Path']; $i = if ($oldPath) { $env:PATH.IndexOf($oldPath) } else { -1 }
    if ($i -gt 0) {
      $cacheObj = @{ vars = $vars; pathPrefix = $env:PATH.Substring(0, $i)
                     pathSuffix = $env:PATH.Substring($i + $oldPath.Length) }
      try { $cacheObj | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $cache -Encoding utf8 } catch {}
    }
  }
}
# $PSScriptRoot is this file's directory even when it is dot-sourced from another script.
$global:MsilRepo    = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$global:MsilBuild   = if ($env:MSIL_BUILD_DIR) { [System.IO.Path]::GetFullPath($env:MSIL_BUILD_DIR) }
                      else { Join-Path $global:MsilRepo 'build' }
# Forward slashes on purpose: CMake pastes this into lit.site.cfg.py string literals, where
# "C:\Users\..." is a SyntaxError (\U escape). Verified the hard way.
$script:MsilPy = if ($env:MSIL_PYTHON) { $env:MSIL_PYTHON }
                 else { 'C:/Users/JeromeHaltom/AppData/Local/Python/pythoncore-3.14-64/python.exe' }
if (-not $env:MSIL_PYTHON -and -not (Test-Path -LiteralPath $script:MsilPy)) {
  $script:PyCmd = Get-Command python.exe, python3.exe -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -First 1
  if ($script:PyCmd) { $script:MsilPy = $script:PyCmd.Source }
}
$global:MsilPython  = $script:MsilPy.Replace('\', '/')
$global:MsilLlvmBin = Join-Path $script:VsRoot 'VC\Tools\Llvm\x64\bin'
$global:MsilIlasm   = 'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\ilasm.exe'
$global:MsilIldasm  = 'C:\Program Files (x86)\Microsoft SDKs\Windows\v10.0A\bin\NETFX 4.8 Tools\x64\ildasm.exe'

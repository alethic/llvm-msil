# msil-env.ps1 - dot-source me:  . .\msil-env.ps1
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Puts a Visual Studio x64 developer environment (cl, clang-cl, lld-link, cmake, INCLUDE/LIB...)
# into the current PowerShell session, unless one is loaded already: a developer prompt, CI's
# msvc-dev-cmd, or a parent script that dot-sourced this file. A loaded environment is trusted
# when the first cl.exe on PATH targets x64 (it lies under '\bin\Host<arch>\x64\', where vcvars
# puts it) or when VSCMD_ARG_TGT_ARCH is x64 and a cl.exe is on PATH. lit passes PATH, INCLUDE,
# LIB and VSINSTALLDIR on to its tests but drops VSCMD_ARG_TGT_ARCH and replaces TEMP, so under
# lit the PATH test is the one that holds. Otherwise the script enters the developer shell of the
# Visual Studio root below. Enter-VsDevShell costs ~5 s, so the variables it changes are cached in
# %TEMP%\msil-vsenv-x64.json and re-applied in ~0.1 s afterwards. Delete that file after a Visual
# Studio update.
#
# The Visual Studio root is $env:VSINSTALLDIR when it names a directory (a loaded developer
# environment and the cache set it), else the newest installation with the x64 C++ tools that
# vswhere finds ('vswhere -latest -products * -requires
# Microsoft.VisualStudio.Component.VC.Tools.x86.x64': any edition, any version). Only when the
# script must enter a developer environment and finds no root does it stop, with a message.
#
# Globals for the other scripts and for interactive use:
#   $MsilRepo     repository root (three levels above this script: msil\utils\build)
#   $MsilBuild    build directory: $env:MSIL_BUILD_DIR if set, else <repo>\build
#   $MsilPython   Python 3, always with forward slashes: $env:MSIL_PYTHON if set, else the
#                 host's pythoncore-3.14 (falling back to python.exe on PATH)
#   $MsilVsRoot   the Visual Studio root above ('' when a trusted environment names none and
#                 vswhere finds none)
#   $MsilLlvmBin  the LLVM tools bundled with that Visual Studio (clang-cl, lld-link, llvm-lib,
#                 clang-format): <root>\VC\Tools\Llvm\x64\bin ('' without a root)
#   $MsilIlasm    .NET Framework ilasm 4.8;  $MsilIldasm  NETFX 4.8 ildasm

# The Visual Studio root (see the header), or '' when there is none.
function Get-MsilVsRoot {
  if ($env:VSINSTALLDIR -and (Test-Path -LiteralPath $env:VSINSTALLDIR -PathType Container)) {
    return $env:VSINSTALLDIR.TrimEnd('\', '/')
  }
  $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    $cmd = Get-Command vswhere.exe -CommandType Application -ErrorAction SilentlyContinue |
      Select-Object -First 1
    $vswhere = if ($cmd) { $cmd.Source } else { '' }
  }
  if ($vswhere) {
    $found = @(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                 -property installationPath 2>$null | ForEach-Object { "$_".Trim() } | Where-Object { $_ })
    if ($found.Count -gt 0 -and (Test-Path -LiteralPath $found[0] -PathType Container)) {
      return $found[0].TrimEnd('\', '/')
    }
  }
  return ''
}

# True when an x64 developer environment is loaded already (see the header).
function Test-MsilX64DevEnv {
  $cl = Get-Command cl.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $cl) { return $false }
  if ($env:VSCMD_ARG_TGT_ARCH -eq 'x64') { return $true }
  return $cl.Source -match '\\bin\\Host[^\\]+\\x64\\cl\.exe$'
}

if (-not (Test-MsilX64DevEnv)) {
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
    $script:VsRoot = Get-MsilVsRoot
    if (-not $script:VsRoot) {
      throw ("msil-env.ps1: no x64 developer environment is loaded (no x64 cl.exe on PATH), and " +
             "no Visual Studio with the x64 C++ tools was found (VSINSTALLDIR is unset, and " +
             "'vswhere -latest -products * -requires " +
             "Microsoft.VisualStudio.Component.VC.Tools.x86.x64' found none): install the " +
             "'Desktop development with C++' workload, or run from an x64 developer prompt")
    }
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
                 else { "$env:LOCALAPPDATA/Python/pythoncore-3.14-64/python.exe" }
if (-not $env:MSIL_PYTHON -and -not (Test-Path -LiteralPath $script:MsilPy)) {
  $script:PyCmd = Get-Command python.exe, python3.exe -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -First 1
  if ($script:PyCmd) { $script:MsilPy = $script:PyCmd.Source }
}
$global:MsilPython  = $script:MsilPy.Replace('\', '/')
# The root of the environment now loaded: VSINSTALLDIR (set by every developer environment and
# by the cache), else vswhere's.
$global:MsilVsRoot  = Get-MsilVsRoot
$global:MsilLlvmBin = if ($global:MsilVsRoot) { Join-Path $global:MsilVsRoot 'VC\Tools\Llvm\x64\bin' } else { '' }
$global:MsilIlasm   = 'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\ilasm.exe'
$global:MsilIldasm  = 'C:\Program Files (x86)\Microsoft SDKs\Windows\v10.0A\bin\NETFX 4.8 Tools\x64\ildasm.exe'

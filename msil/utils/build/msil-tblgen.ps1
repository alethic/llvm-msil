# msil-tblgen.ps1 - run llvm-tblgen on llvm/lib/Target/MSIL/MSIL.td into a scratch directory.
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Writes only into -Out (never into the build or source tree) and takes no lock, so any agent
# may run it at any time. It needs <build>\bin\llvm-tblgen.exe (built by the M1.0 pre-build).
#   pwsh -NoProfile -File msil-tblgen.ps1 -Out <scratch>\tblgen
#   pwsh -NoProfile -File msil-tblgen.ps1 -Out <scratch>\tblgen -Gen instr-info,dag-isel
# then syntax-check C++ against the fresh .inc files:
#   pwsh -NoProfile -File msil-syntax.ps1 -FlagsFrom llvm\lib\Target\WebAssembly\WebAssemblyInstrInfo.cpp `
#     -ExtraInclude llvm\lib\Target\MSIL,<scratch>\tblgen llvm\lib\Target\MSIL\MSILInstrInfo.cpp
# Backends (-Gen, comma-separated; default all six; '-gen-' prefix or file stem also accepted):
#   asm-writer     -> MSILGenAsmWriter.inc
#   dag-isel       -> MSILGenDAGISel.inc
#   instr-info     -> MSILGenInstrInfo.inc
#   register-info  -> MSILGenRegisterInfo.inc (+ ...Enums, ...Header, ...MCDesc, ...TargetDesc.inc)
#   sd-node-info   -> MSILGenSDNodeInfo.inc
#   subtarget      -> MSILGenSubtargetInfo.inc
# The flags are those of the real build (lib/Target/<T>/CMakeLists.txt tablegen() rules in
# build.ninja): -I of the target dir, <build>\include, llvm\include and llvm\lib\Target,
# -omit-comments for dag-isel, --long-string-literals=0. -Target selects another target's
# <T>.td (for example WebAssembly, to test this script before llvm/lib/Target/MSIL exists).
# Exit code: 0 if every backend succeeded, 1 otherwise. A failed backend's old outputs in -Out
# are deleted, so a stale .inc can never pass a later syntax check.
[CmdletBinding(PositionalBinding = $false)]
param(
  [string]$Out = '',
  [string[]]$Gen = @(),
  [string]$Target = 'MSIL',
  [string]$BuildDir = ''          # default: $env:MSIL_BUILD_DIR, else <repo>\build
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'msil-env.ps1')
if (-not $BuildDir) { $BuildDir = $MsilBuild }
if (-not $Out) { throw 'usage: msil-tblgen.ps1 -Out <scratch dir> [-Gen <backend,...>] [-Target MSIL] [-BuildDir <dir>]' }

$backends = [ordered]@{
  'asm-writer'    = 'AsmWriter'
  'dag-isel'      = 'DAGISel'
  'instr-info'    = 'InstrInfo'
  'register-info' = 'RegisterInfo'
  'sd-node-info'  = 'SDNodeInfo'
  'subtarget'     = 'SubtargetInfo'
}
$selected = [System.Collections.Generic.List[string]]::new()
foreach ($g in @($Gen | ForEach-Object { $_ -split '[,;]' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })) {
  $n = ($g -replace '^-?gen-', '' -replace '\.inc$', '' -replace "^${Target}Gen", '').ToLowerInvariant()
  $hit = @($backends.Keys | Where-Object { $_ -eq $n -or $backends[$_].ToLowerInvariant() -eq $n })
  if ($hit.Count -ne 1) { throw "-Gen ${g}: unknown backend (one of: $($backends.Keys -join ', '))" }
  if (-not $selected.Contains($hit[0])) { $selected.Add($hit[0]) }
}
if ($selected.Count -eq 0) { $backends.Keys | ForEach-Object { $selected.Add($_) } }

function Full([string]$p) { [System.IO.Path]::GetFullPath($p).TrimEnd('\') }
function Test-Inside([string]$child, [string]$parent) {
  $c = (Full $child).ToLowerInvariant() + '\'; $p = (Full $parent).ToLowerInvariant() + '\'
  return $c.StartsWith($p)
}
$OutDir = Full $Out
foreach ($forbidden in @($BuildDir, $MsilRepo)) {
  if (Test-Inside $OutDir $forbidden) { throw "-Out $OutDir is inside $(Full $forbidden): write TableGen output to a scratch directory, never into the build or source tree" }
}

$tblgen = Join-Path $BuildDir 'bin\llvm-tblgen.exe'
if (-not (Test-Path -LiteralPath $tblgen)) { throw "$tblgen missing: build it first (build agent: msil-build.ps1 llvm-tblgen)" }
$tdDir = Join-Path $MsilRepo "llvm\lib\Target\$Target"
$td = Join-Path $tdDir "$Target.td"
if (-not (Test-Path -LiteralPath $td)) { throw "$td does not exist" }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$fwd = { param($p) ($p -replace '\\', '/') }
$includes = @("-I$(& $fwd $tdDir)", "-I$(& $fwd (Join-Path $BuildDir 'include'))",
              "-I$(& $fwd (Join-Path $MsilRepo 'llvm\include'))", "-I$(& $fwd (Join-Path $MsilRepo 'llvm\lib\Target'))")
$failed = 0
foreach ($b in $selected) {
  $stem = "${Target}Gen$($backends[$b])"
  $outFile = Join-Path $OutDir "$stem.inc"
  $tgArgs = @("-gen-$b") + $includes
  if ($b -eq 'dag-isel') { $tgArgs += '-omit-comments' }
  $tgArgs += @('--long-string-literals=0', (& $fwd $td), '--write-if-changed', '-o', (& $fwd $outFile))
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $text = & $tblgen @tgArgs 2>&1 | Out-String
  $rc = $LASTEXITCODE
  $secs = [math]::Round($sw.Elapsed.TotalSeconds, 1)
  if ($rc -eq 0) {
    Write-Host "OK   -gen-$b -> $outFile (${secs}s)"
    if ($text.Trim()) { Write-Host $text.TrimEnd() }   # warnings
  } else {
    $failed++
    Write-Host "FAIL -gen-$b (exit $rc, ${secs}s)"
    Write-Host $text.TrimEnd()
    # register-info also writes <stem>Enums/Header/MCDesc/TargetDesc.inc next to <stem>.inc.
    Get-ChildItem -LiteralPath $OutDir -Filter "$stem*.inc" -File -ErrorAction SilentlyContinue |
      Where-Object { $_.BaseName -eq $stem -or ($b -eq 'register-info' -and $_.BaseName -match "^${stem}(Enums|Header|MCDesc|TargetDesc)$") } |
      Remove-Item -Force
  }
}
exit ([int]($failed -ne 0))

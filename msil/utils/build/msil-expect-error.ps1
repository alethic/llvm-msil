# msil-expect-error.ps1 - expect a clean diagnostic from a program, not a crash.
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   pwsh -NoProfile -File msil-expect-error.ps1 -Pattern 'error: .*variadic' -- D:/llvm-msil/build/bin/llc.exe -mtriple=x86_64-pc-windows-msvc-msil in.ll -o NUL
#   pwsh -NoProfile -File msil-expect-error.ps1 -Pattern 'unsupported' -ExitCode 1 -- llc -mtriple=x86_64-unknown-linux-gnu-msil in.ll -o NUL
# Passes (exit 0) only if the program exits with exactly -ExitCode (default 1), its combined
# stdout+stderr matches -Pattern (a .NET regular expression, multi-line mode), and the output
# contains none of the crash markers 'PLEASE submit a bug report', 'LLVM ERROR', 'Stack dump',
# 'Exception Code:'. On Windows report_fatal_error ends in abort() with exit code 3, and plain
# 'not' accepts that as an ordinary failure (llvm/utils/not/not.cpp:69-82); this script does not.
# Otherwise it exits 1 (2 for a usage error).
# A bare program name that exists as <build>\bin\<name>.exe (llc, clang, opt, ...) runs that
# tool; a program inside the build directory runs under the SHARED build lock, like msil-run.ps1.
# Deliberately a plain script ($args, no param block): pwsh -File cannot bind parameters around
# '--', and everything after '--' (or after the first argument that is not an option of this
# script) is passed to the program verbatim.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'msil-lock.ps1')
$usage = 'usage: msil-expect-error.ps1 -Pattern <regex> [-ExitCode N] [-BuildDir <dir>] -- <exe> <args...>'
$Pattern = $null; $ExitCode = 1; $BuildDir = $env:MSIL_BUILD_DIR; $WaitMinutes = 240
$i = 0
while ($i -lt $args.Count) {
  $a = "$($args[$i])"
  if ($a -eq '--') { $i++; break }
  if ($a -notmatch '^-[A-Za-z]') { break }
  if ($i + 1 -ge $args.Count) { Write-Host "$usage`n(missing value for $a)"; exit 2 }
  $v = "$($args[$i + 1])"
  if ($a -ieq '-Pattern') { $Pattern = $v }
  elseif ($a -ieq '-ExitCode') { $ExitCode = [int]$v }
  elseif ($a -ieq '-BuildDir') { $BuildDir = $v }
  elseif ($a -ieq '-WaitMinutes') { $WaitMinutes = [double]$v }
  else { Write-Host "$usage`n(unknown option $a)"; exit 2 }
  $i += 2
}
if (-not $Pattern -or $i -ge $args.Count) { Write-Host $usage; exit 2 }
if (-not $BuildDir) { $BuildDir = Join-Path ([System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))) 'build' }
$prog = "$($args[$i])"; $progArgs = @($args | Select-Object -Skip ($i + 1) | ForEach-Object { "$_" })

$exe = $null
$inBin = Join-Path $BuildDir "bin\$prog.exe"
if ($prog -notmatch '[\\/]' -and -not $prog.EndsWith('.exe') -and (Test-Path -LiteralPath $inBin)) { $exe = $inBin }
elseif (Test-Path -LiteralPath $prog -PathType Leaf) { $exe = [System.IO.Path]::GetFullPath($prog) }
else {
  $c = Get-Command $prog -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($c) { $exe = $c.Source }
}
if (-not $exe) { Write-Host "[msil-expect-error] FAIL: program $prog not found"; exit 1 }

$buildFull = [System.IO.Path]::GetFullPath($BuildDir).TrimEnd('\').ToLowerInvariant() + '\'
$lock = $null
if ([System.IO.Path]::GetFullPath($exe).ToLowerInvariant().StartsWith($buildFull)) {
  $lock = Enter-MsilBuildLock -BuildDir $BuildDir -Shared -What "expect-error $([IO.Path]::GetFileName($exe))" -WaitMinutes $WaitMinutes
}
try {
  $lines = @(& $exe @progArgs 2>&1 | ForEach-Object { "$_" })
  $rc = $LASTEXITCODE
} finally { if ($lock) { $lock.Dispose() } }
$text = $lines -join "`n"
if ($text) { Write-Host $text }

$problems = [System.Collections.Generic.List[string]]::new()
if ($rc -ne $ExitCode) { $problems.Add("exit code $rc, expected $ExitCode") }
foreach ($marker in @('PLEASE submit a bug report', 'LLVM ERROR', 'Stack dump', 'Exception Code:')) {
  if ($text.Contains($marker)) { $problems.Add("crash marker '$marker' in the output") }
}
if (-not [regex]::IsMatch($text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline)) {
  $problems.Add("output does not match -Pattern '$Pattern'")
}
if ($problems.Count -gt 0) {
  Write-Host "[msil-expect-error] FAIL: $($problems -join '; ')"
  exit 1
}
Write-Host "[msil-expect-error] PASS: exit $rc, output matches '$Pattern', no crash markers"
exit 0

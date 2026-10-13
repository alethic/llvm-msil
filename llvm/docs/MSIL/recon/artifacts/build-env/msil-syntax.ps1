# msil-syntax.ps1 - syntax-check C++ files with their real flags from compile_commands.json.
# Never runs ninja and writes nothing into the build tree, so any number of agents may run it
# concurrently with each other and with a build.
#   pwsh -NoProfile -File msil-syntax.ps1 D:\llvm-msil\llvm\lib\Target\MSIL\MSILAsmPrinter.cpp [...]
#   pwsh -NoProfile -File msil-syntax.ps1 -NoPch llvm\lib\TargetParser\Triple.cpp
#   pwsh -NoProfile -File msil-syntax.ps1 llvm\include\llvm\TargetParser\Triple.h   # header: checked standalone
# Exit code: 0 if every file passed, 1 otherwise.
[CmdletBinding(PositionalBinding = $false)]
param(
  [Parameter(Mandatory, ValueFromRemainingArguments)][string[]]$Files,
  [string]$BuildDir = 'D:\llvm-msil\build',
  [switch]$NoPch,       # also drop the /FI of the PCH header (stricter: catches missing #includes)
  [switch]$Obj,         # do a real compile to a temp .obj instead of /Zs (for timing experiments)
  [switch]$Quiet
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'msil-env.ps1')

function Norm([string]$p) { [System.IO.Path]::GetFullPath($p).Replace('/', '\').ToLowerInvariant() }

$ccPath = Join-Path $BuildDir 'compile_commands.json'
if (-not (Test-Path $ccPath)) { throw "No $ccPath - configure first (CMAKE_EXPORT_COMPILE_COMMANDS=ON)" }
$db = @{}; $byDir = @{}
foreach ($e in (Get-Content -Raw $ccPath | ConvertFrom-Json)) {
  $k = Norm $e.file
  if (-not $db.ContainsKey($k)) { $db[$k] = $e }
  $d = Split-Path $k -Parent
  if ($k.EndsWith('.cpp') -and -not $byDir.ContainsKey($d)) { $byDir[$d] = $e }
}

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) "msil-syntax-$PID"
New-Item -ItemType Directory -Force $tmp | Out-Null
$failed = 0
foreach ($f in $Files) {
  $full = [System.IO.Path]::GetFullPath((Join-Path $MsilRepo $f))
  if (Test-Path -LiteralPath $f) { $full = [System.IO.Path]::GetFullPath($f) }
  $key = Norm $full
  $isHeader = $key -match '\.(h|hpp|inc|def)$'
  $entry = if (-not $isHeader) { $db[$key] } else { $null }
  $src = $full
  if (-not $entry) {
    # Header, or a .cpp that is not in the database yet (new file, cmake not re-run):
    # borrow the flags of a .cpp in the same or a parent directory. Public headers
    # (<proj>\include\<proj>\X\...) borrow from <proj>\lib\X\... .
    $probe = $key -creplace '\\(llvm|clang|lld)\\include\\(llvm|clang|lld)\\', '\$1\lib\'
    $d = Split-Path $probe -Parent
    while ($d -and -not $byDir.ContainsKey($d)) { $d = Split-Path $d -Parent }
    if (-not $d) {   # last resort: a plain file of the same project
      $proj = if ($key -match '\\clang\\') { 'clang\lib\basic' } elseif ($key -match '\\lld\\') { 'lld\common' } else { 'llvm\lib\support' }
      $d = Norm (Join-Path $MsilRepo $proj)
    }
    if (-not $byDir.ContainsKey($d)) { Write-Host "SKIP $f : no compile command to borrow"; $failed++; continue }
    $entry = $byDir[$d]
    if (-not $Quiet) { Write-Host "note: borrowing flags of $($entry.file)" }
    if ($isHeader) {
      $src = Join-Path $tmp ("hdr_" + [IO.Path]::GetFileNameWithoutExtension($full) + '.cpp')
      Set-Content -LiteralPath $src -Value "#include `"$($full.Replace('\','/'))`"" -Encoding ascii
    }
  }
  $cmd = $entry.command.Trim()
  $m = [regex]::Match($cmd, '^("[^"]+"|\S+)\s+(.*)$')
  $exe = $m.Groups[1].Value.Trim('"'); $rest = $m.Groups[2].Value
  # Strip output, pdb, PCH-use/create flags and the trailing source; keep everything else.
  $rest = $rest -creplace '\s/F[od]\S+', '' -creplace '\s/Y[uc]\S+', '' -creplace '\s/Fp\S+', ''
  if ($NoPch -or $isHeader) { $rest = $rest -creplace '\s/FI\S*cmake_pch\.hxx', '' }   # headers: test self-containment
  $rest = $rest -creplace '\s-c(\s+--)?\s+\S+$', ''
  $mode = if ($Obj) { "/Fo`"$tmp\out.obj`" -c" } else { '/Zs' }   # /Zs: no -c (clang-cl warns)
  $rsp = Join-Path $tmp 'cmd.rsp'
  Set-Content -LiteralPath $rsp -Value "$rest $mode `"$src`"" -Encoding ascii
  $sw = [Diagnostics.Stopwatch]::StartNew()
  Push-Location $entry.directory
  try { $out = & $exe "@$rsp" 2>&1 | Out-String; $rc = $LASTEXITCODE } finally { Pop-Location }
  $secs = [math]::Round($sw.Elapsed.TotalSeconds, 1)
  if ($rc -eq 0) { if (-not $Quiet) { Write-Host "OK   $f (${secs}s)" } }
  else {
    $failed++
    Write-Host "FAIL $f (${secs}s)"; Write-Host $out
    if ($out -match "\.inc'?:? *(file not found|No such file)|cannot open include file: '[^']*\.inc") {
      Write-Host 'hint: a tablegen-generated .inc is missing; build its *CommonTableGen / *_gen target with msil-build.ps1 first'
    }
  }
}
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
exit ([int]($failed -ne 0))

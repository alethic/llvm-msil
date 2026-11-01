# exit-matrix-legs.ps1 - run-m1-gate.ps1's matrix-legs row alone, for exit-matrix-legs.test
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   pwsh -NoProfile -File exit-matrix-legs.ps1 -Work DIR -Python PY [-Case NAME[,NAME...]]
#
# Loads the functions Add-GateRow, Invoke-SetupCommand, Invoke-MatrixLegsStep and
# Get-ExpectedLegs from the syntax tree of msil/utils/run-m1-gate.ps1, without running that script
# (it would configure, build and run every leg), and gives them the script variables they read:
# $BuildDir (DIR/<case>/build), $ci, $resultsFile (DIR/<case>/rows.tsv, also
# $env:MSIL_CI_RESULTS), $MsilRepo (this repository) and $MsilPython (PY). Each case but the last
# runs the matrix-legs step as 'run-m1-gate.ps1 -Exit' does,
# 'try { Invoke-MatrixLegsStep } catch { <a FAIL row> }', which leaves the row matrix-legs/- in
# DIR/<case>/rows.tsv. The cases, in this order (default: all):
#   pass            $ci is msil/utils/ci: the real 'lit-legs.ps1 -Describe'
#   describe-fails  $ci is DIR/<case>/ci, whose stand-in lit-legs.ps1 -Describe prints four lines
#                   on stderr and exits 3, printing nothing on stdout; the case's build already
#                   holds a legs file (lit-results/legs.json), as an earlier run leaves it
#   drop-legs       the stand-in prints the legs file of the case pass without the legs that the
#                   legs rule counts (those tagged '', 'Wsl' or 'Exit')
#   no-also-run     it prints that file without msil_also_run and also_run in the runs of the
#                   Quick legs (the gate rule's legs)
#   sh-script       it prints that file as if lit-legs.sh had written it ("script":
#                   "lit-legs.sh")
#   expected        Get-ExpectedLegs, with no switch, -Exit and -Exit -NoWsl, against a stand-in
#                   'lit-legs.ps1 -List' that lists a leg of every tag; DIR/expected/legs.txt gets
#                   one line '<switches>: <legs>' each ('no switch: ...' for the first)
# A case that cannot run (no legs file of the case pass, for example) is an error on stderr, and
# the script then exits 1 after the other cases.
param(
  [Parameter(Mandatory)][string]$Work,
  [Parameter(Mandatory)][string]$Python,
  [string[]]$Case = @()
)
$ErrorActionPreference = 'Stop'

$allCases = 'pass', 'describe-fails', 'drop-legs', 'no-also-run', 'sh-script', 'expected'
$cases = @($Case | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
foreach ($c in $cases) {
  if ($allCases -notcontains $c) { throw "unknown case '$c' (cases: $($allCases -join ', '))" }
}
if ($cases.Count -eq 0) { $cases = $allCases }

$MsilRepo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..'))
$MsilPython = $Python
$gateScript = Join-Path $MsilRepo 'msil\utils\run-m1-gate.ps1'
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($gateScript, [ref]$null,
                                                                 [ref]$parseErrors)
if ($parseErrors.Count -gt 0) { throw "run-m1-gate.ps1 does not parse: $($parseErrors[0])" }
$wanted = 'Add-GateRow', 'Invoke-SetupCommand', 'Invoke-MatrixLegsStep', 'Get-ExpectedLegs'
$loaded = @()
foreach ($f in $ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true)) {
  if ($wanted -contains $f.Name) {
    . ([scriptblock]::Create($f.Extent.Text))
    $loaded += $f.Name
  }
}
foreach ($w in $wanted) {
  if ($loaded -notcontains $w) { throw "run-m1-gate.ps1 defines no function $w" }
}

New-Item -ItemType Directory -Force -Path $Work | Out-Null
$Work = [System.IO.Path]::GetFullPath($Work)
# As Inputs/ci-legs.py runs the leg scripts: neither a gate's results file nor a CI step summary
# gets these rows, and no corpus setting of the environment reaches lit-legs.ps1.
foreach ($v in 'MSIL_CORPUS_SKIP', 'MSIL_CORPUS_CACHE', 'GITHUB_STEP_SUMMARY') {
  Remove-Item -LiteralPath "Env:$v" -ErrorAction SilentlyContinue
}
$passLegs = Join-Path $Work 'pass\build\lit-results\legs.json'

# DIR/<case>/ci/lit-legs.ps1: a stand-in with the given body after lit-legs.ps1's parameters.
function New-StandInCi([string]$Dir, [string]$Body) {
  $ciDir = Join-Path $Dir 'ci'
  New-Item -ItemType Directory -Force -Path $ciDir | Out-Null
  $text = "param([string]`$BuildDir, [switch]`$Describe, [switch]`$List)`n" +
          "`$ErrorActionPreference = 'Stop'`n$Body`n"
  [System.IO.File]::WriteAllText((Join-Path $ciDir 'lit-legs.ps1'), $text,
                                 [System.Text.UTF8Encoding]::new($false))
  return $ciDir
}

# The body of a stand-in that prints the legs file of the case pass after the given edit of $j.
function Get-EditBody([string]$Edit) {
  if (-not (Test-Path -LiteralPath $passLegs)) { throw "no legs file of the case pass: $passLegs" }
  "`$j = Get-Content -Raw -LiteralPath '$($passLegs.Replace("'", "''"))' | ConvertFrom-Json`n" +
  "$Edit`n`$j | ConvertTo-Json -Depth 32"
}

$failed = $false
foreach ($name in $cases) {
  $dir = Join-Path $Work $name
  Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force -Path $dir | Out-Null
  $BuildDir = Join-Path $dir 'build'
  $resultsFile = Join-Path $dir 'rows.tsv'
  Set-Content -LiteralPath $resultsFile -Value @() -Encoding utf8
  $env:MSIL_CI_RESULTS = $resultsFile
  try {
    if ($name -eq 'expected') {
      $ci = New-StandInCi $dir (@(
        "'net-core (-Quick)'", "'net8'", "'wsl-net10 (-Wsl)'", "'upstream (-Full)'",
        "'ctest-lto (-Exit)'", "'gc-churn-wsl (-Exit)'") -join "`n")
      $Leg = @(); $Full = $false
      $lines = foreach ($s in @(@($false, $false), @($true, $false), @($true, $true))) {
        $Exit, $NoWsl = $s
        $r = Get-ExpectedLegs
        $switches = @(@('-Exit', $Exit), @('-NoWsl', $NoWsl) | Where-Object { $_[1] } |
                      ForEach-Object { $_[0] }) -join ' '
        "$(if ($switches) { $switches } else { 'no switch' }): " +
          "$(if ($null -eq $r) { '<no list>' } else { $r.Expected -join ', ' })"
      }
      [System.IO.File]::WriteAllText((Join-Path $dir 'legs.txt'), ($lines -join "`n") + "`n",
                                     [System.Text.UTF8Encoding]::new($false))
      continue
    }
    $ci = switch ($name) {
      'pass' { Join-Path $MsilRepo 'msil\utils\ci' }
      'describe-fails' {
        # A legs file of an earlier run, which the step must remove.
        New-Item -ItemType Directory -Force -Path (Join-Path $BuildDir 'lit-results') | Out-Null
        Set-Content -LiteralPath (Join-Path $BuildDir 'lit-results\legs.json') -Value '{}'
        $err = @(1..4 | ForEach-Object { "[Console]::Error.WriteLine('lit-legs.ps1: planted failure $_')" })
        New-StandInCi $dir (($err + 'exit 3') -join "`n")
      }
      'drop-legs' {
        New-StandInCi $dir (Get-EditBody "`$j.legs = @(`$j.legs | Where-Object { `$_.tag -notin '', 'Wsl', 'Exit' })")
      }
      'no-also-run' {
        New-StandInCi $dir (Get-EditBody (
          "foreach (`$l in @(`$j.legs | Where-Object { `$_.tag -eq 'Quick' })) {`n" +
          "  foreach (`$r in `$l.runs) {`n" +
          "    `$r.params.PSObject.Properties.Remove('msil_also_run')`n" +
          "    `$r.also_run = @()`n" +
          "  }`n}"))
      }
      'sh-script' { New-StandInCi $dir (Get-EditBody "`$j.script = 'lit-legs.sh'") }
    }
    try { Invoke-MatrixLegsStep } catch { Add-GateRow 'matrix-legs' '-' 'FAIL' "$_" }
  } catch {
    [Console]::Error.WriteLine("exit-matrix-legs.ps1: case $name`: $_")
    $failed = $true
  }
}
exit $(if ($failed) { 1 } else { 0 })

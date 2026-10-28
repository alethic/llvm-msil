# ci-common.ps1 - dot-source me: helpers shared by the PowerShell scripts in msil/utils/ci.
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Result rows. Every step or leg of a CI script ends in one row (step, name, PASS/FAIL/SKIP,
# seconds, detail). SKIP marks a leg left out on purpose (lit-legs.ps1's ctest leg while
# $env:MSIL_CORPUS_SKIP names c-testsuite): it is no pass, but no failure either, so
# Test-MsilCiFailed counts only FAIL rows. The script prints its rows as a table at the end; the
# rows are also appended
#   - to $env:MSIL_CI_RESULTS (tab-separated; run-m1-gate.ps1 sets it and prints all rows), and
#   - to $env:GITHUB_STEP_SUMMARY as a Markdown table (GitHub Actions).
# ci-common.sh writes the same format.
#
# NuGet tools. Get-MsilNuGetTool finds runtime.<rid>.Microsoft.NETCore.<ILAsm|ILDAsm> at the
# version pinned in msil/utils/tools/msil-tools.proj (MsilILAsmVersion) in the NuGet
# global-packages folder, the way msil/test/lit.cfg.py does. It never downloads anything (and
# never reads NuGet.Config): 'dotnet restore msil/utils/tools/msil-tools.proj
# -p:MsilToolsRid=<rid>' fetches the packages; the CI workflow runs it.

$script:MsilCiRows = [System.Collections.Generic.List[object]]::new()

function Add-MsilCiResult {
  param(
    [Parameter(Mandatory)][string]$Step,
    [Parameter(Mandatory)][string]$Name,
    [Parameter(Mandatory)][ValidateSet('PASS', 'FAIL', 'SKIP')][string]$Status,
    [double]$Seconds = 0,
    [string]$Detail = ''
  )
  $detailText = ($Detail -replace '[\t\r\n]+', ' ').Trim()
  $row = [pscustomobject]@{
    Step = $Step; Name = $Name; Result = $Status
    Time = '{0}:{1:00}' -f [int][math]::Floor($Seconds / 60), [int][math]::Floor($Seconds % 60)
    Detail = $detailText
  }
  $script:MsilCiRows.Add($row)
  if ($env:MSIL_CI_RESULTS) {
    $line = "$Step`t$Name`t$Status`t$([int][math]::Floor($Seconds))`t$detailText"
    Add-Content -LiteralPath $env:MSIL_CI_RESULTS -Value $line -Encoding utf8
  }
  $color = switch ($Status) { 'PASS' { 'Green' } 'SKIP' { 'Yellow' } default { 'Red' } }
  Write-Host "[msil-ci] $Status $Step/$Name ($($row.Time)) $detailText" -ForegroundColor $color
}

function Write-MsilCiSummary {
  param([Parameter(Mandatory)][string]$Title)
  Write-Host ''
  Write-Host "==== $Title ===="
  $script:MsilCiRows | Format-Table -AutoSize -Wrap Step, Name, Result, Time, Detail | Out-String -Width 200 |
    Write-Host
  if ($env:GITHUB_STEP_SUMMARY) {
    $md = @("### $Title", '', '| Step | Name | Result | Time | Detail |', '|---|---|---|---|---|')
    foreach ($r in $script:MsilCiRows) {
      $md += "| $($r.Step) | $($r.Name) | $($r.Result) | $($r.Time) | $($r.Detail -replace '\|', '\|') |"
    }
    Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Value ($md + '') -Encoding utf8
  }
}

# True if a row failed: a SKIP row is no failure.
function Test-MsilCiFailed {
  return [bool]($script:MsilCiRows | Where-Object { $_.Result -notin @('PASS', 'SKIP') })
}

# Lit's summary lines ('  Passed     : 236 (98.33%)') as a hashtable label -> count.
function Get-MsilLitCounts {
  param([string[]]$Lines)
  $counts = @{}
  foreach ($l in $Lines) {
    $m = [regex]::Match($l, '^\s+([A-Za-z][A-Za-z ]*?)\s*:\s*(\d+) \(')
    if ($m.Success) { $counts[$m.Groups[1].Value] = [int]$m.Groups[2].Value }
  }
  return $counts
}

function Get-MsilToolsVersion {
  param([Parameter(Mandatory)][string]$Repo)
  $proj = Join-Path $Repo 'msil/utils/tools/msil-tools.proj'
  $m = [regex]::Match((Get-Content -Raw -LiteralPath $proj), '<MsilILAsmVersion[^>]*>\s*([0-9.]+)\s*</MsilILAsmVersion>')
  if (-not $m.Success) { throw "cannot read MsilILAsmVersion from $proj" }
  return $m.Groups[1].Value
}

function Get-MsilNuGetRoot {
  if ($env:NUGET_PACKAGES) { return $env:NUGET_PACKAGES }
  $dotnet = Get-Command dotnet -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($dotnet) {
    $out = & $dotnet.Source nuget locals global-packages --list 2>$null
    foreach ($l in @($out)) {
      $m = [regex]::Match("$l", '^global-packages:\s*(.+?)\s*$')
      if ($m.Success) { return $m.Groups[1].Value }
    }
  }
  return Join-Path ([Environment]::GetFolderPath('UserProfile')) '.nuget/packages'
}

# <global-packages>/runtime.<rid>.microsoft.netcore.<tool>/<version>/runtimes/<rid>/native/<tool>[.exe]
function Get-MsilNuGetTool {
  param(
    [Parameter(Mandatory)][string]$Repo,
    [Parameter(Mandatory)][ValidateSet('win-x64', 'linux-x64')][string]$Rid,
    [Parameter(Mandatory)][ValidateSet('ilasm', 'ildasm')][string]$Tool
  )
  $version = Get-MsilToolsVersion -Repo $Repo
  $exe = if ($Rid.StartsWith('win-')) { "$Tool.exe" } else { $Tool }
  $path = Join-Path (Get-MsilNuGetRoot) "runtime.$Rid.microsoft.netcore.$Tool/$version/runtimes/$Rid/native/$exe"
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw ("$path not found: run 'dotnet restore msil/utils/tools/msil-tools.proj " +
           "-p:MsilToolsRid=$Rid' (package runtime.$Rid.Microsoft.NETCore.$Tool $version)")
  }
  return [System.IO.Path]::GetFullPath($path).Replace('\', '/')
}

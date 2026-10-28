# run-m1-gate.ps1 - run every leg of the phase gate (the milestone-1 gate of plan M10, extended in
# M11 and, as the Phase 2 exit gate, in M23) on the development host, through the scripts that
# the CI workflow runs, in the CI order, and print one row per step or leg.
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   pwsh -NoProfile -File msil/utils/run-m1-gate.ps1                  # everything
#   pwsh -NoProfile -File msil/utils/run-m1-gate.ps1 -CorpusCache C:/llvm-msil-scratch/corpus-cache
#   pwsh -NoProfile -File msil/utils/run-m1-gate.ps1 -SkipConfigure -SkipBuild -Leg roundtrip
#
# Steps (the Windows job of .github/workflows/msil-ci.yml runs the same scripts in the same order,
# except host-setup, which only the development host needs, and the corpus and determinism rows,
# which the CI runs as its own steps and job):
#   1. msil/utils/ci/configure.ps1   msil-configure.ps1 -Experimental MSIL (the only step that
#                                    differs from CI, which configures with -C MSIL.cmake)
#   2. msil/utils/ci/build.ps1       msil-build.ps1 -Standard: the targets of
#                                    msil/utils/ci/build-targets.txt (plan 5.6.11), jitall
#   3. msil/utils/ci/unit-tests.ps1  TargetParserTests, MCTests, MSILTests, CLITests
#                                    (no skipped tests)                          (~1 min)
#   4. host-setup (not with -NoWsl; M12, plan 5.12.8 and the gates' step G0), idempotent: three
#        rows, wsl-dotnet (msil/utils/msil-wsl-dotnet.sh inside WSL installs the private .NET
#        runtimes of the WSL legs into $HOME/.dotnet-msil; the first run downloads them),
#        linux-sysroot (msil/utils/make-linux-sysroot.py copies the E7 Linux oracle's
#        libraries out of WSL into $env:MSIL_LINUX_SYSROOT, else
#        C:\llvm-msil-scratch\linux-sysroot) and glibc-sysroot (M15:
#        msil/utils/make-glibc-sysroot.sh inside WSL builds the hosted leg's glibc sysroot into
#        $env:MSIL_GLIBC_SYSROOT, else C:\llvm-msil-scratch\glibc-sysroot; it downloads only
#        missing packages). Seconds once done; no root, no system setting.
#   5. corpus (M23, plan 5.12.9; a phase-exit row): python msil/utils/corpus/fetch.py --cache
#        <-CorpusCache> --check (offline, all ten entries of msil/utils/corpus/manifest.json,
#        never --skip; it writes the stamp that lit's corpus legs read), row corpus/check; with
#        -FetchCorpus first fetch.py --cache <-CorpusCache> (the download the user approved on
#        2026-10-21: only entries the cache lacks, about 7 MB, every hash verified), row
#        corpus/fetch. -CorpusCache defaults to $env:MSIL_CORPUS_CACHE, else
#        C:\llvm-msil-scratch\corpus-cache, and is exported to the legs as MSIL_CORPUS_CACHE;
#        MSIL_CORPUS_SKIP is removed from the legs' environment, so the phase gate runs all ten
#        entries (the CI leaves CoreMark and c-testsuite out, plan section 10 item 11). A failed
#        corpus row does not stop the legs (the corpus legs then fail with lit's configuration
#        error).                                                                  (~2 s)
#   6. msil/utils/ci/lit-legs.ps1 -Wsl: every leg of 'lit-legs.ps1 -List' except the -Quick and
#        -Full ones (-Full adds those; -NoWsl leaves out the -Wsl ones): net8 (all MSIL lit
#        tests under obj on .NET 8, .NET Framework 4.8 and the native oracle, msil_equiv=1 with
#        the equivalence link, msil_also_run=10,8+tc0, msil_scale_tests=1, msil_require_csc=1),
#        more-levels, asm-oracle, modern-ilasm, roundtrip, expected, utc, obj-text, lto, det,
#        refs-net48, refs-net8, refs-net10, programs and programs-lto (the exit corpus through
#        objects and LTO), ctest (c-testsuite), wsl-net10, wsl-ilasm, expected-linux,
#        obj-text-wsl, lto-wsl, programs-wsl, programs-lto-wsl, det-wsl and hosted (see
#        lit-legs.ps1's header, which gives each leg's minutes). M23's two phase-gate runs
#        (2026-10-27, on a host shared with other work) took 144 and 217 min of Windows legs
#        (net8 60 and 87, asm-oracle 19 and 22.5, lto 11 and 22.5, ctest 9 and 18, more-levels
#        15.5) and 79 and 117 min of WSL legs (wsl-net10 56 and 103), against plan M23's
#        estimate of 53 and 22; lto-wsl's msil/test/M2/00-link (about +2-3 min) and
#        programs-lto-wsl's msil_equiv=1 (about +1-3) came after them.
#   7. determinism (M23, plan 5.7.12 and section 12 'Cross-host determinism'; a phase-exit row):
#        msil/utils/ci/det-manifest.py build twice, from the work directories
#        C:\llvm-msil-scratch\gates\det\a and ...\b, of every C file of msil/test/M1 with
#        clang -c for both x86_64 MSIL triples at -O0 and -O2 and an llvm-ar archive per triple
#        and level, then det-manifest.py compare (the CI's determinism job compares the two CI
#        hosts' manifests instead; 1 and 0.5 min in M23's gate runs).                  (~1 min)
#   8. msil/utils/ci/upstream-hooks.ps1 (M11; not with -SkipUpstreamHooks)        (~4 min)
#        manifest (msil/utils/upstream-hooks.txt lists every upstream file the fork changes, and
#        every object-format switch has an MSIL case), pins (action pins as upstream's), lit
#        (the upstream lit tests of the hooked files), gtest (all of TargetParserTests and
#        MCTests), compile (ARMAsmParser.cpp and lldb's GDBRemoteCommunicationClient.cpp in the
#        private hooks build tree on C:). CI runs it with -NoCompile -NoManifest -MinPass 250.
#   9. ci-sh (not with -NoWsl)       a smoke check of the Linux job's scripts inside WSL: bash -n
#                                    on every msil/utils/ci/*.sh, 'lit-legs.sh --list' must print
#                                    the legs its header documents, and lit-legs.sh and
#                                    unit-tests.sh must fail (exit non-zero, FAIL rows) against an
#                                    empty build directory. The Linux job's configure, build,
#                                    unit-test and lit steps themselves are not run here (they
#                                    need a Linux build tree); only the CI runs them.
# A failed configure or build stops steps 3 to 8 (nothing in them could pass); ci-sh needs no
# build and still runs. A failed host-setup or corpus row does not stop the legs: the legs that
# need what it makes then fail with a configuration error that names it. The unit tests and every
# leg run even after a failure. The summary table has one row per step or leg; the exit status is
# 0 only if every row is PASS (a SKIP row, which lit-legs.ps1 writes for a leg that
# MSIL_CORPUS_SKIP leaves out, is not, and cannot occur here: the variable is cleared). A child
# script that ends with a non-zero exit status without a FAIL row (an exception before its first
# leg, for example an unknown or removed -Leg, or a killed process) gets a FAIL row, and so does
# every expected leg without a row of its own (the legs of 'lit-legs.ps1 -List' that -NoWsl,
# -Full and -Leg select; -Quick legs only when -Leg names them), so a run that died part-way
# never passes. The corpus and determinism rows run with -Leg too.
#
# Build agent or gate agent only: steps 1 and 2 take the exclusive build-dir lock (cmake, ninja).
# The whole run took 228, 339, 184 and 275 minutes on this host in M23's four gate rounds (gate
# step 16: 129-217 of Windows legs, 50-138 of WSL legs, about 5 for the other steps), more than
# section 10's Q32 budget of 70-90 minutes, which the user accepted (plan section 12, row 'Q32
# amended'), so start it with run_in_background and wait for it (a background job dies with the
# agent's turn).
# -SkipConfigure/-SkipBuild reuse the current build; -Leg runs only the named lit legs (see
# lit-legs.ps1 -List); -SkipUpstreamHooks leaves out step 8; -NoWsl leaves out host-setup, the
# -Wsl legs and ci-sh. The upstream-hooks step always runs all of its legs, and the gate checks
# that each of them (manifest, pins, lit, gtest, compile) has a row. The format and scope checks
# of the gate (plan M23 gate steps 19 and 20) are not legs and are not run here.
[CmdletBinding()]
param(
  [switch]$SkipConfigure,
  [switch]$SkipBuild,
  [switch]$SkipUnitTests,
  [switch]$SkipUpstreamHooks,
  [switch]$NoWsl,
  [switch]$Full,
  [string[]]$Leg = @(),
  [string]$BuildDir = '',         # default: $env:MSIL_BUILD_DIR, else <repo>\build
  [string]$CorpusCache = '',      # default: $env:MSIL_CORPUS_CACHE, else C:\llvm-msil-scratch\corpus-cache
  [switch]$FetchCorpus
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'build\msil-env.ps1')
. (Join-Path $PSScriptRoot 'build\msil-lock.ps1')
if (-not $BuildDir) { $BuildDir = $MsilBuild }
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$ci = Join-Path $PSScriptRoot 'ci'
# The distribution of the WSL legs (lit.cfg.py's default --param msil_wsl_distro; lit-legs.ps1
# passes none).
$wslDistro = 'Ubuntu'
# The E7 Linux oracle's sysroot, as lit-legs.ps1 and lit.cfg.py default it.
$linuxSysroot = if ($env:MSIL_LINUX_SYSROOT) { $env:MSIL_LINUX_SYSROOT } else { 'C:\llvm-msil-scratch\linux-sysroot' }
# The hosted leg's glibc sysroot (make-glibc-sysroot.sh), as lit-legs.ps1 defaults it.
$glibcSysroot = if ($env:MSIL_GLIBC_SYSROOT) { $env:MSIL_GLIBC_SYSROOT } else { 'C:\llvm-msil-scratch\glibc-sysroot' }

# The exit corpus' cache (plan 5.12.9): the legs read it through MSIL_CORPUS_CACHE, and the phase
# gate runs all ten entries, so MSIL_CORPUS_SKIP (the CI's licence default) is cleared.
if (-not $CorpusCache) {
  $CorpusCache = if ($env:MSIL_CORPUS_CACHE) { $env:MSIL_CORPUS_CACHE } else { 'C:\llvm-msil-scratch\corpus-cache' }
}
$CorpusCache = [System.IO.Path]::GetFullPath($CorpusCache)
$env:MSIL_CORPUS_CACHE = $CorpusCache
Remove-Item Env:MSIL_CORPUS_SKIP -ErrorAction SilentlyContinue
# The work directories of the determinism row (det-manifest.py build refuses one inside the
# repository, and empties only one it made, marked, or one that holds only <triple>/
# directories, as these do after earlier runs).
$detRoot = 'C:\llvm-msil-scratch\gates\det'

$resultsFile = Join-Path ([System.IO.Path]::GetTempPath()) "msil-m1-gate-$PID.tsv"
Set-Content -LiteralPath $resultsFile -Value @() -Encoding utf8
$env:MSIL_CI_RESULTS = $resultsFile
$started = Get-Date

function Invoke-CiScript([string]$Name, [string[]]$ScriptArgs) {
  Write-Host ''
  Write-Host "==== $(Get-Date -Format s) msil/utils/ci/$Name $($ScriptArgs -join ' ') ===="
  & pwsh -NoProfile -File (Join-Path $ci $Name) -BuildDir $BuildDir @ScriptArgs | Out-Host
  return $LASTEXITCODE
}

# A row in the format of ci-common.ps1 (step, name, PASS/FAIL, seconds, detail).
function Add-GateRow([string]$Step, [string]$Name, [string]$Status, [string]$Why,
                     [double]$Seconds = 0) {
  $why = ($Why -replace '[\t\r\n]+', ' ').Trim()
  $line = "$Step`t$Name`t$Status`t$([int][math]::Floor($Seconds))`t$why"
  Add-Content -LiteralPath $resultsFile -Value $line -Encoding utf8
  $color = if ($Status -eq 'PASS') { 'Green' } else { 'Red' }
  Write-Host "[m1-gate] $Status $Step/$Name $why" -ForegroundColor $color
}

function Add-SkippedRow([string]$Step, [string]$Why) { Add-GateRow $Step '-' 'FAIL' $Why }

# The rows so far. A line that a killed child left incomplete is a FAIL row.
function Read-GateRows {
  @(Get-Content -LiteralPath $resultsFile -Encoding utf8 | Where-Object { $_ } | ForEach-Object {
    $f = $_ -split "`t", 5
    $seconds = 0
    # SKIP (lit-legs.ps1's row for a leg that MSIL_CORPUS_SKIP leaves out) is a row, but not a
    # pass.
    $ok = $f.Count -ge 4 -and ($f[2] -in @('PASS', 'FAIL', 'SKIP')) -and
          [int]::TryParse($f[3], [ref]$seconds)
    [pscustomobject]@{
      Step = $f[0]; Leg = $(if ($f.Count -ge 2) { $f[1] } else { '?' })
      Result = $(if ($ok) { $f[2] } else { 'FAIL' })
      Time = '{0}:{1:00}' -f [int][math]::Floor($seconds / 60), ($seconds % 60)
      Detail = $(if (-not $ok) { "malformed result row '$_'" } elseif ($f.Count -ge 5) { $f[4] }
                 else { '' })
    }
  })
}

# After a child script: a FAIL row for every expected name without a row of the step, and one
# for a non-zero exit status that no FAIL row of the step explains.
function Test-ChildRows([string]$Step, [string]$Script, [int]$ExitCode, [string[]]$Expected,
                        [string[]]$Known = @()) {
  $rows = @(Read-GateRows | Where-Object { $_.Step -eq $Step })
  foreach ($e in $Expected) {
    if (-not ($rows | Where-Object { $_.Leg -eq $e })) {
      $why = if ($Known.Count -gt 0 -and $Known -notcontains $e) { "unknown leg (not in $Script -List)" }
             else { 'no result row' }
      Add-GateRow $Step $e 'FAIL' "$why; $Script exited $ExitCode"
    }
  }
  $rows = @(Read-GateRows | Where-Object { $_.Step -eq $Step })
  if ($ExitCode -ne 0 -and -not ($rows | Where-Object { $_.Result -ne 'PASS' })) {
    Add-SkippedRow $Step "$Script exited $ExitCode without a FAIL row"
  }
}

# The legs that lit-legs.ps1 runs with the arguments the gate passes: the -Leg names, else the
# legs of 'lit-legs.ps1 -List' without the -Wsl tag under -NoWsl, without -Full unless -Full, and
# never the -Quick ones. $null if the list cannot be read.
function Get-ExpectedLegs {
  $env:MSIL_CI_RESULTS = ''         # -List records no row; make sure of it
  try {
    $lines = @(& pwsh -NoProfile -File (Join-Path $ci 'lit-legs.ps1') -BuildDir $BuildDir -List)
    $rc = $LASTEXITCODE
  } finally { $env:MSIL_CI_RESULTS = $resultsFile }
  if ($rc -ne 0) { return $null }
  $all = @()
  foreach ($l in $lines | Where-Object { "$_".Trim() }) {
    $m = [regex]::Match("$l", '^(\S+)(?: \((-\w+)\))?\s*$')
    if (-not $m.Success) { return $null }
    $all += [pscustomobject]@{ Name = $m.Groups[1].Value; Tag = $m.Groups[2].Value }
  }
  if ($all.Count -eq 0) { return $null }
  $selected = @($Leg | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } |
                Where-Object { $_ })
  # An unknown tag counts as selected: a missing row then fails instead of passing.
  $expected = if ($selected.Count -gt 0) { $selected }
              else {
                @($all | Where-Object {
                    -not (($_.Tag -eq '-Wsl' -and $NoWsl) -or ($_.Tag -eq '-Full' -and -not $Full) -or
                          $_.Tag -eq '-Quick')
                  } | ForEach-Object { $_.Name })
              }
  return [pscustomobject]@{ Expected = $expected; Known = @($all | ForEach-Object { $_.Name }) }
}

# Step 4: the private WSL runtimes, the E7 sysroot and the hosted leg's glibc sysroot (see the
# header), one row each. Runs a command, echoes its output and returns @{ Rc; Lines }.
function Invoke-SetupCommand([string]$Exe, [string[]]$Arguments) {
  $lines = [System.Collections.Generic.List[string]]::new()
  $env:WSL_UTF8 = '1'               # wsl.exe's own messages in UTF-8, not UTF-16
  try {
    & $Exe @Arguments 2>&1 | ForEach-Object { $s = "$_"; $lines.Add($s); Write-Host $s }
    $rc = $LASTEXITCODE
  } catch {
    $lines.Add("$_"); $rc = -1
  }
  return @{ Rc = $rc; Lines = $lines.ToArray() }
}

# X:\a\b -> /mnt/x/a/b, the form a command inside WSL takes; $null for a path without a drive
# letter.
function ConvertTo-WslPath([string]$Path) {
  try { $full = [System.IO.Path]::GetFullPath($Path) } catch { return $null }
  $m = [regex]::Match($full, '^([A-Za-z]):(\\.*)?$')
  if (-not $m.Success) { return $null }
  return "/mnt/$($m.Groups[1].Value.ToLowerInvariant())$($m.Groups[2].Value.Replace('\', '/'))"
}

function Invoke-HostSetup {
  Write-Host ''
  Write-Host "==== $(Get-Date -Format s) host-setup: private WSL runtimes, the E7 Linux sysroot, the glibc sysroot ===="
  $wsl = Get-Command wsl.exe -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty Source
  $sw = [Diagnostics.Stopwatch]::StartNew()
  if (-not $wsl) {
    Add-GateRow 'host-setup' 'wsl-dotnet' 'FAIL' 'wsl.exe not found' $sw.Elapsed.TotalSeconds
  } else {
    $r = Invoke-SetupCommand $wsl @('-d', $wslDistro, '--cd', $MsilRepo, '--exec', 'bash',
                                    'msil/utils/msil-wsl-dotnet.sh')
    $have = @($r.Lines | Where-Object { $_ -match '^msil-wsl-dotnet\.sh: Microsoft\.NETCore\.App \S+ (installed|present) in ' } |
              ForEach-Object { ($_ -replace '^msil-wsl-dotnet\.sh: Microsoft\.NETCore\.App ', '') -replace ' in .*$', '' })
    $detail = if ($have.Count -gt 0) { "Microsoft.NETCore.App $($have -join ', ') in `$HOME/.dotnet-msil (WSL $wslDistro)" }
              else { 'no runtime reported' }
    $status = if ($r.Rc -eq 0 -and $have.Count -gt 0) { 'PASS' } else { 'FAIL' }
    Add-GateRow 'host-setup' 'wsl-dotnet' $status "$detail; msil-wsl-dotnet.sh exited $($r.Rc)" $sw.Elapsed.TotalSeconds
  }
  $sw.Restart()
  $r = Invoke-SetupCommand $MsilPython @((Join-Path $PSScriptRoot 'make-linux-sysroot.py'), '--out', $linuxSysroot,
                                         '--distro', $wslDistro)
  $summary = @($r.Lines | Where-Object { $_ -match '^make-linux-sysroot: .*file\(s\) updated$' } | Select-Object -Last 1)
  $detail = if ($summary.Count -gt 0) { $summary[0] -replace '^make-linux-sysroot: ', '' } else { 'no summary' }
  $status = if ($r.Rc -eq 0 -and $summary.Count -gt 0) { 'PASS' } else { 'FAIL' }
  Add-GateRow 'host-setup' 'linux-sysroot' $status "$detail; make-linux-sysroot.py exited $($r.Rc)" $sw.Elapsed.TotalSeconds
  $sw.Restart()
  $glibcWsl = ConvertTo-WslPath $glibcSysroot
  if (-not $wsl) {
    Add-GateRow 'host-setup' 'glibc-sysroot' 'FAIL' 'wsl.exe not found' $sw.Elapsed.TotalSeconds
  } elseif (-not $glibcWsl) {
    Add-GateRow 'host-setup' 'glibc-sysroot' 'FAIL' "cannot translate '$glibcSysroot' into a WSL path" $sw.Elapsed.TotalSeconds
  } else {
    $r = Invoke-SetupCommand $wsl @('-d', $wslDistro, '--cd', $MsilRepo, '--exec', 'bash',
                                    'msil/utils/make-glibc-sysroot.sh', $glibcWsl)
    $summary = @($r.Lines | Where-Object { $_ -match '^make-glibc-sysroot: .* is up to date \(.*\)$' -or
                                           $_ -match '^make-glibc-sysroot: wrote ' } | Select-Object -Last 1)
    $errors = @($r.Lines | Where-Object { $_ -match '^make-glibc-sysroot: error: ' } | Select-Object -Last 1)
    $detail = if ($summary.Count -gt 0) { $summary[0] -replace '^make-glibc-sysroot: ', '' }
              elseif ($errors.Count -gt 0) { $errors[0] -replace '^make-glibc-sysroot: ', '' }
              else { 'no summary' }
    $status = if ($r.Rc -eq 0 -and $summary.Count -gt 0) { 'PASS' } else { 'FAIL' }
    Add-GateRow 'host-setup' 'glibc-sysroot' $status "$detail; make-glibc-sysroot.sh exited $($r.Rc)" $sw.Elapsed.TotalSeconds
  }
}

# Step 5: fetch.py --check on the corpus cache, after fetch.py with -FetchCorpus (see the header),
# one row each.
function Invoke-CorpusStep {
  Write-Host ''
  Write-Host "==== $(Get-Date -Format s) corpus: msil/utils/corpus/fetch.py --cache $CorpusCache ===="
  $fetch = Join-Path $PSScriptRoot 'corpus\fetch.py'
  $runs = @()
  if ($FetchCorpus) { $runs += @{ Name = 'fetch'; Args = @($fetch, '--cache', $CorpusCache) } }
  $runs += @{ Name = 'check'; Args = @($fetch, '--cache', $CorpusCache, '--check') }
  foreach ($r in $runs) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $res = Invoke-SetupCommand $MsilPython $r.Args
    $summary = @($res.Lines | Where-Object { $_ -match '^fetch\.py: \d+ of \d+ verified' } | Select-Object -Last 1)
    $detail = if ($summary.Count -gt 0) { $summary[0] -replace '^fetch\.py: ', '' } else { 'no summary' }
    $status = if ($res.Rc -eq 0 -and $summary.Count -gt 0) { 'PASS' } else { 'FAIL' }
    Add-GateRow 'corpus' $r.Name $status "$detail ($CorpusCache); fetch.py exited $($res.Rc)" $sw.Elapsed.TotalSeconds
  }
}

# Step 7: det-manifest.py build in two work directories, then compare (see the header).
function Invoke-DeterminismStep {
  Write-Host ''
  Write-Host "==== $(Get-Date -Format s) determinism: msil/utils/ci/det-manifest.py build (twice) and compare ===="
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $det = Join-Path $ci 'det-manifest.py'
  $tools = @('--clang', (Join-Path $BuildDir 'bin\clang.exe'), '--ar', (Join-Path $BuildDir 'bin\llvm-ar.exe'))
  $codes = @()
  $summary = @()
  $lock = Enter-MsilBuildLock -BuildDir $BuildDir -Shared -What 'run-m1-gate determinism'
  try {
    foreach ($w in 'a', 'b') {
      $res = Invoke-SetupCommand $MsilPython (@($det, 'build') + $tools +
                                              @('--work', (Join-Path $detRoot $w), '--out', (Join-Path $detRoot "$w.txt")))
      $codes += $res.Rc
    }
    if (@($codes | Where-Object { $_ -ne 0 }).Count -eq 0) {
      $res = Invoke-SetupCommand $MsilPython @($det, 'compare', (Join-Path $detRoot 'a.txt'), (Join-Path $detRoot 'b.txt'))
      $codes += $res.Rc
      $summary = @($res.Lines | Where-Object { $_ -match '^det-manifest: \d+ common entries' } | Select-Object -Last 1)
    }
  } finally { $lock.Dispose() }
  $detail = if ($summary.Count -gt 0) { ($summary[0] -replace '^det-manifest: ', '') -replace ', \d+ only in .*$', '' }
            else { 'no comparison' }
  $ok = $codes.Count -eq 3 -and @($codes | Where-Object { $_ -ne 0 }).Count -eq 0 -and $summary.Count -gt 0
  Add-GateRow 'determinism' '-' $(if ($ok) { 'PASS' } else { 'FAIL' }) `
    "$detail (det-manifest.py exited $($codes -join ', ')); $detRoot" $sw.Elapsed.TotalSeconds
}

# Step 9: the Linux job's scripts inside WSL (see the header). Returns the failures.
function Invoke-CiShSmoke {
  $problems = [System.Collections.Generic.List[string]]::new()
  $wsl = Get-Command wsl.exe -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty Source
  if (-not $wsl) { $problems.Add('wsl.exe not found'); return $problems }
  # Their output goes to the log; stderr is not mixed into what is compared below.
  # The rows of these runs must not reach the gate's results file or a GitHub step summary.
  $clean = @('env', '-u', 'MSIL_CI_RESULTS', '-u', 'GITHUB_STEP_SUMMARY')

  # (a) Every script parses.
  $scripts = @(Get-ChildItem -LiteralPath $ci -Filter '*.sh' -File | Sort-Object Name)
  if ($scripts.Count -eq 0) { $problems.Add("no .sh file in $ci") }
  foreach ($s in $scripts) {
    & $wsl -d $wslDistro --cd $MsilRepo --exec bash -n "msil/utils/ci/$($s.Name)" | Out-Host
    if ($LASTEXITCODE -ne 0) { $problems.Add("bash -n $($s.Name) exited $LASTEXITCODE") }
  }

  # (b) 'lit-legs.sh --list' prints the legs of its header, in order, '--full:' legs tagged.
  $header = @()
  $inLegs = $false
  foreach ($l in Get-Content -LiteralPath (Join-Path $ci 'lit-legs.sh')) {
    if (-not $inLegs) { $inLegs = $l -match '^# Legs, in order'; continue }
    # '#   <leg>  <description>', continued on lines indented further; the block ends at the
    # first other line after a leg.
    $m = [regex]::Match($l, '^#   ([A-Za-z0-9][A-Za-z0-9-]*)\s{2,}(\S.*)$')
    if ($m.Success) {
      $tag = if ($m.Groups[2].Value -like '--full:*') { ' (--full)' } else { '' }
      $header += $m.Groups[1].Value + $tag
    } elseif ($header.Count -gt 0 -and $l -notmatch '^#\s{4,}\S') { break }
  }
  $listed = @(& $wsl -d $wslDistro --cd $MsilRepo --exec @clean bash msil/utils/ci/lit-legs.sh --list |
              ForEach-Object { "$_".TrimEnd() } | Where-Object { $_ })
  $rc = $LASTEXITCODE
  Write-Host "lit-legs.sh --list: $($listed -join ', ') (exit $rc)"
  if ($rc -ne 0) { $problems.Add("lit-legs.sh --list exited $rc") }
  elseif ($header.Count -eq 0) { $problems.Add('no legs found in the lit-legs.sh header') }
  elseif (($listed -join '|') -cne ($header -join '|')) {
    $problems.Add("lit-legs.sh --list printed '$($listed -join ', ')', " +
                  "its header documents '$($header -join ', ')'")
  }

  # (c) The failure path: an empty build directory must fail both scripts with FAIL rows.
  Write-Host 'ci-sh: lit-legs.sh --leg net8 and unit-tests.sh with an empty build directory (must fail):'
  $tmp = "$(& $wsl -d $wslDistro --exec mktemp -d 2>$null)".Trim()
  if ($LASTEXITCODE -ne 0 -or $tmp -notmatch '^/tmp/') {
    $problems.Add("mktemp -d in WSL failed ('$tmp')"); return $problems
  }
  try {
    foreach ($c in @(@{ Script = 'lit-legs.sh'; Args = @('--leg', 'net8'); Row = 'lit-legs/net8' },
                     @{ Script = 'unit-tests.sh'; Args = @(); Row = 'unit-tests/MSILTests' })) {
      $out = @(& $wsl -d $wslDistro --cd $MsilRepo --exec @clean bash "msil/utils/ci/$($c.Script)" `
                 @($c.Args) $tmp | ForEach-Object { "$_" })
      $rc = $LASTEXITCODE
      $out | Select-Object -Last 8 | Out-Host
      if ($rc -eq 0) { $problems.Add("$($c.Script) exited 0 with an empty build directory") }
      elseif (-not ($out | Where-Object { $_.StartsWith("[msil-ci] FAIL $($c.Row) ") })) {
        $problems.Add("$($c.Script) exited $rc with an empty build directory, but without a " +
                      "FAIL row for $($c.Row)")
      }
    }
  } finally {
    & $wsl -d $wslDistro --exec rm -rf -- $tmp | Out-Host
  }
  return $problems
}

$stop = $false
if (-not $SkipConfigure) {
  if ((Invoke-CiScript 'configure.ps1' @()) -ne 0) {
    $stop = $true; Add-SkippedRow 'build' 'not run: configure failed'
  }
}
if (-not $stop -and -not $SkipBuild) {
  if ((Invoke-CiScript 'build.ps1' @()) -ne 0) {
    $stop = $true; Add-SkippedRow 'unit-tests' 'not run: build failed'
  }
}
if (-not $stop) {
  if (-not $SkipUnitTests) {
    $rc = Invoke-CiScript 'unit-tests.ps1' @()
    Test-ChildRows 'unit-tests' 'unit-tests.ps1' $rc @()
  }
  if (-not $NoWsl) { Invoke-HostSetup }
  Invoke-CorpusStep
  $legArgs = @()
  if (-not $NoWsl) { $legArgs += '-Wsl' }
  if ($Full) { $legArgs += '-Full' }
  if ($Leg.Count -gt 0) { $legArgs += @('-Leg', ($Leg -join ',')) }
  $rc = Invoke-CiScript 'lit-legs.ps1' $legArgs
  $legs = Get-ExpectedLegs
  if ($null -eq $legs) {
    Add-SkippedRow 'lit-legs' 'cannot check for missing legs: lit-legs.ps1 -List failed'
    Test-ChildRows 'lit-legs' 'lit-legs.ps1' $rc @()
  } else {
    Test-ChildRows 'lit-legs' 'lit-legs.ps1' $rc $legs.Expected $legs.Known
  }
  try { Invoke-DeterminismStep } catch { Add-GateRow 'determinism' '-' 'FAIL' "$_" }
  if (-not $SkipUpstreamHooks) {
    $rc = Invoke-CiScript 'upstream-hooks.ps1' @()
    Test-ChildRows 'upstream-hooks' 'upstream-hooks.ps1' $rc @('manifest', 'pins', 'lit', 'gtest', 'compile')
  }
}
if (-not $NoWsl) {
  Write-Host ''
  Write-Host "==== $(Get-Date -Format s) ci-sh: msil/utils/ci/*.sh inside WSL ($wslDistro) ===="
  $sw = [Diagnostics.Stopwatch]::StartNew()
  try { $problems = @(Invoke-CiShSmoke) } catch { $problems = @("$_") }
  $detail = if ($problems.Count -eq 0) { 'bash -n, lit-legs.sh --list vs its header, failure path' }
            else { $problems -join '; ' }
  $status = if ($problems.Count -eq 0) { 'PASS' } else { 'FAIL' }
  Add-GateRow 'ci-sh' '-' $status $detail $sw.Elapsed.TotalSeconds
}

$rows = @(Read-GateRows)
Remove-Item -LiteralPath $resultsFile -ErrorAction SilentlyContinue
$failed = @($rows | Where-Object { $_.Result -ne 'PASS' })
$elapsed = (Get-Date) - $started
Write-Host ''
Write-Host "==== phase gate: $($rows.Count) row(s), $($failed.Count) not PASS, $([int]$elapsed.TotalMinutes) min ===="
$rows | Format-Table -AutoSize -Wrap Step, Leg, Result, Time, Detail | Out-String -Width 220 | Write-Host
if ($rows.Count -eq 0) { Write-Host 'run-m1-gate: nothing ran'; exit 1 }
exit $(if ($failed.Count -eq 0) { 0 } else { 1 })

# upstream-hooks.ps1 - the upstream regression tests of the upstream files that the MSIL fork
# changes, and the checks that keep their list complete: step 5 of msil/utils/run-m1-gate.ps1,
# a step of the Windows CI job (.github/workflows/msil-ci.yml) and of the upstream-merge gate
# (plan M11, items 2, 6 and 7).
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#   pwsh -NoProfile -File msil/utils/ci/upstream-hooks.ps1                       # every leg
#   pwsh -NoProfile -File msil/utils/ci/upstream-hooks.ps1 -NoCompile -NoManifest -MinPass 250
#   pwsh -NoProfile -File msil/utils/ci/upstream-hooks.ps1 -Leg manifest,pins
#   pwsh -NoProfile -File msil/utils/ci/upstream-hooks.ps1 -SelfTest
#
# Windows only: the Linux CI job builds the same tools, and its nightly --full 'upstream' leg
# (lit-legs.sh) runs all of check-llvm and check-clang. The manifest of hooked files and their
# tests is msil/utils/upstream-hooks.txt (format in its header); check-upstream-hooks.py reads it.
#
# Legs, in order (one row each, step 'upstream-hooks'; times on the development host):
#   manifest  check-upstream-hooks.py --check: the manifest has a block for every upstream file
#             the fork changes and no stale entry, against the upstream base ($MSIL_UPSTREAM_BASE;
#             else MERGE_HEAD while a merge is in progress; else the second parent of the last
#             '[MSIL] Merge upstream llvm-project main at ' merge), no fork-only line names a
#             file of the base, every 'compile' line names a compile-leg file or a header one
#             of them #includes directly, every utc name is on the utc leg's list (equal in
#             lit-legs.ps1 and lit-legs.sh), every gtest line names a binary of the gtest leg
#             below (whose list must equal GTEST_BINARIES), and every exhaustive
#             object-format switch of the monorepo has an MSIL case in that same switch
#             (switch-default and switch-other exempt per switch) (about 11-20 s)
#   pins      check-action-pins.py: the msil-*.yml workflows pin each action as upstream's
#             workflows do, every uses: line of theirs is a pin it can read or a local or
#             docker:// one, and msil-ci.yml's windows job runs this script's CI command after
#             its Build step, guarded by that step's id (about 1 s)
#   lit       one msil-lit.ps1 call (shared build-dir lock) over the manifest's lit inputs
#             (check-upstream-hooks.py --list-lit): the first input is the path argument, the
#             others go to <build>/lit-results/upstream-hooks.rsp, one per line, passed as
#             '@<file>' inside -LitArgs, so the list never meets the Windows command-line limit.
#             lit writes upstream-hooks.json (-o) and .xml there, and the log is
#             upstream-hooks.log; check-upstream-hooks.py --lit-verdict judges the JSON report:
#             every FAIL allow-listed, no stale allow entry, no UNRESOLVED, TIMEOUT or XPASS,
#             every input run, a PASS for every input under a fork prefix (lld/test/MSIL/
#             today), and at least min-pass PASS results (the manifest's, or -MinPass)
#             (about 1 min)
#   gtest     msil-gtest.ps1 -NoSkip on TargetParserTests (-MinTests 700) without the host-gated
#             HostTest cases, on MCTests (-MinTests 40) without
#             TargetRegistry.MSILLookupWithoutTarget (it skips whenever MSIL is built) and the
#             Disassembler test of a target that 'llvm-config --targets-built' does not list (it
#             calls GTEST_SKIP, and -NoSkip fails a skip), and from M13 on all of
#             LLDAsLibAllTests (-MinTests 1: AsLib.AllDrivers, every lld flavor msil-ld included,
#             in-process) (10-25 s)
#   compile   compile coverage of the two hooked upstream files that the main build never
#             compiles, llvm/lib/Target/ARM/AsmParser/ARMAsmParser.cpp and
#             lldb/source/Plugins/Process/gdb-remote/GDBRemoteCommunicationClient.cpp (the list
#             is COMPILE_FILES of check-upstream-hooks.py, read with --list-compile), in a
#             private build tree (-HooksBuildDir, else $env:MSIL_HOOKS_BUILD_DIR, else
#             C:\llvm-msil-hooks-build; projects clang and lldb, target ARM): cmake when the tree
#             has no build.ninja, then ninja the ARMAsmParser.cpp object (which also generates the
#             ARM .inc files), then msil-syntax.ps1 /Zs of both files with -Werror=switch on every
#             run. The leg holds the tree's exclusive build lock and runs cmake and ninja itself
#             (msil-configure.ps1 and msil-build.ps1 take the same lock and would wait for their
#             own parent). It never builds the lldb object (its order-only dependencies are about
#             1,000 unrelated TUs, 19 min), never deletes or cleans the tree (a broken cache takes
#             'cmake --fresh' by hand or a new -HooksBuildDir) and refuses the main build
#             directory. About 47 s in steady state, about 13 min for a new tree (about 140 MB),
#             5-13 min after an upstream merge; the development host's tree is about 460 MB (the
#             recon also built the lldb object there). Residual gap: the lldb file is only
#             syntax-checked, and no ARM or lldb test runs.
# Every selected leg runs even after a failure; the exit status is 1 if any row failed.
#
# Switches: -Leg runs only the named legs (comma-separated works). -NoManifest and -NoCompile
# print a SKIP line for their leg and record no row (the CI job: its shallow checkout has no
# merge commit, and the hooks tree is a development-host cost). -MinPass overrides the
# manifest's min-pass (the CI build lacks WebAssembly, so the list's WebAssembly tests are
# UNSUPPORTED there). -SelfTest runs only the self-tests (rows of step 'upstream-hooks-self-test'):
# check-upstream-hooks.py --self-test (planted manifest, fork-only, fork-prefix, per-switch
# coverage and exemption, utc-list, gtest-list, compile-line, allow-list, MERGE_HEAD and
# lit-verdict defects, the lld suite's test names among them),
# check-action-pins.py --self-test (planted .github trees: stale unquoted, quoted and
# comment-less pins, an unparsable uses: line, and a misspelled or missing step id or a missing
# upstream-hooks step in msil-ci.yml fail, the clean tree passes), the MCTests filter for the
# target lists 'X86' (without Disassembler.WebAssemblyTest) and 'X86 WebAssembly' (both
# Disassembler tests), and a mutation check of the compile leg: copies of both files without
# their MSIL case line, in a directory under [IO.Path]::GetTempPath() that is removed
# afterwards, must fail /Zs -Werror=switch with 'not handled in switch', and the originals must
# pass. It needs the hooks tree (run the compile leg first) and writes nothing inside the
# repository.
[CmdletBinding()]
param(
  [string]$BuildDir = '',         # default: $env:MSIL_BUILD_DIR, else <repo>\build
  [string]$HooksBuildDir = '',    # default: $env:MSIL_HOOKS_BUILD_DIR, else C:\llvm-msil-hooks-build
  [string[]]$Leg = @(),           # run only these legs: manifest, pins, lit, gtest, compile
  [switch]$NoManifest,
  [switch]$NoCompile,
  [int]$MinPass = -1,             # -1: the manifest's min-pass
  [switch]$SelfTest
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\build\msil-env.ps1')
. (Join-Path $PSScriptRoot '..\build\msil-lock.ps1')
. (Join-Path $PSScriptRoot 'ci-common.ps1')
if (-not $BuildDir) { $BuildDir = $MsilBuild }
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
if (-not $HooksBuildDir) {
  $HooksBuildDir = if ($env:MSIL_HOOKS_BUILD_DIR) { $env:MSIL_HOOKS_BUILD_DIR } else { 'C:\llvm-msil-hooks-build' }
}
$HooksBuildDir = [System.IO.Path]::GetFullPath($HooksBuildDir).TrimEnd('\')

$checker = Join-Path $PSScriptRoot 'check-upstream-hooks.py'
$pinsChecker = Join-Path $PSScriptRoot 'check-action-pins.py'
$msilLit = Join-Path $MsilRepo 'msil/utils/build/msil-lit.ps1'
$msilGtest = Join-Path $MsilRepo 'msil/utils/build/msil-gtest.ps1'
$msilSyntax = Join-Path $MsilRepo 'msil/utils/build/msil-syntax.ps1'
$step = 'upstream-hooks'
$legNames = @('manifest', 'pins', 'lit', 'gtest', 'compile')

# The object that brings the ARM .inc files of ARMAsmParser.cpp up to date.
$armObject = 'lib\Target\ARM\AsmParser\CMakeFiles\LLVMARMAsmParser.dir\ARMAsmParser.cpp.obj'

# The compile leg's translation units: COMPILE_FILES of check-upstream-hooks.py, the one list
# that its --check also validates the manifest's 'compile' lines against.
function Get-CompileFiles {
  $files = @(& $MsilPython $checker --list-compile | ForEach-Object { "$_".Trim() } | Where-Object { $_ })
  if ($LASTEXITCODE -ne 0 -or $files.Count -eq 0) {
    throw "check-upstream-hooks.py --list-compile exited $LASTEXITCODE with $($files.Count) file(s)"
  }
  return $files
}
# HostTest cases that GTEST_SKIP off macOS and AIX. The leading '*' keeps 'pwsh -File' from
# taking a value that starts with '-' and contains ':' for a parameter name.
$targetParserFilter = '*-HostTest.getMacOSHostVersion:HostTest.AIXHostVersionDetect:' +
                      'HostTest.AIXTargetVersionDetect:HostTest.AIXHostCPUDetect'

# The MCTests filter for the targets of a build ('llvm-config --targets-built'):
# TargetRegistry.MSILLookupWithoutTarget skips whenever MSIL is built, and each Disassembler test
# skips without its target (llvm/unittests/MC/Disassembler.cpp). Every build of msil/utils/ci
# has X86 (msil/cmake/caches/MSIL.cmake, msil-configure.ps1); without it the X86-based Dwarf* and
# MCInstPrinter tests skip as well, and the leg fails, as it should for such a build.
function Get-McTestsFilter([string]$TargetsBuilt) {
  $built = @("$TargetsBuilt" -split '\s+' | Where-Object { $_ })
  $filter = '*-TargetRegistry.MSILLookupWithoutTarget'
  if ($built -notcontains 'WebAssembly') { $filter += ':Disassembler.WebAssemblyTest' }
  if ($built -notcontains 'X86') { $filter += ':Disassembler.X86Test' }
  return $filter
}

# Runs a native command, streams its output to the host and returns @{ Rc; Lines }; -Log also
# writes the lines to that file.
function Invoke-Tool {
  param([Parameter(Mandatory)][string]$Exe, [string[]]$Arguments = @(), [string]$Log = '')
  $lines = [System.Collections.Generic.List[string]]::new()
  & $Exe @Arguments 2>&1 | ForEach-Object { $s = "$_"; $lines.Add($s); Write-Host $s }
  $rc = $LASTEXITCODE
  if ($Log) { Set-Content -LiteralPath $Log -Value $lines -Encoding utf8 }
  return @{ Rc = $rc; Lines = $lines.ToArray() }
}

function Get-LastLine([string[]]$Lines, [string]$Pattern) {
  $hit = @($Lines | Where-Object { $_ -match $Pattern } | Select-Object -Last 1)
  if ($hit.Count -gt 0) { return $hit[0] } else { return '' }
}

function Invoke-ManifestLeg {
  $r = Invoke-Tool -Exe $MsilPython -Arguments @($checker, '--repo', $MsilRepo, '--check') `
         -Log (Join-Path $results 'upstream-hooks-manifest.log')
  $summary = (Get-LastLine $r.Lines '^check-upstream-hooks --check: ') -replace '^check-upstream-hooks --check: ', ''
  $detail = if ($summary) { $summary } else { 'no summary' }
  $first = @($r.Lines | Where-Object { $_ -like 'FAIL *' } | Select-Object -First 1)
  if ($r.Rc -ne 0 -and $first.Count -gt 0) { $detail += "; first: $($first[0] -replace '^FAIL ', '')" }
  return @{ Ok = ($r.Rc -eq 0); Detail = "$detail (exit $($r.Rc))" }
}

function Invoke-PinsLeg {
  $r = Invoke-Tool -Exe $MsilPython -Arguments @($pinsChecker, $MsilRepo) `
         -Log (Join-Path $results 'upstream-hooks-pins.log')
  $summary = (Get-LastLine $r.Lines '^check-action-pins: ') -replace '^check-action-pins: ', ''
  return @{ Ok = ($r.Rc -eq 0); Detail = "$(if ($summary) { $summary } else { 'no summary' }) (exit $($r.Rc))" }
}

function Invoke-LitLeg {
  # msil-lit.ps1 splits -LitArgs at blanks, and the response file and reports live in the
  # build directory.
  if ($BuildDir -match '\s') {
    return @{ Ok = $false; Detail = "the build directory '$BuildDir' contains a blank, which msil-lit.ps1 would split inside -LitArgs" }
  }
  $inputs = @(& $MsilPython $checker --repo $MsilRepo --list-lit | ForEach-Object { "$_".Trim() } |
              Where-Object { $_ })
  $rc = $LASTEXITCODE
  if ($rc -ne 0 -or $inputs.Count -eq 0) {
    return @{ Ok = $false; Detail = "check-upstream-hooks.py --list-lit exited $rc with $($inputs.Count) input(s)" }
  }
  $paths = @($inputs | ForEach-Object { (Join-Path $MsilRepo $_).Replace('\', '/') })
  $rsp = Join-Path $results 'upstream-hooks.rsp'
  $json = Join-Path $results 'upstream-hooks.json'
  $xml = Join-Path $results 'upstream-hooks.xml'
  $log = Join-Path $results 'upstream-hooks.log'
  # A report of an earlier run must not be judged if lit dies before writing one.
  foreach ($f in @($json, $xml)) { if (Test-Path -LiteralPath $f) { Remove-Item -LiteralPath $f -Force } }
  Set-Content -LiteralPath $rsp -Value @($paths | Select-Object -Skip 1) -Encoding utf8
  # '-LitArgs:<value>', not '-LitArgs <value>': 'pwsh -File' takes an argument that starts with
  # '-' and contains ':' (a drive path) for '-<name>:<value>' (as in lit-legs.ps1). The
  # response file comes last: msil-lit.ps1 appends the path argument, and lit's argparse takes
  # its test paths only as one run of positional arguments, so no option may come between the
  # paths of the response file and that one.
  $litArgs = "-LitArgs:-sv -o $($json.Replace('\', '/')) --xunit-xml-output=$($xml.Replace('\', '/')) " +
             "@$($rsp.Replace('\', '/'))"
  Write-Host "[msil-ci] upstream-hooks lit: msil-lit.ps1 `"$litArgs`" $($paths[0]) (and $($paths.Count - 1) inputs in $rsp)"
  $r = Invoke-Tool -Exe 'pwsh' -Log $log `
         -Arguments @('-NoProfile', '-File', $msilLit, '-BuildDir', $BuildDir, $litArgs, $paths[0])
  if (-not (Test-Path -LiteralPath $json)) {
    return @{ Ok = $false; Detail = "lit wrote no JSON report (lit exit $($r.Rc)); see $log" }
  }
  $verdictArgs = @($checker, '--repo', $MsilRepo, '--lit-verdict', $json)
  if ($MinPass -ge 0) { $verdictArgs += @('--min-pass', "$MinPass") }
  $v = Invoke-Tool -Exe $MsilPython -Arguments $verdictArgs
  Add-Content -LiteralPath $log -Value $v.Lines -Encoding utf8
  $summary = (Get-LastLine $v.Lines '^check-upstream-hooks --lit-verdict: ') -replace '^check-upstream-hooks --lit-verdict: ', ''
  $detail = "$(if ($summary) { $summary } else { "--lit-verdict exit $($v.Rc), no summary" }); " +
            "$($inputs.Count) input(s), lit exit $($r.Rc)"
  $first = @($v.Lines | Where-Object { $_ -like 'FAIL *' } | Select-Object -First 1)
  if ($v.Rc -ne 0 -and $first.Count -gt 0) { $detail += "; first: $($first[0] -replace '^FAIL ', '')" }
  return @{ Ok = ($v.Rc -eq 0); Detail = $detail }
}

function Invoke-GtestLeg {
  $llvmConfig = Join-Path $BuildDir 'bin\llvm-config.exe'
  if (-not (Test-Path -LiteralPath $llvmConfig)) {
    return @{ Ok = $false; Detail = "$llvmConfig not found: build llvm-config first" }
  }
  $targets = "$(& $llvmConfig --targets-built)".Trim()
  if ($LASTEXITCODE -ne 0 -or -not $targets) {
    return @{ Ok = $false; Detail = "llvm-config --targets-built exited $LASTEXITCODE ('$targets')" }
  }
  $ok = $true
  $parts = @()
  # The binaries are GTEST_BINARIES of check-upstream-hooks.py (the manifest's gtest lines may
  # name only these): its --check fails unless the Exe entries below are exactly that list.
  foreach ($t in @(@{ Name = 'TargetParserTests'; Exe = 'unittests/TargetParser/TargetParserTests.exe'
                      Filter = $targetParserFilter; Min = 700 },
                   @{ Name = 'MCTests'; Exe = 'unittests/MC/MCTests.exe'
                      Filter = (Get-McTestsFilter $targets); Min = 40 },
                   # AsLib.AllDrivers runs every lld flavor in-process, msil-ld included (plan
                   # 5.6.3, M13): the test of lld's AllDrivers.cpp hook.
                   @{ Name = 'LLDAsLibAllTests'; Exe = 'tools/lld/unittests/AsLibAll/LLDAsLibAllTests.exe'
                      Filter = '*'; Min = 1 })) {
    $r = Invoke-Tool -Exe 'pwsh' -Log (Join-Path $results "upstream-hooks-gtest-$($t.Name).log") `
           -Arguments @('-NoProfile', '-File', $msilGtest, '-Exe', (Join-Path $BuildDir $t.Exe),
                        '-Filter', $t.Filter, '-NoSkip', '-MinTests', "$($t.Min)", '-BuildDir', $BuildDir)
    if ($r.Rc -ne 0) { $ok = $false }
    # '[msil-gtest] PASS: 714 test(s) passed, 0 skipped line(s) (<exe> --gtest_filter=...)'
    $verdict = Get-LastLine $r.Lines '^\[msil-gtest\] (PASS|FAIL):'
    $what = if ($verdict) { $verdict -replace '^\[msil-gtest\] ', '' -replace ' \([^()]*\)$', '' }
            else { "msil-gtest.ps1 exit $($r.Rc)" }
    $parts += "$($t.Name) $what"
  }
  return @{ Ok = $ok; Detail = "$($parts -join '; ') (targets: $targets)" }
}

function Invoke-CompileLeg {
  foreach ($main in @($BuildDir, $MsilBuild)) {
    if ($HooksBuildDir -ieq ([System.IO.Path]::GetFullPath($main).TrimEnd('\'))) {
      return @{ Ok = $false; Detail = "refusing to use the main build directory $main as the hooks build tree" }
    }
  }
  foreach ($tool in @('cmake', 'ninja')) {
    if (-not (Get-Command $tool -CommandType Application -ErrorAction SilentlyContinue)) {
      return @{ Ok = $false; Detail = "$tool not found (msil-env.ps1 provides the Visual Studio cmake and ninja)" }
    }
  }
  $hookFiles = @(Get-CompileFiles)
  $log = Join-Path $results 'upstream-hooks-compile.log'
  $configureLog = Join-Path $results 'upstream-hooks-compile-configure.log'
  $ninjaLog = Join-Path $results 'upstream-hooks-compile-ninja.log'
  $parts = @()
  $lock = Enter-MsilBuildLock -BuildDir $HooksBuildDir -What 'upstream-hooks compile'
  try {
    if (-not (Test-Path -LiteralPath (Join-Path $HooksBuildDir 'build.ninja'))) {
      # plan M11 (hooks.md section 4.1): clang and lldb (lldb needs clang), the ARM target only,
      # nothing optional; the compilers, Python and job limits of msil-configure.ps1.
      $cmakeArgs = @(
        '-G', 'Ninja', '-S', (Join-Path $MsilRepo 'llvm'), '-B', $HooksBuildDir,
        '-DCMAKE_BUILD_TYPE=Release', '-DLLVM_ENABLE_ASSERTIONS=ON',
        '-DLLVM_ENABLE_PROJECTS=clang;lldb', '-DLLVM_TARGETS_TO_BUILD=ARM',
        '-DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=', '-DLLVM_EXTERNAL_PROJECTS=',
        '-DLLVM_OPTIMIZED_TABLEGEN=OFF', '-DLLVM_PARALLEL_COMPILE_JOBS=8', '-DLLVM_PARALLEL_LINK_JOBS=2',
        '-DLLVM_ENABLE_PDB=OFF', '-DLLVM_INCLUDE_BENCHMARKS=OFF', '-DLLVM_INCLUDE_EXAMPLES=OFF',
        '-DLLVM_INCLUDE_TESTS=OFF', '-DLLVM_INCLUDE_DOCS=OFF', '-DLLVM_BUILD_TESTS=OFF',
        '-DCLANG_INCLUDE_TESTS=OFF', '-DCLANG_INCLUDE_DOCS=OFF', '-DLLDB_INCLUDE_TESTS=OFF',
        '-DLLVM_APPEND_VC_REV=OFF', '-DLLVM_ENABLE_ZLIB=OFF', '-DLLVM_ENABLE_ZSTD=OFF',
        '-DLLVM_ENABLE_LIBXML2=OFF', '-DLLVM_ENABLE_DIA_SDK=OFF', '-DLLVM_ENABLE_TELEMETRY=OFF',
        '-DCLANG_ENABLE_STATIC_ANALYZER=OFF', '-DCLANG_ENABLE_OBJC_REWRITER=OFF',
        '-DLLDB_ENABLE_PYTHON=OFF', '-DLLDB_ENABLE_LUA=OFF', '-DLLDB_ENABLE_SWIG=OFF',
        '-DLLDB_ENABLE_LIBXML2=OFF', '-DLLDB_ENABLE_LIBEDIT=OFF', '-DLLDB_ENABLE_CURSES=OFF',
        '-DLLDB_ENABLE_TREESITTER=OFF',
        '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', "-DPython3_EXECUTABLE=$MsilPython",
        "-DCMAKE_C_COMPILER=$MsilLlvmBin\clang-cl.exe", "-DCMAKE_CXX_COMPILER=$MsilLlvmBin\clang-cl.exe",
        "-DCMAKE_LINKER=$MsilLlvmBin\lld-link.exe", "-DCMAKE_AR=$MsilLlvmBin\llvm-lib.exe",
        '-DLLVM_ENABLE_LLD=ON')
      $sw = [Diagnostics.Stopwatch]::StartNew()
      $r = Invoke-Tool -Exe 'cmake' -Arguments $cmakeArgs -Log $configureLog
      if ($r.Rc -ne 0) {
        return @{ Ok = $false; Detail = "configure of $HooksBuildDir exited $($r.Rc); see $configureLog" }
      }
      $parts += "configure $([int]$sw.Elapsed.TotalSeconds) s"
    }
    # As msil-build.ps1: other agents' experiments may hold most of the RAM.
    $ninjaArgs = @('-C', $HooksBuildDir)
    $freeGB = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1MB
    if ($freeGB -lt 6) { $ninjaArgs += @('-j', '2') } elseif ($freeGB -lt 16) { $ninjaArgs += @('-j', '4') }
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $r = Invoke-Tool -Exe 'ninja' -Arguments ($ninjaArgs + $armObject) -Log $ninjaLog
    if ($r.Rc -ne 0) {
      return @{ Ok = $false; Detail = (@($parts) + "ninja $armObject exited $($r.Rc); see $ninjaLog") -join '; ' }
    }
    $parts += "ninja ARM object $([int]$sw.Elapsed.TotalSeconds) s"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $r = Invoke-Tool -Exe 'pwsh' -Log $log -Arguments (@('-NoProfile', '-File', $msilSyntax,
           '-BuildDir', $HooksBuildDir, '-ExtraFlags', '/clang:-Werror=switch') +
           @($hookFiles | ForEach-Object { Join-Path $MsilRepo $_ }))
    $parts += "/Zs -Werror=switch of $($hookFiles.Count) files $([int]$sw.Elapsed.TotalSeconds) s (exit $($r.Rc))"
    return @{ Ok = ($r.Rc -eq 0); Detail = "$($parts -join '; ') in $HooksBuildDir" }
  } finally { $lock.Dispose() }
}

# ---- -SelfTest ----------------------------------------------------------------------------

function Test-McTestsFilter {
  $problems = @()
  $base = '*-TargetRegistry.MSILLookupWithoutTarget'
  foreach ($c in @(@{ Targets = 'X86'; Want = "${base}:Disassembler.WebAssemblyTest" },
                   @{ Targets = 'X86 WebAssembly'; Want = $base },
                   @{ Targets = 'X86 WebAssembly MSIL'; Want = $base },
                   @{ Targets = 'WebAssembly MSIL'; Want = "${base}:Disassembler.X86Test" })) {
    $got = Get-McTestsFilter $c.Targets
    $ok = $got -ceq $c.Want
    Write-Host "$(if ($ok) { 'ok  ' } else { 'FAIL' })  targets '$($c.Targets)': $got"
    if (-not $ok) { $problems += "targets '$($c.Targets)' gave '$got', expected '$($c.Want)'" }
  }
  $llvmConfig = Join-Path $BuildDir 'bin\llvm-config.exe'
  if (Test-Path -LiteralPath $llvmConfig) {
    $targets = "$(& $llvmConfig --targets-built)".Trim()
    Write-Host "this build ($targets): $(Get-McTestsFilter $targets)"
  }
  return $problems
}

function Test-CompileMutation {
  if (-not (Test-Path -LiteralPath (Join-Path $HooksBuildDir 'build.ninja'))) {
    return @("no $HooksBuildDir\build.ninja: run the compile leg first (upstream-hooks.ps1 -Leg compile)")
  }
  $problems = @()
  $hookFiles = @(Get-CompileFiles)
  $tmp = Join-Path ([System.IO.Path]::GetTempPath()) "msil-upstream-hooks-self-test-$PID"
  New-Item -ItemType Directory -Force -Path $tmp | Out-Null
  # Shared: no compile leg may regenerate the tree's .inc files meanwhile.
  $lock = Enter-MsilBuildLock -BuildDir $HooksBuildDir -Shared -What 'upstream-hooks self-test'
  try {
    foreach ($f in $hookFiles) {
      $orig = Join-Path $MsilRepo $f
      $text = [System.IO.File]::ReadAllText($orig)
      $case = [regex]::Matches($text, '(?m)^[ \t]*case[ \t]+(?:llvm::)?(?:Triple::MSIL|MCContext::IsMSIL)[ \t]*:[ \t]*\r?\n')
      if ($case.Count -ne 1) { $problems += "${f}: $($case.Count) MSIL case lines, expected 1"; continue }
      $copy = Join-Path $tmp ([System.IO.Path]::GetFileName($f))
      [System.IO.File]::WriteAllText($copy, $text.Remove($case[0].Index, $case[0].Length))
      Write-Host "self-test: $f without '$($case[0].Value.Trim())' (must fail with 'not handled in switch')"
      # The copy borrows the original's flags, and its directory comes first for the file's own
      # quoted includes.
      $r = Invoke-Tool -Exe 'pwsh' -Arguments @('-NoProfile', '-File', $msilSyntax,
             '-BuildDir', $HooksBuildDir, '-FlagsFrom', $orig, '-ExtraInclude', (Split-Path -Parent $orig),
             '-ExtraFlags', '/clang:-Werror=switch', $copy)
      if ($r.Rc -eq 0) { $problems += "$f without its MSIL case passed /Zs -Werror=switch" }
      elseif (-not ($r.Lines | Where-Object { $_ -match 'not handled in switch' })) {
        $problems += "$f without its MSIL case failed (exit $($r.Rc)), but not with 'not handled in switch'"
      }
      Write-Host "self-test: $f as it is (must pass)"
      $r = Invoke-Tool -Exe 'pwsh' -Arguments @('-NoProfile', '-File', $msilSyntax,
             '-BuildDir', $HooksBuildDir, '-ExtraFlags', '/clang:-Werror=switch', $orig)
      if ($r.Rc -ne 0) { $problems += "$f failed /Zs -Werror=switch (exit $($r.Rc))" }
    }
  } finally {
    $lock.Dispose()
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
  }
  if (Test-Path -LiteralPath $tmp) { $problems += "could not remove $tmp" }
  return $problems
}

function Invoke-SelfTest {
  $selfStep = 'upstream-hooks-self-test'
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $r = Invoke-Tool -Exe $MsilPython -Arguments @($checker, '--self-test')
  $summary = Get-LastLine $r.Lines '^check-upstream-hooks self-test: '
  Add-MsilCiResult -Step $selfStep -Name 'checker' -Status $(if ($r.Rc -eq 0) { 'PASS' } else { 'FAIL' }) `
    -Seconds $sw.Elapsed.TotalSeconds -Detail "$(if ($summary) { $summary } else { 'no summary' }) (exit $($r.Rc))"

  $sw.Restart()
  $r = Invoke-Tool -Exe $MsilPython -Arguments @($pinsChecker, '--self-test')
  $summary = Get-LastLine $r.Lines '^check-action-pins self-test: '
  Add-MsilCiResult -Step $selfStep -Name 'pins-checker' -Status $(if ($r.Rc -eq 0) { 'PASS' } else { 'FAIL' }) `
    -Seconds $sw.Elapsed.TotalSeconds -Detail "$(if ($summary) { $summary } else { 'no summary' }) (exit $($r.Rc))"

  foreach ($t in @(@{ Name = 'gtest-filter'; Test = { Test-McTestsFilter }
                      Ok = 'MCTests filter for X86, X86 WebAssembly, X86 WebAssembly MSIL, WebAssembly MSIL' },
                   @{ Name = 'compile-mutation'; Test = { Test-CompileMutation }
                      Ok = "both files fail /Zs -Werror=switch without their MSIL case and pass with it ($HooksBuildDir)" })) {
    $sw.Restart()
    try { $problems = @(& $t.Test) } catch { $problems = @("$_") }
    Add-MsilCiResult -Step $selfStep -Name $t.Name -Status $(if ($problems.Count -eq 0) { 'PASS' } else { 'FAIL' }) `
      -Seconds $sw.Elapsed.TotalSeconds -Detail $(if ($problems.Count -eq 0) { $t.Ok } else { $problems -join '; ' })
  }
}

# ---- main ---------------------------------------------------------------------------------

if ($SelfTest) {
  if ($Leg.Count -gt 0 -or $NoManifest -or $NoCompile -or $MinPass -ge 0) {
    throw '-SelfTest takes only -BuildDir and -HooksBuildDir'
  }
  Invoke-SelfTest
  Write-MsilCiSummary -Title 'MSIL CI: upstream-hooks self-test'
  exit $(if (Test-MsilCiFailed) { 1 } else { 0 })
}

$selected = @($Leg | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
foreach ($s in $selected) {
  if ($legNames -notcontains $s) { throw "unknown leg '$s' (legs: $($legNames -join ', '))" }
}
$results = Join-Path $BuildDir 'lit-results'
New-Item -ItemType Directory -Force -Path $results | Out-Null

foreach ($name in $legNames) {
  if ($selected.Count -gt 0 -and $selected -notcontains $name) { continue }
  if ($name -eq 'manifest' -and $NoManifest) {
    Write-Host "[msil-ci] SKIP $step/manifest (-NoManifest)" -ForegroundColor Yellow; continue
  }
  if ($name -eq 'compile' -and $NoCompile) {
    Write-Host "[msil-ci] SKIP $step/compile (-NoCompile)" -ForegroundColor Yellow; continue
  }
  Write-Host ''
  Write-Host "==== $(Get-Date -Format s) upstream-hooks leg $name ===="
  $sw = [Diagnostics.Stopwatch]::StartNew()
  try {
    $r = switch ($name) {
      'manifest' { Invoke-ManifestLeg }
      'pins' { Invoke-PinsLeg }
      'lit' { Invoke-LitLeg }
      'gtest' { Invoke-GtestLeg }
      'compile' { Invoke-CompileLeg }
    }
  } catch {
    $r = @{ Ok = $false; Detail = "$_" }
  }
  Add-MsilCiResult -Step $step -Name $name -Status $(if ($r.Ok) { 'PASS' } else { 'FAIL' }) `
    -Seconds $sw.Elapsed.TotalSeconds -Detail $r.Detail
}

Write-MsilCiSummary -Title 'MSIL CI: upstream hooks'
exit $(if (Test-MsilCiFailed) { 1 } else { 0 })

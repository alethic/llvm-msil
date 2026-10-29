# ctest2.ps1 <list> <results>: the c-testsuite tests that libc-free.txt leaves out,
# through the prototype pipeline (clang -S -emit-llvm, msil-expand-variadics,
# llc -filetype=obj, msil-ld) at -O0 and -O2 on both x86_64 MSIL triples.
# Windows images run here on .NET 8 against <name>.c.expected; Linux images run
# later under WSL (ctest-wsl.sh). One result line is appended per build, and a
# build whose image already exists is not repeated.
param([string]$List, [string]$Results)
. D:\llvm-msil\msil\utils\build\msil-env.ps1
$BIN = 'D:\llvm-msil\build\bin'
$GS = 'C:\llvm-msil-scratch\glibc-sysroot'
$LS = 'C:\llvm-msil-scratch\linux-sysroot'
$Suite = 'C:\llvm-msil-scratch\corpus-cache\c-testsuite-5c7275656d75\tests\single-exec'
$Tool = 'C:\llvm-msil-scratch\p3\variadics\proto\msil-expand-variadics.exe'
$RC = 'C:\llvm-msil-scratch\p3\variadics\emu\net8.runtimeconfig.json'
Set-Location C:\llvm-msil-scratch\p3\variadics\ctest
New-Item -ItemType Directory -Force bin | Out-Null
foreach ($n in Get-Content $List) {
  foreach ($trip in 'win','lnx') {
    $tt = if ($trip -eq 'win') { 'x86_64-pc-windows-msvc-msil' } else { 'x86_64-unknown-linux-gnu-msil' }
    $isr = if ($trip -eq 'lnx') { @("--sysroot=$GS") } else { @() }
    $lsr = if ($trip -eq 'lnx') { @("--sysroot=$LS") } else { @() }
    foreach ($o in '-O0','-O2') {
      $base = "bin\$n.$trip$o"
      $st = 'ok'
      if (-not (Test-Path "$base.exe")) {
        & "$BIN\clang.exe" --target=$tt $isr $o -w -S -emit-llvm "$Suite\$n.c" -o "$base.ll" 2>"$base.err"
        if ($LASTEXITCODE) { $st = 'clang' }
        if ($st -eq 'ok') { & $Tool "$base.ll" "$base.x.ll" 2>>"$base.err"; if ($LASTEXITCODE) { $st = 'expand' } }
        if ($st -eq 'ok') { & "$BIN\llc.exe" $o -filetype=obj "$base.x.ll" -o "$base.o" 2>>"$base.err"; if ($LASTEXITCODE) { $st = 'llc' } }
        if ($st -eq 'ok') { & "$BIN\clang.exe" --target=$tt $lsr $o "$base.o" -o "$base.exe" 2>>"$base.err"; if ($LASTEXITCODE) { $st = 'link' } }
      }
      if ($st -eq 'ok' -and $trip -eq 'win') {
        $out = & dotnet exec --runtimeconfig $RC "$base.exe" 2>&1 | Out-String
        $rc = $LASTEXITCODE
        $exp = (Get-Content -Raw "$Suite\$n.c.expected" -ErrorAction SilentlyContinue)
        if ($null -eq $exp) { $exp = '' }
        $st = if ($rc -ne 0) { "exit$rc" } elseif ($out.Replace("`r`n", "`n") -eq $exp.Replace("`r`n", "`n")) { 'PASS' } else { 'DIFF' }
      }
      Add-Content -Path $Results -Value "$n $trip$o $st"
    }
  }
}
Add-Content -Path $Results -Value "# done"

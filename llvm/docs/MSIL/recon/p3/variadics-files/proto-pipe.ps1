# pipe.ps1 <name> <srcs...>: native references and the prototype pipeline on both triples at -O0/-O2.
param([string]$Name, [string[]]$Srcs)
. D:\llvm-msil\msil\utils\build\msil-env.ps1
$B = 'D:\llvm-msil\build\bin'
$GS = 'C:\llvm-msil-scratch\glibc-sysroot'
$LS = 'C:\llvm-msil-scratch\linux-sysroot'
$LL = "$LS\usr\lib\x86_64-linux-gnu"
$Tool = 'C:\llvm-msil-scratch\p3\variadics\proto\msil-expand-variadics.exe'
Set-Location C:\llvm-msil-scratch\p3\variadics\proto
New-Item -ItemType Directory -Force out | Out-Null
foreach ($t in 'win','lnx') {
  $tt = if ($t -eq 'win') { 'x86_64-pc-windows-msvc-msil' } else { 'x86_64-unknown-linux-gnu-msil' }
  $isr = if ($t -eq 'lnx') { @("--sysroot=$GS") } else { @() }
  $lsr = if ($t -eq 'lnx') { @("--sysroot=$LS") } else { @() }
  foreach ($o in '-O0','-O2') {
    $objs = @()
    foreach ($s in $Srcs) {
      $base = "out\$Name.$([IO.Path]::GetFileNameWithoutExtension($s)).$t$o"
      & "$B\clang.exe" --target=$tt $isr $o -S -emit-llvm -Wno-deprecated-declarations $s -o "$base.ll"
      if ($LASTEXITCODE) { "FAIL clang $base"; continue }
      & $Tool "$base.ll" "$base.x.ll"
      if ($LASTEXITCODE) { "FAIL expand $base"; continue }
      & "$B\llc.exe" $o -filetype=obj "$base.x.ll" -o "$base.o"
      if ($LASTEXITCODE) { "FAIL llc $base"; continue }
      $objs += "$base.o"
    }
    & "$B\clang.exe" --target=$tt $lsr $o $objs -o "out\$Name.$t$o.exe"
    if ($LASTEXITCODE) { "FAIL link $Name $t$o" } else { "ok $Name $t$o" }
  }
}
& "$B\clang.exe" --target=x86_64-pc-windows-msvc -O2 -Wno-deprecated-declarations $Srcs -o "out\$Name.native.win.exe"
$nobjs = @()
foreach ($s in $Srcs) { $o = "out\$Name.$([IO.Path]::GetFileNameWithoutExtension($s)).native.lnx.o"; & "$B\clang.exe" --target=x86_64-unknown-linux-gnu "--sysroot=$GS" -fno-pic -O2 -c $s -o $o; $nobjs += $o }
& "$B\ld.lld.exe" -dynamic-linker /lib64/ld-linux-x86-64.so.2 --as-needed D:\llvm-msil\build\tools\msil\test\linux-oracle\start.o $nobjs "$LL\libm.so.6" "$LL\libc.so.6" "$LL\libgcc_s.so.1" -o "out\$Name.native.lnx"
"native done"

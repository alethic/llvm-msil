. D:\llvm-msil\msil\utils\build\msil-env.ps1
$B = 'D:\llvm-msil\build\bin'
$GS = 'C:\llvm-msil-scratch\glibc-sysroot'
$LS = 'C:\llvm-msil-scratch\linux-sysroot'
Set-Location C:\llvm-msil-scratch\p3\variadics\x1
$files = 'call_printf','call_snprintf','call_open','def_same','valist_fn','fnptr'
foreach ($t in 'x86_64-unknown-linux-gnu-msil','x86_64-pc-windows-msvc-msil') {
  $sr = if ($t -like '*linux*') { @("--sysroot=$GS") } else { @() }
  $tag = if ($t -like '*linux*') { 'lnx' } else { 'win' }
  foreach ($f in $files) {
    foreach ($o in '-O0','-O2') {
      "=== $tag $f $o clang -c"
      & "$B\clang.exe" --target=$t $sr $o -c "$f.c" -o "$f.$tag$o.o" 2>&1 | ForEach-Object { "$_" }
      "exit $LASTEXITCODE"
      if (Test-Path "$f.$tag$o.o") {
        "=== $tag $f $o link"
        & "$B\clang.exe" --target=$t $sr "--sysroot=$LS" $o "$f.$tag$o.o" -o "$f.$tag$o.exe" 2>&1 | ForEach-Object { "$_" }
        "exit $LASTEXITCODE"
      }
    }
    "=== $tag $f -O2 LTO"
    & "$B\clang.exe" --target=$t $sr -O2 -flto -c "$f.c" -o "$f.$tag.bc" 2>&1 | ForEach-Object { "$_" }
    & "$B\clang.exe" --target=$t "--sysroot=$LS" -O2 -flto "$f.$tag.bc" -o "$f.$tag.lto.exe" 2>&1 | ForEach-Object { "$_" }
    "exit $LASTEXITCODE"
  }
  "=== $tag xtu obj"
  & "$B\clang.exe" --target=$t $sr -O2 -c def_xtu_a.c -o "a.$tag.o" 2>&1 | ForEach-Object { "$_" }
  & "$B\clang.exe" --target=$t $sr -O2 -c def_xtu_b.c -o "b.$tag.o" 2>&1 | ForEach-Object { "$_" }
  "exit $LASTEXITCODE"
}

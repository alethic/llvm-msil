. D:\llvm-msil\msil\utils\build\msil-env.ps1
Set-Location C:\llvm-msil-scratch\p3\variadics\ctest
$BIN = 'D:\llvm-msil\build\bin'
$S = 'C:\llvm-msil-scratch\corpus-cache\c-testsuite-5c7275656d75\tests\single-exec'
& "$BIN\clang.exe" --target=x86_64-pc-windows-msvc-msil -O2 -w -S -emit-llvm ucrt_stdio_defs.c -o h.ll
& C:\llvm-msil-scratch\p3\variadics\proto\msil-expand-variadics.exe h.ll h.x.ll
& "$BIN\llc.exe" -O2 -filetype=obj h.x.ll -o ucrt_stdio_defs.o; "helper llc $LASTEXITCODE"
& "$BIN\llvm-ar.exe" rcs libucrt_stdio_defs.a ucrt_stdio_defs.o
foreach ($n in '00210','00211','00213','00215','00217','00218') { foreach ($o in '-O0','-O2') {
  $base = "bin\$n.win$o"
  if (-not (Test-Path "$base.o")) {
    & "$BIN\clang.exe" --target=x86_64-pc-windows-msvc-msil $o -w -S -emit-llvm "$S\$n.c" -o "$base.ll"
    & C:\llvm-msil-scratch\p3\variadics\proto\msil-expand-variadics.exe "$base.ll" "$base.x.ll"
    & "$BIN\llc.exe" $o -filetype=obj "$base.x.ll" -o "$base.o"
  }
  & "$BIN\clang.exe" --target=x86_64-pc-windows-msvc-msil $o "$base.o" libucrt_stdio_defs.a -o "$base.h.exe"; "$n $o link $LASTEXITCODE"
} }

. D:\llvm-msil\msil\utils\build\msil-env.ps1 *> $null
$clang = 'D:\llvm-msil\build\bin\clang.exe'
foreach ($t in 'crt-xt', 'p6-dtors', 'atexit-order') {
  $srcs = @(Get-ChildItem "$t\*.c" | ForEach-Object { $_.FullName })
  foreach ($rt in 'static', 'dll') {
    & $clang --target=x86_64-pc-windows-msvc -fuse-ld=lld "-fms-runtime-lib=$rt" -O1 -I. @srcs -o "$t\$t.$rt.exe"
  }
}

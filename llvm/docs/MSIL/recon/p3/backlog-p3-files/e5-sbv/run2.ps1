. D:\llvm-msil\msil\utils\build\msil-env.ps1
Set-Location $PSScriptRoot
$B='D:\llvm-msil\build\bin'
& "$B\clang.exe" --target=x86_64-pc-windows-msvc-msil -O2 div.c -o div.exe 2>&1; & .\div.exe; "Framework exit=$LASTEXITCODE (expect 92)"
& "$B\clang.exe" --target=x86_64-unknown-linux-gnu-msil --sysroot=C:\llvm-msil-scratch\linux-sysroot -O2 div.c -o divl.exe 2>&1; "linux link exit=$LASTEXITCODE"

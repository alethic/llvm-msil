. D:\llvm-msil\msil\utils\build\msil-env.ps1
Set-Location $PSScriptRoot
$B='D:\llvm-msil\build\bin'
"== windows-msvc-msil"; & "$B\clang.exe" --target=x86_64-pc-windows-msvc-msil -O2 lldiv.c -o lldiv.exe 2>&1; "link exit=$LASTEXITCODE"
if (Test-Path lldiv.exe) { & .\lldiv.exe; "run exit (Framework)=$LASTEXITCODE" }
"== linux-gnu-msil"; & "$B\clang.exe" --target=x86_64-unknown-linux-gnu-msil --sysroot=C:\llvm-msil-scratch\linux-sysroot -O2 lldiv.c -o lldiv-linux.exe 2>&1; "link exit=$LASTEXITCODE"

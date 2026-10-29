. D:\llvm-msil\msil\utils\build\msil-env.ps1
Set-Location C:\llvm-msil-scratch\p3\callbacks\today
$c = 'D:\llvm-msil\build\bin\clang.exe'
& $c --target=x86_64-pc-windows-msvc-msil -O2 -ffreestanding qs.c -o qs-win.exe 2>&1
& $c --target=x86_64-unknown-linux-gnu-msil -O2 -ffreestanding --sysroot=C:/llvm-msil-scratch/glibc-sysroot qs.c -o qs-lnx.exe 2>&1
& $c --target=x86_64-pc-windows-msvc-msil -O2 -ffreestanding -S qs.c -o qs-win.il 2>&1
& $c --target=x86_64-pc-windows-msvc -O2 qs.c -o qs-native.exe 2>&1
Get-ChildItem qs*

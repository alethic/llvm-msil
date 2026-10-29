. D:\llvm-msil\msil\utils\build\msil-env.ps1
Set-Location C:\llvm-msil-scratch\p3\callbacks\today
$c = 'D:\llvm-msil\build\bin\clang.exe'
& $c --target=x86_64-pc-windows-msvc-msil -O2 -ffreestanding -S thr.c -o thr-win.il 2>&1
& $c --target=x86_64-unknown-linux-gnu-msil -O2 -ffreestanding -S thr.c -o thr-lnx.il 2>&1
& $c --target=x86_64-pc-windows-msvc -O2 thr.c -o thr-native.exe 2>&1

. D:\llvm-msil\msil\utils\build\msil-env.ps1
Set-Location C:\llvm-msil-scratch\p3\callbacks\native
& D:\llvm-msil\build\bin\clang-cl.exe /nologo /O2 /LD /MD cbnative.c /Fe:cbnative.dll /link /fuseld 2>&1
if (-not (Test-Path cbnative.dll)) { & D:\llvm-msil\build\bin\clang-cl.exe /nologo /O2 /LD /MD cbnative.c -fuse-ld=lld /Fe:cbnative.dll 2>&1 }
Get-ChildItem cbnative.*

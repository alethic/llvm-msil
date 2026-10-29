. D:\llvm-msil\msil\utils\build\msil-env.ps1
Set-Location $PSScriptRoot
$B='D:\llvm-msil\build\bin'
& "$B\clang.exe" --target=x86_64-pc-windows-msvc-msil -O2 sret.c -o sret.exe 2>&1; "link exit=$LASTEXITCODE"
& .\sret.exe; "Framework 4.8 exit=$LASTEXITCODE (expect 42)"
'{"runtimeOptions":{"tfm":"net8.0","framework":{"name":"Microsoft.NETCore.App","version":"8.0.0"}}}' | Set-Content -Encoding ascii sret.runtimeconfig.json
& dotnet .\sret.exe; ".NET 8 Windows exit=$LASTEXITCODE (expect 42)"
& "$B\clang.exe" --target=x86_64-unknown-linux-gnu-msil --sysroot=C:\llvm-msil-scratch\linux-sysroot -O2 sret.c -o sretl.exe 2>&1; "linux link exit=$LASTEXITCODE"
Copy-Item sret.runtimeconfig.json sretl.runtimeconfig.json

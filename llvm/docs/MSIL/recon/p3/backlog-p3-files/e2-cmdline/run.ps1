$ErrorActionPreference = 'Continue'
. D:\llvm-msil\msil\utils\build\msil-env.ps1
$B = 'D:\llvm-msil\build\bin'
Set-Location $PSScriptRoot
& "$B\clang.exe" --target=x86_64-pc-windows-msvc-msil -O2 cmdline.c -o cmdline.exe 2>&1
'{"runtimeOptions":{"tfm":"net8.0","framework":{"name":"Microsoft.NETCore.App","version":"8.0.0"}}}' | Set-Content -Encoding ascii cmdline.runtimeconfig.json
"== .NET Framework 4.8, direct"; & .\cmdline.exe one "two words" 2>&1
"== dotnet cmdline.exe (runtimeconfig next to it)"; & dotnet .\cmdline.exe one "two words" 2>&1
"== dotnet exec --runtimeconfig"; & dotnet exec --runtimeconfig .\cmdline.runtimeconfig.json .\cmdline.exe one "two words" 2>&1

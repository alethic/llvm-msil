# CMake integration test (recon driver-toolchain): configure, build and run the demo
# project with the prototype driver and the msil-ld stand-in. Writes only to scratch.
. D:\llvm-msil\msil\utils\build\msil-env.ps1 *> $null
$ErrorActionPreference = 'Continue'
$S = 'C:\llvm-msil-scratch\p2\driver-toolchain'
$bin = "$S\proto\bin"
$env:MSIL_STANDIN_PYTHON = $MsilPython
$env:MSIL_GLIBC_SYSROOT = "C:/llvm-msil-scratch/p2/driver-toolchain/sysroot-glibc"
$env:MSIL_LD_STANDIN_LOG = "$S\cmake\standin-lnx.log"
Remove-Item -Force -ErrorAction SilentlyContinue $env:MSIL_LD_STANDIN_LOG
# rebuild the launcher
& "$MsilLlvmBin\clang-cl.exe" /nologo /O2 /D_CRT_SECURE_NO_WARNINGS "$S\scripts\standin-launcher.c" /Fe:"$bin\msil-ld.exe" /Fo:"$env:TEMP\standin-launcher.obj" | Out-Null
Copy-Item "$S\scripts\msil-ld-standin.py" $bin -Force
$b = "$S\cmake\build-lnx"
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $b
$t0 = Get-Date
cmake -G Ninja -S "$S\cmake\proj-lnx" -B $b "-DCMAKE_TOOLCHAIN_FILE=$S\cmake\msil-x64-linux.cmake" `
  "-DMSIL_LLVM_BIN=$($bin.Replace('\','/'))" "-DMSIL_AR=$($MsilLlvmBin.Replace('\','/'))/llvm-ar.exe" `
  -DCMAKE_BUILD_TYPE=Release 2>&1 | Select-Object -Last 25
"configure exit=$LASTEXITCODE ($([int]((Get-Date)-$t0).TotalSeconds) s)"
cmake --build $b -v 2>&1 | Select-Object -Last 25
"build exit=$LASTEXITCODE"
Get-ChildItem $b -File | Where-Object { $_.Extension -in '.exe','.dll','.a','.json' } | Select-Object Name, Length | Format-Table -AutoSize
"--- WSL: dotnet app.exe (.NET 10, runtimeconfig from CMake)"
wsl -d Ubuntu -- dotnet /mnt/c/llvm-msil-scratch/p2/driver-toolchain/cmake/build-lnx/app.exe; "exit=$LASTEXITCODE"
"--- CMake cache facts"
Select-String -Path "$b\CMakeCache.txt" -Pattern 'CMAKE_C_COMPILER_ID|CMAKE_AR:' | ForEach-Object { $_.Line }
Get-ChildItem "$b\CMakeFiles\*\CMakeCCompiler.cmake" | ForEach-Object { Select-String -Path $_ -Pattern 'set\(CMAKE_C_COMPILER_ID |SIMULATE_ID |FRONTEND_VARIANT|IMPLICIT_LINK_LIBRARIES|IMPLICIT_LINK_DIRECTORIES|CMAKE_C_COMPILER_TARGET|CMAKE_LINKER ' | ForEach-Object { $_.Line } }
"--- stand-in log (linker command lines)"
Get-Content $env:MSIL_LD_STANDIN_LOG -ErrorAction SilentlyContinue | ForEach-Object { $_ -replace 'C:\\Users\\[^ ]*\\Temp\\', '$TMP\' } | Select-Object -Last 8

@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo VCVARS FAILED & exit /b 1)
echo === where ===
where cl clang-cl lld-link cmake ninja link mt rc llvm-lib lib
echo === cl ===
cl 2>&1 | findstr /C:"Version"
echo === clang-cl ===
clang-cl --version
echo === lld-link ===
lld-link --version
echo === cmake ===
cmake --version
echo === ninja ===
ninja --version
echo === env ===
echo VSCMD_VER=%VSCMD_VER%
echo VCToolsVersion=%VCToolsVersion%
echo WindowsSDKVersion=%WindowsSDKVersion%
echo UCRTVersion=%UCRTVersion%
echo VCINSTALLDIR=%VCINSTALLDIR%

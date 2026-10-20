@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "<scratch>\recon\tests-ci\proto\fake"
"C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Tools\Llvm\x64\bin\clang.exe" -O1 -o bin\clang.exe launcher.c
echo rc=%ERRORLEVEL%

@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
python "<scratch>\recon\tests-ci\proto\msil\utils\update-expected.py" --clang "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Tools\Llvm\x64\bin\clang.exe" --check "<scratch>\recon\tests-ci\proto\msil\test\M1"
echo check rc=%ERRORLEVEL%
copy /y "<scratch>\recon\tests-ci\proto\msil\test\M1\fib.expected" "<scratch>\recon\tests-ci\proto\fib.expected.bak" >nul
python "<scratch>\recon\tests-ci\proto\msil\utils\update-expected.py" --clang "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Tools\Llvm\x64\bin\clang.exe" "<scratch>\recon\tests-ci\proto\msil\test\M1\fib.c"
echo write rc=%ERRORLEVEL%
fc /b "<scratch>\recon\tests-ci\proto\msil\test\M1\fib.expected" "<scratch>\recon\tests-ci\proto\fib.expected.bak"

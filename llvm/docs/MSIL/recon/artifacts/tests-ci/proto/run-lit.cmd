@echo off
rem PROTOTYPE ONLY: run the prototype suite from a VS x64 environment.
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
"%LOCALAPPDATA%\Python\pythoncore-3.14-64\python.exe" D:\llvm-msil\llvm\utils\lit\lit.py %* "<scratch>\recon\tests-ci\proto\build\tools\msil\test"

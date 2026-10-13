@echo off
rem PROTOTYPE ONLY: run the prototype suite from a VS x64 environment.
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
"C:\Users\JeromeHaltom\AppData\Local\Python\pythoncore-3.14-64\python.exe" D:\llvm-msil\llvm\utils\lit\lit.py %* "C:\Users\JEROME~1\AppData\Local\Temp\claude\D--llvm-msil\fa776849-ce33-4cee-bcc7-05382d063d3e\scratchpad\recon\tests-ci\proto\build\tools\msil\test"

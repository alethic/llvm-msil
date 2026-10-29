. D:\llvm-msil\msil\utils\build\msil-env.ps1
Set-Location C:\llvm-msil-scratch\p3\variadics\proto
$L = 'D:\llvm-msil\build\lib'
$libs = 'LLVMIRReader','LLVMAsmParser','LLVMBitReader','LLVMCore','LLVMRemarks','LLVMBitstreamReader','LLVMBinaryFormat','LLVMTargetParser','LLVMSupport','LLVMDemangle' | ForEach-Object { "$L\$_.lib" }
$sw = [Diagnostics.Stopwatch]::StartNew()
& clang-cl.exe /nologo /DWIN32 /D_WINDOWS /Zc:inline /Zc:__cplusplus /O2 -std:c++17 -MD -UNDEBUG /EHs-c- /GR- /w `
  -ID:\llvm-msil\llvm\include -ID:\llvm-msil\build\include /c msil-expand-variadics.cpp /Fomsil-expand-variadics.obj
"compile exit $LASTEXITCODE ($($sw.Elapsed.TotalSeconds) s)"
& lld-link.exe /nologo /out:msil-expand-variadics.exe msil-expand-variadics.obj $libs psapi.lib shell32.lib ole32.lib uuid.lib advapi32.lib ws2_32.lib ntdll.lib delayimp.lib
"link exit $LASTEXITCODE ($($sw.Elapsed.TotalSeconds) s)"

param([string]$S)
$ErrorActionPreference = 'Stop'
. D:\llvm-msil\msil\utils\build\msil-env.ps1 | Out-Null
$cc = 'C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Tools\Llvm\x64\bin\clang-cl.exe'
$lld = 'C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Tools\Llvm\x64\bin\lld-link.exe'
$flags = @('/nologo','-TP','-DUNICODE','-D_CRT_NONSTDC_NO_DEPRECATE','-D_CRT_NONSTDC_NO_WARNINGS','-D_CRT_SECURE_NO_DEPRECATE','-D_CRT_SECURE_NO_WARNINGS','-D_GLIBCXX_ASSERTIONS','-D_HAS_EXCEPTIONS=0','-D_SCL_SECURE_NO_DEPRECATE','-D_SCL_SECURE_NO_WARNINGS','-D_UNICODE','-D__STDC_CONSTANT_MACROS','-D__STDC_FORMAT_MACROS','-D__STDC_LIMIT_MACROS',
  '-ID:/llvm-msil/build/lib/Target/MSIL','-ID:/llvm-msil/llvm/lib/Target/MSIL','-ID:/llvm-msil/build/include','-ID:/llvm-msil/llvm/include',
  '/DWIN32','/D_WINDOWS','/Zc:inline','/Zc:__cplusplus','/Oi','/bigobj','/permissive-','/W4','-Wextra','-Wno-unused-parameter','/Gw','/O2','/Ob2','-std:c++17','-MD','-UNDEBUG','/Brepro','/EHs-c-','/GR-')
& $cc @flags "/Fo$S\fix\MSILTargetMachine.cpp.obj" -c -- "$S\fix\MSILTargetMachine.cpp"
if ($LASTEXITCODE) { throw "compile failed" }
foreach ($t in @('llc','clang')) {
  & python "$S\tools\mkrsp.py" D:\llvm-msil\build\build.ninja "bin/$t.exe" "$S\fix\$t.exe" "$S\fix\MSILTargetMachine.cpp.obj" | Set-Content -Encoding ascii "$S\fix\$t.rsp"
  & $lld "@$S\fix\$t.rsp" "/pdb:$S\fix\$t.pdb" "/implib:$S\fix\$t.lib"
  if ($LASTEXITCODE) { throw "link $t failed" }
}
Get-ChildItem "$S\fix\*.exe" | Format-Table Name, Length, LastWriteTime

param([string]$V)
$ErrorActionPreference = 'Stop'
. D:/llvm-msil/msil/utils/build/msil-env.ps1
$S = '<scratch>\m11\eh-passes'
$d = Join-Path $S "build\$V"
Set-Location D:/llvm-msil/build
$cc = 'C:\PROGRA~1\MICROS~3\18\PROFES~1\VC\Tools\Llvm\x64\bin\clang-cl.exe'
$flags = @('/nologo','-TP','-DUNICODE','-D_CRT_NONSTDC_NO_DEPRECATE','-D_CRT_NONSTDC_NO_WARNINGS','-D_CRT_SECURE_NO_DEPRECATE','-D_CRT_SECURE_NO_WARNINGS','-D_GLIBCXX_ASSERTIONS','-D_HAS_EXCEPTIONS=0','-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE','-D_SCL_SECURE_NO_DEPRECATE','-D_SCL_SECURE_NO_WARNINGS','-D_UNICODE','-D__STDC_CONSTANT_MACROS','-D__STDC_FORMAT_MACROS','-D__STDC_LIMIT_MACROS','-ID:/llvm-msil/build/lib/Target/MSIL','-ID:/llvm-msil/llvm/lib/Target/MSIL','-ID:/llvm-msil/build/include','-ID:/llvm-msil/llvm/include','/DWIN32','/D_WINDOWS','/Zc:inline','/Zc:__cplusplus','/Oi','/bigobj','/permissive-','-Werror=unguarded-availability-new','/W4','-Wextra','-Wno-unused-parameter','-Wwrite-strings','-Wcast-qual','-Wmissing-field-initializers','-Wimplicit-fallthrough','-Wcovered-switch-default','-Wno-noexcept-type','-Wnon-virtual-dtor','-Wdelete-non-virtual-dtor','-Wsuggest-override','-Wstring-conversion','-Wno-pass-failed','-Wmisleading-indentation','-Wctad-maybe-unsupported','/Gw','-Xclang','-fno-pch-timestamp','/O2','/Ob2','-std:c++17','-MD','-UNDEBUG','/Brepro','/EHs-c-','/GR-','/YuD:/llvm-msil/build/lib/CodeGen/CMakeFiles/LLVMCodeGen.dir/cmake_pch.hxx','/FpD:/llvm-msil/build/lib/CodeGen/CMakeFiles/LLVMCodeGen.dir/.//cmake_pch.cxx.pch','/FID:/llvm-msil/build/lib/CodeGen/CMakeFiles/LLVMCodeGen.dir/cmake_pch.hxx')
$obj = Join-Path $d 'MSILTargetMachine.cpp.obj'
& $cc @flags "/Fo$obj" "/Fd$(Join-Path $d 'tm.pdb')" -c -- (Join-Path $d 'MSILTargetMachine.cpp')
if ($LASTEXITCODE) { throw "compile failed" }
$libs = (Select-String -Path D:/llvm-msil/build/build.ninja -Pattern '^build bin/llc.exe:' -Context 0,12).Context.PostContext | Where-Object { $_ -match '^\s+LINK_LIBRARIES = ' } | Select-Object -First 1
$libs = ($libs -replace '^\s+LINK_LIBRARIES = ','') -split '\s+' | Where-Object { $_ }
$lld = 'C:\PROGRA~1\MICROS~3\18\PROFES~1\VC\Tools\Llvm\x64\bin\lld-link.exe'
$out = Join-Path $d 'bin\llc.exe'
New-Item -ItemType Directory -Force (Join-Path $d 'bin') | Out-Null
& $lld /nologo $obj tools/llc/CMakeFiles/llc.dir/llc.cpp.obj tools/llc/CMakeFiles/llc.dir/__/__/resources/windows_version_resource.rc.res "/out:$out" "/pdb:$(Join-Path $d 'bin\llc.pdb')" /version:0.0 /machine:x64 /INCREMENTAL:NO /subsystem:console D:\llvm-msil\build\lib\CodeGen\CMakeFiles\LLVMCodeGen.dir\cmake_pch.cxx.obj /STACK:10000000 /Brepro @libs
if ($LASTEXITCODE) { throw "link failed" }
Get-Item $out | Select-Object FullName, Length

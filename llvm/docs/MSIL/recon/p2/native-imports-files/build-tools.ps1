# Builds yaml2obj, obj2yaml, llvm-ifs and the nisym prototype against the shared
# build's static libraries (no cmake/ninja in the shared build). Output: tools\bin.
param([string[]]$Only = @('yaml2obj','obj2yaml','llvm-ifs','nisym'))
$ErrorActionPreference = 'Stop'
. D:\llvm-msil\msil\utils\build\msil-env.ps1
$Repo = 'D:/llvm-msil'; $B = 'D:/llvm-msil/build'
$T = 'C:/llvm-msil-scratch/p2/native-imports/tools'
$O = "$T/build"; $Bin = "$T/bin"
New-Item -ItemType Directory -Force $O, $Bin | Out-Null
$cl = Join-Path $MsilLlvmBin 'clang-cl.exe'; $link = Join-Path $MsilLlvmBin 'lld-link.exe'
$tblgen = "$B/bin/llvm-tblgen.exe"
$common = @('/nologo','-TP','-DLLVM_BUILD_STATIC','-DUNICODE','-D_CRT_NONSTDC_NO_DEPRECATE','-D_CRT_NONSTDC_NO_WARNINGS',
  '-D_CRT_SECURE_NO_DEPRECATE','-D_CRT_SECURE_NO_WARNINGS','-D_HAS_EXCEPTIONS=0','-D_SCL_SECURE_NO_DEPRECATE',
  '-D_SCL_SECURE_NO_WARNINGS','-D_UNICODE','-D__STDC_CONSTANT_MACROS','-D__STDC_FORMAT_MACROS','-D__STDC_LIMIT_MACROS',
  "-I$B/include","-I$Repo/llvm/include",'/DWIN32','/D_WINDOWS','/Zc:inline','/Zc:__cplusplus','/Oi','/bigobj',
  '/permissive-','-w','/Gw','/O2','/Ob2','-std:c++17','-MD','-UNDEBUG','/EHs-c-','/GR-')
function Compile($src, $obj, $extra = @()) {
  if ((Test-Path $obj) -and ((Get-Item $obj).LastWriteTime -gt (Get-Item $src).LastWriteTime)) { return }
  & $cl @common @extra /Fo$obj -c $src
  if ($LASTEXITCODE) { throw "compile failed: $src" }
}
function TblGen($td, $out) {
  & $tblgen -gen-opt-parser-defs "-I$Repo/llvm/include" $td -o $out
  if ($LASTEXITCODE) { throw "tblgen failed: $td" }
}
$libs = @('LLVMInterfaceStub_local.lib','LLVMObjectYAML.lib','LLVMObject.lib','LLVMTextAPI.lib','LLVMTextAPIBinaryReader.lib',
  'LLVMDebugInfoCodeView.lib','LLVMDebugInfoDWARF.lib','LLVMDebugInfoDWARFLowLevel.lib','LLVMDebugInfoMSF.lib',
  'LLVMProfileData.lib','LLVMIRReader.lib','LLVMAsmParser.lib','LLVMBitReader.lib','LLVMCore.lib','LLVMRemarks.lib',
  'LLVMBitstreamReader.lib','LLVMMCParser.lib','LLVMMC.lib','LLVMOption.lib','LLVMBinaryFormat.lib','LLVMTargetParser.lib',
  'LLVMSymbolize.lib','LLVMDebugInfoPDB.lib','LLVMDebugInfoBTF.lib','LLVMDebuginfod.lib','LLVMHTTP.lib','LLVMDebugInfoGSYM.lib',
  'LLVMSupport.lib','LLVMDemangle.lib',
  'psapi.lib','shell32.lib','ole32.lib','uuid.lib','advapi32.lib','ws2_32.lib','ntdll.lib','delayimp.lib','winhttp.lib')
function LinkExe($name, $objs) {
  & $link /nologo /out:"$Bin/$name.exe" /libpath:"$B/lib" /libpath:$O @objs @libs
  if ($LASTEXITCODE) { throw "link failed: $name" }
}
# LLVMInterfaceStub is not built in the shared tree: compile its 3 files here.
$isObjs = @()
foreach ($f in 'ELFObjHandler','IFSHandler','IFSStub') {
  Compile "$Repo/llvm/lib/InterfaceStub/$f.cpp" "$O/IS_$f.obj"; $isObjs += "$O/IS_$f.obj" }
& (Join-Path $MsilLlvmBin 'llvm-lib.exe') /nologo /out:"$O/LLVMInterfaceStub_local.lib" @isObjs
if ('yaml2obj' -in $Only) {
  New-Item -ItemType Directory -Force "$O/yaml2obj" | Out-Null
  TblGen "$Repo/llvm/tools/yaml2obj/Opts.td" "$O/yaml2obj/Opts.inc"
  Compile "$Repo/llvm/tools/yaml2obj/yaml2obj.cpp" "$O/yaml2obj.obj" @("-I$O/yaml2obj")
  LinkExe 'yaml2obj' @("$O/yaml2obj.obj") }
if ('obj2yaml' -in $Only) {
  New-Item -ItemType Directory -Force "$O/obj2yaml" | Out-Null
  TblGen "$Repo/llvm/tools/obj2yaml/Opts.td" "$O/obj2yaml/Opts.inc"
  $objs = @()
  foreach ($f in 'archive2yaml','obj2yaml','coff2yaml','dwarf2yaml','dxcontainer2yaml','elf2yaml','goff2yaml','macho2yaml','minidump2yaml','offload2yaml','xcoff2yaml','wasm2yaml') {
    Compile "$Repo/llvm/tools/obj2yaml/$f.cpp" "$O/o2y_$f.obj" @("-I$O/obj2yaml","-I$Repo/llvm/tools/obj2yaml"); $objs += "$O/o2y_$f.obj" }
  LinkExe 'obj2yaml' $objs }
if ('llvm-ifs' -in $Only) {
  New-Item -ItemType Directory -Force "$O/llvm-ifs" | Out-Null
  TblGen "$Repo/llvm/tools/llvm-ifs/Opts.td" "$O/llvm-ifs/Opts.inc"
  Set-Content -Path "$O/llvm-ifs/driver.cpp" -Value @'
#include "llvm/Support/Driver.h"
int llvm_ifs_main(int argc, char **argv, const llvm::ToolContext &);
int main(int argc, char **argv) { return llvm_ifs_main(argc, argv, {argv[0], nullptr, false}); }
'@
  $objs = @()
  foreach ($f in 'llvm-ifs','ErrorCollector') {
    Compile "$Repo/llvm/tools/llvm-ifs/$f.cpp" "$O/ifs_$f.obj" @("-I$O/llvm-ifs","-I$Repo/llvm/tools/llvm-ifs"); $objs += "$O/ifs_$f.obj" }
  Compile "$O/llvm-ifs/driver.cpp" "$O/ifs_driver.obj"; $objs += "$O/ifs_driver.obj"
  LinkExe 'llvm-ifs' $objs }
if ('nisym' -in $Only) {
  Compile "$T/nisym.cpp" "$O/nisym.obj"
  LinkExe 'nisym' @("$O/nisym.obj") }
Write-Output "built: $Only"

$ErrorActionPreference = 'Stop'
. D:\llvm-msil\msil\utils\build\msil-env.ps1
$P = Split-Path -Parent $MyInvocation.MyCommand.Path
$cmd = (Get-Content "$P\cmd.txt" -Raw).Trim()
$cmd = $cmd.Replace('/Folib\Target\MSIL\CMakeFiles\LLVMMSILCodeGen.dir\MSILISelDAGToDAG.cpp.obj', "/Fo$P\MSILISelDAGToDAG.cpp.obj")
$cmd = $cmd.Replace('/Fdlib\Target\MSIL\CMakeFiles\LLVMMSILCodeGen.dir\LLVMCodeGen.pdb', "/Fd$P\proto.pdb")
$cmd = $cmd.Replace('-- D:\llvm-msil\llvm\lib\Target\MSIL\MSILISelDAGToDAG.cpp', "-- $P\MSILISelDAGToDAG.cpp")
Set-Location D:\llvm-msil\build
Write-Output $cmd.Substring($cmd.Length - 500)
cmd /c $cmd
"compile exit=$LASTEXITCODE"

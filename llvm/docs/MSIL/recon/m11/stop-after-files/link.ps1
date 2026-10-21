$ErrorActionPreference = 'Stop'
. D:\llvm-msil\msil\utils\build\msil-env.ps1
$P = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location D:\llvm-msil\build
& lld-link "@$P\llc.rsp"
"link exit=$LASTEXITCODE"

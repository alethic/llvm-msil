# Native x86_64-pc-windows-msvc builds with the tree's clang and lld-link: static CRT (libcmt, clang's default) and
# dynamic CRT (-fms-runtime-lib=dll: vcruntime140 + ucrtbase, what the MSIL triple's imports correspond to).
param([string]$Out, [string[]]$Src, [string[]]$Extra = @())
. D:\llvm-msil\msil\utils\build\msil-env.ps1 *> $null
$clang = 'D:\llvm-msil\build\bin\clang.exe'
foreach ($rt in 'static', 'dll') {
  & $clang --target=x86_64-pc-windows-msvc -fuse-ld=lld "-fms-runtime-lib=$rt" -O1 @Src -o "$Out.$rt.exe" @Extra
  if ($LASTEXITCODE -ne 0) { throw "build failed: $rt" }
}

# Windows multi-module probe: libpb.dll and libpc.dll and main.exe, all /MD (dynamic CRT) or all /MT (static CRT).
. D:\llvm-msil\msil\utils\build\msil-env.ps1 *> $null
$clang = 'D:\llvm-msil\build\bin\clang.exe'
foreach ($rt in 'dll', 'static') {
  New-Item -ItemType Directory -Force $rt > $null
  foreach ($l in 'pb', 'pc') {
    & $clang --target=x86_64-pc-windows-msvc -fuse-ld=lld "-fms-runtime-lib=$rt" -O1 -shared "lib$l.c" -o "$rt\lib$l.dll"
  }
  & $clang --target=x86_64-pc-windows-msvc -fuse-ld=lld "-fms-runtime-lib=$rt" -O1 main.c "$rt\libpb.lib" "$rt\libpc.lib" -o "$rt\main.exe"
}

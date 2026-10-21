#!/bin/bash
# Prototype scenarios (recon lld-flavor). Run from the t1 directory in Git Bash.
set -u
CL=/d/llvm-msil/build/bin/clang.exe
M=../proto/bin/msil-ld.exe
AR="/c/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/llvm-ar.exe"
ILASM=/c/Windows/Microsoft.NET/Framework64/v4.0.30319/ilasm.exe
run() { echo "\$ $*"; "$@" 2>&1; echo "[rc=$?]"; }
echo "## inputs: a.o stock clang (native triple), b.o/h1.o/h2.o/dup.o/undef.o MSIL clang, all -flto"
run $CL --target=x86_64-pc-windows-msvc -fno-ms-volatile -O2 -flto -c a.c -o a.o
run $CL --target=x86_64-pc-windows-msvc-msil -O2 -flto -c b.c -o b.o
run $CL --target=x86_64-pc-windows-msvc-msil -O2 -flto -c h1.c -o h1.o
run $CL --target=x86_64-pc-windows-msvc-msil -O2 -flto -c h2.c -o h2.o
run $CL --target=x86_64-pc-windows-msvc-msil -flto -c dup.c -o dup.o
run $CL --target=x86_64-pc-windows-msvc-msil -flto -c undef.c -o undef.o
run $CL --target=x86_64-unknown-linux-gnu -flto -c b.c -o b-linux.o
rm -f lib/libhelp.a; run "$AR" rcs lib/libhelp.a h1.o h2.o
echo "## flavor dispatch"
cp $M lld.exe; cp $M x86_64-msil-ld.exe; cp $M wasm-ld.exe
run $M --version
run ./lld.exe -flavor msil --version
run ./x86_64-msil-ld.exe --version
run ./wasm-ld.exe --version
run ./lld.exe --version
rm -f lld.exe x86_64-msil-ld.exe wasm-ld.exe
echo "## link: stock + MSIL bitcode, lazy archive member, native DLL exports"
run $M --lto-emit-asm -o prog.il a.o b.o -L lib -lhelp -L C:/Windows/System32 -lucrtbase -mllvm -msil-assembly-name=prog -Map prog.map --why-extract=why.txt --reproduce repro.tar
echo "\$ cat why.txt"; cat why.txt
echo "\$ grep -v defined prog.map"; grep -v 'defined' prog.map
echo "\$ grep pinvokeimpl prog.il"; grep pinvokeimpl prog.il
echo "\$ grep -c never_used prog.il"; grep -c never_used prog.il
echo "\$ tar tf repro.tar"; tar tf repro.tar
run $ILASM -nologo -quiet -dll -pe64 -x64 -output=prog.dll prog.il
mkdir -p run && cp prog.dll run/
run dotnet exec --runtimeconfig ../retarget/net8.runtimeconfig.json run/prog.dll
echo "## diagnostics"
run $M --lto-emit-asm -o x.il a.o b.o dup.o --whole-archive lib/libhelp.a --no-whole-archive -L C:/Windows/System32 -lucrtbase
run $M --lto-emit-asm -o x.il undef.o
run $M --lto-emit-asm -o x.il a.o b-linux.o
run $M --lto-emit-asm -o x.il a.o -lnosuch
echo "## response file"
printf -- '--lto-emit-asm\n-o rsp.il\na.o b.o\n-L lib -lhelp\n-L C:/Windows/System32 -lucrtbase\n' > link.rsp
run $M @link.rsp
echo "## ThinLTO input (needs the object format)"
for f in a b h1; do $CL --target=x86_64-pc-windows-msvc-msil -O2 -flto=thin -c $f.c -o $f.thin.o; done
run $M --lto-emit-asm -o thin.il a.thin.o b.thin.o h1.thin.o -L C:/Windows/System32 -lucrtbase

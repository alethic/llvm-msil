#!/bin/bash
# E2: determinism and path-independence of each Phase-1/Phase-2-prototype step.
W=$1; mkdir -p $W/d1 $W/d2/sub; cd $W || exit 1
cp /d/llvm-msil/msil/test/M1/11-programs/dispatch-vm.c d1/prog.c && cp d1/prog.c d2/sub/prog.c
CL=/d/llvm-msil/build/bin/clang.exe; INC=/d/llvm-msil/msil/test/Inputs; T=--target=x86_64-pc-windows-msvc-msil; F="-ffreestanding -I$INC -O2"
ILN=/d/packages/NuGet/runtime.win-x64.microsoft.netcore.ilasm/10.0.12/runtimes/win-x64/native/ilasm.exe
ILF=/c/Windows/Microsoft.NET/Framework64/v4.0.30319/ilasm.exe
MLD=/c/llvm-msil-scratch/p2/tests-p2/tools/msil-ld.exe
(cd d1 && $CL $T $F -S prog.c -o a.il && $CL $T $F -S prog.c -o b.il && cmp a.il b.il && echo "clang -S: same dir twice: identical")
(cd d2/sub && $CL $T $F -S prog.c -o a.il) && cmp d1/a.il d2/sub/a.il && echo "clang -S: other dir, relative path: identical"
$CL $T $F -S $W/d2/sub/prog.c -o d2/abs.il; cmp d1/a.il d2/abs.il > /dev/null && echo "clang -S: absolute source path: identical" || { echo "clang -S: absolute source path DIFFERS:"; diff d1/a.il d2/abs.il | head -6; }
(cd d1 && $CL $T $F -flto -c prog.c -o a.o && $CL $T $F -flto -c prog.c -o b.o && cmp a.o b.o && echo "clang -flto -c: same dir twice: identical")
(cd d2/sub && $CL $T $F -flto -c prog.c -o a.o); cmp d1/a.o d2/sub/a.o > /dev/null && echo "clang -flto -c: other dir relative: identical" || echo "clang -flto -c: other dir relative DIFFERS"
$CL $T $F -flto -c $W/d2/sub/prog.c -o d2/abs.o; cmp d1/a.o d2/abs.o > /dev/null && echo "clang -flto -c: absolute path: identical" || echo "clang -flto -c: absolute path DIFFERS"
$CL $T $F -flto -c $W/d2/sub/prog.c -ffile-prefix-map=$W/d2/sub/= -o d2/absmap.o; cmp d1/a.o d2/absmap.o > /dev/null && echo "clang -flto -c: absolute + -ffile-prefix-map: identical" || echo "clang -flto -c: absolute + -ffile-prefix-map DIFFERS"
(cd d1 && $ILF -nologo -quiet -dll -pe64 -x64 -output=f1.dll a.il && $ILF -nologo -quiet -dll -pe64 -x64 -output=f2.dll a.il; cmp f1.dll f2.dll > /dev/null && echo "ilasm 4.8 twice: identical" || echo "ilasm 4.8 twice: DIFFERS ($(cmp -l f1.dll f2.dll | wc -l) bytes)")
(cd d1 && $ILN -nologo -quiet -dll -pe64 -x64 -output=n1.dll a.il && $ILN -nologo -quiet -dll -pe64 -x64 -output=n2.dll a.il; cmp n1.dll n2.dll > /dev/null && echo "ilasm 10 twice: identical" || echo "ilasm 10 twice: DIFFERS ($(cmp -l n1.dll n2.dll | wc -l) bytes)")
(cd d1 && $ILN -nologo -quiet -dll -pe64 -x64 -det -output=x.dll a.il && cp x.dll x1.dll && sleep 1 && $ILN -nologo -quiet -dll -pe64 -x64 -det -output=x.dll a.il; cmp x.dll x1.dll > /dev/null && echo "ilasm 10 -DET twice: identical" || echo "ilasm 10 -DET twice: DIFFERS")
(cd d2/sub && $ILN -nologo -quiet -dll -pe64 -x64 -det -output=x.dll a.il; cmp x.dll ../../d1/x.dll > /dev/null && echo "ilasm 10 -DET other dir: identical" || echo "ilasm 10 -DET other dir: DIFFERS")
(cd d1 && $ILN -nologo -quiet -dll -pe64 -x64 -det -output=$W/d1/xabs.dll a.il; cmp xabs.dll x1.dll > /dev/null && echo "ilasm 10 -DET absolute -output: identical" || echo "ilasm 10 -DET absolute -output (other file name too): DIFFERS")
(cd d1 && $MLD --lto-emit-asm -o l1.il a.o -L C:/Windows/System32 -lucrtbase && $MLD --lto-emit-asm -o l2.il a.o -L C:/Windows/System32 -lucrtbase && cmp l1.il l2.il && echo "msil-ld prototype twice: identical")
(cd d2/sub && $MLD --lto-emit-asm -o l1.il a.o -L C:/Windows/System32 -lucrtbase); cmp d1/l1.il d2/sub/l1.il > /dev/null && echo "msil-ld prototype other dir: identical" || { echo "msil-ld prototype other dir: DIFFERS"; diff d1/l1.il d2/sub/l1.il | head -5; }

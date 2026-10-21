#!/bin/bash
# What '-lc -lm' meets in a glibc sysroot under Design.md 5.2's static-first search,
# measured with the lld-flavor recon's prototype msil-ld (LTO, ILAsm output).
ML=/c/llvm-msil-scratch/p2/lld-flavor/proto/bin/msil-ld.exe
SR=C:/llvm-msil-scratch/p2/driver-toolchain/sysroot-glibc
L=$SR/usr/lib/x86_64-linux-gnu
cd /c/llvm-msil-scratch/p2/driver-toolchain/e2e
/d/llvm-msil/build/bin/clang.exe --target=x86_64-unknown-linux-gnu-msil -ffreestanding -O2 -flto -c a.c -o la.o
/d/llvm-msil/build/bin/clang.exe --target=x86_64-unknown-linux-gnu-msil -ffreestanding -O2 -flto -c b.c -o lb.o
echo "sysroot files: libc.a $(stat -c %s $L/libc.a) bytes, $(/c/Program\ Files/Microsoft\ Visual\ Studio/18/Professional/VC/Tools/Llvm/x64/bin/llvm-ar.exe t $L/libc.a | wc -l) members"
echo "libc.so (text):"; sed -n '/GROUP/p' $L/libc.so
echo "libm.so (text):"; sed -n '/GROUP/p' $L/libm.so
echo "libm.a (text):";  sed -n '/GROUP/p' $L/libm.a
$ML --target=x86_64-unknown-linux-gnu-msil --lto-emit-asm -L$L la.o lb.o -lc -lm -o l1.il > l1.log 2>&1
echo "prototype msil-ld -L<sysroot> -lc -lm: exit $?, $(grep -c 'is neither an MSIL object file nor LLVM bitcode' l1.log) 'neither an MSIL object nor bitcode' warnings (libc.a members), other diagnostics:"
grep -v 'is neither an MSIL' l1.log | sort | uniq -c

#!/bin/bash
# Hosted headers through the Phase-1 backend: what compiles, and which diagnostics
# remain (they are Phase-2 linker and Phase-3 libc work).
B=/d/llvm-msil/build/bin
D=/c/llvm-msil-scratch/p2/driver-toolchain/hosted
SR=C:/llvm-msil-scratch/p2/driver-toolchain/sysroot-glibc
VC="C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/MSVC/14.51.36231"
SDK="C:/Program Files (x86)/Windows Kits/10"
cd $D
LNX=(--target=x86_64-unknown-linux-gnu-msil --sysroot=$SR)
WIN=(--target=x86_64-pc-windows-msvc-msil -Xmicrosoft-visualc-tools-root "$VC" -Xmicrosoft-windows-sdk-root "$SDK")
for src in hello.c libcuse.c; do
  for O in -O0 -O2; do
    for t in LNX WIN; do
      if [ $t = LNX ]; then args=("${LNX[@]}"); else args=("${WIN[@]}"); fi
      out=$($B/clang.exe "${args[@]}" $O -S $src -o /dev/null 2>&1); rc=$?
      echo "== $src $t $O: exit $rc"
      echo "$out" | grep -E "error:" | sed 's/^.*error: //' | sed "s/ is not defined in this module and is not a Phase-1 import.*/: not a Phase-1 import/" | sort | uniq -c
    done
  done
done
echo "== UCRT stdio.h at -O0 after removing unreferenced linkonce_odr definitions (opt -passes=globaldce):"
$B/clang.exe "${WIN[@]}" -O0 -S -emit-llvm hello.c -o hw.ll
echo "definitions before: $(grep -c '^define' hw.ll)"
$B/opt.exe -passes=globaldce hw.ll -S -o hw-dce.ll
echo "definitions after: $(grep -c '^define' hw-dce.ll)"
$B/llc.exe hw-dce.ll -o hw.il && /c/Windows/Microsoft.NET/Framework64/v4.0.30319/ilasm.exe -nologo -quiet -exe -pe64 -x64 -output=hw.exe hw.il && ./hw.exe; echo "Framework 4.8 run exit=$?"

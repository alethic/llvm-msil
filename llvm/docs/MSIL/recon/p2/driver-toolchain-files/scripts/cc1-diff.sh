#!/bin/bash
# cc1 arguments for -S: shared-build clang (native toolchains) vs the prototype MSIL toolchain.
P=/c/llvm-msil-scratch/p2/driver-toolchain/proto/bin/clang.exe
O=/d/llvm-msil/build/bin/clang.exe
cd /c/llvm-msil-scratch/p2/driver-toolchain/probe
filt() { grep '"-cc1"' | tr ' ' '\n' | grep -v 'Temp\|resource-dir\|lib.clang\|compilation-dir\|^"-o"\|a.il\|clang.exe\|mingw32\|build.include\|proto.include'; }
for T in x86_64-unknown-linux-gnu-msil x86_64-pc-windows-msvc-msil x86_64-w64-windows-gnu-msil aarch64-unknown-linux-musl-msil; do
  for L in -O0 -O2; do
    $O -### --target=$T $L -S a.c -o a.il 2>&1 | filt > old.txt
    $P -### --target=$T $L -S a.c -o a.il 2>&1 | filt > new.txt
    echo "$T $L: removed: $(diff old.txt new.txt | grep '^<' | cut -c3- | tr '\n' ' ') added: $(diff old.txt new.txt | grep '^>' | cut -c3- | tr '\n' ' ')"
  done
done

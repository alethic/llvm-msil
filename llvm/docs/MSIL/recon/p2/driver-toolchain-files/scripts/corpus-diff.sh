#!/bin/bash
# IL identity of the Phase-1 corpus: shared-build clang vs the prototype MSIL toolchain.
P=/c/llvm-msil-scratch/p2/driver-toolchain/proto/bin/clang.exe
O=/d/llvm-msil/build/bin/clang.exe
T=/d/llvm-msil/msil/test
W=/c/llvm-msil-scratch/p2/driver-toolchain/corpus-diff
mkdir -p $W; n=0; d=0
for f in $(find $T/M1 -name "*.c" | sort); do
  for trip in x86_64-pc-windows-msvc-msil x86_64-unknown-linux-gnu-msil; do
    for lvl in O0 O2; do
      n=$((n+1))
      $O --target=$trip -ffreestanding -Werror=return-type -I$T/Inputs -$lvl -S $f -o $W/old.il 2>/dev/null; r1=$?
      $P --target=$trip -ffreestanding -Werror=return-type -I$T/Inputs -$lvl -S $f -o $W/new.il 2>/dev/null; r2=$?
      if [ $r1 -ne $r2 ] || ! cmp -s $W/old.il $W/new.il; then d=$((d+1)); echo "DIFF $f $trip $lvl"; fi
    done
  done
done
echo "programs=$(find $T/M1 -name '*.c' | wc -l) compiles=$n differing=$d"

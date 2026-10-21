#!/bin/bash
cd "$(dirname "$0")"
L=/usr/lib/x86_64-linux-gnu
for p in fib crc32 mandelbrot-fixed kr-legacy; do
  ld -o $p -dynamic-linker /lib64/ld-linux-x86-64.so.2 start.o $p.o $L/libc.so.6 $L/libm.so.6 || { echo "link fail $p"; continue; }
  ./$p > $p.out; echo "[exit $?]" >> $p.out
  if diff -q <(tr -d '\r' < /mnt/d/llvm-msil/msil/test/M1/11-programs/$p.expected) $p.out >/dev/null; then echo "$p: matches .expected"; else echo "$p: DIFFERS"; diff <(tr -d '\r' < /mnt/d/llvm-msil/msil/test/M1/11-programs/$p.expected) $p.out | head -5; fi
done

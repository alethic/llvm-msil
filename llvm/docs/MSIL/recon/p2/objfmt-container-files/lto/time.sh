#!/bin/bash
# Times: per-TU -c -flto (bitcode), per-TU -S (full codegen), LTO-style link (merge + codegen once).
export MSYS_NO_PATHCONV=1
C=/d/llvm-msil/build/bin/clang; T=x86_64-pc-windows-msvc-msil
now() { python -c 'import time; print(time.perf_counter())'; }
t0=$(now); for f in tu*.c main.c; do $C --target=$T -O2 -c -flto $f -o ${f%.c}.bc || exit 1; done; t1=$(now)
echo "per-TU -c -flto (frontend+opt, 101 TUs, serial): $(python -c "print(round($t1-$t0,1))") s"
t0=$(now); for f in tu*.c; do $C --target=$T -O2 -S $f -o ${f%.c}.il || exit 1; done; t1=$(now)
echo "per-TU -S (frontend+opt+codegen, 100 TUs, serial): $(python -c "print(round($t1-$t0,1))") s"
args=""; for f in tu*.bc; do args="$args -Xclang -mlink-bitcode-file -Xclang $f"; done
t0=$(now); $C --target=$T -O2 -S -x ir main.bc $args -mllvm -msil-assembly-name=big -o big.il || exit 1; t1=$(now)
echo "LTO-style link: merge 101 bitcode files + O2 + codegen once: $(python -c "print(round($t1-$t0,1))") s"
t0=$(now); $C --target=$T -O0 -S -x ir main.bc $args -mllvm -msil-assembly-name=big0 -o big0.il || exit 1; t1=$(now)
echo "LTO-style link at -O0 codegen (no re-optimization): $(python -c "print(round($t1-$t0,1))") s"

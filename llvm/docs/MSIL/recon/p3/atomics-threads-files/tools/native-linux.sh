#!/bin/bash
# native-linux.sh <out.elf> <src.c...> [-- extra link libs]: native x86_64 Linux build with the E7 recipe
# (msil/utils/linux-oracle/start.c), glibc-sysroot headers and libraries.
set -e
B=/d/llvm-msil/build/bin; S=C:/llvm-msil-scratch/glibc-sysroot; T=C:/llvm-msil-scratch/p3/atomics-threads/tools
out=$1; shift; srcs=(); libs=()
while (($#)); do if [ "$1" = "--" ]; then shift; libs=("$@"); break; fi; srcs+=("$1"); shift; done
[ -f $T/start.o ] || $B/clang --target=x86_64-unknown-linux-gnu -ffreestanding -fno-pic -c /d/llvm-msil/msil/utils/linux-oracle/start.c -o $T/start.o
objs=()
for s in "${srcs[@]}"; do o=${s%.c}.nat.o; $B/clang --target=x86_64-unknown-linux-gnu --sysroot=$S -O2 -w -fno-pic -c "$s" -o "$o"; objs+=("$o"); done
MSYS_NO_PATHCONV=1 $B/ld.lld -dynamic-linker /lib64/ld-linux-x86-64.so.2 --as-needed $T/start.o "${objs[@]}" "${libs[@]}" \
  $S/lib/x86_64-linux-gnu/libm.so.6 $S/lib/x86_64-linux-gnu/libc.so.6 -o "$out"

#!/bin/bash
# nat.sh <win|lin> <outdir> <exe-name> <cflags...> -- <srcs...> [-- <ldlibs>]
# Native oracle builds: Windows with the tree's clang (lld-link), Linux with clang + ld.lld + glibc crt files.
source /c/llvm-msil-scratch/p3/hosted-libc/tools/env.sh
T=$1; OUT=$2; NAME=$3; shift 3
CF=(); while [ "$1" != "--" ]; do CF+=("$1"); shift; done; shift
SR=(); while [ $# -gt 0 ] && [ "$1" != "--" ]; do SR+=("$1"); shift; done; [ "$1" = "--" ] && shift
LL=("$@")
mkdir -p $OUT
LIBD=$GS/usr/lib/x86_64-linux-gnu
if [ $T = win ]; then
  $B/clang --target=x86_64-pc-windows-msvc -fuse-ld=lld "${CF[@]}" "${SR[@]}" "${LL[@]}" -o $OUT/$NAME.exe
else
  objs=()
  for s in "${SR[@]}"; do o=$OUT/$(basename ${s%.c}).nat.o; $B/clang --target=x86_64-unknown-linux-gnu --sysroot=$GS -fno-pic "${CF[@]}" -c $s -o $o || exit 1; objs+=($o); done
  MSYS_NO_PATHCONV=1 $B/ld.lld --sysroot=$GS -dynamic-linker /lib64/ld-linux-x86-64.so.2 -no-pie $LIBD/crt1.o $LIBD/crti.o "${objs[@]}" -L$LIBD "${LL[@]}" -lc $LIBD/crtn.o -o $OUT/$NAME.elf
fi

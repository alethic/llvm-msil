#!/bin/bash
# pv.sh <win|lin> <out.exe> <cflags/srcs...>: one-shot simulated-variadics build of C sources (each arg ending .c is a source).
source /c/llvm-msil-scratch/p3/hosted-libc/tools/env.sh
T=$1; OUT=$2; shift 2
V=/c/llvm-msil-scratch/p3/variadics/proto/msil-expand-variadics.exe
if [ $T = win ]; then TT=x86_64-pc-windows-msvc-msil; SR=(); else TT=x86_64-unknown-linux-gnu-msil; SR=(--sysroot=$GS); fi
CF=(); SRCS=(); LD=()
for a in "$@"; do case $a in *.c) SRCS+=("$a");; -l*|-Wl,*) LD+=("$a");; *) CF+=("$a");; esac; done
OBJS=()
for s in "${SRCS[@]}"; do b=${OUT%.exe}.$(basename ${s%.c})
  $B/clang --target=$TT "${SR[@]}" -O2 "${CF[@]}" -S -emit-llvm -Wno-deprecated-declarations $s -o $b.ll || exit 1
  $V $b.ll $b.x.ll || exit 1
  $B/llc -O2 -filetype=obj $b.x.ll -o $b.o || exit 1
  OBJS+=($b.o); done
$B/clang --target=$TT "${SR[@]}" "${OBJS[@]}" "${LD[@]}" -o $OUT

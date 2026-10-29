#!/bin/bash
# Native x86_64-unknown-linux-gnu build with the glibc 2.43 sysroot and glibc's own crt files
# (crt1/crti/crtn; no crtbegin: ld.lld defines __dso_handle). Usage: build-linux.sh out.elf src.c [more.c|-lfoo|-Lx ...]
set -e
B=/d/llvm-msil/build/bin
S=C:/llvm-msil-scratch/glibc-sysroot
L=$S/usr/lib/x86_64-linux-gnu
out=$1; shift
objs=(); extra=()
for a in "$@"; do
  case "$a" in
    *.c) o="${a%.c}.linux.o"; $B/clang --target=x86_64-unknown-linux-gnu --sysroot=$S -O1 -fPIC $CFLAGS -c "$a" -o "$o"; objs+=("$o");;
    *) extra+=("$a");;
  esac
done
MSYS_NO_PATHCONV=1 $B/ld.lld --sysroot=$S -pie -dynamic-linker /lib64/ld-linux-x86-64.so.2 -o "$out" $L/Scrt1.o $L/crti.o "${objs[@]}" -L$L -L$S/lib/x86_64-linux-gnu "${extra[@]}" -lc $L/crtn.o --rpath '$ORIGIN'

#!/bin/bash
# build.sh <src.c> <stem> [extra clang args]: prototype MSIL builds for both x86_64 triples
set -e
B=/d/llvm-msil/build/bin; T=/c/llvm-msil-scratch/p3/atomics-threads/tools; ILASM=/c/Windows/Microsoft.NET/Framework64/v4.0.30319/ilasm.exe
src=$1; stem=$2; shift 2
$B/clang --target=x86_64-pc-windows-msvc-msil -O2 -ffreestanding -DPROTO -DLIBC='"ucrtbase.dll"' -S $src -o $stem.win.il "$@"
$B/clang --target=x86_64-unknown-linux-gnu-msil -O2 -ffreestanding -DPROTO -DLIBC='"libc.so.6"' -S $src -o $stem.lnx.il "$@"
for t in win lnx; do python $T/post.py $stem.$t.il $stem.$t.post.il --thunk start; $ILASM -nologo -quiet -exe -pe64 -x64 $stem.$t.post.il -output=$stem.$t.exe; done

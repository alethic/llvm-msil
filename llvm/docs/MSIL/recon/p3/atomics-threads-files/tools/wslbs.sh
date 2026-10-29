#!/bin/bash
cd "$1"
for w in 0 1 2; do
  echo "native: $(timeout 120 ./bs$w.elf)"
  for v in 8 10; do echo "net$v: $(timeout 120 $HOME/.dotnet-msil/dotnet exec --runtimeconfig /mnt/c/llvm-msil-scratch/p3/atomics-threads/tools/rc$v.json bs$w.lnx.exe)"; done
done

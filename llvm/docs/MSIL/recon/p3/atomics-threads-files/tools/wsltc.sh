#!/bin/bash
cd "$1"
for i in 1 2 3; do
  echo "native: $(./tcreate.elf | tail -1) us"
  for v in 8 10; do echo "net$v: $($HOME/.dotnet-msil/dotnet exec --runtimeconfig /mnt/c/llvm-msil-scratch/p3/atomics-threads/tools/rc$v.json tcreate.lnx.exe | tail -1) us"; done
done
s=$(date +%s%N); timeout 20 ./texitrun.elf; echo "exit-while-running native: rc=$? $(( ($(date +%s%N)-s)/1000000 )) ms"
for v in 8 10; do s=$(date +%s%N); timeout 20 $HOME/.dotnet-msil/dotnet exec --runtimeconfig /mnt/c/llvm-msil-scratch/p3/atomics-threads/tools/rc$v.json texitrun.lnx.exe; echo "exit-while-running net$v: rc=$? $(( ($(date +%s%N)-s)/1000000 )) ms"; done

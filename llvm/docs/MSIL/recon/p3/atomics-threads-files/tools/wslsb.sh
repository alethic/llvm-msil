#!/bin/bash
cd "$1"
for v in sc relacq; do for r in 8 10; do echo "wsl net$r $v: $(timeout 300 $HOME/.dotnet-msil/dotnet exec --runtimeconfig /mnt/c/llvm-msil-scratch/p3/atomics-threads/tools/rc$r.json sb.$v.lnx.exe | tail -1)"; done; [ -x sb.$v.elf ] && echo "native linux $v: $(./sb.$v.elf | tail -1)"; done

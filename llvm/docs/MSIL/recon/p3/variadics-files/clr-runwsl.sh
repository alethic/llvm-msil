#!/bin/bash
cd /mnt/c/llvm-msil-scratch/p3/variadics/clr
D=$HOME/.dotnet-msil/dotnet
export LD_LIBRARY_PATH=$PWD
for v in 8 10; do
  rc=VaProbe.runtimeconfig.json; [ $v = 10 ] && rc=VaProbe.net10.runtimeconfig.json
  echo "### .NET $v Linux"; $D exec --runtimeconfig $rc VaProbe.exe; echo "exit $?"
  echo "### .NET $v Linux TC=0"; DOTNET_TieredCompilation=0 $D exec --runtimeconfig $rc VaProbe.exe; echo "exit $?"
done

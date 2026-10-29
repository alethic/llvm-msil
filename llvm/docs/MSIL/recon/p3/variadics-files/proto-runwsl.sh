#!/bin/bash
# runwsl.sh <name>: native reference and the prototype images on .NET 8/10 (+tc0).
cd /mnt/c/llvm-msil-scratch/p3/variadics/proto/out
D=$HOME/.dotnet-msil/dotnet
N=$1
./$N.native.lnx > $N.ref.lnx.txt; echo "native exit=$?"; cat $N.ref.lnx.txt
for e in $N.lnx-O0.exe $N.lnx-O2.exe; do
  for rt in net8 net10; do
    for tc in "" 0; do
      if [ -n "$tc" ]; then export DOTNET_TieredCompilation=0; else unset DOTNET_TieredCompilation; fi
      $D exec --runtimeconfig $rt.runtimeconfig.json $e > o.lnx.txt 2>&1; rc=$?
      if cmp -s o.lnx.txt $N.ref.lnx.txt; then r=SAME; else r=DIFF; diff o.lnx.txt $N.ref.lnx.txt | head -6; fi
      echo "$e $rt${tc:++tc0} exit=$rc $r"
    done
  done
done

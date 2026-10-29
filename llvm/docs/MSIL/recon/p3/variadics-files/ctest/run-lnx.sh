#!/bin/bash
# run-lnx.sh (inside WSL): runs the Linux images on the private .NET 8 and
# compares stdout with <name>.c.expected.
S=/mnt/c/llvm-msil-scratch/corpus-cache/c-testsuite-5c7275656d75/tests/single-exec
D=$HOME/.dotnet-msil/dotnet
cd /mnt/c/llvm-msil-scratch/p3/variadics/ctest
: > run-lnx.txt
for n in $(cat excluded.txt | tr -d '\r'); do
  for o in -O0 -O2; do
    e=bin/$n.lnx$o.exe
    if [ ! -f $e ]; then echo "$n lnx$o notbuilt" >> run-lnx.txt; continue; fi
    timeout 60 $D exec --runtimeconfig ../emu/net8.runtimeconfig.json $e > o.lnx.txt 2>&1; rc=$?
    if [ $rc -ne 0 ]; then st="exit$rc"; elif cmp -s o.lnx.txt $S/$n.c.expected; then st=PASS; else st=DIFF; fi
    echo "$n lnx$o $st" >> run-lnx.txt
  done
done

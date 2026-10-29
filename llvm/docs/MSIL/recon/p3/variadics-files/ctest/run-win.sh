#!/bin/bash
# run-win.sh: runs the Windows images of the c-testsuite prototype builds on
# .NET 8 and compares stdout with <name>.c.expected.
S=/c/llvm-msil-scratch/corpus-cache/c-testsuite-5c7275656d75/tests/single-exec
cd /c/llvm-msil-scratch/p3/variadics/ctest
: > run-win.txt
for n in $(cat excluded.txt); do
  for o in -O0 -O2; do
    e=bin/$n.win$o.exe
    if [ ! -f $e ]; then echo "$n win$o notbuilt" >> run-win.txt; continue; fi
    timeout 60 dotnet exec --runtimeconfig ../emu/net8.runtimeconfig.json $e > o.win.txt 2>&1; rc=$?
    if [ $rc -ne 0 ]; then st="exit$rc"; elif cmp -s <(tr -d '\r' < o.win.txt) $S/$n.c.expected; then st=PASS; else st=DIFF; fi
    echo "$n win$o $st" >> run-win.txt
  done
done

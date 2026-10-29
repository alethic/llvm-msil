#!/bin/bash
# matrix.sh <exe> <tests...>: runs each test on every runtime, one process each.
exe=$1; shift
for t in "$@"; do
  for rt in fx n8 n10 w8 w10; do
    case "$exe:$rt" in uco*.exe:fx) continue;; esac
    out=$(./run.sh /c/llvm-msil-scratch/p3/callbacks/out $exe $rt $t ${N:-1000000} 2>&1 | grep -v '^runtime' | tr -d '\r' | grep -v '^\s*at ' | head -6 | tr '\n' '|')
    echo "$t | $rt | $out"
  done
done

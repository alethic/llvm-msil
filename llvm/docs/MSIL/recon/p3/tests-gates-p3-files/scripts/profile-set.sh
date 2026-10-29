#!/bin/bash
# profile-set.sh <label> <lit params...>: the sample set, one lit run at -j1 inside a job object.
label=$1; shift
args="-sv -j1 --time-tests"
for p in "$@"; do args="$args --param $p"; done
out=/c/llvm-msil-scratch/p3/tests-gates-p3/logs/set-$label.log
python /c/llvm-msil-scratch/p3/tests-gates-p3/scripts/jobrun.py --label "$label" -- \
  pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 "-LitArgs:$args" \
  msil/test/M1/03-control/loops.c msil/test/M1/01-int/many-args.ll msil/test/M2/00-link/two-tu.test \
  msil/test/M2/08-semantics/zoo.test > "$out" 2>&1
grep -E "^JOBRUN|Passed|Failed|Unsupported|fatal|^[ 0-9.]+s: " "$out" | tail -12

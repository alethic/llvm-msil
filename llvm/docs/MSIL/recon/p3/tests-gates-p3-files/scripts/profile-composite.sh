#!/bin/bash
# profile-composite.sh <label> <test path> <lit params...>: one lit run, -j1, inside a job object.
label=$1; test=$2; shift 2
args="-sv -j1"
for p in "$@"; do args="$args --param $p"; done
out=/c/llvm-msil-scratch/p3/tests-gates-p3/logs/$label.log
python /c/llvm-msil-scratch/p3/tests-gates-p3/scripts/jobrun.py --label "$label" -- \
  pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 "-LitArgs:$args" "$test" > "$out" 2>&1
grep -E "^JOBRUN|Passed|Failed|Unsupported|fatal" "$out" | tail -5

#!/bin/bash
# runs E5 in WSL: lnx-run.sh <dir>
cd "$1" || exit 1
for t in heap stdio errno; do
  for v in 8 10; do
    echo "===== $(basename $1) $t net$v"
    timeout 60 dotnet exec --runtimeconfig ../../rc$v.json crtprog.exe $t 2>&1 | grep -v "^   at "; echo "  [exit ${PIPESTATUS[0]}]"
  done
done

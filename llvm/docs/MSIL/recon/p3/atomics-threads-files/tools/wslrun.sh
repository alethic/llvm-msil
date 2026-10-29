#!/bin/bash
# usage: wslrun.sh <dir> <exe> [extra env assignments...]; runs on .NET 8 and 10 (private runtimes)
cd "$1"; exe="$2"; shift 2
for v in 8 10; do
  out=$(env "$@" timeout 120 "$HOME/.dotnet-msil/dotnet" exec --runtimeconfig /mnt/c/llvm-msil-scratch/p3/atomics-threads/tools/rc$v.json "$exe" 2>&1); rc=$?
  echo "[wsl net$v rc=$rc] $out"
done

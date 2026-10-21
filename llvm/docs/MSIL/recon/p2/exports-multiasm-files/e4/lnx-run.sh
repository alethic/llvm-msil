#!/bin/bash
# E4 in WSL: module-initializer timing on Linux CoreCLR
cd "$(dirname "$0")"
for t in pure ldftn field typeof load cold reflect; do
  for v in 8 10; do
    echo "===== $t net$v: $(timeout 60 dotnet exec --runtimeconfig ../rc$v.json prog4.exe $t 2>&1 | tr '\n' '|')"
  done
done

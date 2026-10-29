#!/bin/bash
# Runs the IL prototype under WSL on the private .NET 8 and .NET 10 runtimes; stderr+stdout merged; exit status.
cd "$(dirname "$0")"
DN="$HOME/.dotnet-msil/dotnet"
for rt in net8.0:8.0.31 net10.0:10.0.13; do
  tfm=${rt%%:*}; fx=${rt##*:}
  echo "{ \"runtimeOptions\": { \"tfm\": \"$tfm\", \"framework\": { \"name\": \"Microsoft.NETCore.App\", \"version\": \"$fx\" } } }" > /tmp/pprog.$tfm.rc.json
  for m in ${1//,/ }; do
    echo "=== wsl $tfm $m"
    timeout -s KILL 60 "$DN" exec --runtimeconfig /tmp/pprog.$tfm.rc.json pprog.exe $m 2>&1 | cat
    echo "status=${PIPESTATUS[0]}"
  done
done

#!/bin/bash
# Runs the lifecycle probe under WSL on the private .NET 8 and .NET 10 runtimes ($HOME/.dotnet-msil, plan 5.12.8),
# stderr and stdout merged into one pipe, 60 s limit; prints the trace, the exit status and the wall time.
cd "$(dirname "$0")"
DN="$HOME/.dotnet-msil/dotnet"
modes=$1; rts=${2:-net8.0,net10.0}
for rt in ${rts//,/ }; do
  if [ $rt = net8.0 ]; then fx=8.0.31; else fx=10.0.13; fi
  cat > /tmp/lc.$rt.rc.json <<J
{ "runtimeOptions": { "tfm": "$rt", "framework": { "name": "Microsoft.NETCore.App", "version": "$fx" } } }
J
  for m in ${modes//,/ }; do
    echo "=== wsl $rt $m"
    s=$(date +%s%N)
    out=$(timeout -s KILL 60 "$DN" exec --runtimeconfig /tmp/lc.$rt.rc.json bin/Release/$rt/lifecycle.dll $m 2>&1 | cat; echo "status=${PIPESTATUS[0]}")
    e=$(date +%s%N)
    echo "$out wall=$(( (e - s) / 1000000 )) ms"
  done
done

#!/bin/bash
# Runs the hand-written assemblies on Linux .NET (8 and 10) under WSL.
cd "$(dirname "$0")"
for f in "$@"; do
  for v in 8 10; do
    dotnet exec --runtimeconfig rc$v.json "$f"; echo "linux net$v $f exit=$?"
    DOTNET_TieredCompilation=0 dotnet exec --runtimeconfig rc$v.json "$f" > /dev/null; echo "linux net$v TC=0 $f exit=$?"
  done
done

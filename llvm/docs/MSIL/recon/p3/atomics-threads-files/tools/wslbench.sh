#!/bin/bash
cd "$1"; exe=$2; shift 2
for v in 8 10; do echo "== wsl net$v TC=0"; DOTNET_TieredCompilation=0 timeout 300 "$HOME/.dotnet-msil/dotnet" exec --runtimeconfig /mnt/c/llvm-msil-scratch/p3/atomics-threads/tools/rc$v.json "$exe" "$@" | tail -14; done

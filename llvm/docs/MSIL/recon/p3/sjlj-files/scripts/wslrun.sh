#!/bin/bash
# usage: wslrun.sh <dir> <net8|net10> <exe> [args]
D=$HOME/.dotnet-msil/dotnet
cd "$1"; rc=$2; shift 2
timeout 120 $D exec --runtimeconfig /mnt/c/llvm-msil-scratch/p3/sjlj/rc/$rc.json "$@"
echo "exit=$?"

#!/bin/bash
# usage: wsltime.sh <dir> <kind: native|net8|net10> <prog> [args]  -- prints wall seconds
D=$HOME/.dotnet-msil/dotnet
cd "$1"; k=$2; shift 2
s=$(date +%s.%N)
if [ "$k" = native ]; then timeout 300 ./"$@"; else timeout 300 $D exec --runtimeconfig /mnt/c/llvm-msil-scratch/p3/sjlj/rc/$k.json "$@"; fi
rc=$?
e=$(date +%s.%N)
echo "exit=$rc wall=$(echo "$e - $s" | bc) s"

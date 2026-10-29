#!/bin/bash
# usage: wslrun.sh <8|10> <prog.exe> [args]
V=$1; shift; P=$1; shift
D=$(bash /mnt/d/llvm-msil/msil/utils/msil-wsl-dotnet.sh --print-path)
exec "$D" exec --runtimeconfig /mnt/c/llvm-msil-scratch/p3/fp-helpers/scripts/rc$V.json "$P" "$@"

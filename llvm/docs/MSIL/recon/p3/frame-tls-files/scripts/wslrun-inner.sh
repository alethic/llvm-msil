#!/bin/bash
# wslrun-inner.sh Name args... (inside WSL): run bin/Name.exe on the private .NET 8 and .NET 10
D=$HOME/.dotnet-msil/dotnet; H=/mnt/c/llvm-msil-scratch/p3/frame-tls; n=$1; shift
for v in 8 10; do echo "--- wsl net$v"; $D exec --runtimeconfig $H/rc$v.json $H/bin/$n.exe "$@"; echo "exit=$?"; done

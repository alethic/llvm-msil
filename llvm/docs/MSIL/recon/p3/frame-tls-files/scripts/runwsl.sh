#!/bin/bash
# runwsl.sh Name args... : run bin/Name.exe on .NET 8 and .NET 10 under WSL (private runtimes)
MSYS_NO_PATHCONV=1 wsl -d Ubuntu -- bash /mnt/c/llvm-msil-scratch/p3/frame-tls/wslrun-inner.sh "$@"

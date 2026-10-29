#!/bin/bash
# usage: wsljit.sh <8|10> <prog.exe> [args]  -- prints the JIT's method summary (tier/opts) to stdout
V=$1; shift; P=$1; shift
D=$(bash /mnt/d/llvm-msil/msil/utils/msil-wsl-dotnet.sh --print-path)
DOTNET_TieredCompilation=0 DOTNET_JitDisasmSummary=1 DOTNET_JitStdOutFile=/tmp/jitsum.$$ "$D" exec --runtimeconfig /mnt/c/llvm-msil-scratch/p3/fp-helpers/scripts/rc$V.json "$P" "$@" > /dev/null
grep -E "main|__|Main|:f[(]" /tmp/jitsum.$$ | head -20; rm -f /tmp/jitsum.$$

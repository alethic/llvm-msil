#!/bin/bash
cd /mnt/c/llvm-msil-scratch/p2/bcl-binding/exp/runs
echo "| Variant | WSL .NET 8 | WSL .NET 10 |"
echo "|---|---|---|"
for f in v*.exe; do
  r8=$(dotnet exec --runtimeconfig rc8.json "$f" 2>&1 >/dev/null | grep -m1 -o '[A-Za-z.]*Exception: [^.]*' ; echo "rc=${PIPESTATUS[0]}")
  dotnet exec --runtimeconfig rc8.json "$f" >/dev/null 2>&1; c8=$?
  e8=$(dotnet exec --runtimeconfig rc8.json "$f" 2>&1 | grep -m1 -o '[A-Za-z.]*Exception: .\{0,120\}')
  dotnet exec --runtimeconfig rc10.json "$f" >/dev/null 2>&1; c10=$?
  e10=$(dotnet exec --runtimeconfig rc10.json "$f" 2>&1 | grep -m1 -o '[A-Za-z.]*Exception: .\{0,120\}')
  echo "| ${f%.exe} | $c8 ${e8:+($e8)} | $c10 ${e10:+($e10)} |"
done

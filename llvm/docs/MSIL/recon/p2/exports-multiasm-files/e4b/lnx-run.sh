#!/bin/bash
cd "$(dirname "$0")"
for d in v1 v2; do for v in 8 10; do echo "===== e4b $d net$v: $(cd $d && timeout 60 dotnet exec --runtimeconfig ../../rc$v.json prog.exe 2>&1 | tr '\n' '|')"; done; done
cd ../e4c
for p in ab ba; do for v in 8 10; do echo "===== e4c $p net$v: $(timeout 60 dotnet exec --runtimeconfig ../rc$v.json prog_$p.exe 2>&1 | tr '\n' '|')"; done; done
cd ../e5c
for d in separate shared; do for v in 8 10; do echo "===== e5c $d net$v: $(cd $d && timeout 60 dotnet exec --runtimeconfig ../../rc$v.json dprog.exe 2>&1 | tr '\n' '|')"; done; done
cd ../e2
for p in prog2 prog2c; do for v in 8 10; do echo "===== e2 $p net$v: $(timeout 60 dotnet exec --runtimeconfig ../rc$v.json $p.exe 2>&1 | grep twice | tr '\n' '|')"; done; done

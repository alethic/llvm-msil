#!/bin/bash
cd "$(dirname "$0")"
for d in ver-eq ver-lower ver-rev ver-major ver-none ver-pkt name-a; do for v in 8 10; do echo "===== $d net$v: $(cd $d && timeout 60 dotnet exec --runtimeconfig ../../rc$v.json prog.exe ok 2>&1 | grep -v '^   at' | head -3 | tr '\n' '|')"; done; done
for t in missing badsig noclass global cold; do echo "===== $t net8: $(timeout 60 dotnet exec --runtimeconfig ../rc8.json prog.exe $t 2>&1 | grep -v '^   at' | head -3 | tr '\n' '|')"; done

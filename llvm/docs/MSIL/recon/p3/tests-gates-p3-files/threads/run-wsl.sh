#!/bin/bash
cd "$(dirname "$0")"
for O in O0 O2; do
  for m in atomic tls pingpong; do ./threads-linux-$O.elf $m 4 1000000; echo "native-$O $m exit $?"; done
  f=0; for i in $(seq 20); do ./threads-linux-$O.elf tls-nobarrier 4 1000000 >/dev/null || f=$((f+1)); done; echo "native-$O tls-nobarrier: $f of 20 failed"
  f=0; for i in $(seq 20); do ./threads-linux-$O.elf tls 4 1000000 >/dev/null || f=$((f+1)); done; echo "native-$O tls (barrier): $f of 20 failed"
  ./threads-linux-$O.elf plain 4 1000000
done
D=$HOME/.dotnet-msil/dotnet
for maj in 8 10; do for m in atomic plain tls; do $D exec --runtimeconfig rc$maj.json Threads.exe $m 4 1000000; echo "wsl net$maj $m exit $?"; done; done

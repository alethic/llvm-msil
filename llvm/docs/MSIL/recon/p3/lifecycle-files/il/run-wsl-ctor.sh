#!/bin/bash
# The exit-from-a-constructor cases of the IL prototype under WSL.
cd "$(dirname "$0")"
for v in PPROG_EXIT_IN_CTOR PLIB_EXIT_IN_CTOR; do
  export $v=1
  bash run-wsl.sh ret | sed "s/^=== \(.*\)/=== \1 $v/"
  unset $v
done

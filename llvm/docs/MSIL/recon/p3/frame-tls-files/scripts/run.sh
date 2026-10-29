#!/bin/bash
# run.sh Name args... : run bin/Name.exe on Framework 4.8, .NET 8 and .NET 10 (Windows)
here=/c/llvm-msil-scratch/p3/frame-tls
n=$1; shift
echo "--- net48"; "$here/bin/$n.exe" "$@"; echo "exit=$?"
echo "--- net8"; dotnet exec --runtimeconfig "$here/rc8.json" "$here/bin/$n.exe" "$@"; echo "exit=$?"
echo "--- net10"; dotnet exec --runtimeconfig "$here/rc10.json" "$here/bin/$n.exe" "$@"; echo "exit=$?"

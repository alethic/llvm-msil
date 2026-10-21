#!/bin/bash
# E1 on Linux: run Windows-built EXE/DLL images of the linux-triple hello under several names.
D=/mnt/c/llvm-msil-scratch/p2/driver-toolchain/naming
W=/tmp/msil-naming; rm -rf $W; mkdir -p $W
cat > $W/rc8.json <<'J'
{ "runtimeOptions": { "tfm": "net8.0", "framework": { "name": "Microsoft.NETCore.App", "version": "8.0.0" } } }
J
cat > $W/rc10.json <<'J'
{ "runtimeOptions": { "tfm": "net10.0", "framework": { "name": "Microsoft.NETCore.App", "version": "10.0.0" } } }
J
for kind in exe dll; do
  for name in p.exe p.dll a.out prog; do
    src=$D/lnx-$kind.img
    mkdir -p $W/$kind-$name; cp $src $W/$kind-$name/$name
    stem=${name%.*}; cp $W/rc8.json $W/$kind-$name/$stem.runtimeconfig.json
    out=$(dotnet exec --runtimeconfig $W/rc10.json $W/$kind-$name/$name 2>&1 | head -1); rc=$?
    echo "$kind image as $name: dotnet exec --runtimeconfig rc10 -> $(dotnet exec --runtimeconfig $W/rc10.json $W/$kind-$name/$name >/dev/null 2>&1; echo $?) : $out"
    out=$(cd $W/$kind-$name && dotnet ./$name 2>&1 | head -1)
    echo "$kind image as $name: dotnet ./$name (rc8 next to it) -> $(cd $W/$kind-$name && dotnet ./$name >/dev/null 2>&1; echo $?) : $out"
  done
done

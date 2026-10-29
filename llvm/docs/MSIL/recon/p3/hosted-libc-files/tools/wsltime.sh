#!/bin/bash
# wsltime.sh: time SQLite t1/t2 natively and on the private .NET 8/10 in a WSL-native temp dir
D=$(mktemp -d); S=/mnt/c/llvm-msil-scratch/p3/hosted-libc
cp $S/build/native/sqlite/shell.elf $S/build/sqlite-default/lin-O2-sim2/shell.exe $S/sql-data/t1.sql $S/sql-data/t2.sql $S/tools/rc8.json $S/tools/rc10.json $D/
cp $S/build/native/lua/lua.elf $S/build/lua-default/lin-O2-sim/lua.exe $D/; cp -r $S/src/lua-5.4.9-tests $D/lt
cd $D
for i in 1 2; do
  rm -f db*
  /usr/bin/time -f "sqlite-t2 native %e s wall %U s user" ./shell.elf dbn < t2.sql > /dev/null
  /usr/bin/time -f "sqlite-t2 net8 %e s wall %U s user" ~/.dotnet-msil/dotnet exec --runtimeconfig rc8.json shell.exe db8 < t2.sql > /dev/null
  /usr/bin/time -f "sqlite-t2 net10 %e s wall %U s user" ~/.dotnet-msil/dotnet exec --runtimeconfig rc10.json shell.exe db10 < t2.sql > /dev/null
  /usr/bin/time -f "sqlite-t1 native %e s wall" ./shell.elf :memory: < t1.sql > /dev/null
  /usr/bin/time -f "sqlite-t1 net8 %e s wall" ~/.dotnet-msil/dotnet exec --runtimeconfig rc8.json shell.exe :memory: < t1.sql > /dev/null
done
cd lt
/usr/bin/time -f "lua-suite native %e s wall %U s user" ../lua.elf -e_U=true all.lua 2>&1 | grep 'total time'
/usr/bin/time -f "lua-suite net8 %e s wall %U s user" ~/.dotnet-msil/dotnet exec --runtimeconfig ../rc8.json ../lua.exe -e_U=true all.lua 2>&1 | grep 'total time'
/usr/bin/time -f "lua-suite net10 %e s wall %U s user" ~/.dotnet-msil/dotnet exec --runtimeconfig ../rc10.json ../lua.exe -e_U=true all.lua 2>&1 | grep 'total time'
cd /; rm -rf $D

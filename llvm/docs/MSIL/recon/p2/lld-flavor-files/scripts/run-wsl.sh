#!/bin/bash
# Runs every assembled <testdir>__<name>.dll of one retarget directory under WSL
# .NET and compares stdout + exit status with msil/test/M1/<testdir>/<name>.expected.
dir="$1"; rc="$2"; src=/mnt/d/llvm-msil/msil/test/M1
pass=0; fail=0
for dll in "$dir"/*__*.dll; do
  b=$(basename "$dll" .dll); tdir=${b%%__*}; name=${b#*__}
  exp="$src/$tdir/$name.expected"
  [ -f "$exp" ] || continue
  tmp=$(mktemp -d); cp "$dll" "$tmp/$name.dll"
  out=$(cd "$tmp" && timeout 60 dotnet exec --runtimeconfig "$rc" "$tmp/$name.dll" 2>/dev/null; echo "[exit $?]")
  rm -rf "$tmp"
  want=$(tr -d '\r' < "$exp")
  if [ "$out" == "$want" ]; then pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL $tdir/$name"; fi
done
echo "pass=$pass fail=$fail"

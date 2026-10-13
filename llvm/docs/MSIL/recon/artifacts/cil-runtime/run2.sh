#!/bin/bash
# usage: run.sh <name> [ilasm extra flags...]   (MODERN ilasm 10.0.12; assembles <name>.il to out2/<name>.dll, runs on net8 and net10)
set -u
D="$(cd "$(dirname "$0")" && pwd)"
N="$1"; shift
ILASM=/d/packages/NuGet/runtime.win-x64.microsoft.netcore.ilasm/10.0.12/runtimes/win-x64/native/ilasm.exe
mkdir -p "$D/out2/$N"
cd "$D"
"$ILASM" -nologo -quiet -dll "$@" "$N.il" "-output=out2/$N/$N.dll" > "out2/$N/ilasm.log" 2>&1
rc=$?
echo "== ilasm exit $rc"; cat "out2/$N/ilasm.log"
[ $rc -ne 0 ] && exit 1
for v in 8 10; do
  cp rc$v.json "out2/$N/$N.runtimeconfig.json"
  echo "== net$v: dotnet out2/$N/$N.dll ${ARGS:-}"
  dotnet "out2/$N/$N.dll" ${ARGS:-}
  echo "== net$v exit code: $?"
done

#!/bin/bash
# usage: run.sh <name> [ilasm extra flags...]   (assembles <name>.il to out/<name>.dll, runs on net8 and net10)
set -u
D="$(cd "$(dirname "$0")" && pwd)"
N="$1"; shift
ILASM=/c/Windows/Microsoft.NET/Framework64/v4.0.30319/ilasm.exe
mkdir -p "$D/out/$N"
cd "$D"
"$ILASM" -nologo -quiet -dll "$@" "$N.il" "-output=out/$N/$N.dll" > "out/$N/ilasm.log" 2>&1
rc=$?
echo "== ilasm exit $rc"; cat "out/$N/ilasm.log"
[ $rc -ne 0 ] && exit 1
for v in 8 10; do
  cp rc$v.json "out/$N/$N.runtimeconfig.json"
  echo "== net$v: dotnet out/$N/$N.dll ${ARGS:-}"
  dotnet "out/$N/$N.dll" ${ARGS:-}
  echo "== net$v exit code: $?"
done

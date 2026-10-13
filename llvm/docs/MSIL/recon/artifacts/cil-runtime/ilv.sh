#!/bin/bash
# usage: ilv.sh <name> [extra ilverify args]  -> summarized error kinds per method
D="$(cd "$(dirname "$0")" && pwd)"
N="$1"; shift
ilverify "$D/out/$N/$N.dll" -r 'C:\Program Files\dotnet\shared\Microsoft.NETCore.App\10.0.12\*.dll' "$@" 2>&1 | python3 "$D/ilv.py"

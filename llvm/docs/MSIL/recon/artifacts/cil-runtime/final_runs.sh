#!/bin/bash
# Re-runs every Windows experiment with both ilasm builds and all runtimes; prints a sanitized log.
D="$(cd "$(dirname "$0")" && pwd)"; cd "$D"
FXILASM=/c/Windows/Microsoft.NET/Framework64/v4.0.30319/ilasm.exe
NEWILASM=/d/packages/NuGet/runtime.win-x64.microsoft.netcore.ilasm/10.0.12/runtimes/win-x64/native/ilasm.exe
san() { python3 "$D/san.py"; }
run_one() {  # $1=name $2=args
  local N=$1; shift
  for tool in fx new; do
    if [ $tool = fx ]; then IL=$FXILASM; O=final/fx/$N; else IL=$NEWILASM; O=final/new/$N; fi
    mkdir -p $O
    "$IL" -nologo -quiet -dll $N.il -output=$O/$N.dll > $O/ilasm.log 2>&1; rc=$?
    echo "[$tool-ilasm -dll] exit=$rc $(cat $O/ilasm.log | tr '\n' ' ')"
    [ $rc -ne 0 ] && continue
    for v in 8 9 10; do
      cp rc$v.json $O/$N.runtimeconfig.json
      timeout 120 dotnet $O/$N.dll "$@" > $O/out$v.txt 2>&1; rc=$?
      echo "[$tool-ilasm, net$v] exit=$rc"; sed 's/^/    /' $O/out$v.txt | san
    done
  done
  mkdir -p final/net48
  if "$FXILASM" -nologo -quiet -exe $N.il -output=final/net48/$N.exe > /dev/null 2>&1; then
    (cd final/net48 && timeout 120 ./$N.exe "$@" > $N.out 2>&1; echo "[fx-ilasm -exe, .NET Framework 4.8] exit=$?"; sed 's/^/    /' $N.out | san)
  fi
}
for e in e01_hello e01b_bareref e01c_sysruntime e03_sigs e04_mem e05_conv e05b_u64fp e06_cmp e07_fnptr e08_pinvoke e08b_noflush e08c_modern e08d_negative e09_modinit e11_names e12_access e12b_access e12c_access e13_i64ptr e16_syntax e18_ldftn_pinvoke e19_memmove e20_align e21_voidptr; do
  echo "################ $e"; run_one $e
done
echo "################ e02_exit (args: 7)"; run_one e02_exit 7
echo "################ e10_main (args: alpha beta)"; run_one e10_main alpha beta
echo "################ e10_main (args: xray -> native exit(7))"; run_one e10_main xray

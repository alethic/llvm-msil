#!/bin/bash
# Reproduces the metadata-writer recon experiments (Git Bash on Windows).
# Usage: run_all.sh <scratch dir>   (expects py/, tools/bin/mdump/ in it)
# Exit codes are the low 8 bits (Git Bash); main returns 63 when every check
# passes except the float32 signaling-NaN constant (bit 64), 127 when all pass.
S=${1:-$(cd "$(dirname "$0")" && pwd)}
OUT=$S/out
mkdir -p "$OUT" && cd "$OUT" || exit 1
PY="python $S/py/hello.py"
MDUMP="dotnet $S/tools/bin/mdump/mdump.dll"
printf '{"runtimeOptions":{"framework":{"name":"Microsoft.NETCore.App","version":"8.0.0"}}}' > rc8.json
printf '{"runtimeOptions":{"framework":{"name":"Microsoft.NETCore.App","version":"10.0.0"}}}' > rc10.json

run() {  # name file
  local n=$1 f=$2 r
  dotnet exec --runtimeconfig rc8.json "$f" > /dev/null 2> "$n.err"; r=$?
  printf '%-28s net8    exit=%-3s %s\n' "$n" "$r" "$(head -c 110 "$n.err" | tr '\r\n' '  ')"
  DOTNET_TieredCompilation=0 dotnet exec --runtimeconfig rc8.json "$f" > /dev/null 2> "$n.err"; r=$?
  printf '%-28s net8TC0 exit=%-3s %s\n' "$n" "$r" "$(head -c 110 "$n.err" | tr '\r\n' '  ')"
  dotnet exec --runtimeconfig rc10.json "$f" > /dev/null 2> "$n.err"; r=$?
  printf '%-28s net10   exit=%-3s %s\n' "$n" "$r" "$(head -c 110 "$n.err" | tr '\r\n' '  ')"
  case "$f" in *.exe)
    "./$f" > /dev/null 2> "$n.err"; r=$?
    printf '%-28s fw4.8   exit=%-3s %s\n' "$n" "$r" "$(head -c 110 "$n.err" | tr '\r\n' '  ')";;
  esac
  rm -f "$n.err"
}

echo "== hello: Roslyn-style envelope (PE32+ AMD64, ILONLY, no import/stub/reloc)"
$PY -o hello.dll > /dev/null && run hello.dll hello.dll
$PY -o hello.exe --exe > /dev/null && run hello.exe hello.exe
echo "== ilasm-style envelope (mscoree import, x64 stub, .reloc DIR64)"
$PY -o hellostub.exe --exe --stub > /dev/null && run hellostub.exe hellostub.exe
echo "== fat EH section format"
$PY -o fateh.exe --exe --fat-eh > /dev/null && run fateh.exe fateh.exe
echo "== PE32 / CorFlags variants"
$PY -o any.exe --exe --pe32 --stub > /dev/null && run pe32-anycpu-stub any.exe
$PY -o anynostub.exe --exe --pe32 > /dev/null && run pe32-anycpu-nostub anynostub.exe
$PY -o req32.exe --exe --pe32 --stub --req32 > /dev/null && run pe32-32bitreq req32.exe
$PY -o pref32.exe --exe --pe32 --stub --req32 --pref32 > /dev/null && run pe32-32bitreq+pref pref32.exe
$PY -o x64req32.exe --exe --stub --req32 > /dev/null && run pe32plus-32bitreq x64req32.exe
echo "== float32 signaling NaN: 248 = still 0x7F800001, 252 = quieted to 0x7FC00001"
$PY -o snanc.exe --exe --snan-probe const > /dev/null && run snan-ldc.r4 snanc.exe
$PY -o snanm.exe --exe --snan-probe move > /dev/null && run snan-ldind-stind snanm.exe
echo "== RVA data: read-only in .text; .sdata RW section; writing into it"
$PY -o sdata.exe --exe --sdata > /dev/null && run sdata sdata.exe
$PY -o rvw-text.exe --exe --rvawrite > /dev/null && run rvawrite-text rvw-text.exe
$PY -o rvw-sdata.exe --exe --rvawrite --sdata > /dev/null && run rvawrite-sdata rvw-sdata.exe
echo "== 4-byte indexes: 70000 extra fields split over two classes"
$PY -o split.exe --exe --many-fields 70000 --split-fields > /dev/null && run split-70000-fields split.exe
echo "== per-type limits"
$PY -o mlim.exe --exe --many-methods 65517 > /dev/null && run methods-65524 mlim.exe
$PY -o mlim2.exe --exe --many-methods 65518 > /dev/null && run methods-65525 mlim2.exe
$PY -o flim.exe --exe --many-fields 65532 > /dev/null && run fields-65536 flim.exe
$MDUMP --srm-fields 65534 srm65534.exe > /dev/null && run srm-fields-65534 srm65534.exe
$MDUMP --srm-fields 65535 srm65535.exe > /dev/null && run srm-fields-65535 srm65535.exe
rm -f split.exe mlim.exe mlim2.exe flim.exe srm6553*.exe
echo "== determinism"
$PY -o det.dll > /dev/null && cp det.dll det1.bin && $PY -o det.dll > /dev/null && cmp det.dll det1.bin && echo "two runs: byte-identical"; rm -f det1.bin
echo "== Linux (WSL): hello-linux.dll (ModuleRef libc), .sdata write"
$PY -o hello-linux.dll --os linux > /dev/null
$PY -o rvw-sdata-l.dll --os linux --rvawrite --sdata > /dev/null
$PY -o rvw-text-l.dll --os linux --rvawrite > /dev/null
cp "$S/run-linux.sh" . 2>/dev/null
MSYS_NO_PATHCONV=1 wsl -d Ubuntu -- bash "$(echo "$OUT" | sed -E 's#^/([a-z])/#/mnt/\1/#')/run-linux.sh" hello-linux.dll rvw-sdata-l.dll rvw-text-l.dll 2>&1 | grep -E "^linux"

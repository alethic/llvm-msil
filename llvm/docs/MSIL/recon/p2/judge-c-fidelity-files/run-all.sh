#!/bin/bash
# judge-c-fidelity: re-runs every MSIL-side and native-Linux experiment of the report.
# Layout: $J/probes/*.c (sources), $J/tools (copy of objfmt-binding-files/tools with the irinfo.py
# weak-alias regex fix), $J/x1/rc8.json, rc10.json, $J/probes/native-linux/{libc.so.6,start.c}.
# Native Windows runs: native-win.ps1 (needs the VS developer environment).
J=/c/llvm-msil-scratch/p2/judge-c-fidelity
CL=/d/llvm-msil/build/bin/clang.exe
ILASM="/c/Windows/Microsoft.NET/Framework64/v4.0.30319/ilasm.exe"
V="/c/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin"
M="dotnet $J/tools/msilobj/out/msilobj.dll"; RC8=$J/x1/rc8.json; RC10=$J/x1/rc10.json
W=x86_64-pc-windows-msvc-msil; L=x86_64-unknown-linux-gnu-msil
run() { # $1 dll, $2 exe (optional): exit codes on .NET 8, .NET 10, Framework 4.8
  dotnet exec --runtimeconfig $RC8 $1 >/dev/null 2>&1; local a=$?
  dotnet exec --runtimeconfig $RC10 $1 >/dev/null 2>&1; local b=$?
  local c=-; if [ -n "$2" ]; then $2 >/dev/null 2>&1; c=$?; fi
  echo ".NET8 $a .NET10 $b NETFX $c"; }
asm() { "$ILASM" -nologo -quiet -dll -pe64 -x64 -output=$2.dll $1 >/dev/null && "$ILASM" -nologo -quiet -exe -pe64 -x64 -output=$2.exe $1 >/dev/null; }
lto() { # $1 triple, $2 out name, $3 cflags, rest: sources (first one is the main module)
  local T=$1 n=$2 fl=$3; shift 3; local first=$1; shift; local args=""
  $CL --target=$T -O1 $fl -w -flto -c $J/probes/$first.c -o $first.$T.bc || { echo "$n: -c -flto failed"; return; }
  for s in "$@"; do $CL --target=$T -O1 $fl -w -flto -c $J/probes/$s.c -o $s.$T.bc || { echo "$n: -c -flto $s failed"; return; }; args="$args -Xclang -mlink-bitcode-file -Xclang $s.$T.bc"; done
  local err; err=$($CL --target=$T -O1 -S -x ir $first.$T.bc $args -o $n.$T.il 2>&1 | grep error | tr '\n' ' ')
  if [ -n "$err" ]; then echo "$n [$T] LTO: -c -flto ok; link/codegen: $err"; else asm $n.$T.il $n.$T; echo "$n [$T] LTO: $(run $n.$T.dll ./$n.$T.exe)"; fi; }
obj() { # $1 out name, $2 cflags, rest: sources -> option-2 prototype objects + msilobj link
  local n=$1 fl=$2; shift 2; local objs=""
  for s in "$@"; do cp $J/probes/$s.c .; EXTRA_CFLAGS="$fl -w" OPTLEVEL=-O1 $J/tools/mkobj.sh $W $s.c > mk-$s.log 2>&1 || { echo "$n objects: mkobj $s failed: $(grep -m1 error mk-$s.log)"; return; }; objs="$objs $s.o"; done
  rm -f $n.dll; local out; out=$($M link -o $n.dll $objs 2>&1 | grep -i error | tr '\n' ' ')
  [ -f $n.dll ] || { echo "$n objects (option-2 prototype): $out"; return; }
  $M link --exe -o $n.exe $objs >/dev/null 2>&1; echo "$n objects (option-2 prototype, link order:$*): $(run $n.dll ./$n.exe)"; }
mkdir -p $J/out && cd $J/out
echo "== J1 zoo (objfmt-binding E2) through the option-2 prototype, -O1 windows-msvc (native: 63)"
cp $J/x1/{a,b}.c $J/probes/ 2>/dev/null; obj zoo "-fcommon" a b
echo "== J2 zoo through the LTO-style path (clang -c -flto + cc1 -mlink-bitcode-file)"
lto $W zoo "-fcommon" a b
echo "== J3 weak alias overridden by a strong definition (native: 144; without p1b: 114)"
lto $W p1ab "-std=gnu17" p1a p1b; lto $W p1ac "-std=gnu17" p1a p1c
obj p1mwb "" p1m p1w p1b; obj p1mbw "" p1m p1b p1w; obj p1mwc "" p1m p1w p1c
echo "== J4 K&R calls whose prototype-less call types differ from the definitions (native: 23)"
lto $W p2 "-std=gnu17" p2a p2b; lto $L p2 "-std=gnu17" p2a p2b
obj p2 "-std=gnu17" p2a p2b
echo "== J4b prototype that disagrees with the definition (objfmt-binding E16; native: 41)"
cp $J/probes/sig/caller.c $J/probes/sig/callee.c $J/probes/ 2>/dev/null
lto $W sig "" caller callee; obj sig "" caller callee
for x in sig.dll; do [ -f $x ] && echo "   $(dotnet exec --runtimeconfig $RC8 $x 2>&1 | head -1)"; done
echo "== J5 __start_/__stop_ section registration (native linux: 15)"
lto $L p3 "" p3a p3b
echo "== J6 constructor registered through a named section (native linux and windows: 42)"
for T in $W $L; do $CL --target=$T -O1 -S $J/probes/p8.c -o p8.$T.il 2>&1 | grep error; asm p8.$T.il p8.$T; echo "p8 [$T] -S + ilasm (no diagnostic): $(run p8.$T.dll ./p8.$T.exe)"; done
echo "== J7 __declspec(selectany) data in two objects (native windows: 15)"
lto $W p4 "" p4a p4b; obj p4 "" p4a p4b
echo "== J8 thread_local across objects (native: 12) and J9 destructors with priorities"
lto $W p5 "" p5a p5b; lto $W p6 "" p6a p6b
echo "== J10 weak definition vs common definition (native lld/ELF and lld-link: 1)"
lto $W p9ab "-fcommon" p9a p9b; lto $W p9ba "-fcommon" p9b p9a
obj p9ab "-fcommon" p9a p9b; obj p9ba "-fcommon" p9b p9a
echo "== J11 ELF vocabulary with an unassigned e_machine (0xC11): native x86_64 objects patched"
mkdir -p elf && cd elf
for s in p1w p3b p5a p6b p7; do $CL --target=x86_64-unknown-linux-gnu -O1 -w -std=gnu17 -ffunction-sections -fdata-sections -c $J/probes/$s.c -o $s.o; python -c "import sys,struct;b=bytearray(open('$s.o','rb').read());b[18:20]=struct.pack('<H',0xC11);open('$s.em.o','wb').write(b)"; done
"$V/llvm-readobj.exe" -h p1w.em.o | grep -E "Format|Machine:"
"$V/llvm-nm.exe" p1w.em.o; rm -f lib.a; "$V/llvm-ar.exe" rcs lib.a p1w.em.o p6b.em.o; "$V/llvm-nm.exe" --print-armap lib.a | sed -n '1,4p'
"$V/llvm-readobj.exe" --symbols p1w.em.o | awk '/Name: (hook|dummy_hook|arr_alias|base_arr) /{n=$2} /Binding:/{b=$2} /Type:/{t=$2} /Section:/{if(n!=""){print "   sym",n,b,t,$2;n=""}}'
"$V/llvm-readobj.exe" --sections p3b.em.o p6b.em.o p7.em.o | grep -E "Name: (myreg|\.init_array|\.fini_array|\.deplibs|\.llvm_addrsig)" | sed 's/^/   /'
"$V/llvm-readobj.exe" --symbols p5a.em.o | grep -A5 "Name: tcount" | grep -E "Type" | sed 's/^/   tcount /'
MSYS_NO_PATHCONV=1 "$V/ld.lld.exe" -o x.elf p1w.em.o 2>&1 | head -1
cd ..
echo "== J12 #pragma comment(lib) per triple"
printf '#pragma comment(lib, "m")\nint x;\n' > p7b.c
for T in $L x86_64-unknown-linux-gnu $W; do echo "   $T: $($CL --target=$T -S -emit-llvm p7b.c -o - 2>&1 | grep -E 'linker.options =|dependent-libraries =|warning' | tr '\n' ' ')"; done

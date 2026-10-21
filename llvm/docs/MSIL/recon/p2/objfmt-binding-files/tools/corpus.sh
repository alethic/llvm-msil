#!/bin/bash
# corpus.sh <triple> <opt> : every M1 C program through the option-2 object pipeline (clang -c
# emulation + msilobj link of the single object), run on .NET 8 (and Framework 4.8 with NETFX=1),
# transcript compared with the committed .expected file.
T=$1; O=$2
ROOT=/c/llvm-msil-scratch/p2/objfmt-binding/corpus/$T$O
mkdir -p $ROOT; cd $ROOT
export OPTLEVEL=$O EXTRA_CFLAGS="-ffreestanding -Werror=return-type -I/d/llvm-msil/msil/test/Inputs"
RUN=/d/llvm-msil/msil/utils/msil-run.py
RC=/c/llvm-msil-scratch/p2/objfmt-binding/zoo/rc8.json
pass=0; fail=0; failed=""
for c in /d/llvm-msil/msil/test/M1/*/*.c; do
  n=$(basename ${c%.c}); d=$(dirname $c); mkdir -p $n; cp $c $n/; cd $n
  if ! /c/llvm-msil-scratch/p2/objfmt-binding/tools/mkobj.sh $T $n.c > mkobj.log 2>&1; then
    fail=$((fail+1)); failed="$failed $n(obj)"; cd ..; continue; fi
  if ! dotnet /c/llvm-msil-scratch/p2/objfmt-binding/tools/msilobj/out/msilobj.dll link -o $n.dll $n.o > link.log 2>&1; then
    fail=$((fail+1)); failed="$failed $n(link)"; cd ..; continue; fi
  if python $RUN --dotnet dotnet --runtimeconfig $RC --expect $d/$n --out $n.out $n.dll > run.log 2>&1; then
    ok=1
    if [ -n "$NETFX" ]; then
      dotnet /c/llvm-msil-scratch/p2/objfmt-binding/tools/msilobj/out/msilobj.dll link --exe -o $n.exe $n.o > /dev/null 2>&1 &&
      python $RUN --netfx --expect $d/$n --out $n.fx.out $n.exe > runfx.log 2>&1 || ok=0
    fi
    if [ $ok = 1 ]; then pass=$((pass+1)); else fail=$((fail+1)); failed="$failed $n(netfx)"; fi
  else fail=$((fail+1)); failed="$failed $n(run)"; fi
  cd ..
done
echo "$T $O: pass $pass fail $fail:$failed"

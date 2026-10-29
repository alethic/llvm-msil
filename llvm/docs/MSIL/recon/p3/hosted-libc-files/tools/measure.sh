#!/bin/bash
# measure.sh: run the three programs (simulated-P3 builds and native oracles) on every runtime,
# record output, exit status and wall time in m2/<prog>.<case>.<rt>.{out,time}, compare with native.
S=/c/llvm-msil-scratch/p3/hosted-libc; T=$S/tools/run.sh; M=$S/m2; cd $S
WZ=build/zlib-default/win-O2-proto; LZ=build/zlib-default/lin-O2-proto
WL=build/lua-default/win-O2-sim; LL=build/lua-default/lin-O2-sim
WQ=build/sqlite-default/win-O2-sim3; LQ=build/sqlite-default/lin-O2-sim2
NW=build/native;
one() { # name rt image stdin-file args...
  local name=$1 rt=$2 img=$3 in=$4; shift 4
  local t0=$(date +%s%N)
  if [ -n "$in" ]; then bash $T $rt $img "$@" < $in > $M/$name.$rt.out 2> $M/$name.$rt.err; else bash $T $rt $img "$@" > $M/$name.$rt.out 2> $M/$name.$rt.err; fi
  local t1=$(date +%s%N); echo "$(( (t1 - t0) / 1000000 ))" > $M/$name.$rt.time
  echo "$name $rt $(cat $M/$name.$rt.time)ms $(tail -1 $M/$name.$rt.err) md5=$(md5sum < $M/$name.$rt.out | cut -c1-8)"
}
WIN="net8 net10 fx"; LIN="wsl8 wsl10"
# zlib
head -c 2000000 /dev/zero >/dev/null
for r in natwin; do one zlib-example $r $NW/zlib/example.exe ""; one zlib-mgz-c $r $NW/zlib/minigzip.exe data/in.txt; done
for r in $WIN; do one zlib-example $r $WZ/example.exe ""; one zlib-mgz-c $r $WZ/minigzip.exe data/in.txt; done
one zlib-example natlin $NW/zlib/example.elf ""; one zlib-mgz-c natlin $NW/zlib/minigzip.elf data/in.txt
for r in $LIN; do one zlib-example $r $LZ/example.exe ""; one zlib-mgz-c $r $LZ/minigzip.exe data/in.txt; done
for r in natwin $WIN; do img=$WZ/minigzip.exe; [ $r = natwin ] && img=$NW/zlib/minigzip.exe; one zlib-mgz-d $r $img $M/zlib-mgz-c.natwin.out -d; done
for r in natlin $LIN; do img=$LZ/minigzip.exe; [ $r = natlin ] && img=$NW/zlib/minigzip.elf; one zlib-mgz-d $r $img $M/zlib-mgz-c.natlin.out -d; done
# lua (no errors: prog1); errors need P3 sjlj on Windows (prog2 Linux only, native glibc setjmp)
export RUNDATA=$S/lua-data
one lua-prog1 natwin $NW/lua/lua.exe "" prog1.lua; for r in $WIN; do one lua-prog1 $r $WL/lua.exe "" prog1.lua; done
one lua-prog1 natlin $NW/lua/lua.elf "" prog1.lua; for r in $LIN; do one lua-prog1 $r $LL/lua.exe "" prog1.lua; done
one lua-prog2 natlin $NW/lua/lua.elf "" prog2.lua; for r in $LIN; do one lua-prog2 $r $LL/lua.exe "" prog2.lua; done
export RUNDATA=$S/src/lua-5.4.9-tests
one lua-suite natlin $NW/lua/lua.elf "" -e_U=true all.lua; for r in $LIN; do one lua-suite $r $LL/lua.exe "" -e_U=true all.lua; done
# sqlite
export RUNDATA=$S/sql-data
for q in 1 2 3; do
  one sql$q natwin $NW/sqlite/shell.exe sql-data/t$q.sql db$q; for r in $WIN; do one sql$q $r $WQ/shell.exe sql-data/t$q.sql db$q; done
  one sql$q natlin $NW/sqlite/shell.elf sql-data/t$q.sql db$q; for r in $LIN; do one sql$q $r $LQ/shell.exe sql-data/t$q.sql db$q; done
done

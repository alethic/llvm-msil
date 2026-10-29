#!/bin/bash
# run.sh <rt> <image> [args...]  rt: net8 net10 fx (Windows)  wsl8 wsl10 (WSL)  natwin natlin
# Runs from a fresh directory under C:/llvm-msil-scratch/p3/hosted-libc/run/<rt>-<n>, stdin passed through.
S=/c/llvm-msil-scratch/p3/hosted-libc
RT=$1; IMG=$2; shift 2
D=$S/run/$RT-$(basename $IMG)-$$; rm -rf $D; mkdir -p $D; cp $IMG $D/
[ -n "$RUNDATA" ] && cp -r $RUNDATA/. $D/
cd $D; I=$(basename $IMG)
case $RT in
  net8) timeout 600 dotnet exec --runtimeconfig $S/tools/rc8.json $I "$@" ;;
  net10) timeout 600 dotnet exec --runtimeconfig $S/tools/rc10.json $I "$@" ;;
  fx) timeout 600 ./$I "$@" ;;
  natwin) timeout 600 ./$I "$@" ;;
  wsl8|wsl10) V=${RT#wsl}; WD=/mnt/c${D#/c}; MSYS_NO_PATHCONV=1 timeout 600 wsl -d Ubuntu -- bash -c "cd $WD && exec ~/.dotnet-msil/dotnet exec --runtimeconfig /mnt/c/llvm-msil-scratch/p3/hosted-libc/tools/rc$V.json $I $*" ;;
  natlin) WD=/mnt/c${D#/c}; MSYS_NO_PATHCONV=1 timeout 600 wsl -d Ubuntu -- bash -c "cd $WD && exec ./$I $*" ;;
esac
rc=$?; echo "[exit $rc]" >&2

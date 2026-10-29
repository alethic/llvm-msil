#!/bin/bash
# run.sh <dir-with-exe-and-native> <exe> <runtime: fx|n8|n10|w8|w10> args...
d=$1; exe=$2; rt=$3; shift 3
S=C:/llvm-msil-scratch/p3/callbacks
case $rt in
  fx) (cd $d && timeout 300 ./$exe "$@"; echo "exit=$?") ;;
  n8) (cd $d && timeout 300 dotnet exec --runtimeconfig $S/rc8.json $exe "$@"; echo "exit=$?") ;;
  n10) (cd $d && timeout 300 dotnet exec --runtimeconfig $S/rc10.json $exe "$@"; echo "exit=$?") ;;
  w8|w10)
    v=8; [ $rt = w10 ] && v=10
    wd=$(echo "$d" | sed -E 's#^/?([a-zA-Z]):?/#/mnt/\L\1/#; s#^C:#/mnt/c#')
    MSYS_NO_PATHCONV=1 wsl -d Ubuntu -- bash -c "cd $wd && ${WENV:-} timeout 300 \$HOME/.dotnet-msil/dotnet exec --runtimeconfig /mnt/c/llvm-msil-scratch/p3/callbacks/rc$v.json $exe $*; echo exit=\$?" ;;
esac

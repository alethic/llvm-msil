#!/bin/bash
# mkobj.sh <triple> <file.c> [undef form: stub|memberref] [name=form ...]
# Emulates 'clang -c' for the option-2 object shape: clang -emit-llvm, irinfo prep,
# Phase-1 llc, objectify, ilasm (Framework ilasm, the D31 default).
set -e
T=$1; C=$2; shift 2
B=${C%.c}
TOOLS=$(dirname "$0")
CL=/d/llvm-msil/build/bin/clang.exe
LLC=/d/llvm-msil/build/bin/llc.exe
ILASM="/c/Windows/Microsoft.NET/Framework64/v4.0.30319/ilasm.exe"
OPT=${OPTLEVEL:--O1}
$CL --target=$T $OPT -fcommon $EXTRA_CFLAGS -S -emit-llvm $C -o $B.ll
python "$TOOLS/irinfo.py" prep $B.ll $B.p1.ll $B.info.json
$LLC -mtriple=$T $B.p1.ll -o $B.p1.il
python "$TOOLS/objectify.py" $B.p1.il $B.info.json $B.o $B.o.il $B.link.json "$@"
mkdir -p obj-$B && cp $B.link.json obj-$B/msil.link && cp $B.o.il obj-$B/
(cd obj-$B && "$ILASM" -nologo -quiet -dll -pe64 -x64 -output=../$B.o $B.o.il)

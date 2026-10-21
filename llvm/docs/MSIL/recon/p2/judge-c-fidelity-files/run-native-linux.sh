#!/bin/bash
# Native Linux oracle (tests-p2 E7 method): this tree's clang for x86_64-unknown-linux-gnu, VS ld.lld,
# start.c (tests-p2-files/linux-oracle) and a copy of WSL's libc.so.6; the programs run in WSL.
J=/c/llvm-msil-scratch/p2/judge-c-fidelity; D=$J/probes/native-linux
CL=/d/llvm-msil/build/bin/clang.exe
LLD="/c/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/Llvm/x64/bin/ld.lld.exe"
cd $D; $CL --target=x86_64-unknown-linux-gnu -ffreestanding -fno-pic -O2 -c start.c -o start.o
build() { local n=$1; shift; local objs=""; for s in "$@"; do $CL --target=x86_64-unknown-linux-gnu -fno-pic -fcommon -O1 -std=gnu17 -w -c $J/probes/$s.c -o $s.o; objs="$objs $s.o"; done
  MSYS_NO_PATHCONV=1 "$LLD" -o $n.elf -dynamic-linker /lib64/ld-linux-x86-64.so.2 --as-needed start.o $objs libc.so.6; }
build p1ab p1a p1b; build p1ac p1a p1c; build p1mwb p1m p1w p1b; build p1mbw p1m p1b p1w; build p1mwc p1m p1w p1c
build p2 p2a p2b; build sig caller callee; build p3 p3a p3b; build p5 p5a p5b; build p6 p6a p6b; build p8 p8
build p9ab p9a p9b; build p9ba p9b p9a
cat > run.sh <<'R'
#!/bin/bash
cd "$(dirname "$0")"
for p in p1ab p1ac p1mwb p1mbw p1mwc p2 sig p3 p5 p6 p8 p9ab p9ba; do out=$(./$p.elf); e=$?; echo "native linux $p: out=[$out] exit $e"; done
R
MSYS_NO_PATHCONV=1 wsl -d Ubuntu -- bash /mnt/c/llvm-msil-scratch/p2/judge-c-fidelity/probes/native-linux/run.sh

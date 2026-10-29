#!/bin/bash
# extras.sh: the remaining experiments, one line per (program, runtime, mode).
cd /c/llvm-msil-scratch/p3/callbacks
O=/c/llvm-msil-scratch/p3/callbacks/out
r() { echo "$1 | $2 | $(./run.sh $O "$@" 2>&1 | tr -d '\r' | grep -v '^\s*at ' | grep -v '^runtime' | head -8 | tr '\n' '|')"; }
r() { local exe=$1 rt=$2; shift 2; echo "$exe $* | $rt | $(./run.sh $O $exe $rt "$@" 2>&1 | tr -d '\r' | grep -v '^\s*at ' | grep -v '^runtime' | head -8 | tr '\n' '|')"; }
echo "## today: qsort/fp/thread programs compiled by the MSIL toolchain (Phase 2 shape)"
for rt in fx n8 n10; do r qs-win.exe $rt; r fp-win.exe $rt; r thr-win.exe $rt; done
for rt in w8 w10; do r qs-lnx.exe $rt; r fp-lnx.exe $rt; r thr-lnx.exe $rt; done
echo "## proposed shape: the same -S output rewritten by patch_fp.py"
for rt in fx n8 n10; do r fp-win-delegate.exe $rt; r thr-win-delegate.exe $rt; done
for rt in n8 n10; do r fp-win-uco.exe $rt; r thr-win-uco.exe $rt; done
for rt in w8 w10; do for m in delegate uco; do r fp-lnx-$m.exe $rt; r thr-lnx-$m.exe $rt; done; done
echo "## UnmanagedCallersOnly bound by name: private attribute class (uconame) and [mscorlib] TypeRef (ucomscorlib)"
for e in uconame.exe ucomscorlib.exe; do for rt in fx n8 n10 w8 w10; do r $e $rt uco-direct; done; done
echo "## struct-by-value parameters and results through thunks"
for rt in fx n8 n10 w8 w10; do r sbv.exe $rt del; [ $rt != fx ] && r sbv.exe $rt uco; done
echo "## signal handlers (Linux)"
for rt in w8 w10; do for k in sync async; do for m in del uco; do r sigt.exe $rt $k $m; done; done; done
echo "## pthread key destructors (Linux)"
for rt in w8 w10; do for m in del uco; do r keyt.exe $rt $m; done; done
echo "## process-wide registry slot"
for rt in fx n8 n10 w8 w10; do r regt.exe $rt; done
echo "## thunk and ldftn addresses"
for rt in fx n8 n10 w8 w10; do r addrfx.exe $rt; done
for rt in n8 n10 w8 w10; do r addruco.exe $rt; done

#!/bin/bash
cd /c/llvm-msil-scratch/p3/tests-gates-p3
REQ="msil_require_verify=1 msil_require_exec=1 msil_require_link=1"
run() { # label paths-string litargs
  label=$1; shift; paths=$1; shift; args="-sv $*"
  python scripts/jobrun.py --label "$label" -- pwsh -NoProfile -File D:/llvm-msil/msil/utils/build/msil-lit.ps1 "-LitArgs:$args" $paths > logs/b3-$label.log 2>&1
  grep -E "^JOBRUN" logs/b3-$label.log
}
P=""; for p in msil_pipeline=obj msil_dotnet_runtime=8 msil_native_oracle=required msil_run_netfx=1 msil_equiv=1 msil_also_run=10,8+tc0 $REQ; do P="$P --param $p"; done
run fc-codegen-20 "llvm/test/CodeGen/MSIL" -j1 --max-tests=20 $P
run fc-lld-20 "lld/test/MSIL" -j1 --max-tests=20 $P
A=""; for p in msil_dotnet_runtime=10 msil_pipeline=asm $REQ; do A="$A --param $p"; done
run asm-net10-2 "msil/test/M1/03-control/loops.c msil/test/M1/01-int/many-args.ll" -j1 $A
W=""; for p in msil_pipeline=obj msil_runner=wsl msil_triple=x86_64-unknown-linux-gnu-msil msil_wsl_dotnet=private msil_dotnet_runtime=10 msil_native_oracle=required msil_linux_sysroot=C:/llvm-msil-scratch/linux-sysroot msil_equiv=1 msil_also_run=8,10+tc0 $REQ; do W="$W --param $p"; done
run wsl-full-loops "msil/test/M1/03-control/loops.c" -j1 -a $W
WL=""; for p in msil_pipeline=obj msil_runner=wsl msil_triple=x86_64-unknown-linux-gnu-msil msil_wsl_dotnet=private msil_dotnet_runtime=10 $REQ; do WL="$WL --param $p"; done
run wsl-lean-loops "msil/test/M1/03-control/loops.c" -j1 -a $WL

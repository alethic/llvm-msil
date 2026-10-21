#!/bin/bash
# Parse every C standard header (and some POSIX/Windows headers) for both MSIL triples:
# glibc 2.43 sysroot (Ubuntu 26.04 .debs) and VS 2026 VC + Windows SDK 10.0.26100.
CL=/d/llvm-msil/build/bin/clang.exe
H=/c/llvm-msil-scratch/p2/driver-toolchain/hdr
SR=C:/llvm-msil-scratch/p2/driver-toolchain/sysroot-glibc
VC="C:/Program Files/Microsoft Visual Studio/18/Professional/VC/Tools/MSVC/14.51.36231"
SDK="C:/Program Files (x86)/Windows Kits/10"
cd $H
probe() { # $1 label, rest: clang args
  local label=$1; shift; local ok=0 bad=""
  for f in "${FILES[@]}"; do
    if out=$($CL "$@" -fsyntax-only $f 2>&1) && [ -z "$out" ]; then ok=$((ok+1)); else bad="$bad $f"; fi
  done
  echo "$label: ${#FILES[@]} headers, $ok clean; not clean:${bad:- none}"
}
FILES=(h_*.c p_*.c)
probe "x86_64-unknown-linux-gnu-msil --sysroot (C + POSIX)" --target=x86_64-unknown-linux-gnu-msil --sysroot=$SR
probe "x86_64-unknown-linux-gnu    --sysroot (C + POSIX)" --target=x86_64-unknown-linux-gnu --sysroot=$SR
FILES=(h_*.c w_*.c)
probe "x86_64-pc-windows-msvc-msil VC+SDK (C + Windows)" --target=x86_64-pc-windows-msvc-msil -Xmicrosoft-visualc-tools-root "$VC" -Xmicrosoft-windows-sdk-root "$SDK"
probe "x86_64-pc-windows-msvc      VC+SDK (C + Windows)" --target=x86_64-pc-windows-msvc -Xmicrosoft-visualc-tools-root "$VC" -Xmicrosoft-windows-sdk-root "$SDK"
echo "tgmath.h on windows-msvc (native and MSIL):"; $CL --target=x86_64-pc-windows-msvc-msil -Xmicrosoft-visualc-tools-root "$VC" -Xmicrosoft-windows-sdk-root "$SDK" -fsyntax-only h_tgmath.c 2>&1 | grep error | head -2
FILES=(h_stdint.c h_stddef.c h_stdarg.c h_float.c h_limits.c h_stdbool.c h_iso646.c h_stdalign.c h_stdnoreturn.c)
probe "x86_64-pc-windows-msvc-msil -ffreestanding, no VC/SDK (freestanding headers)" --target=x86_64-pc-windows-msvc-msil -ffreestanding
probe "x86_64-unknown-linux-gnu-msil -ffreestanding, no sysroot (freestanding headers)" --target=x86_64-unknown-linux-gnu-msil -ffreestanding
echo "stdio.h with only the SDK root (no VC detected):"; $CL --target=x86_64-pc-windows-msvc-msil -Xmicrosoft-windows-sdk-root "$SDK" -fsyntax-only h_stdio.c 2>&1 | grep error | head -1

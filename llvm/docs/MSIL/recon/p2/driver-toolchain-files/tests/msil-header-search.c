// Proposed clang/test/Driver/msil-header-search.c (recon driver-toolchain; P2-B).
// The MSIL toolchain compiles exactly like the native one (Design.md 3 and 8):
// same header search (sysroot and multiarch on Linux, VC/UCRT/SDK on MSVC),
// same effective triple, same predefines; only the relocation model (static:
// there is no PIC in an assembly) and the unwind tables (none) differ.

// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -E \
// RUN:   --sysroot=%S/Inputs/debian_multiarch_tree %s 2>&1 \
// RUN:   | FileCheck --check-prefix=LNX --implicit-check-not=-pic-is-pie --implicit-check-not=-funwind-tables %s
// LNX: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil"
// LNX-SAME: "-mrelocation-model" "static"
// LNX-SAME: "-internal-externc-isystem" "[[SYSROOT:[^"]+]]/usr/include/x86_64-linux-gnu"
// LNX-SAME: "-internal-externc-isystem" "[[SYSROOT]]/usr/include"

// RUN: rm -rf %t && split-file %s %t
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -E -Xmicrosoft-windows-sys-root %t %s 2>&1 \
// RUN:   | FileCheck --check-prefix=WIN --implicit-check-not=-funwind-tables --implicit-check-not=-fms-volatile %s
// WIN: "-cc1" "-triple" "x86_64-pc-windows-msvc{{[0-9.]+}}-msil"
// WIN-SAME: "-mrelocation-model" "static"
// WIN-SAME: "-internal-isystem" "[[ROOT:[^"]*]]{{/|\\\\}}VC{{/|\\\\}}Tools{{/|\\\\}}MSVC{{/|\\\\}}27.1828.18284{{/|\\\\}}include"
// WIN-SAME: "-internal-isystem" "[[ROOT]]{{/|\\\\}}Windows Kits{{/|\\\\}}10{{/|\\\\}}Include{{/|\\\\}}10.0.19041.0{{/|\\\\}}ucrt"
// WIN-SAME: "-internal-isystem" "[[ROOT]]{{/|\\\\}}Windows Kits{{/|\\\\}}10{{/|\\\\}}Include{{/|\\\\}}10.0.19041.0{{/|\\\\}}um"

// The MSVC toolchain forces PIC for x86_64 natively; the MSIL one does not.
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -S -fno-pic %s 2>&1 \
// RUN:   | FileCheck --check-prefix=NOPIC %s
// NOPIC: "-mrelocation-model" "static"

// -ffreestanding is still what Phase-1-style tests pass; it does not remove
// the system include directories (-nostdlibinc does).
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -E -ffreestanding -nostdlibinc \
// RUN:   --sysroot=%S/Inputs/debian_multiarch_tree %s 2>&1 \
// RUN:   | FileCheck --check-prefix=FREE --implicit-check-not=debian_multiarch_tree/usr/include %s
// FREE: "-ffreestanding"

// The compiler-rt-style helper archive of an MSIL triple has its own per-target
// directory, so native builtins are never picked up (Design.md 6, plan P3-A).
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil --rtlib=compiler-rt -print-libgcc-file-name \
// RUN:   -resource-dir=%t/res 2>&1 | FileCheck --check-prefix=RTLIB %s
// RTLIB: res{{/|\\}}lib{{/|\\}}x86_64-unknown-linux-gnu-msil{{/|\\}}libclang_rt.builtins.a
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -resource-dir=%t/res %s 2>&1 \
// RUN:   | FileCheck --check-prefix=RTLINK %s
// RTLINK: "-lc" "-lm" "{{[^"]*}}res{{/|\\\\}}lib{{/|\\\\}}x86_64-unknown-linux-gnu-msil{{/|\\\\}}libclang_rt.builtins.a" "-o"

//--- VC/Tools/MSVC/27.1828.18284/include/vcruntime.h
//--- Windows Kits/10/Include/10.0.19041.0/ucrt/stdio.h
//--- res/lib/x86_64-unknown-linux-gnu-msil/libclang_rt.builtins.a

int main(void) { return 0; }

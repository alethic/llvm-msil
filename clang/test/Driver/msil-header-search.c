// The MSIL toolchain compiles exactly like the native one (Design.md 3 and 8;
// ImplementationPlan.md 5.10.1; recon p2/driver-toolchain.md 4.2 and 6):
// the same header search (sysroot and multiarch on Linux, VC/UCRT/SDK on
// MSVC), the same effective triple, the same predefines; only the
// relocation model (static: there is no PIC in an assembly, decision D8) and
// the unwind tables (none) differ. Hosted headers stay native in Phase 2
// (Q16). The runtime library is compiler-rt style, in a per-target directory
// of its own, so a native builtins archive is never picked up; the link adds
// it after the default libraries when it exists (Design.md 6, plan P3-A).

// RUN: rm -rf %t && split-file %s %t

// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -E \
// RUN:   --sysroot=%S/Inputs/debian_multiarch_tree %s 2>&1 \
// RUN:   | FileCheck --check-prefix=LNX --implicit-check-not=-pic-is-pie --implicit-check-not=-funwind-tables \
// RUN:     --implicit-check-not=error: %s
// LNX: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil"
// LNX-SAME: "-mrelocation-model" "static"
// LNX-SAME: "-internal-externc-isystem" "[[SYSROOT:[^"]+]]/usr/include/x86_64-linux-gnu"
// LNX-SAME: "-internal-externc-isystem" "[[SYSROOT]]/usr/include"

// The -S cc1 line differs from Phase 1's only by the relocation model and the
// missing unwind tables, on every triple family.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -S %s -o %t/x.il 2>&1 \
// RUN:   | FileCheck --check-prefix=STATIC --implicit-check-not=-pic-level --implicit-check-not=-pic-is-pie \
// RUN:     --implicit-check-not=-funwind-tables --implicit-check-not=error: %s
// RUN: %clang -### --target=aarch64-unknown-linux-gnu-msil -S %s -o %t/x.il 2>&1 \
// RUN:   | FileCheck --check-prefix=STATIC --implicit-check-not=-pic-level --implicit-check-not=-pic-is-pie \
// RUN:     --implicit-check-not=-funwind-tables --implicit-check-not=error: %s
// RUN: %clang -### --target=x86_64-w64-windows-gnu-msil -S %s -o %t/x.il 2>&1 \
// RUN:   | FileCheck --check-prefix=STATIC --implicit-check-not=-pic-level \
// RUN:     --implicit-check-not=-funwind-tables --implicit-check-not=error: %s
// STATIC: "-cc1" "-triple" "{{[^"]+}}-msil"
// STATIC-SAME: "-S"
// STATIC-SAME: "-mrelocation-model" "static"

// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -E -Xmicrosoft-windows-sys-root %t %s 2>&1 \
// RUN:   | FileCheck --check-prefix=WIN --implicit-check-not=-funwind-tables --implicit-check-not=-fms-volatile \
// RUN:     --implicit-check-not=-pic-level --implicit-check-not=error: %s
// WIN: "-cc1" "-triple" "x86_64-pc-windows-msvc{{[0-9.]+}}-msil"
// WIN-SAME: "-mrelocation-model" "static"
// WIN-SAME: "-internal-isystem" "[[ROOT:[^"]*]]{{/|\\\\}}VC{{/|\\\\}}Tools{{/|\\\\}}MSVC{{/|\\\\}}27.1828.18284{{/|\\\\}}include"
// WIN-SAME: "-internal-isystem" "[[ROOT]]{{/|\\\\}}Windows Kits{{/|\\\\}}10{{/|\\\\}}Include{{/|\\\\}}10.0.19041.0{{/|\\\\}}ucrt"
// WIN-SAME: "-internal-isystem" "[[ROOT]]{{/|\\\\}}Windows Kits{{/|\\\\}}10{{/|\\\\}}Include{{/|\\\\}}10.0.19041.0{{/|\\\\}}um"

// The MSVC toolchain forces PIC for x86_64 natively; the MSIL one does not,
// so -fno-pic is no ignored option.
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -S -fno-pic %s -o %t/x.il 2>&1 \
// RUN:   | FileCheck --check-prefix=NOPIC --implicit-check-not=warning: %s
// NOPIC: "-mrelocation-model" "static"

// -ffreestanding is still what Phase-1-style tests pass; it does not remove
// the system include directories (-nostdlibinc does).
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -E -ffreestanding -nostdlibinc \
// RUN:   --sysroot=%S/Inputs/debian_multiarch_tree %s 2>&1 \
// RUN:   | FileCheck --check-prefix=FREE --implicit-check-not=debian_multiarch_tree/usr/include %s
// FREE: "-ffreestanding"

// The helper archive of an MSIL triple has its own per-target directory, and
// compiler-rt is the default runtime library (GetDefaultRuntimeLibType), so
// --rtlib=compiler-rt changes nothing.
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil --rtlib=compiler-rt -print-libgcc-file-name \
// RUN:   -resource-dir=%t/res 2>&1 | FileCheck --check-prefix=RTLIB %s
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -print-libgcc-file-name \
// RUN:   -resource-dir=%t/res 2>&1 | FileCheck --check-prefix=RTLIB %s
// RTLIB: res{{/|\\}}lib{{/|\\}}x86_64-unknown-linux-gnu-msil{{/|\\}}libclang_rt.builtins.a
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -print-libgcc-file-name \
// RUN:   -resource-dir=%t/res 2>&1 | FileCheck --check-prefix=RTLIB-WIN %s
// RTLIB-WIN: res{{/|\\}}lib{{/|\\}}x86_64-pc-windows-msvc-msil{{/|\\}}clang_rt.builtins.lib

// The link names the archive after the default libraries when it exists.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -resource-dir=%t/res %s 2>&1 \
// RUN:   | FileCheck --check-prefix=RTLINK %s
// RTLINK: "{{[^"]*}}msil-ld{{(\.exe)?}}"
// RTLINK-SAME: "-lc" "-lm" "{{[^"]*}}res{{/|\\\\}}lib{{/|\\\\}}x86_64-unknown-linux-gnu-msil{{/|\\\\}}libclang_rt.builtins.a" "-o"
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -resource-dir=%t/empty-res %s 2>&1 \
// RUN:   | FileCheck --check-prefix=NO-RTLINK --implicit-check-not=clang_rt %s
// NO-RTLINK: "{{[^"]*}}msil-ld{{(\.exe)?}}"
// NO-RTLINK-SAME: "-lc" "-lm" "-o" "a.exe"

//--- VC/Tools/MSVC/27.1828.18284/include/vcruntime.h
//--- Windows Kits/10/Include/10.0.19041.0/ucrt/stdio.h
//--- res/lib/x86_64-unknown-linux-gnu-msil/libclang_rt.builtins.a
//--- res/lib/x86_64-pc-windows-msvc-msil/clang_rt.builtins.lib
//--- empty-res/include/stddef.h

int main(void) { return 0; }

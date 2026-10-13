// Driver behavior for '<native triple>-msil' targets in Phase 1
// (Design.md sections 3 and 5.5; ImplementationPlan.md M1.c): the native
// toolchain is used, the cc1 triple keeps the '-msil' suffix, -S (ILAsm text)
// and bitcode output work, and the actions below that would write an object
// file are errors until phase 2. Object-format-specific flags for ELF and COFF
// are not passed.

// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -S %s -o %t.il 2>&1 \
// RUN:   | FileCheck --check-prefix=LNX --implicit-check-not=error: --implicit-check-not=warning: \
// RUN:     --implicit-check-not=-faddrsig --implicit-check-not=-fdwarf2-cfi-asm \
// RUN:     --implicit-check-not=-fhalf-no-semantic-interposition %s
// LNX: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil"
// LNX-SAME: "-S"
// LNX-SAME: "-target-cpu" "x86-64"
// LNX-SAME: "-o" "{{[^"]*}}.il"

// The command line of Design.md section 12.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -O2 -ffreestanding -S %s -o %t.il 2>&1 \
// RUN:   | FileCheck --check-prefix=FREE --implicit-check-not=error: --implicit-check-not=-faddrsig %s
// FREE: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil" "-O2" "-S"
// FREE-SAME: "-ffreestanding"

// RUN: %clang -### --target=x86_64-unknown-linux-musl-msil -S %s -o %t.il 2>&1 \
// RUN:   | FileCheck --check-prefix=MUSL --implicit-check-not=error: --implicit-check-not=-faddrsig %s
// MUSL: "-cc1" "-triple" "x86_64-unknown-linux-musl-msil"
// MUSL-SAME: "-S"

// RUN: %clang -### --target=aarch64-unknown-linux-gnu-msil -S %s -o %t.il 2>&1 \
// RUN:   | FileCheck --check-prefix=A64 --implicit-check-not=error: --implicit-check-not=-faddrsig \
// RUN:     --implicit-check-not=-fdwarf2-cfi-asm %s
// A64: "-cc1" "-triple" "aarch64-unknown-linux-gnu-msil"
// A64-SAME: "-S"

// The MSVC toolchain inserts the MSVC version before the '-msil' suffix.
// Unlike the native x86 MSVC triple, -fms-volatile is not on by default: CIL's
// 'volatile.' prefix already gives volatile accesses acquire/release semantics,
// and the atomic accesses -fms-volatile makes are unsupported until phase 3
// (section 12 row 'M1.c amended: the driver does not turn on -fms-volatile';
// decisions D24 and D36). An explicit -fms-volatile is still passed.
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -fms-compatibility-version=19.40 -S %s -o %t.il 2>&1 \
// RUN:   | FileCheck --check-prefix=WIN --implicit-check-not=error: --implicit-check-not=-faddrsig \
// RUN:     --implicit-check-not=-fdwarf2-cfi-asm --implicit-check-not=-fms-volatile %s
// WIN: "-cc1" "-triple" "x86_64-pc-windows-msvc19.40.0-msil"
// WIN-SAME: "-S"
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -S %s -o %t.il 2>&1 \
// RUN:   | FileCheck --check-prefix=WIN-ANYVER --implicit-check-not=error: --implicit-check-not=-fms-volatile %s
// WIN-ANYVER: "-cc1" "-triple" "x86_64-pc-windows-msvc{{[0-9.]*}}-msil"
// WIN-ANYVER-SAME: "-S"
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -fms-volatile -S %s -o %t.il 2>&1 \
// RUN:   | FileCheck --check-prefixes=WIN-ANYVER,MS-VOLATILE --implicit-check-not=error: %s
// RUN: %clang -### --target=x86_64-pc-windows-msvc -S %s -o %t.s 2>&1 \
// RUN:   | FileCheck --check-prefix=MS-VOLATILE --implicit-check-not=error: %s
// MS-VOLATILE: "-fms-volatile"

// The MinGW toolchain passes -exception-model=seh for MSIL as it does for the
// native triple; CodeGen, not the driver, leaves the exception model out of
// MSIL modules (CodeGen/MSIL/triples.c).
// RUN: %clang -### --target=x86_64-w64-windows-gnu-msil -S %s -o %t.il 2>&1 \
// RUN:   | FileCheck --check-prefix=MINGW --implicit-check-not=error: --implicit-check-not=-faddrsig %s
// MINGW: "-cc1" "-triple" "x86_64-w64-windows-gnu-msil"
// MINGW-SAME: "-S"
// MINGW-SAME: "-exception-model=seh"

// Vendor-less Windows spellings keep the msil object format (section 12 row
// 'D3 amended'): they normalize to '<arch>-unknown-windows-<env>-msil'.
// RUN: %clang -### --target=x86_64-windows-msvc-msil -S %s -o %t.il 2>&1 \
// RUN:   | FileCheck --check-prefix=SHORT-X64-MSVC --implicit-check-not=error: %s
// SHORT-X64-MSVC: "-cc1" "-triple" "x86_64-unknown-windows-msvc{{[0-9.]*}}-msil"
// SHORT-X64-MSVC-SAME: "-S"
// RUN: %clang -### --target=x86_64-windows-gnu-msil -S %s -o %t.il 2>&1 \
// RUN:   | FileCheck --check-prefix=SHORT-X64-GNU --implicit-check-not=error: %s
// SHORT-X64-GNU: "-cc1" "-triple" "x86_64-unknown-windows-gnu-msil"
// SHORT-X64-GNU-SAME: "-S"
// RUN: %clang -### --target=aarch64-windows-msvc-msil -S %s -o %t.il 2>&1 \
// RUN:   | FileCheck --check-prefix=SHORT-A64-MSVC --implicit-check-not=error: %s
// SHORT-A64-MSVC: "-cc1" "-triple" "aarch64-unknown-windows-msvc{{[0-9.]*}}-msil"
// SHORT-A64-MSVC-SAME: "-S"

// 'x86_64-pc-win32-msil' normalizes to 'x86_64-pc-windows-msil', which names
// no Windows environment and is rejected as an unknown target triple (decision
// D6). The driver accepts the triple, so the rejection comes from the frontend,
// which only runs without -###.
// RUN: not %clang --target=x86_64-pc-win32-msil -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=WIN32 --implicit-check-not="PLEASE submit" %s
// WIN32: error: unknown target triple 'x86_64-pc-windows-msil'

// Object files are not supported until phase 2. With the integrated assembler
// (the default) and with an external one (-fno-integrated-as), each of these
// reports err_drv_msil_no_object_output: -c on x86_64 Linux, MinGW and MSVC;
// and on x86_64 Linux a link, -save-temps=obj -c and -c of assembler input.
// With the integrated assembler, -c on aarch64 Linux too. Every link is
// rejected, with or without LTO (below): it reports the error once and plans
// no link job.
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-LNX %s
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil %s -o %t.dll 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-LNX --implicit-check-not=error: --implicit-check-not='"ld"' %s
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -save-temps=obj -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-LNX %s
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -c -x assembler %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-LNX %s
// OBJ-LNX: error: emitting object files is not yet supported for MSIL target 'x86_64-unknown-linux-gnu-msil'; use -S to produce ILAsm text, or -emit-llvm or -flto to produce bitcode
// RUN: not %clang -### --target=aarch64-unknown-linux-gnu-msil -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-A64 %s
// OBJ-A64: error: emitting object files is not yet supported for MSIL target 'aarch64-unknown-linux-gnu-msil'
// RUN: not %clang -### --target=x86_64-w64-windows-gnu-msil -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-MINGW %s
// OBJ-MINGW: error: emitting object files is not yet supported for MSIL target 'x86_64-w64-windows-gnu-msil'
// RUN: not %clang -### --target=x86_64-pc-windows-msvc-msil -fms-compatibility-version=19.40 -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-WIN %s
// OBJ-WIN: error: emitting object files is not yet supported for MSIL target 'x86_64-pc-windows-msvc{{[0-9.]*}}-msil'

// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -fno-integrated-as -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-LNX %s
// RUN: not %clang -### --target=x86_64-w64-windows-gnu-msil -fno-integrated-as -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-MINGW %s
// RUN: not %clang -### --target=x86_64-pc-windows-msvc-msil -fms-compatibility-version=19.40 -fno-integrated-as \
// RUN:   -c %s -o %t.o 2>&1 | FileCheck --check-prefix=OBJ-WIN %s
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -fno-integrated-as -save-temps=obj -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-LNX %s
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -fno-integrated-as %s -o %t.dll 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-LNX --implicit-check-not=error: --implicit-check-not='"ld"' \
// RUN:     --implicit-check-not='"as"' %s
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -fno-integrated-as -c -x assembler %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-LNX %s

// LTO links are rejected too: Phase 1 has no MSIL linker, and no native linker
// can link MSIL bitcode (Design.md section 5.5; section 12 row 'fix-loop
// ownership: clang/lib/Driver/ToolChain.cpp -> fix-6'), so the driver plans
// neither GNU ld with the LLVMgold plugin nor lld. This holds for full and thin
// LTO, -fuse-ld=lld, bitcode input, and the Linux (glibc, musl, aarch64), MinGW
// and MSVC toolchains. The native MSVC toolchain requires lld for LTO
// (err_drv_lto_without_lld), so the MSVC run names it.
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -flto %s -o %t.dll 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-LNX --implicit-check-not=error: --implicit-check-not='"ld"' \
// RUN:     --implicit-check-not=-plugin --implicit-check-not=LLVMgold %s
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -flto=thin %s -o %t.dll 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-LNX --implicit-check-not=error: --implicit-check-not='"ld"' \
// RUN:     --implicit-check-not=-plugin --implicit-check-not=LLVMgold %s
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -fuse-ld=lld %s -o %t.dll 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-LNX --implicit-check-not=error: --implicit-check-not='"ld"' \
// RUN:     --implicit-check-not=-plugin --implicit-check-not=LLVMgold --implicit-check-not=ld.lld %s
// RUN: not %clang -### --target=x86_64-unknown-linux-musl-msil -flto %s -o %t.dll 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-MUSL --implicit-check-not=error: --implicit-check-not='"ld"' \
// RUN:     --implicit-check-not=-plugin --implicit-check-not=LLVMgold %s
// OBJ-MUSL: error: emitting object files is not yet supported for MSIL target 'x86_64-unknown-linux-musl-msil'
// RUN: not %clang -### --target=aarch64-unknown-linux-gnu-msil -flto %s -o %t.dll 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-A64 --implicit-check-not=error: --implicit-check-not='"ld"' \
// RUN:     --implicit-check-not=-plugin --implicit-check-not=LLVMgold %s
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -c -emit-llvm %s -o %t.lto-input.bc
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -flto %t.lto-input.bc -o %t.dll 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-LNX --implicit-check-not=error: --implicit-check-not='"ld"' \
// RUN:     --implicit-check-not=-plugin --implicit-check-not=LLVMgold %s
// RUN: not %clang -### --target=x86_64-w64-windows-gnu-msil -flto %s -o %t.dll 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-MINGW --implicit-check-not=error: --implicit-check-not='"ld"' \
// RUN:     --implicit-check-not=-plugin --implicit-check-not=LLVMgold %s
// RUN: not %clang -### --target=x86_64-pc-windows-msvc-msil -fms-compatibility-version=19.40 -flto -fuse-ld=lld \
// RUN:   %s -o %t.dll 2>&1 | FileCheck --check-prefix=OBJ-WIN --implicit-check-not=error: --implicit-check-not='"ld"' \
// RUN:     --implicit-check-not=-plugin --implicit-check-not=LLVMgold --implicit-check-not=lld-link %s

// -S needs no assembler, so -fno-integrated-as -S works.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -fno-integrated-as -S %s -o %t.il 2>&1 \
// RUN:   | FileCheck --check-prefix=NOIAS-S --implicit-check-not=error: --implicit-check-not='"as"' %s
// NOIAS-S: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil"
// NOIAS-S-SAME: "-S"
// NOIAS-S-SAME: "-o" "{{[^"]*}}.il"

// Bitcode output still works.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -c -emit-llvm %s -o %t.bc 2>&1 \
// RUN:   | FileCheck --check-prefix=BC --implicit-check-not=error: %s
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -c -emit-llvm %s -o %t.bc 2>&1 \
// RUN:   | FileCheck --check-prefix=BC --implicit-check-not=error: %s
// BC: "-cc1" "-triple" "x86_64-{{pc-windows-msvc[0-9.]*|unknown-linux-gnu}}-msil"
// BC-SAME: "-emit-llvm-bc"
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -c %s -o %t.bc 2>&1 \
// RUN:   | FileCheck --check-prefix=LTO --implicit-check-not=error: %s
// LTO: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil"
// LTO-SAME: "-emit-llvm-bc"
// LTO-SAME: "-flto=full"
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -S -emit-llvm %s -o %t.ll 2>&1 \
// RUN:   | FileCheck --check-prefix=LL --implicit-check-not=error: %s
// LL: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil"
// LL-SAME: "-emit-llvm"

// -fsanitize=kcfi checks indirect calls in native code and writes its type ids
// as module asm, which the msil format cannot have (decision D26), so it is not
// offered for MSIL triples (section 12 row 'fix-loop ownership:
// clang/lib/Driver/ToolChain.cpp -> fix-6'); the native triple accepts it.
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -fsanitize=kcfi -flto -c %s -o %t.bc 2>&1 \
// RUN:   | FileCheck --check-prefix=KCFI -DTRIPLE=x86_64-unknown-linux-gnu-msil --implicit-check-not=error: \
// RUN:     --implicit-check-not=-fsanitize=kcfi %s
// RUN: not %clang -### --target=x86_64-pc-windows-msvc-msil -fsanitize=kcfi -flto -c %s -o %t.bc 2>&1 \
// RUN:   | FileCheck --check-prefix=KCFI -DTRIPLE=x86_64-pc-windows-msvc-msil --implicit-check-not=error: \
// RUN:     --implicit-check-not=-fsanitize=kcfi %s
// KCFI: error: unsupported option '-fsanitize=kcfi' for target '[[TRIPLE]]'
// RUN: %clang -### --target=x86_64-unknown-linux-gnu -fsanitize=kcfi -flto -c %s -o %t.bc 2>&1 \
// RUN:   | FileCheck --check-prefix=KCFI-NATIVE --implicit-check-not=error: %s
// KCFI-NATIVE: "-cc1" "-triple" "x86_64-unknown-linux-gnu"
// KCFI-NATIVE-SAME: "-fsanitize=kcfi"

// -fembed-bitcode puts bitcode in an object-file section, and the msil format
// has none, so the driver rejects every form of it except 'off' (section 12 row
// 'M1.b items (5) and (7) amended'), without a crash. The run without -### shows
// that nothing reaches the frontend. cc1 given -fembed-bitcode=all or =marker
// directly (both ask for the bitcode section name) stops with a fatal usage
// error, not an UNREACHABLE crash. %clang_cc1 is not available in Driver tests,
// so those lines use '%clang -cc1'. On Windows the cc1 fatal-error handler also
// prints the 'PLEASE submit' banner and a stack dump (RunInterruptHandlers runs
// the signal handlers), so they check only the message and the absence of
// UNREACHABLE.
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -fembed-bitcode -S -emit-llvm %s -o %t.ll 2>&1 \
// RUN:   | FileCheck --check-prefix=EMBED --implicit-check-not="PLEASE submit" --implicit-check-not=UNREACHABLE %s
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -fembed-bitcode=marker -S -emit-llvm %s -o %t.ll 2>&1 \
// RUN:   | FileCheck --check-prefix=EMBED --implicit-check-not="PLEASE submit" --implicit-check-not=UNREACHABLE %s
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -fembed-bitcode -c -emit-llvm %s -o %t.bc 2>&1 \
// RUN:   | FileCheck --check-prefix=EMBED --implicit-check-not="PLEASE submit" --implicit-check-not=UNREACHABLE %s
// RUN: not %clang -### --target=x86_64-pc-windows-msvc-msil -fembed-bitcode=all -S -emit-llvm %s -o %t.ll 2>&1 \
// RUN:   | FileCheck --check-prefix=EMBED --implicit-check-not="PLEASE submit" --implicit-check-not=UNREACHABLE %s
// RUN: not %clang --target=x86_64-unknown-linux-gnu-msil -fembed-bitcode -S -emit-llvm %s -o %t.ll 2>&1 \
// RUN:   | FileCheck --check-prefix=EMBED --implicit-check-not="PLEASE submit" --implicit-check-not=UNREACHABLE %s
// EMBED: error: unsupported option '-fembed-bitcode{{.*}}' for target
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -fembed-bitcode=off -S -emit-llvm %s -o %t.ll 2>&1 \
// RUN:   | FileCheck --check-prefix=EMBED-OFF --implicit-check-not=error: --implicit-check-not=-fembed-bitcode %s
// EMBED-OFF: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil"
// EMBED-OFF-SAME: "-emit-llvm"
// RUN: not %clang -cc1 -triple x86_64-unknown-linux-gnu-msil -fembed-bitcode=all -emit-llvm %s -o %t.ll 2>&1 \
// RUN:   | FileCheck --check-prefix=EMBED-CC1 --implicit-check-not=UNREACHABLE %s
// RUN: not %clang -cc1 -triple x86_64-unknown-linux-gnu-msil -fembed-bitcode=marker -emit-llvm %s -o %t.ll 2>&1 \
// RUN:   | FileCheck --check-prefix=EMBED-CC1 --implicit-check-not=UNREACHABLE %s
// EMBED-CC1: error: {{.*}}embedding bitcode is not supported for the MSIL object format

int main(void) { return 0; }

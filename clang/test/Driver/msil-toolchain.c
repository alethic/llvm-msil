// Driver behavior for '<native triple>-msil' targets (Design.md sections 3
// and 5.5; ImplementationPlan.md M1.c, 5.10 and 5.12.13): the MSIL toolchain
// wraps the native one (MSILToolChain<NativeTC>, M14), the cc1 triple keeps
// the '-msil' suffix, -S (ILAsm text) and bitcode output work, -c writes LLVM
// bitcode until CIL objects exist (M19), and links run msil-ld (msil-link.c,
// msil-lto.c, msil-header-search.c). Object-format-specific flags for ELF and
// COFF are not passed, and clang-cl does not turn /GS on by default (section
// 12 row 'D25 amended'). Basic block sections, late function splitting, the
// basic block address map and static data partitioning need an ELF (the
// address map also an x86-64 COFF) object file, so they are 'unsupported
// option' errors for every MSIL triple (section 12 row 'D8 amended (function
// splitting)').

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

// clang-cl turns /GS (the stack protector) on by default, but not for MSIL
// triples, which have no stack protector (decision D25; section 12 row 'D25
// amended'). An explicit /GS is still passed, and MSILPrepareIR strips it with
// a warning (CodeGen/MSIL/stack-protector-warning.ll). The native triple keeps
// the default. These runs check the cc1 line of ILAsm output (/clang:-S); /c
// writes bitcode (below).
// RUN: %clang_cl -### --target=x86_64-pc-windows-msvc-msil /clang:-S -- %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CL --implicit-check-not=error: --implicit-check-not=warning: \
// RUN:     --implicit-check-not=-stack-protector %s
// RUN: %clang_cl -### --target=x86_64-pc-windows-msvc-msil /clang:-S /GS- -- %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CL --implicit-check-not=error: --implicit-check-not=warning: \
// RUN:     --implicit-check-not=-stack-protector %s
// RUN: %clang_cl -### --target=x86_64-pc-windows-msvc-msil /clang:-S /GS -- %s 2>&1 \
// RUN:   | FileCheck --check-prefixes=CL,CL-GS --implicit-check-not=error: --implicit-check-not=warning: %s
// CL: "-cc1" "-triple" "x86_64-pc-windows-msvc{{[0-9.]*}}-msil"
// CL-SAME: "-S"
// CL-GS-SAME: "-stack-protector" "2"
// RUN: %clang_cl -### --target=x86_64-pc-windows-msvc /clang:-S -- %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CL-NATIVE --implicit-check-not=error: %s
// CL-NATIVE: "-cc1" "-triple" "x86_64-pc-windows-msvc{{[0-9.]*}}"
// CL-NATIVE-SAME: "-stack-protector" "2"

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

// Until M19 (CIL objects), -c writes LLVM bitcode for every MSIL triple
// (ImplementationPlan.md 5.10.5, Q6): the toolchain's LTO mode is full LTO
// unless -S or -emit-llvm is given, so -c is one 'cc1 -emit-llvm-bc
// -flto=full' job and no assembler runs, also with -fno-integrated-as and with
// -save-temps=obj (whose preprocessed temporary still compiles to bitcode). A
// link compiles to bitcode and plans msil-ld, never 'ld' (msil-link.c and
// msil-lto.c check the link jobs; this file's former LTO-link block moved
// there). Assembling ILAsm text (-c -x assembler) is err_drv_msil_ilasm_input
// with the integrated assembler and err_no_external_assembler without it,
// and -fno-lto -c asks for a CIL object file, err_drv_msil_no_object_output.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-LNX --implicit-check-not=error: --implicit-check-not=-cc1as \
// RUN:     --implicit-check-not=msil-ld %s
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -fno-integrated-as -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-LNX --implicit-check-not=error: --implicit-check-not=-cc1as \
// RUN:     --implicit-check-not='"as"' --implicit-check-not=msil-ld %s
// OBJ-LNX: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil"
// OBJ-LNX-SAME: "-emit-llvm-bc"
// OBJ-LNX-SAME: "-flto=full"
// OBJ-LNX-SAME: "-o" "{{[^"]*}}.o"
// RUN: %clang -### --target=aarch64-unknown-linux-gnu-msil -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-A64 --implicit-check-not=error: --implicit-check-not=-cc1as %s
// OBJ-A64: "-cc1" "-triple" "aarch64-unknown-linux-gnu-msil"
// OBJ-A64-SAME: "-emit-llvm-bc"
// OBJ-A64-SAME: "-flto=full"
// RUN: %clang -### --target=x86_64-w64-windows-gnu-msil -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-MINGW --implicit-check-not=error: --implicit-check-not=-cc1as %s
// RUN: %clang -### --target=x86_64-w64-windows-gnu-msil -fno-integrated-as -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-MINGW --implicit-check-not=error: --implicit-check-not=-cc1as \
// RUN:     --implicit-check-not='"as"' %s
// OBJ-MINGW: "-cc1" "-triple" "x86_64-w64-windows-gnu-msil"
// OBJ-MINGW-SAME: "-emit-llvm-bc"
// OBJ-MINGW-SAME: "-flto=full"
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -fms-compatibility-version=19.40 -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-WIN --implicit-check-not=error: --implicit-check-not=-cc1as %s
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -fms-compatibility-version=19.40 -fno-integrated-as \
// RUN:   -c %s -o %t.o 2>&1 | FileCheck --check-prefix=OBJ-WIN --implicit-check-not=error: \
// RUN:     --implicit-check-not=-cc1as %s
// RUN: %clang_cl -### --target=x86_64-pc-windows-msvc-msil -fms-compatibility-version=19.40 /c -- %s 2>&1 \
// RUN:   | FileCheck --check-prefix=OBJ-WIN --implicit-check-not=error: --implicit-check-not=-cc1as %s
// OBJ-WIN: "-cc1" "-triple" "x86_64-pc-windows-msvc19.40.0-msil"
// OBJ-WIN-SAME: "-emit-llvm-bc"
// OBJ-WIN-SAME: "-flto=full"

// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -save-temps=obj -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=SAVE --implicit-check-not=error: --implicit-check-not=-cc1as %s
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -fno-integrated-as -save-temps=obj -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=SAVE --implicit-check-not=error: --implicit-check-not=-cc1as \
// RUN:     --implicit-check-not='"as"' %s
// SAVE: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil" {{.*}}"-E"
// SAVE: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil" {{.*}}"-emit-llvm-bc"{{.*}} "-flto=full"

// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil %s -o %t.dll 2>&1 \
// RUN:   | FileCheck --check-prefix=LINK --implicit-check-not=error: --implicit-check-not='"ld"' %s
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -fno-integrated-as %s -o %t.dll 2>&1 \
// RUN:   | FileCheck --check-prefix=LINK --implicit-check-not=error: --implicit-check-not='"ld"' \
// RUN:     --implicit-check-not='"as"' %s
// LINK: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil"
// LINK-SAME: "-emit-llvm-bc"
// LINK-SAME: "-flto=full"
// LINK: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-unknown-linux-gnu-msil"
// LINK-SAME: "-o" "{{[^"]*}}.dll"

// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -c -x assembler %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=ILASM --implicit-check-not="PLEASE submit" %s
// ILASM: error: assembling ILAsm text is not supported for MSIL target 'x86_64-unknown-linux-gnu-msil'; compile C or LLVM IR sources instead, or use -S to write ILAsm text
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -fno-integrated-as -c -x assembler %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=NO-AS --implicit-check-not="PLEASE submit" --implicit-check-not='"as"' %s
// NO-AS: error: there is no external assembler that can be used on this platform

// The options that keep the assemble step from being collapsed (clang-cl /FA
// and /Fa<file>, -via-file-asm) also turn the bitcode default off when no
// -flto, -flto= or -fno-lto is given, as -S and -emit-llvm do (5.10.5's
// getLTOMode rule, section 12 row of 2026-10-22): the assemble step stays and
// reports err_drv_msil_ilasm_input, and no bitcode job is planned.
// DEFINE: %{no_bc} = --implicit-check-not="PLEASE submit" --implicit-check-not=-emit-llvm-bc
// RUN: not %clang_cl -### --target=x86_64-pc-windows-msvc-msil /FA /c -- %s 2>&1 \
// RUN:   | FileCheck --check-prefix=ILASM-WIN %{no_bc} %s
// RUN: not %clang_cl -### --target=x86_64-pc-windows-msvc-msil /Fa%t.asm /c -- %s 2>&1 \
// RUN:   | FileCheck --check-prefix=ILASM-WIN %{no_bc} %s
// RUN: not %clang -### --target=x86_64-pc-windows-msvc-msil -via-file-asm -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=ILASM-WIN %{no_bc} %s
// ILASM-WIN: error: assembling ILAsm text is not supported for MSIL target 'x86_64-pc-windows-msvc{{[0-9.]*}}-msil'; compile C or LLVM IR sources instead, or use -S to write ILAsm text

// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -fno-lto -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=NO-LTO %s
// NO-LTO: error: CIL object files are not supported for MSIL target 'x86_64-unknown-linux-gnu-msil' yet; use -flto to write LLVM bitcode objects, or -S to write ILAsm text
// RUN: not %clang -### --target=x86_64-pc-windows-msvc-msil -fno-lto -c %s -o %t.o 2>&1 \
// RUN:   | FileCheck --check-prefix=NO-LTO-WIN %s
// NO-LTO-WIN: error: CIL object files are not supported for MSIL target 'x86_64-pc-windows-msvc{{[0-9.]*}}-msil' yet;

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

// The clang driver accepts basic block sections, late function splitting,
// the basic block address map and static data partitioning only for ELF
// (and the address map also for x86-64 COFF); the object format of every
// MSIL triple is MSIL, so each of them is an 'unsupported option' error for
// the MSIL triples, as for any other non-ELF target. These lines guard that
// upstream driver behaviour, which MSIL relies on to tell the user; what
// reaches the backend through -Xclang or -mllvm is ignored there (decision D8;
// llvm/test/CodeGen/MSIL/basic-block-sections.ll, function-splitting.ll and
// clang/test/CodeGen/MSIL/split-machine-functions.c).
// -fno-split-machine-functions and -fbasic-block-sections=none are accepted
// and not passed on. -fbasic-block-sections=labels is the exception: the
// driver turns it into -fbasic-block-address-map with a deprecation warning
// and no target check, and the MSIL backend then ignores the map with a
// warning (llvm/test/CodeGen/MSIL/stack-protector-warning.ll).
// -fno-basic-block-address-map is not pinned: upstream rejects it for these
// targets too.

// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -S -fsplit-machine-functions %s 2>&1 \
// RUN:   | FileCheck --check-prefix=SPLIT -DT=x86_64-unknown-linux-gnu-msil %s
// RUN: not %clang -### --target=x86_64-w64-windows-gnu-msil -S -fsplit-machine-functions %s 2>&1 \
// RUN:   | FileCheck --check-prefix=SPLIT -DT=x86_64-w64-windows-gnu-msil %s
// RUN: not %clang -### --target=aarch64-unknown-linux-gnu-msil -S -fsplit-machine-functions %s 2>&1 \
// RUN:   | FileCheck --check-prefix=SPLIT -DT=aarch64-unknown-linux-gnu-msil %s
// RUN: not %clang -### --target=x86_64-pc-windows-msvc-msil -fms-compatibility-version=19.40 -S \
// RUN:   -fsplit-machine-functions %s 2>&1 \
// RUN:   | FileCheck --check-prefix=SPLIT -DT=x86_64-pc-windows-msvc19.40.0-msil %s
// SPLIT: error: unsupported option '-fsplit-machine-functions' for target '[[T]]'

// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -S -fsplit-machine-functions \
// RUN:   -fno-split-machine-functions %s 2>&1 \
// RUN:   | FileCheck --check-prefix=NOSPLIT --implicit-check-not=error: --implicit-check-not=split-machine %s
// RUN: %clang -### --target=aarch64-unknown-linux-gnu-msil -S -fsplit-machine-functions \
// RUN:   -fno-split-machine-functions %s 2>&1 \
// RUN:   | FileCheck --check-prefix=NOSPLIT --implicit-check-not=error: --implicit-check-not=split-machine %s
// NOSPLIT: "-cc1"

// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -S -fbasic-block-sections=all %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BBS-ALL -DT=x86_64-unknown-linux-gnu-msil %s
// RUN: not %clang -### --target=aarch64-unknown-linux-gnu-msil -S -fbasic-block-sections=all %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BBS-ALL -DT=aarch64-unknown-linux-gnu-msil %s
// BBS-ALL: error: unsupported option '-fbasic-block-sections=all' for target '[[T]]'

// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -S -fbasic-block-sections=list=%s %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BBS-LIST -DT=x86_64-unknown-linux-gnu-msil %s
// RUN: not %clang -### --target=aarch64-unknown-linux-gnu-msil -S -fbasic-block-sections=list=%s %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BBS-LIST -DT=aarch64-unknown-linux-gnu-msil %s
// BBS-LIST: error: unsupported option '-fbasic-block-sections=list={{.*}}msil-toolchain.c' for target '[[T]]'

// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -S -fbasic-block-sections=none %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BBS-NONE --implicit-check-not=error: --implicit-check-not=basic-block %s
// RUN: %clang -### --target=aarch64-unknown-linux-gnu-msil -S -fbasic-block-sections=none %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BBS-NONE --implicit-check-not=error: --implicit-check-not=basic-block %s
// BBS-NONE: "-cc1"

// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -S -fbasic-block-sections=labels %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BBS-LABELS --implicit-check-not=error: %s
// BBS-LABELS: warning: argument '-fbasic-block-sections=labels' is deprecated, use '-fbasic-block-address-map' instead
// BBS-LABELS: "-fbasic-block-address-map"

// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -S -fbasic-block-address-map %s 2>&1 \
// RUN:   | FileCheck --check-prefix=MAP -DT=x86_64-unknown-linux-gnu-msil %s
// RUN: not %clang -### --target=x86_64-w64-windows-gnu-msil -S -fbasic-block-address-map %s 2>&1 \
// RUN:   | FileCheck --check-prefix=MAP -DT=x86_64-w64-windows-gnu-msil %s
// RUN: not %clang -### --target=x86_64-pc-windows-msvc-msil -fms-compatibility-version=19.40 -S \
// RUN:   -fbasic-block-address-map %s 2>&1 \
// RUN:   | FileCheck --check-prefix=MAP -DT=x86_64-pc-windows-msvc19.40.0-msil %s
// RUN: not %clang -### --target=aarch64-unknown-linux-gnu-msil -S -fbasic-block-address-map %s 2>&1 \
// RUN:   | FileCheck --check-prefix=MAP -DT=aarch64-unknown-linux-gnu-msil %s
// MAP: error: unsupported option '-fbasic-block-address-map' for target '[[T]]'

// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -S -fpartition-static-data-sections %s 2>&1 \
// RUN:   | FileCheck --check-prefix=PSDS -DT=x86_64-unknown-linux-gnu-msil %s
// PSDS: error: unsupported option '-fpartition-static-data-sections' for target '[[T]]'

int main(void) { return 0; }

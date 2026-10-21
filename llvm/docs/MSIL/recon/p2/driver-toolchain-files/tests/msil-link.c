// Proposed clang/test/Driver/msil-link.c (recon driver-toolchain; P2-B).
// Linking for '<native triple>-msil' targets: the MSIL toolchain keeps the native
// toolchain's sysroot and library directories and replaces its linker with
// msil-ld: no crt files, no dynamic linker, no PIE, no gold plugin; the C library
// of the triple by default (Design.md 5.2, 6); a program is 'a.exe' by default.
// Until P2-A, only LTO links can run (objects are bitcode), so every link here
// uses -flto; after P2-A the same lines hold without it.

// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto \
// RUN:   --sysroot=%S/Inputs/debian_multiarch_tree %s 2>&1 \
// RUN:   | FileCheck --check-prefix=LNX --implicit-check-not=crt1.o --implicit-check-not=crti.o \
// RUN:     --implicit-check-not=crtbegin --implicit-check-not=crtn.o --implicit-check-not=-dynamic-linker \
// RUN:     --implicit-check-not='"-pie"' --implicit-check-not=-plugin --implicit-check-not=LLVMgold \
// RUN:     --implicit-check-not=ld.lld --implicit-check-not='"ld"' --implicit-check-not=error: %s
// LNX: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-unknown-linux-gnu-msil"
// LNX-SAME: "--sysroot=[[SYSROOT:[^"]+]]"
// LNX-SAME: "-L[[SYSROOT]]/usr/lib/x86_64-linux-gnu"
// LNX-SAME: "{{[^"]*}}.o"
// LNX-SAME: "-lc" "-lm" "-o" "a.exe"

// musl keeps libm in libc.
// RUN: %clang -### --target=x86_64-unknown-linux-musl-msil -flto %s 2>&1 \
// RUN:   | FileCheck --check-prefix=MUSL --implicit-check-not=-lm --implicit-check-not=error: %s
// MUSL: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-unknown-linux-musl-msil"
// MUSL-SAME: "-lc" "-o" "a.exe"

// RUN: %clang -### --target=aarch64-unknown-linux-gnu-msil -flto %s 2>&1 \
// RUN:   | FileCheck --check-prefix=A64 --implicit-check-not=error: %s
// A64: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=aarch64-unknown-linux-gnu-msil"
// A64-SAME: "-lc" "-lm" "-o" "a.exe"

// windows-msvc: msil-ld does not read %LIB%; the driver passes the UCRT and SDK
// library directories (or %LIB%'s entries) and links ucrt.lib and kernel32.lib.
// No 'LTO requires -fuse-ld=lld' (err_drv_lto_without_lld) and no link.exe.
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -flto -Xmicrosoft-windows-sys-root %t %s 2>&1 \
// RUN:   | FileCheck --check-prefix=WIN --implicit-check-not=link.exe --implicit-check-not=lld-link \
// RUN:     --implicit-check-not=-defaultlib --implicit-check-not=error: %s
// WIN: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-pc-windows-msvc-msil"
// WIN-SAME: "-L[[ROOT:[^"]*]]{{/|\\\\}}VC{{/|\\\\}}Tools{{/|\\\\}}MSVC{{/|\\\\}}27.1828.18284{{/|\\\\}}lib{{/|\\\\}}x64"
// WIN-SAME: "-L[[ROOT]]{{/|\\\\}}Windows Kits{{/|\\\\}}10{{/|\\\\}}Lib{{/|\\\\}}10.0.19041.0{{/|\\\\}}ucrt{{/|\\\\}}x64"
// WIN-SAME: "-L[[ROOT]]{{/|\\\\}}Windows Kits{{/|\\\\}}10{{/|\\\\}}Lib{{/|\\\\}}10.0.19041.0{{/|\\\\}}um{{/|\\\\}}x64"
// WIN-SAME: "-lucrt" "-lkernel32" "-o" "a.exe"
// RUN: %clang_cl -### --target=x86_64-pc-windows-msvc-msil -flto /winsysroot %t /Fe:prog.exe -- %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CL --implicit-check-not=link.exe --implicit-check-not=error: %s
// CL: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-pc-windows-msvc-msil"
// CL-SAME: "{{[^"]*}}.obj"
// CL-SAME: "-lucrt" "-lkernel32" "-o" "prog.exe"

// MinGW: msvcrt and the system libraries native MinGW programs get; a C runtime
// the user names (-lucrt, -lmsvcr*) replaces msvcrt.
// RUN: %clang -### --target=x86_64-w64-windows-gnu-msil -flto %s 2>&1 \
// RUN:   | FileCheck --check-prefix=MINGW --implicit-check-not=crt2.o --implicit-check-not=-lmingw32 \
// RUN:     --implicit-check-not=-lgcc --implicit-check-not=error: %s
// MINGW: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-w64-windows-gnu-msil"
// MINGW-SAME: "-lmsvcrt" "-ladvapi32" "-lshell32" "-luser32" "-lkernel32" "-o" "a.exe"
// RUN: %clang -### --target=x86_64-w64-windows-gnu-msil -flto %s -lucrt 2>&1 \
// RUN:   | FileCheck --check-prefix=MINGW-UCRT --implicit-check-not=-lmsvcrt %s
// MINGW-UCRT: "-lucrt" "-ladvapi32" "-lshell32" "-luser32" "-lkernel32"

// -nostdlib and -nodefaultlibs drop the default libraries; -pthread adds
// libpthread on Linux.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -nostdlib %s -o p.exe 2>&1 \
// RUN:   | FileCheck --check-prefix=NOSTD --implicit-check-not=-lc --implicit-check-not=-lm %s
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -flto -nodefaultlibs %s -o p.exe 2>&1 \
// RUN:   | FileCheck --check-prefix=NOSTD --implicit-check-not=-lucrt --implicit-check-not=-lkernel32 %s
// NOSTD: "{{[^"]*}}msil-ld{{(\.exe)?}}"
// NOSTD-SAME: "-o" "p.exe"
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -pthread %s 2>&1 \
// RUN:   | FileCheck --check-prefix=PTHREAD %s
// PTHREAD: "-lpthread" "-lc" "-lm"

// -shared builds a library assembly; linker options, -L, -l, managed and native
// .dll inputs and object files reach msil-ld in command-line order; references
// are linker options (bcl-binding recon) and the export class is -Wl.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -shared %s %t/b.o %t/lib.dll \
// RUN:   -L%t -lfoo -Wl,--export-class=Contoso.Zlib.Native -Wl,--reference=%t/refs -e start -u sym \
// RUN:   -o zlib.dll 2>&1 | FileCheck --check-prefix=SHARED --implicit-check-not=error: %s
// SHARED: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-unknown-linux-gnu-msil" "--shared"
// SHARED-SAME: "-L{{[^"]*}}" "-u" "sym"
// SHARED-SAME: "{{[^"]*}}.o" "{{[^"]*}}b.o" "{{[^"]*}}lib.dll" "-lfoo"
// SHARED-SAME: "--export-class=Contoso.Zlib.Native" "--reference={{[^"]*}}refs" "-e" "start"
// SHARED-SAME: "-lc" "-lm" "-o" "zlib.dll"

// -fuse-ld: msil-ld is the only linker. 'lld' and 'ld' name it, an absolute
// path or --ld-path= is used as is, anything else is an error.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -fuse-ld=lld %s 2>&1 \
// RUN:   | FileCheck --check-prefix=LD --implicit-check-not=ld.lld --implicit-check-not=error: %s
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -flto -fuse-ld=lld %s 2>&1 \
// RUN:   | FileCheck --check-prefix=LD --implicit-check-not=lld-link --implicit-check-not=error: %s
// LD: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -fuse-ld=bfd %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BFD %s
// BFD: error: invalid linker name in argument '-fuse-ld=bfd'

// Unsupported: a static C library (no MSIL triple has one yet), sanitizers
// with a native runtime.
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -static %s 2>&1 \
// RUN:   | FileCheck --check-prefix=STATIC %s
// STATIC: error: unsupported option '-static' for target 'x86_64-unknown-linux-gnu-msil'
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -fsanitize=address %s 2>&1 \
// RUN:   | FileCheck --check-prefix=ASAN %s
// ASAN: error: unsupported option '-fsanitize=address' for target 'x86_64-unknown-linux-gnu-msil'
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -fsanitize=undefined \
// RUN:   -fsanitize-trap=undefined %s 2>&1 | FileCheck --check-prefix=UBSAN-TRAP --implicit-check-not=error: %s
// UBSAN-TRAP: "-fsanitize-trap=

// -pie, -no-pie and -rdynamic mean nothing for an assembly and are accepted.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -pie -rdynamic %s 2>&1 \
// RUN:   | FileCheck --check-prefix=PIE --implicit-check-not=warning: --implicit-check-not='"-pie"' \
// RUN:     --implicit-check-not=-export-dynamic %s
// PIE: "{{[^"]*}}msil-ld{{(\.exe)?}}"

// --emit-static-lib archives with llvm-ar on every MSIL triple (the native MSVC
// toolchain has no static library tool).
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -flto --emit-static-lib %s -o libx.a 2>&1 \
// RUN:   | FileCheck --check-prefix=AR --implicit-check-not=error: %s
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto --emit-static-lib %s -o libx.a 2>&1 \
// RUN:   | FileCheck --check-prefix=AR --implicit-check-not=error: %s
// AR: "{{[^"]*}}llvm-ar{{(\.exe)?}}" "rcsD" "libx.a"

//--- lib.dll
//--- b.o
//--- VC/Tools/MSVC/27.1828.18284/include/vcruntime.h
//--- Windows Kits/10/Include/10.0.19041.0/ucrt/stdio.h

int main(void) { return 0; }

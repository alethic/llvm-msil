// Linking for '<native triple>-msil' targets (ImplementationPlan.md 5.10.1,
// 5.10.3-5.10.5; recon p2/driver-toolchain.md 4 and 6). The MSIL toolchain
// keeps the native toolchain's sysroot and library directories and replaces
// its linker with msil-ld: no crt files, no dynamic linker, no PIE, no gold
// plugin, never ld.lld, lld-link or link.exe; the C library of the triple by
// default (Design.md 5.2 and 6; 5.10.4); a program is 'a.exe' by default on
// every MSIL triple, and -o is never given an extension. Until M20 a
// compile-and-link compiles to bitcode (5.10.5), so every link here is an LTO
// link through msil-ld; the checked parts of the link lines do not depend on
// that. The driver never passes
// --lto-emit-asm and never runs ilasm (5.9.13). msil-ld is matched as
// '"<dir>msil-ld[.exe]"': no msil-ld binary, SDK or sysroot is needed.

// RUN: rm -rf %t && split-file %s %t

// No job at all: no msil-ld, ld or ld.lld line (a job line starts with
// ' "'), no crt*.o, no -pie, and one error.
// DEFINE: %{no_link_job} = --implicit-check-not='PLEASE submit' --implicit-check-not=error: \
// DEFINE:   --implicit-check-not=msil-ld --implicit-check-not=ld.lld --implicit-check-not='ld"' \
// DEFINE:   --implicit-check-not='{{crt[0-9a-zA-Z]*\.o}}' --implicit-check-not='"-pie"' \
// DEFINE:   --implicit-check-not='{{^ "}}'

// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil \
// RUN:   --sysroot=%S/Inputs/debian_multiarch_tree %s 2>&1 \
// RUN:   | FileCheck --check-prefix=LNX --implicit-check-not=crt1.o --implicit-check-not=crti.o \
// RUN:     --implicit-check-not=crtbegin --implicit-check-not=crtn.o --implicit-check-not=-dynamic-linker \
// RUN:     --implicit-check-not='"-pie"' --implicit-check-not=-plugin --implicit-check-not=LLVMgold \
// RUN:     --implicit-check-not=ld.lld --implicit-check-not='"ld"' --implicit-check-not=error: \
// RUN:     --implicit-check-not=--lto-emit-asm --implicit-check-not=ilasm %s
// LNX: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-unknown-linux-gnu-msil"
// LNX-SAME: "--sysroot=[[SYSROOT:[^"]+]]"
// LNX-SAME: "-L[[SYSROOT]]/usr/lib/x86_64-linux-gnu"
// LNX-SAME: "{{[^"]*}}.o"
// LNX-SAME: "-lc" "-lm" "-o" "a.exe"

// musl keeps libm in libc.
// RUN: %clang -### --target=x86_64-unknown-linux-musl-msil %s 2>&1 \
// RUN:   | FileCheck --check-prefix=MUSL --implicit-check-not=-lm --implicit-check-not=error: %s
// MUSL: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-unknown-linux-musl-msil"
// MUSL-SAME: "-lc" "-o" "a.exe"

// RUN: %clang -### --target=aarch64-unknown-linux-gnu-msil %s 2>&1 \
// RUN:   | FileCheck --check-prefix=A64 --implicit-check-not=error: %s
// A64: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=aarch64-unknown-linux-gnu-msil"
// A64-SAME: "-lc" "-lm" "-o" "a.exe"

// windows-msvc: msil-ld is a GNU-style flavor and does not read %LIB%, so the
// driver passes the detected VC, UCRT and SDK library directories (here from
// a /winsysroot tree) and the import libraries ucrt, kernel32 and oldnames;
// --ucrtbase-imports follows in M15. The target triple has no MSVC version.
// No 'LTO requires -fuse-ld=lld' (err_drv_lto_without_lld), no link.exe, no
// /DEFAULTLIB, never msvcrt.lib, libcmt.lib or vcruntime.lib.
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -Xmicrosoft-windows-sys-root %t %s 2>&1 \
// RUN:   | FileCheck --check-prefix=WIN --implicit-check-not=link.exe --implicit-check-not=lld-link \
// RUN:     --implicit-check-not=-defaultlib --implicit-check-not=-lmsvcrt --implicit-check-not=-llibcmt \
// RUN:     --implicit-check-not=-lvcruntime --implicit-check-not=--ucrtbase-imports \
// RUN:     --implicit-check-not=error: %s
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -flto -Xmicrosoft-windows-sys-root %t %s 2>&1 \
// RUN:   | FileCheck --check-prefix=WIN --implicit-check-not=link.exe --implicit-check-not=lld-link \
// RUN:     --implicit-check-not=error: %s
// WIN: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-pc-windows-msvc-msil"
// WIN-SAME: "-L[[ROOT:[^"]*]]{{/|\\\\}}VC{{/|\\\\}}Tools{{/|\\\\}}MSVC{{/|\\\\}}27.1828.18284{{/|\\\\}}lib{{/|\\\\}}x64"
// WIN-SAME: "-L[[ROOT]]{{/|\\\\}}Windows Kits{{/|\\\\}}10{{/|\\\\}}Lib{{/|\\\\}}10.0.19041.0{{/|\\\\}}ucrt{{/|\\\\}}x64"
// WIN-SAME: "-L[[ROOT]]{{/|\\\\}}Windows Kits{{/|\\\\}}10{{/|\\\\}}Lib{{/|\\\\}}10.0.19041.0{{/|\\\\}}um{{/|\\\\}}x64"
// WIN-SAME: "-lucrt" "-lkernel32" "-loldnames" "-o" "a.exe"

// With LIB set and no /winsysroot-style option, its entries are the library
// directories.
// RUN: mkdir -p %t/libenv
// RUN: env LIB=%t/libenv %clang -### --target=x86_64-pc-windows-msvc-msil %s 2>&1 \
// RUN:   | FileCheck --check-prefix=WIN-LIB --implicit-check-not=error: %s
// WIN-LIB: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-pc-windows-msvc-msil"
// WIN-LIB-SAME: "-L{{[^"]*}}libenv"
// WIN-LIB-SAME: "-lucrt" "-lkernel32" "-loldnames" "-o" "a.exe"

// clang-cl: /Fe names the output, inputs compile to bitcode '.obj' files.
// RUN: %clang_cl -### --target=x86_64-pc-windows-msvc-msil /winsysroot %t /Fe:prog.exe -- %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CL --implicit-check-not=link.exe --implicit-check-not=error: %s
// CL: "-cc1" "-triple" "x86_64-pc-windows-msvc{{[0-9.]*}}-msil"
// CL-SAME: "-emit-llvm-bc"
// CL: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-pc-windows-msvc-msil"
// CL-SAME: "{{[^"]*}}.obj"
// CL-SAME: "-lucrt" "-lkernel32" "-loldnames" "-o" "prog.exe"

// MinGW: msvcrt and the system libraries native MinGW programs get; a C
// runtime the user names (-lucrt, -lmsvcr*, -lcrtdll) replaces msvcrt in
// place (MinGW.cpp's rule).
// RUN: %clang -### --target=x86_64-w64-windows-gnu-msil %s 2>&1 \
// RUN:   | FileCheck --check-prefix=MINGW --implicit-check-not=crt2.o --implicit-check-not=-lmingw32 \
// RUN:     --implicit-check-not=-lgcc --implicit-check-not=-lmingwex --implicit-check-not=error: %s
// MINGW: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-w64-windows-gnu-msil"
// MINGW-SAME: "-lmsvcrt" "-ladvapi32" "-lshell32" "-luser32" "-lkernel32" "-o" "a.exe"
// RUN: %clang -### --target=x86_64-w64-windows-gnu-msil %s -lucrt 2>&1 \
// RUN:   | FileCheck --check-prefix=MINGW-UCRT --implicit-check-not=-lmsvcrt %s
// MINGW-UCRT: "-lucrt" "-ladvapi32" "-lshell32" "-luser32" "-lkernel32" "-o" "a.exe"
// RUN: %clang -### --target=x86_64-w64-windows-gnu-msil %s -lmsvcr120 2>&1 \
// RUN:   | FileCheck --check-prefix=MINGW-MSVCR --implicit-check-not=-lmsvcrt %s
// MINGW-MSVCR: "-lmsvcr120" "-ladvapi32" "-lshell32" "-luser32" "-lkernel32" "-o" "a.exe"

// -nostdlib and -nodefaultlibs drop the default libraries; -pthread adds
// libpthread on Linux (on MinGW: below).
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -nostdlib %s -o p.exe 2>&1 \
// RUN:   | FileCheck --check-prefix=NOSTD --implicit-check-not=-lc --implicit-check-not=-lm %s
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -nodefaultlibs %s -o p.exe 2>&1 \
// RUN:   | FileCheck --check-prefix=NOSTD --implicit-check-not=-lc --implicit-check-not=-lm %s
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -nodefaultlibs %s -o p.exe 2>&1 \
// RUN:   | FileCheck --check-prefix=NOSTD --implicit-check-not=-lucrt --implicit-check-not=-lkernel32 \
// RUN:     --implicit-check-not=-loldnames %s
// RUN: %clang -### --target=x86_64-w64-windows-gnu-msil -nostdlib %s -o p.exe 2>&1 \
// RUN:   | FileCheck --check-prefix=NOSTD --implicit-check-not=-lmsvcrt --implicit-check-not=-lkernel32 %s
// NOSTD: "{{[^"]*}}msil-ld{{(\.exe)?}}"
// NOSTD-SAME: "-o" "p.exe"
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -pthread %s 2>&1 \
// RUN:   | FileCheck --check-prefix=PTHREAD %s
// PTHREAD: "-lpthread" "-lc" "-lm"

// -nolibc is claimed on the Linux and MinGW MSIL triples and drops the C
// library and the system libraries that go with it, as the native GNU and
// MinGW link jobs do (5.10.3 step 7, section 12 row '5.10.3 step 7
// amended'): -lc and -lm on Linux; msvcrt, advapi32, shell32, user32 and
// kernel32 on MinGW. -lpthread for -pthread stays. No 'argument unused
// during compilation'.
// DEFINE: %{nolibc} = --implicit-check-not='argument unused' --implicit-check-not=error:
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil %t/b.o -nolibc 2>&1 \
// RUN:   | FileCheck --check-prefix=NOLIBC %{nolibc} --implicit-check-not=-lc --implicit-check-not=-lm %s
// RUN: %clang -### --target=x86_64-unknown-linux-musl-msil %t/b.o -nolibc 2>&1 \
// RUN:   | FileCheck --check-prefix=NOLIBC %{nolibc} --implicit-check-not=-lc --implicit-check-not=-lm %s
// RUN: %clang -### --target=x86_64-w64-windows-gnu-msil %t/b.o -nolibc 2>&1 \
// RUN:   | FileCheck --check-prefix=NOLIBC %{nolibc} --implicit-check-not=-lmsvcrt \
// RUN:     --implicit-check-not=-ladvapi32 --implicit-check-not=-lshell32 --implicit-check-not=-luser32 \
// RUN:     --implicit-check-not=-lkernel32 %s
// NOLIBC: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=
// NOLIBC-SAME: "{{[^"]*}}b.o"
// NOLIBC-SAME: "-o" "a.exe"
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil %t/b.o -pthread -nolibc 2>&1 \
// RUN:   | FileCheck --check-prefix=NOLIBC-PTHREAD %{nolibc} --implicit-check-not=-lc --implicit-check-not=-lm %s
// NOLIBC-PTHREAD: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-unknown-linux-gnu-msil"
// NOLIBC-PTHREAD-SAME: "{{[^"]*}}b.o" "-lpthread"
// NOLIBC-PTHREAD-SAME: "-o" "a.exe"

// MinGW: -pthread adds -lpthread (winpthreads) after the C runtime and before
// -ladvapi32, where the native MinGW link job puts it, and with -nolibc it is
// the only default library (5.10.3 step 7 and 5.10.4, section 12 row '5.10.3
// step 7 and 5.10.4's *-windows-gnu-msil row amended'). No 'argument unused
// during compilation'.
// DEFINE: %{mingw_pthread} = --implicit-check-not='argument unused during compilation' \
// DEFINE:   --implicit-check-not=error:
// RUN: %clang -### --target=x86_64-w64-windows-gnu-msil %t/b.o -pthread 2>&1 \
// RUN:   | FileCheck --check-prefix=MINGW-PTHREAD %{mingw_pthread} %s
// MINGW-PTHREAD: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-w64-windows-gnu-msil"
// MINGW-PTHREAD-SAME: "{{[^"]*}}b.o" "-lmsvcrt" "-lpthread" "-ladvapi32"
// MINGW-PTHREAD-SAME: "-o" "a.exe"
// RUN: %clang -### --target=x86_64-w64-windows-gnu-msil %t/b.o -pthread -nolibc 2>&1 \
// RUN:   | FileCheck --check-prefix=MINGW-NOLIBC-PTHREAD %{mingw_pthread} --implicit-check-not=-lmsvcrt \
// RUN:     --implicit-check-not=-ladvapi32 --implicit-check-not=-lshell32 --implicit-check-not=-luser32 \
// RUN:     --implicit-check-not=-lkernel32 %s
// MINGW-NOLIBC-PTHREAD: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-w64-windows-gnu-msil"
// MINGW-NOLIBC-PTHREAD-SAME: "{{[^"]*}}b.o" "-lpthread"
// MINGW-NOLIBC-PTHREAD-SAME: "-o" "a.exe"

// The output name is used as given, without an extension added.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil %s -o prog 2>&1 \
// RUN:   | FileCheck --check-prefix=NOEXT %s
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil %s -o prog 2>&1 \
// RUN:   | FileCheck --check-prefix=NOEXT %s
// NOEXT: "{{[^"]*}}msil-ld{{(\.exe)?}}"
// NOEXT-SAME: "-o" "prog"

// -shared becomes --shared (msil-ld rejects it until M22); the default name
// stays a.exe. Linker options, -L, -l, managed and native .dll inputs and
// object files reach msil-ld in command-line order; references and the
// export class are linker options (Q9, Design.md 5.4). -s becomes
// --strip-all.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -shared %s 2>&1 \
// RUN:   | FileCheck --check-prefix=SHARED-DEFAULT --implicit-check-not=error: %s
// SHARED-DEFAULT: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-unknown-linux-gnu-msil" "--shared"
// SHARED-DEFAULT-SAME: "-o" "a.exe"
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -shared %s %t/b.o %t/lib.dll \
// RUN:   -L%t -lfoo -Wl,--export-class=Contoso.Zlib.Native -Wl,--reference=%t/refs -e start -u sym \
// RUN:   -o zlib.dll 2>&1 | FileCheck --check-prefix=SHARED --implicit-check-not=error: %s
// SHARED: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-unknown-linux-gnu-msil" "--shared"
// SHARED-SAME: "-L{{[^"]*}}" "-u" "sym"
// SHARED-SAME: "{{[^"]*}}.o" "{{[^"]*}}b.o" "{{[^"]*}}lib.dll" "-lfoo"
// SHARED-SAME: "--export-class=Contoso.Zlib.Native" "--reference={{[^"]*}}refs" "-e" "start"
// SHARED-SAME: "-lc" "-lm" "-o" "zlib.dll"
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -s %s 2>&1 \
// RUN:   | FileCheck --check-prefix=STRIP --implicit-check-not=error: %s
// STRIP: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-pc-windows-msvc-msil"
// STRIP-SAME: "--strip-all"

// -fuse-ld: msil-ld is the only linker. 'lld', 'ld' and 'msil-ld' name it,
// --ld-path= and an absolute -fuse-ld path are used as given, anything else
// is an error; CLANG_DEFAULT_LINKER is ignored. The program is always plain
// msil-ld, never a triple-prefixed name (lld would pick the GNU flavor for
// '...-gnu-msil-ld').
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -fuse-ld=lld %s 2>&1 \
// RUN:   | FileCheck --check-prefix=LD --implicit-check-not=ld.lld --implicit-check-not=error: %s
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -fuse-ld=lld %s 2>&1 \
// RUN:   | FileCheck --check-prefix=LD --implicit-check-not=lld-link --implicit-check-not=error: %s
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -fuse-ld=ld %s 2>&1 \
// RUN:   | FileCheck --check-prefix=LD --implicit-check-not=error: %s
// RUN: %clang -### --target=x86_64-w64-windows-gnu-msil -fuse-ld=msil-ld %s 2>&1 \
// RUN:   | FileCheck --check-prefix=LD --implicit-check-not=error: %s
// LD: "{{([^"]*[/\\])?}}msil-ld{{(\.exe)?}}" "--target=
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil \
// RUN:   --ld-path=%S/Inputs/basic_freebsd_tree/usr/bin/ld.bfd %s 2>&1 \
// RUN:   | FileCheck --check-prefix=LDPATH --implicit-check-not=error: %s
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil \
// RUN:   -fuse-ld=%S/Inputs/basic_freebsd_tree/usr/bin/ld.bfd %s 2>&1 \
// RUN:   | FileCheck --check-prefix=LDPATH --implicit-check-not=error: %s
// LDPATH: "{{[^"]*}}basic_freebsd_tree{{/|\\\\}}usr{{/|\\\\}}bin{{/|\\\\}}ld.bfd" "--target=x86_64-unknown-linux-gnu-msil"
// --ld-path= wins over -fuse-ld=lld, in either order, and -fuse-ld= is still
// claimed, as upstream ToolChain::GetLinkerPath reads it first: no 'argument
// unused during compilation'.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil \
// RUN:   --ld-path=%S/Inputs/basic_freebsd_tree/usr/bin/ld.bfd -fuse-ld=lld %s 2>&1 \
// RUN:   | FileCheck --check-prefix=LDPATH --implicit-check-not=error: --implicit-check-not='argument unused' \
// RUN:     --implicit-check-not=msil-ld %s
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil \
// RUN:   -fuse-ld=lld --ld-path=%S/Inputs/basic_freebsd_tree/usr/bin/ld.bfd %s 2>&1 \
// RUN:   | FileCheck --check-prefix=LDPATH-WIN --implicit-check-not=error: --implicit-check-not='argument unused' \
// RUN:     --implicit-check-not=msil-ld --implicit-check-not=lld-link %s
// LDPATH-WIN: "{{[^"]*}}basic_freebsd_tree{{/|\\\\}}usr{{/|\\\\}}bin{{/|\\\\}}ld.bfd" "--target=x86_64-pc-windows-msvc-msil"
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -fuse-ld=bfd %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD-LD -DLD=bfd %s
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -fuse-ld=gold %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD-LD -DLD=gold %s
// RUN: not %clang -### --target=x86_64-pc-windows-msvc-msil -fuse-ld=lld-link %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD-LD -DLD=lld-link %s
// BAD-LD: error: invalid linker name in argument '-fuse-ld=[[LD]]'

// Unsupported: a static C library (no MSIL triple has one), and sanitizers
// with a native runtime; UBSan in trap mode is fine.
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -static %s 2>&1 \
// RUN:   | FileCheck --check-prefix=STATIC %s
// STATIC: error: unsupported option '-static' for target 'x86_64-unknown-linux-gnu-msil'
// RUN: not %clang -### --target=x86_64-unknown-linux-gnu-msil -fsanitize=address %s 2>&1 \
// RUN:   | FileCheck --check-prefix=ASAN %s
// ASAN: error: unsupported option '-fsanitize=address' for target 'x86_64-unknown-linux-gnu-msil'
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -fsanitize=undefined \
// RUN:   -fsanitize-trap=undefined %s 2>&1 | FileCheck --check-prefix=UBSAN-TRAP --implicit-check-not=error: %s
// UBSAN-TRAP: "-fsanitize-trap=

// An MSIL triple that D6 rejects (Darwin, FreeBSD, Cygwin, ...) never gets a
// native toolchain's linker either (5.10.1, section 12 row '5.10.1 amended'):
// a link-only command (only an object file, so the frontend's rejection of
// the triple never runs) builds no job and ends with clang's unknown-triple
// error and exit code 1, never a native linker, crt files, -pie or a crash.
// RUN: not %clang -### --target=x86_64-apple-darwin-msil %t/b.o 2>&1 \
// RUN:   | FileCheck --check-prefix=DARWIN %{no_link_job} %s
// RUN: not %clang -### --target=x86_64-unknown-freebsd-msil %t/b.o 2>&1 \
// RUN:   | FileCheck --check-prefix=FREEBSD %{no_link_job} %s
// RUN: not %clang -### --target=x86_64-pc-windows-cygnus-msil %t/b.o 2>&1 \
// RUN:   | FileCheck --check-prefix=CYGNUS %{no_link_job} %s
// DARWIN: error: unknown target triple 'x86_64-apple-{{(darwin|macosx)[0-9.]*}}-msil'
// FREEBSD: error: unknown target triple 'x86_64-unknown-freebsd{{[0-9.]*}}-msil'
// CYGNUS: error: unknown target triple 'x86_64-pc-windows-cygnus-msil'

// -pie, -no-pie and -rdynamic mean nothing for an assembly and are claimed
// silently.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -pie -rdynamic %s 2>&1 \
// RUN:   | FileCheck --check-prefix=PIE --implicit-check-not=warning: --implicit-check-not='"-pie"' \
// RUN:     --implicit-check-not=-export-dynamic --implicit-check-not=error: %s
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -no-pie %s 2>&1 \
// RUN:   | FileCheck --check-prefix=PIE --implicit-check-not=warning: --implicit-check-not=-no-pie \
// RUN:     --implicit-check-not=error: %s
// PIE: "{{[^"]*}}msil-ld{{(\.exe)?}}"

// -nostartfiles (there are no crt files) on every MSIL triple, and
// -static-libgcc and -shared-libgcc (no libgcc is linked) where the native
// toolchain consumes them, are claimed silently (5.10.3 step 3, section 12
// row '5.10.3 step 3 amended'): no 'argument unused during compilation'.
// DEFINE: %{claimed} = --implicit-check-not='argument unused' --implicit-check-not=error: \
// DEFINE:   --implicit-check-not='{{crt[0-9a-zA-Z]*\.o}}'
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -nostartfiles %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CLAIMED %{claimed} %s
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -nostartfiles %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CLAIMED %{claimed} %s
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -static-libgcc %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CLAIMED %{claimed} --implicit-check-not=-lgcc %s
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -shared-libgcc %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CLAIMED %{claimed} --implicit-check-not=-lgcc %s
// RUN: %clang -### --target=x86_64-w64-windows-gnu-msil -static-libgcc %s 2>&1 \
// RUN:   | FileCheck --check-prefix=CLAIMED %{claimed} --implicit-check-not=-lgcc %s
// CLAIMED: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=
// CLAIMED-SAME: "-o" "a.exe"

// --emit-static-lib archives with llvm-ar on every MSIL triple (the native
// MSVC toolchain has no static library tool).
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil --emit-static-lib %s -o libx.a 2>&1 \
// RUN:   | FileCheck --check-prefix=AR --implicit-check-not=error: --implicit-check-not=msil-ld %s
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil --emit-static-lib %s -o libx.a 2>&1 \
// RUN:   | FileCheck --check-prefix=AR --implicit-check-not=error: --implicit-check-not=msil-ld %s
// AR: "{{[^"]*}}llvm-ar{{(\.exe)?}}" "rcsD" "libx.a"

// A path with non-ASCII characters reaches the msil-ld job line unchanged
// (5.10.3: M14's driver test covers a non-ASCII path). That is all a -###
// run shows: Driver::ExecuteCompilation prints the jobs and returns before
// it sets up response files, and Command::Execute alone writes them, so the
// UTF-8 encoding of msil-ld's response files (ResponseFileSupport::AtFileUTF8
// in ToolChains/MSIL.cpp) cannot be observed here. msil-ld's reading of UTF-8
// response files is covered by lld/test/MSIL/response-file.test.
// RUN: mkdir -p %t/café
// RUN: echo > %t/café/b.o
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil %s %t/café/b.o 2>&1 \
// RUN:   | FileCheck --check-prefix=UTF8 --implicit-check-not=error: %s
// UTF8: "{{[^"]*}}msil-ld{{(\.exe)?}}"
// UTF8-SAME: "{{[^"]*}}café{{/|\\\\}}b.o"

//--- lib.dll
//--- b.o
//--- VC/Tools/MSVC/27.1828.18284/include/vcruntime.h
//--- Windows Kits/10/Include/10.0.19041.0/ucrt/stdio.h

int main(void) { return 0; }

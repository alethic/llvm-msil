// Library links for '<native triple>-msil' targets (ImplementationPlan.md
// 5.10.3, M22.d item 1; Q19; recon p2/exports-multiasm.md 10.3): -shared,
// and clang-cl's /LD and /LDd, give msil-ld '--shared' right after
// '--target=<triple>' (5.10.3 step 3), which writes a library assembly (a
// DLL image with no entry point, M22). Naming stays upstream's: -shared
// without -o writes 'a.exe' (Q19, as every MSIL link without -o), -o is used
// as given, and '/LD foo.c' writes 'foo.dll'. Linker options such as the
// export class reach msil-ld in command-line order, and -nostdlib drops the
// default libraries as for a program. msil-ld is matched as
// '"<dir>msil-ld[.exe]"': no msil-ld binary, SDK or sysroot is needed.

// RUN: rm -rf %t && split-file %s %t

// DEFINE: %{none} = --implicit-check-not=error: --implicit-check-not='"-dll"' --implicit-check-not=lld-link \
// DEFINE:   --implicit-check-not=link.exe --implicit-check-not=--lto-emit-asm

// -shared on the four x86_64 MSIL triples, without -o and with -o lib.dll.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -shared %s 2>&1 \
// RUN:   | FileCheck --check-prefixes=SHARED,AEXE -DTRIPLE=x86_64-unknown-linux-gnu-msil %{none} %s
// RUN: %clang -### --target=x86_64-unknown-linux-musl-msil -shared %s 2>&1 \
// RUN:   | FileCheck --check-prefixes=SHARED,AEXE -DTRIPLE=x86_64-unknown-linux-musl-msil %{none} %s
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -shared %s 2>&1 \
// RUN:   | FileCheck --check-prefixes=SHARED,AEXE -DTRIPLE=x86_64-pc-windows-msvc-msil %{none} %s
// RUN: %clang -### --target=x86_64-w64-windows-gnu-msil -shared %s 2>&1 \
// RUN:   | FileCheck --check-prefixes=SHARED,AEXE -DTRIPLE=x86_64-w64-windows-gnu-msil %{none} %s
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -shared %s -o lib.dll 2>&1 \
// RUN:   | FileCheck --check-prefixes=SHARED,LIBDLL -DTRIPLE=x86_64-unknown-linux-gnu-msil %{none} %s
// RUN: %clang -### --target=x86_64-unknown-linux-musl-msil -shared %s -o lib.dll 2>&1 \
// RUN:   | FileCheck --check-prefixes=SHARED,LIBDLL -DTRIPLE=x86_64-unknown-linux-musl-msil %{none} %s
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -shared %s -o lib.dll 2>&1 \
// RUN:   | FileCheck --check-prefixes=SHARED,LIBDLL -DTRIPLE=x86_64-pc-windows-msvc-msil %{none} %s
// RUN: %clang -### --target=x86_64-w64-windows-gnu-msil -shared %s -o lib.dll 2>&1 \
// RUN:   | FileCheck --check-prefixes=SHARED,LIBDLL -DTRIPLE=x86_64-w64-windows-gnu-msil %{none} %s
// SHARED: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=[[TRIPLE]]" "--shared"
// AEXE-SAME: "-o" "a.exe"
// LIBDLL-SAME: "-o" "lib.dll"

// Without -shared there is no --shared.
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil %s 2>&1 \
// RUN:   | FileCheck --check-prefix=PROGRAM --implicit-check-not=--shared %{none} %s
// PROGRAM: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-pc-windows-msvc-msil"
// PROGRAM-SAME: "-o" "a.exe"

// clang-cl: /LD and /LDd compile CIL objects and link a library named after
// the first source file; /Fe still names it. /LD is used, so there is no
// 'argument unused during compilation'.
// RUN: cd %t && %clang_cl -### --target=x86_64-pc-windows-msvc-msil /winsysroot %t /LD -- foo.c 2>&1 \
// RUN:   | FileCheck --check-prefixes=CL,CL-FOO %{none} --implicit-check-not='argument unused' %s
// RUN: cd %t && %clang_cl -### --target=x86_64-pc-windows-msvc-msil /winsysroot %t /LDd -- foo.c 2>&1 \
// RUN:   | FileCheck --check-prefixes=CL,CL-FOO %{none} --implicit-check-not='argument unused' %s
// RUN: cd %t && %clang_cl -### --target=x86_64-pc-windows-msvc-msil /winsysroot %t /LD /Fe:zlib.dll -- foo.c 2>&1 \
// RUN:   | FileCheck --check-prefixes=CL,CL-FE %{none} --implicit-check-not='argument unused' %s
// CL: "-cc1" "-triple" "x86_64-pc-windows-msvc{{[0-9.]*}}-msil"
// CL-SAME: "-emit-obj"
// CL: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-pc-windows-msvc-msil" "--shared"
// CL-SAME: "{{[^"]*}}.obj"
// CL-FOO-SAME: "-o" "foo.dll"
// CL-FE-SAME: "-o" "zlib.dll"

// The export class is a linker option, passed in command-line order.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -shared %s -Wl,--export-class=NS.Name -o lib.dll 2>&1 \
// RUN:   | FileCheck --check-prefix=CLASS %{none} %s
// CLASS: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-unknown-linux-gnu-msil" "--shared"
// CLASS-SAME: "{{[^"]*}}.o" "--export-class=NS.Name"
// CLASS-SAME: "-o" "lib.dll"

// -shared -nostdlib: no default libraries.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -shared -nostdlib %s -o lib.dll 2>&1 \
// RUN:   | FileCheck --check-prefix=NOSTD -DTRIPLE=x86_64-unknown-linux-gnu-msil %{none} \
// RUN:     --implicit-check-not='"-lc"' --implicit-check-not='"-lm"' %s
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -shared -nostdlib %s -o lib.dll 2>&1 \
// RUN:   | FileCheck --check-prefix=NOSTD -DTRIPLE=x86_64-pc-windows-msvc-msil %{none} \
// RUN:     --implicit-check-not=-lucrt --implicit-check-not=-lkernel32 --implicit-check-not=-loldnames \
// RUN:     --implicit-check-not=--ucrtbase-imports %s
// RUN: %clang -### --target=x86_64-w64-windows-gnu-msil -shared -nostdlib %s -o lib.dll 2>&1 \
// RUN:   | FileCheck --check-prefix=NOSTD -DTRIPLE=x86_64-w64-windows-gnu-msil %{none} \
// RUN:     --implicit-check-not=-lmsvcrt --implicit-check-not=-lkernel32 %s
// NOSTD: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=[[TRIPLE]]" "--shared"
// NOSTD-SAME: "-o" "lib.dll"

//--- foo.c
int foo(void) { return 42; }

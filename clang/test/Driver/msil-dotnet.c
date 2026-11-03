// The portable targets' toolchain, DotNetToolChain (ImplementationPlan.md
// 5.18.4 PV1-PV6, milestone P-1a; recon p3/portable-target.md 3.2 and 6.1).
// msil64-unknown-dotnet and msil32-unknown-dotnet need no '-msil' suffix and
// have no native toolchain underneath:
// - The sysroot is --sysroot, else <InstalledDir>/../lib/clang-runtimes/
//   <normalized triple> (BareMetal's convention). Headers come in
//   WebAssembly's order: the resource directory's include, then
//   <sysroot>/include/<triple>, then <sysroot>/include; -nostdinc,
//   -nobuiltininc and -nostdlibinc are honoured.
// - The link runs msil-ld with -L<sysroot>/lib/<triple> only, then the
//   default libraries: -lc, then the helper archive
//   <resource>/lib/<triple>/libclang_rt.builtins.a when it exists (5.18.8;
//   -print-libgcc-file-name names it). Never -lm, -lpthread or -ldl (the math
//   library is in c.dll), no crt files, no native linker. -nostdlib and
//   -nodefaultlibs drop both, -nolibc drops -lc and keeps the helper archive
//   (the freestanding programs' link). A program is a.exe by default.
// - -shared gives --shared; -static is T-P9: "'-static' is not supported for
//   'msil64-unknown-dotnet' yet: the managed C library links only as the
//   shared assembly c.dll" (PV4).
// - -print-target-triple prints the normalized triple ('msil64-dotnet' and
//   'msil64-unknown-dotnet-msil' both name msil64-unknown-dotnet).
// - msil32-unknown-dotnet builds its compile and link jobs (msil-ld refuses
//   the link, T-P1).
// - PT5: an architecture other than msil with OS dotnet
//   (x86_64-unknown-dotnet), an msil architecture with another OS
//   (msil64-unknown-linux), with another object format
//   (msil64-unknown-dotnet-elf) or with an environment
//   (msil64-unknown-dotnet-gnu) are unknown target triples, for compiles
//   and for link-only invocations alike, which never plan a native linker.
//   Another vendor is accepted and treated as unknown: msil64-pc-dotnet
//   plans the jobs of msil64-unknown-dotnet.
// - A C++ source compiles as C++ and links with msil-ld like C (the
//   freestanding C++ of the P-1a exit criterion).
// - PV5: -fshort-wchar, -mlong-double-80, -mcpu= other than generic and
//   -march= are errors.
// msil-ld is matched as '"<dir>msil-ld[.exe]"': no msil-ld binary or sysroot
// is needed. The link checks pass -resource-dir=%t/res (with the msil64
// archive) or %t/empty-res (without one), so they do not depend on whether
// the build has made the archives.

// RUN: rm -rf %t && split-file %s %t

// DEFINE: %{nojunk} = --implicit-check-not=error: --implicit-check-not=-lm --implicit-check-not=-lpthread \
// DEFINE:   --implicit-check-not=-ldl --implicit-check-not='{{crt[0-9a-zA-Z]*\.o}}' \
// DEFINE:   --implicit-check-not=ld.lld --implicit-check-not='"ld"' --implicit-check-not=-dynamic-linker \
// DEFINE:   --implicit-check-not='"-pie"' --implicit-check-not='PLEASE submit'

// The default sysroot and the default libraries.
// RUN: %clang -### --target=msil64-unknown-dotnet -resource-dir=%t/res %s 2>&1 \
// RUN:   | FileCheck --check-prefixes=CC1,LINK %{nojunk} %s
// RUN: %clang -### --target=msil64-dotnet -resource-dir=%t/res %s 2>&1 \
// RUN:   | FileCheck --check-prefixes=CC1,LINK %{nojunk} %s
// RUN: %clang -### --target=msil64-unknown-dotnet-msil -resource-dir=%t/res %s 2>&1 \
// RUN:   | FileCheck --check-prefixes=CC1,LINK %{nojunk} %s
// CC1:       "-cc1" "-triple" "msil64-unknown-dotnet"
// LINK:      "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=msil64-unknown-dotnet"
// LINK-SAME: "-L{{[^"]*}}{{/|\\\\}}clang-runtimes{{/|\\\\}}msil64-unknown-dotnet{{/|\\\\}}lib{{/|\\\\}}msil64-unknown-dotnet"
// LINK-SAME: "{{[^"]*}}.o"
// LINK-SAME: "-lc" "{{[^"]*}}res{{/|\\\\}}lib{{/|\\\\}}msil64-unknown-dotnet{{/|\\\\}}libclang_rt.builtins.a" "-o" "a.exe"

// --sysroot.
// RUN: %clang -### --target=msil64-unknown-dotnet -resource-dir=%t/res --sysroot=%t/dotnet-sysroot %s 2>&1 \
// RUN:   | FileCheck --check-prefix=SYSROOT %{nojunk} --implicit-check-not=clang-runtimes %s
// SYSROOT:      "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=msil64-unknown-dotnet"
// SYSROOT-SAME: "-L{{[^"]*}}dotnet-sysroot{{/|\\\\}}lib{{/|\\\\}}msil64-unknown-dotnet"
// SYSROOT-SAME: "-lc" "{{[^"]*}}res{{/|\\\\}}lib{{/|\\\\}}msil64-unknown-dotnet{{/|\\\\}}libclang_rt.builtins.a" "-o" "a.exe"

// No helper archive in the resource directory: -lc alone.
// RUN: %clang -### --target=msil64-unknown-dotnet -resource-dir=%t/empty-res %s -o prog.exe 2>&1 \
// RUN:   | FileCheck --check-prefix=NORT %{nojunk} --implicit-check-not=clang_rt %s
// NORT:      "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=msil64-unknown-dotnet"
// NORT-SAME: "-lc" "-o" "prog.exe"

// -nostdlib and -nodefaultlibs drop both; -nolibc keeps the helper archive.
// RUN: %clang -### --target=msil64-unknown-dotnet -resource-dir=%t/res -nostdlib %t/b.o 2>&1 \
// RUN:   | FileCheck --check-prefix=NOSTD %{nojunk} --implicit-check-not=clang_rt --implicit-check-not=-lc %s
// RUN: %clang -### --target=msil64-unknown-dotnet -resource-dir=%t/res -nodefaultlibs %t/b.o 2>&1 \
// RUN:   | FileCheck --check-prefix=NOSTD %{nojunk} --implicit-check-not=clang_rt --implicit-check-not=-lc %s
// NOSTD:      "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=msil64-unknown-dotnet"
// NOSTD-SAME: "{{[^"]*}}b.o" "-o" "a.exe"
// RUN: %clang -### --target=msil64-unknown-dotnet -resource-dir=%t/res -nolibc %t/b.o 2>&1 \
// RUN:   | FileCheck --check-prefix=NOLIBC %{nojunk} --implicit-check-not=-lc \
// RUN:     --implicit-check-not='argument unused' %s
// NOLIBC:      "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=msil64-unknown-dotnet"
// NOLIBC-SAME: "{{[^"]*}}b.o" "{{[^"]*}}res{{/|\\\\}}lib{{/|\\\\}}msil64-unknown-dotnet{{/|\\\\}}libclang_rt.builtins.a" "-o" "a.exe"

// The helper archive's path.
// RUN: %clang --target=msil64-unknown-dotnet -print-libgcc-file-name -resource-dir=%t/res 2>&1 \
// RUN:   | FileCheck --check-prefix=RTLIB %s
// RTLIB: res{{/|\\}}lib{{/|\\}}msil64-unknown-dotnet{{/|\\}}libclang_rt.builtins.a

// -shared and -static.
// RUN: %clang -### --target=msil64-unknown-dotnet -resource-dir=%t/empty-res -shared %s -o lib.dll 2>&1 \
// RUN:   | FileCheck --check-prefix=SHARED %{nojunk} %s
// SHARED:      "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=msil64-unknown-dotnet"
// SHARED-SAME: "--shared"
// SHARED-SAME: "-o" "lib.dll"
// RUN: not %clang -### --target=msil64-unknown-dotnet -resource-dir=%t/empty-res -static %s 2>&1 \
// RUN:   | FileCheck --check-prefix=STATIC --implicit-check-not='PLEASE submit' %s
// RUN: not %clang -### --target=msil64-unknown-dotnet -resource-dir=%t/empty-res -static %t/b.o 2>&1 \
// RUN:   | FileCheck --check-prefix=STATIC --implicit-check-not='PLEASE submit' %s
// STATIC: error: '-static' is not supported for 'msil64-unknown-dotnet' yet: the managed C library links only as the shared assembly c.dll
// STATIC-NOT: unsupported option '-static'

// The include order.
// RUN: %clang -### --target=msil64-unknown-dotnet -resource-dir=%t/res --sysroot=%t/dotnet-sysroot -E %s 2>&1 \
// RUN:   | FileCheck --check-prefix=INC --implicit-check-not=error: %s
// INC:      "-cc1" "-triple" "msil64-unknown-dotnet"
// INC-SAME: "-internal-isystem" "{{[^"]*}}res{{/|\\\\}}include"
// INC-SAME: "-internal-{{(externc-)?}}isystem" "{{[^"]*}}dotnet-sysroot{{/|\\\\}}include{{/|\\\\}}msil64-unknown-dotnet"
// INC-SAME: "-internal-{{(externc-)?}}isystem" "{{[^"]*}}dotnet-sysroot{{/|\\\\}}include"
// RUN: %clang -### --target=msil64-unknown-dotnet -resource-dir=%t/res -E %s 2>&1 \
// RUN:   | FileCheck --check-prefix=INC-DEFAULT --implicit-check-not=error: %s
// INC-DEFAULT:      "-cc1" "-triple" "msil64-unknown-dotnet"
// INC-DEFAULT-SAME: "-internal-isystem" "{{[^"]*}}res{{/|\\\\}}include"
// INC-DEFAULT-SAME: "-internal-{{(externc-)?}}isystem" "{{[^"]*}}clang-runtimes{{/|\\\\}}msil64-unknown-dotnet{{/|\\\\}}include{{/|\\\\}}msil64-unknown-dotnet"
// INC-DEFAULT-SAME: "-internal-{{(externc-)?}}isystem" "{{[^"]*}}clang-runtimes{{/|\\\\}}msil64-unknown-dotnet{{/|\\\\}}include"
// RUN: %clang -### --target=msil64-unknown-dotnet -resource-dir=%t/res --sysroot=%t/dotnet-sysroot -nostdlibinc \
// RUN:   -E %s 2>&1 | FileCheck --check-prefix=NOSTDLIBINC \
// RUN:     --implicit-check-not='dotnet-sysroot{{/|\\\\}}include' %s
// NOSTDLIBINC: "-internal-isystem" "{{[^"]*}}res{{/|\\\\}}include"
// RUN: %clang -### --target=msil64-unknown-dotnet -resource-dir=%t/res --sysroot=%t/dotnet-sysroot -nobuiltininc \
// RUN:   -E %s 2>&1 | FileCheck --check-prefix=NOBUILTININC \
// RUN:     --implicit-check-not='res{{/|\\\\}}include"' %s
// NOBUILTININC: "-internal-{{(externc-)?}}isystem" "{{[^"]*}}dotnet-sysroot{{/|\\\\}}include"
// RUN: %clang -### --target=msil64-unknown-dotnet -resource-dir=%t/res --sysroot=%t/dotnet-sysroot -nostdinc \
// RUN:   -E %s 2>&1 | FileCheck --check-prefix=NOSTDINC --implicit-check-not=-internal-isystem \
// RUN:     --implicit-check-not=-internal-externc-isystem %s
// NOSTDINC: "-cc1" "-triple" "msil64-unknown-dotnet"

// The normalized triple.
// RUN: %clang --target=msil64-unknown-dotnet -print-target-triple 2>&1 | FileCheck --check-prefix=TRIPLE %s
// RUN: %clang --target=msil64-dotnet -print-target-triple 2>&1 | FileCheck --check-prefix=TRIPLE %s
// RUN: %clang --target=msil64-unknown-dotnet-msil -print-target-triple 2>&1 | FileCheck --check-prefix=TRIPLE %s
// TRIPLE: {{^}}msil64-unknown-dotnet{{$}}

// A fourth component that names no environment ("unknown") is dropped by
// normalization (section 12, P-1a, PT5): the printed triple, the jobs, the
// sysroot's library directory and the helper archive name
// msil64-unknown-dotnet, and no text keeps 'dotnet-unknown'.
// DEFINE: %{nounk} = --implicit-check-not=dotnet-unknown
// RUN: %clang --target=msil64-unknown-dotnet-unknown -print-target-triple 2>&1 \
// RUN:   | FileCheck --check-prefix=TRIPLE %{nounk} %s
// RUN: %clang --target=msil32-unknown-dotnet-unknown -print-target-triple 2>&1 \
// RUN:   | FileCheck --check-prefix=TRIPLE32 %{nounk} %s
// RUN: %clang -### --target=msil64-unknown-dotnet-unknown -resource-dir=%t/res %s 2>&1 \
// RUN:   | FileCheck --check-prefixes=CC1,LINK %{nojunk} %{nounk} %s
// RUN: %clang -### --target=msil64-unknown-dotnet-unknown -resource-dir=%t/res -nolibc -ffreestanding %t/b.o 2>&1 \
// RUN:   | FileCheck --check-prefix=UNKENV %{nojunk} %{nounk} --implicit-check-not=-lc %s
// UNKENV:      "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=msil64-unknown-dotnet"
// UNKENV-SAME: "-L{{[^"]*}}{{/|\\\\}}clang-runtimes{{/|\\\\}}msil64-unknown-dotnet{{/|\\\\}}lib{{/|\\\\}}msil64-unknown-dotnet"
// UNKENV-SAME: "{{[^"]*}}b.o" "{{[^"]*}}res{{/|\\\\}}lib{{/|\\\\}}msil64-unknown-dotnet{{/|\\\\}}libclang_rt.builtins.a" "-o" "a.exe"

// msil32 builds its jobs; msil-ld refuses the link (T-P1), not the driver.
// RUN: %clang -### --target=msil32-unknown-dotnet -resource-dir=%t/empty-res %s 2>&1 \
// RUN:   | FileCheck --check-prefix=MSIL32 %{nojunk} %s
// RUN: %clang --target=msil32-dotnet -print-target-triple 2>&1 | FileCheck --check-prefix=TRIPLE32 %s
// MSIL32:      "-cc1" "-triple" "msil32-unknown-dotnet"
// MSIL32:      "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=msil32-unknown-dotnet"
// MSIL32-SAME: "-L{{[^"]*}}{{/|\\\\}}clang-runtimes{{/|\\\\}}msil32-unknown-dotnet{{/|\\\\}}lib{{/|\\\\}}msil32-unknown-dotnet"
// MSIL32-SAME: "-lc" "-o" "a.exe"
// TRIPLE32: {{^}}msil32-unknown-dotnet{{$}}

// The refused combinations (PT5). The frontend's rejection only runs without
// -###; clang/test/CodeGen/MSIL/triples.c checks it with -cc1.
// RUN: not %clang --target=x86_64-unknown-dotnet -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=X64DOTNET --implicit-check-not='PLEASE submit' %s
// X64DOTNET: error: unknown target triple 'x86_64-unknown-dotnet'
// RUN: not %clang --target=msil64-unknown-linux -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=MSILLINUX --implicit-check-not='PLEASE submit' %s
// MSILLINUX: error: unknown target triple 'msil64-unknown-linux'
// RUN: not %clang --target=msil64-unknown-dotnet-gnu -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=MSILGNU --implicit-check-not='PLEASE submit' %s
// RUN: not %clang --target=msil64-unknown-dotnet-musl -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=MSILMUSL --implicit-check-not='PLEASE submit' %s
// MSILGNU:  error: unknown target triple 'msil64-unknown-dotnet-gnu'
// MSILMUSL: error: unknown target triple 'msil64-unknown-dotnet-musl'

// The same refusals for link-only invocations: no compile job reaches the
// frontend, so the toolchain refuses the link itself, and neither msil-ld nor
// a native linker is planned.
// DEFINE: %{nolink} = --implicit-check-not='PLEASE submit' --implicit-check-not=msil-ld \
// DEFINE:   --implicit-check-not='"gcc"' --implicit-check-not='"ld"' --implicit-check-not=ld.lld \
// DEFINE:   --implicit-check-not='link.exe' --implicit-check-not=lld-link
// RUN: not %clang --target=x86_64-unknown-dotnet -### %t/b.o -o %t/prog 2>&1 \
// RUN:   | FileCheck --check-prefix=X64DOTNET %{nolink} %s
// RUN: not %clang --target=msil64-unknown-dotnet-elf -### %t/b.o -o %t/prog 2>&1 \
// RUN:   | FileCheck --check-prefix=MSILELF %{nolink} %s
// RUN: not %clang --target=msil64-unknown-linux -### %t/b.o -o %t/prog 2>&1 \
// RUN:   | FileCheck --check-prefix=MSILLINUX %{nolink} %s
// RUN: not %clang --target=msil64-unknown-dotnet-gnu -### %t/b.o -o %t/prog 2>&1 \
// RUN:   | FileCheck --check-prefix=MSILGNU %{nolink} %s
// MSILELF: error: unknown target triple 'msil64-unknown-dotnet-elf'

// Another vendor: the jobs, the sysroot and the helper archive name
// msil64-unknown-dotnet.
// RUN: %clang -### --target=msil64-pc-dotnet -resource-dir=%t/res %s 2>&1 \
// RUN:   | FileCheck --check-prefixes=CC1,LINK %{nojunk} --implicit-check-not=pc-dotnet %s
// RUN: %clang --target=msil64-pc-dotnet -print-libgcc-file-name -resource-dir=%t/res 2>&1 \
// RUN:   | FileCheck --check-prefix=RTLIB %s

// A C++ source (freestanding, without the C library): a C++ compile job,
// then msil-ld with the helper archive and no C++ or C library
// (clang/test/CodeGen/MSIL/portable-cxx.cpp checks the code).
// RUN: %clang -### --target=msil64-unknown-dotnet -resource-dir=%t/res -ffreestanding -nolibc \
// RUN:   -fno-exceptions -fno-rtti %t/prog.cpp 2>&1 \
// RUN:   | FileCheck --check-prefix=CXX %{nojunk} --implicit-check-not=-lc --implicit-check-not=-lstdc++ %s
// CXX:      "-cc1" "-triple" "msil64-unknown-dotnet"
// CXX-SAME: "-x" "c++" "{{[^"]*}}prog.cpp"
// CXX-NEXT: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=msil64-unknown-dotnet"
// CXX-SAME: "{{[^"]*}}.o" "{{[^"]*}}res{{/|\\\\}}lib{{/|\\\\}}msil64-unknown-dotnet{{/|\\\\}}libclang_rt.builtins.a" "-o" "a.exe"

// The portable triples compile C (the frontend only here).
// RUN: %clang --target=msil64-unknown-dotnet -ffreestanding -fsyntax-only %s
// RUN: %clang --target=msil32-unknown-dotnet -ffreestanding -fsyntax-only %s
// RUN: %clang --target=msil64-unknown-dotnet -mcpu=generic -ffreestanding -fsyntax-only %s

// PV5's errors.
// RUN: not %clang --target=msil64-unknown-dotnet -fshort-wchar -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=SHORTWCHAR %s
// RUN: not %clang --target=msil64-unknown-dotnet -mlong-double-80 -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=LD80 %s
// RUN: not %clang --target=msil64-unknown-dotnet -mcpu=x86-64 -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=MCPU %s
// RUN: not %clang --target=msil64-unknown-dotnet -march=x86-64 -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=MARCH %s
// SHORTWCHAR: error: {{.*}}-fshort-wchar
// LD80:       error: {{.*}}-mlong-double-80
// MCPU:       error: {{.*}}x86-64
// MARCH:      error: {{.*}}x86-64

//--- b.o
//--- prog.cpp
//--- empty-res/include/stddef.h
//--- res/lib/msil64-unknown-dotnet/libclang_rt.builtins.a
//--- dotnet-sysroot/include/stdio.h

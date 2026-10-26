// LTO for '<native triple>-msil' targets (ImplementationPlan.md 5.10.3 item 5
// and 5.10.5; recon p2/driver-toolchain.md 4.8 and 6). With -flto (full or
// thin) every compile of a compile-and-link is an LTO compile ('-emit-llvm-bc
// -flto=...') and the link runs msil-ld, where LTO and the MSIL code
// generator run. From M20 MSILToolChain::getLTOMode is gone, so without
// -flto a compile-and-link compiles CIL objects ('cc1 -emit-obj', as -c does
// from M19, msil-object.c) and msil-ld links them with no LTO options (M14-M19
// compiled bitcode by default). -S and -emit-llvm keep their Phase 1 cc1
// lines (no -flto). Under LTO the options that steer code generation reach
// msil-ld in lld's spellings, never through tools::addLTOOptions: no gold
// plugin, no -plugin-opt=, and the driver never passes --lto-emit-asm (the
// harness does, 5.9.13). -O0 is --lto-O0, -O1 and -Og --lto-O1, -O2, -Os and
// -Oz --lto-O2, -O3 and -Ofast --lto-O3, and without -O msil-ld's default (2)
// applies; -flto-jobs, -flto-partitions, -save-temps and each -mllvm (the
// backend runs inside msil-ld, as HIP passes them to its device linker) are
// forwarded too (5.10.3 step 5: only when LTO is used).

// DEFINE: %{fc} = FileCheck %s --implicit-check-not=-plugin --implicit-check-not=LLVMgold \
// DEFINE:   --implicit-check-not=--lto-emit-asm --implicit-check-not=error:

// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -O2 %s -o p.exe 2>&1 \
// RUN:   | %{fc} --check-prefix=FULL
// FULL: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil" "-O2" "-emit-llvm-bc" "-flto=full"
// FULL: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-unknown-linux-gnu-msil"
// FULL-SAME: "--lto-O2"
// FULL-SAME: "-o" "p.exe"

// Without -flto: a CIL object and a plain link, no LTO option (M20).
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -O2 %s -o p.exe 2>&1 \
// RUN:   | %{fc} --check-prefix=OBJ --implicit-check-not=-emit-llvm-bc --implicit-check-not=-flto= \
// RUN:     --implicit-check-not=--lto-O
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -O2 %s -o p.exe 2>&1 \
// RUN:   | %{fc} --check-prefix=OBJ-WIN --implicit-check-not=-emit-llvm-bc --implicit-check-not=-flto= \
// RUN:     --implicit-check-not=--lto-O --implicit-check-not=lld-link
// OBJ: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil" "-O2" "-emit-obj"
// OBJ: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-unknown-linux-gnu-msil"
// OBJ-SAME: "-o" "p.exe"
// OBJ-WIN: "-cc1" "-triple" "x86_64-pc-windows-msvc{{[0-9.]*}}-msil" "-O2" "-emit-obj"
// OBJ-WIN: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-pc-windows-msvc-msil"

// The optimization level, at compile and at link time (LTO).
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -O0 %s 2>&1 | %{fc} --check-prefix=O0
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -O1 %s 2>&1 | %{fc} --check-prefix=O1
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -Og %s 2>&1 | %{fc} --check-prefix=O1
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -Os %s 2>&1 | %{fc} --check-prefix=O2
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -Oz %s 2>&1 | %{fc} --check-prefix=O2
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -O3 %s 2>&1 | %{fc} --check-prefix=O3
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -Ofast %s 2>&1 | %{fc} --check-prefix=O3
// O0: "{{[^"]*}}msil-ld{{(\.exe)?}}"
// O0-SAME: "--lto-O0"
// O1: "{{[^"]*}}msil-ld{{(\.exe)?}}"
// O1-SAME: "--lto-O1"
// O2: "{{[^"]*}}msil-ld{{(\.exe)?}}"
// O2-SAME: "--lto-O2"
// O3: "{{[^"]*}}msil-ld{{(\.exe)?}}"
// O3-SAME: "--lto-O3"
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto %s 2>&1 \
// RUN:   | %{fc} --check-prefix=NO-O --implicit-check-not=--lto-O
// NO-O: "{{[^"]*}}msil-ld{{(\.exe)?}}"

// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto=thin -flto-jobs=4 -O3 %s -o p.exe 2>&1 \
// RUN:   | %{fc} --check-prefix=THIN
// THIN: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil" "-O3" "-emit-llvm-bc" "-flto=thin"
// THIN: "{{[^"]*}}msil-ld{{(\.exe)?}}"
// THIN-SAME: "--lto-O3" "--thinlto-jobs=4"

// Under LTO -mllvm reaches both the compile and the link; without LTO only
// the compile, where the code generator runs (5.10.3 step 5).
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -flto -mllvm -msil-assembly-name=foo %s -o foo.exe 2>&1 \
// RUN:   | %{fc} --check-prefix=MLLVM
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -mllvm -msil-assembly-name=foo %s -o foo.exe 2>&1 \
// RUN:   | %{fc} --check-prefix=MLLVM-OBJ --implicit-check-not=--mllvm
// MLLVM-OBJ: "-cc1" {{.*}}"-emit-obj"{{.*}}"-mllvm" "-msil-assembly-name=foo"
// MLLVM-OBJ: "{{[^"]*}}msil-ld{{(\.exe)?}}"
// MLLVM: "-cc1" {{.*}}"-mllvm" "-msil-assembly-name=foo"
// MLLVM: "{{[^"]*}}msil-ld{{(\.exe)?}}"
// MLLVM-SAME: "--mllvm=-msil-assembly-name=foo"

// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -flto-partitions=2 -save-temps %s -o p.exe 2>&1 \
// RUN:   | FileCheck %s --check-prefix=PART --implicit-check-not=-plugin
// PART: "{{[^"]*}}msil-ld{{(\.exe)?}}"
// PART-SAME: "--lto-partitions=2" "--save-temps"

// windows-msvc: no 'LTO requires -fuse-ld=lld', with -flto or without.
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -flto %s -o p.exe 2>&1 \
// RUN:   | %{fc} --check-prefix=WIN --implicit-check-not=lld-link
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -flto=thin %s -o p.exe 2>&1 \
// RUN:   | %{fc} --check-prefix=WIN --implicit-check-not=lld-link
// RUN: %clang -### --target=x86_64-w64-windows-gnu-msil -flto %s -o p.exe 2>&1 \
// RUN:   | %{fc} --check-prefix=MINGW
// WIN: "-cc1" "-triple" "x86_64-pc-windows-msvc{{[0-9.]*}}-msil"
// WIN-SAME: "-emit-llvm-bc"
// WIN: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-pc-windows-msvc-msil"
// MINGW: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-w64-windows-gnu-msil"

// -S and -emit-llvm keep the Phase 1 cc1 lines: no LTO.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -S %s -o p.il 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NOLTO-S --implicit-check-not=-flto --implicit-check-not=msil-ld
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -c -emit-llvm %s -o p.bc 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NOLTO-BC --implicit-check-not=-flto --implicit-check-not=msil-ld
// NOLTO-S: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil"
// NOLTO-S-SAME: "-S"
// NOLTO-BC: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil"
// NOLTO-BC-SAME: "-emit-llvm-bc"

// Bitcode inputs go to msil-ld as they are (HasNativeLLVMSupport); whether a
// stock-clang native-triple bitcode file links is msil-ld's business
// (5.9.13). The compile writes bitcode without the backend.
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -c -emit-llvm %s -o %t.o
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil %t.o -o p.exe 2>&1 \
// RUN:   | %{fc} --check-prefix=BC --implicit-check-not=-cc1
// BC: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-unknown-linux-gnu-msil"
// BC-SAME: "{{[^"]*}}.o"

// A link-only LTO command (object inputs, no compile job, -flto): each -mllvm
// still reaches msil-ld (5.10.3 step 5), and the link job claims it, so there
// is no 'argument unused during compilation'.
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto %t.o -mllvm -foo=1 2>&1 \
// RUN:   | %{fc} --check-prefix=MLLVM-LINK --implicit-check-not='argument unused' --implicit-check-not=-cc1
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -flto %t.o -mllvm -foo=1 -mllvm -bar 2>&1 \
// RUN:   | %{fc} --check-prefix=MLLVM-LINK-WIN --implicit-check-not='argument unused' --implicit-check-not=-cc1
// MLLVM-LINK: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-unknown-linux-gnu-msil"
// MLLVM-LINK-SAME: "--mllvm=-foo=1"
// MLLVM-LINK-SAME: "{{[^"]*}}.o"
// MLLVM-LINK-WIN: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-pc-windows-msvc-msil"
// MLLVM-LINK-WIN-SAME: "--mllvm=-foo=1" "--mllvm=-bar"
// MLLVM-LINK-WIN-SAME: "{{[^"]*}}.o"

int main(void) { return 0; }

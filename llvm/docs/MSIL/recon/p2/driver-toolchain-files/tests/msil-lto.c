// Proposed clang/test/Driver/msil-lto.c (recon driver-toolchain; P2-B).
// LTO for MSIL triples: compiles write bitcode, msil-ld runs LTO and the MSIL
// code generator. The options that steer code generation go to msil-ld in
// lld spellings (no gold plugin, no -plugin-opt=), -mllvm options too, because
// the backend runs at link time (as HIP forwards them to its device linker).

// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -O2 %s -o p.exe 2>&1 \
// RUN:   | FileCheck --check-prefix=FULL --implicit-check-not=-plugin --implicit-check-not=LLVMgold \
// RUN:     --implicit-check-not=error: %s
// FULL: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil" "-O2" "-emit-llvm-bc" "-flto=full"
// FULL: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-unknown-linux-gnu-msil"
// FULL-SAME: "--lto-O2"
// FULL-SAME: "-o" "p.exe"

// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto=thin -flto-jobs=4 -O3 %s -o p.exe 2>&1 \
// RUN:   | FileCheck --check-prefix=THIN --implicit-check-not=-plugin --implicit-check-not=error: %s
// THIN: "-cc1" "-triple" "x86_64-unknown-linux-gnu-msil" "-O3" "-emit-llvm-bc" "-flto=thin"
// THIN: "{{[^"]*}}msil-ld{{(\.exe)?}}"
// THIN-SAME: "--lto-O3" "--thinlto-jobs=4"

// -mllvm reaches both the compile and the link.
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -flto -mllvm -msil-assembly-name=foo %s -o foo.exe 2>&1 \
// RUN:   | FileCheck --check-prefix=MLLVM --implicit-check-not=error: %s
// MLLVM: "-cc1" {{.*}}"-mllvm" "-msil-assembly-name=foo"
// MLLVM: "{{[^"]*}}msil-ld{{(\.exe)?}}"
// MLLVM-SAME: "--mllvm=-msil-assembly-name=foo"

// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -flto -flto-partitions=2 -save-temps %s -o p.exe 2>&1 \
// RUN:   | FileCheck --check-prefix=PART %s
// PART: "{{[^"]*}}msil-ld{{(\.exe)?}}"
// PART-SAME: "--lto-partitions=2" "--save-temps"

// windows-msvc: no 'LTO requires -fuse-ld=lld'.
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -flto %s -o p.exe 2>&1 \
// RUN:   | FileCheck --check-prefix=WIN --implicit-check-not=error: %s
// WIN: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-pc-windows-msvc-msil"

// Bitcode inputs go to msil-ld as they are (HasNativeLLVMSupport); a stock-clang
// native-triple bitcode file is msil-ld's to accept or reject (lld-flavor 3.3).
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -flto -c %s -o %t.o
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil %t.o -o p.exe 2>&1 \
// RUN:   | FileCheck --check-prefix=BC --implicit-check-not=-cc1 --implicit-check-not=error: %s
// BC: "{{[^"]*}}msil-ld{{(\.exe)?}}" "--target=x86_64-unknown-linux-gnu-msil"
// BC-SAME: "{{[^"]*}}.o"

int main(void) { return 0; }

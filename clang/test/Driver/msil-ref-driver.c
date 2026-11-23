// .NET references through the driver, triple by triple (ImplementationPlan.md
// 5.21.14 RV17, milestone R-a): with <cli/cli.h> (msil-cli-header.c) and no
// other option, a C file with references compiles with no diagnostic on each
// of the eight host triples, msil64-unknown-dotnet and msil32-unknown-dotnet,
// and its IR (-S -emit-llvm) has target("msil.ref") in the signature of a
// static function on each (the IR runs; CodeGen/MSIL/ref-abi.c checks the ABI
// in detail at cc1). With an explicit -fblocks both meanings of '^' live side
// by side (RC7: a block over a function type, a reference over a class
// struct; BLOCKS). C++ through the driver is T-R5 (CXX). At -Og the driver
// passes -fextend-variable-liveness=all (5.21.6 RI4: clang then emits
// llvm.fake.use for every local, references included, which the back end
// accepts), at -O2 it does not (OG, O2).

// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -nostdlibinc -fsyntax-only -Xclang -verify %s
// RUN: %clang --target=x86_64-unknown-linux-musl-msil -nostdlibinc -fsyntax-only -Xclang -verify %s
// RUN: %clang --target=aarch64-unknown-linux-gnu-msil -nostdlibinc -fsyntax-only -Xclang -verify %s
// RUN: %clang --target=aarch64-unknown-linux-musl-msil -nostdlibinc -fsyntax-only -Xclang -verify %s
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -nostdlibinc -fsyntax-only -Xclang -verify %s
// RUN: %clang --target=x86_64-w64-windows-gnu-msil -nostdlibinc -fsyntax-only -Xclang -verify %s
// RUN: %clang --target=aarch64-pc-windows-msvc-msil -nostdlibinc -fsyntax-only -Xclang -verify %s
// RUN: %clang --target=aarch64-w64-windows-gnu-msil -nostdlibinc -fsyntax-only -Xclang -verify %s
// RUN: %clang --target=msil64-unknown-dotnet -nostdlibinc -fsyntax-only -Xclang -verify %s
// RUN: %clang --target=msil32-unknown-dotnet -nostdlibinc -fsyntax-only -Xclang -verify %s

// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -nostdlibinc -S -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang --target=x86_64-unknown-linux-musl-msil -nostdlibinc -S -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang --target=aarch64-unknown-linux-gnu-msil -nostdlibinc -S -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang --target=aarch64-unknown-linux-musl-msil -nostdlibinc -S -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -nostdlibinc -S -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang --target=x86_64-w64-windows-gnu-msil -nostdlibinc -S -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang --target=aarch64-pc-windows-msvc-msil -nostdlibinc -S -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang --target=aarch64-w64-windows-gnu-msil -nostdlibinc -S -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang --target=msil64-unknown-dotnet -nostdlibinc -S -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang --target=msil32-unknown-dotnet -nostdlibinc -S -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// IR-DAG: define internal target("msil.ref") @upcast(target("msil.ref") {{.*}}%s)
// IR-DAG: call {{.*}}i1 @__msil_ref.eq(target("msil.ref") {{.*}}, target("msil.ref") {{.*}})

// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -nostdlibinc -fblocks -fsyntax-only -DBLOCKS -Xclang -verify %s
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -nostdlibinc -fblocks -fsyntax-only -DBLOCKS -Xclang -verify %s
// RUN: %clang --target=msil64-unknown-dotnet -nostdlibinc -fblocks -fsyntax-only -DBLOCKS -Xclang -verify %s

// RUN: not %clang --target=x86_64-unknown-linux-gnu-msil -nostdlibinc -x c++ -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=CXX
// RUN: not %clang --target=x86_64-pc-windows-msvc-msil -nostdlibinc -x c++ -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=CXX
// CXX: error: .NET references in C++ are not supported for MSIL yet{{$}}

// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -Og -c %s 2>&1 | FileCheck %s --check-prefix=OG
// RUN: %clang -### --target=x86_64-unknown-linux-gnu-msil -Og -c %s 2>&1 | FileCheck %s --check-prefix=OG
// RUN: %clang -### --target=msil64-unknown-dotnet -Og -c %s 2>&1 | FileCheck %s --check-prefix=OG
// OG: "-cc1" {{.*}}"-fextend-variable-liveness=all"
// RUN: %clang -### --target=x86_64-pc-windows-msvc-msil -O2 -c %s 2>&1 | FileCheck %s --check-prefix=O2 --implicit-check-not=-fextend-variable-liveness
// O2: "-cc1"

// expected-no-diagnostics

#include <cli/cli.h>

typedef struct CLI_TYPE("System.String") String String;
typedef struct CLI_TYPE("System.Object") Object Object;

static Object ^upcast(String ^s) { return s; }
static int same(Object ^a, Object ^b) { return a == b; }
int use(void) {
  String ^s = 0;
  return same(upcast(s), (Object ^)0);
}

#if defined(BLOCKS)
// RC7: a block and a reference in one signature and one call.
static int both(String ^s, int (^blk)(int)) { return (s == 0) + blk(1); }
int use_blocks(void) { return both((String ^)0, ^(int x) { return x + 1; }); }
#endif

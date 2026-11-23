// .NET references through -ast-merge (ImplementationPlan.md 5.21.3 RC8
// (import, structural equivalence) and 5.21.14 RV16, milestone R-a). Two ASTs
// are merged into one unit (ASTImporter):
// - Declarations of one managed call (external linkage) whose types spell
//   the same class differently ('String ^' against 'Text ^', equal keys) are
//   structurally equivalent: no diagnostic. Different classes are not:
//   clang's ODR warning names both types with their aka text (merged.c
//   pins it with -verify).
// - The imported function bodies keep their references (the canonical type
//   is made again in the destination context, the sugared type keeps its
//   imported struct), so the merged unit compiles to the IR of each body:
//   the comparison through the helper, the NULL store, and the cast stub.

// RUN: rm -rf %t && split-file %s %t
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-pch -o %t/a.ast %t/a.c
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-pch -o %t/b.ast %t/b.c
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -ast-merge %t/a.ast -ast-merge %t/b.ast -fsyntax-only -verify %t/merged.c
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -ast-merge %t/a.ast -ast-merge %t/b.ast -emit-llvm -o - %t/merged.c \
// RUN:   | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-pch -o %t/a.lnx.ast %t/a.c
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-pch -o %t/b.lnx.ast %t/b.c
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -ast-merge %t/a.lnx.ast -ast-merge %t/b.lnx.ast -fsyntax-only -verify %t/merged.c
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -ast-merge %t/a.lnx.ast -ast-merge %t/b.lnx.ast -emit-llvm -o - %t/merged.c \
// RUN:   | FileCheck %s
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -emit-pch -o %t/a.p64.ast %t/a.c
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -emit-pch -o %t/b.p64.ast %t/b.c
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -ast-merge %t/a.p64.ast -ast-merge %t/b.p64.ast -fsyntax-only -verify %t/merged.c
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -ast-merge %t/a.p64.ast -ast-merge %t/b.p64.ast -emit-llvm -o - %t/merged.c \
// RUN:   | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -emit-pch -o %t/a.mus.ast %t/a.c
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -emit-pch -o %t/b.mus.ast %t/b.c
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -ast-merge %t/a.mus.ast -ast-merge %t/b.mus.ast -fsyntax-only -verify %t/merged.c
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -ast-merge %t/a.mus.ast -ast-merge %t/b.mus.ast -emit-llvm -o - %t/merged.c \
// RUN:   | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -emit-pch -o %t/a.gnu.ast %t/a.c
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -emit-pch -o %t/b.gnu.ast %t/b.c
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -ast-merge %t/a.gnu.ast -ast-merge %t/b.gnu.ast -fsyntax-only -verify %t/merged.c
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -ast-merge %t/a.gnu.ast -ast-merge %t/b.gnu.ast -emit-llvm -o - %t/merged.c \
// RUN:   | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -emit-pch -o %t/a.a64win.ast %t/a.c
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -emit-pch -o %t/b.a64win.ast %t/b.c
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -ast-merge %t/a.a64win.ast -ast-merge %t/b.a64win.ast -fsyntax-only -verify %t/merged.c
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -ast-merge %t/a.a64win.ast -ast-merge %t/b.a64win.ast -emit-llvm -o - %t/merged.c \
// RUN:   | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -emit-pch -o %t/a.a64lnx.ast %t/a.c
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -emit-pch -o %t/b.a64lnx.ast %t/b.c
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -ast-merge %t/a.a64lnx.ast -ast-merge %t/b.a64lnx.ast -fsyntax-only -verify %t/merged.c
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -ast-merge %t/a.a64lnx.ast -ast-merge %t/b.a64lnx.ast -emit-llvm -o - %t/merged.c \
// RUN:   | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -emit-pch -o %t/a.a64mus.ast %t/a.c
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -emit-pch -o %t/b.a64mus.ast %t/b.c
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -ast-merge %t/a.a64mus.ast -ast-merge %t/b.a64mus.ast -fsyntax-only -verify %t/merged.c
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -ast-merge %t/a.a64mus.ast -ast-merge %t/b.a64mus.ast -emit-llvm -o - %t/merged.c \
// RUN:   | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -emit-pch -o %t/a.a64gnu.ast %t/a.c
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -emit-pch -o %t/b.a64gnu.ast %t/b.c
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -ast-merge %t/a.a64gnu.ast -ast-merge %t/b.a64gnu.ast -fsyntax-only -verify %t/merged.c
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -ast-merge %t/a.a64gnu.ast -ast-merge %t/b.a64gnu.ast -emit-llvm -o - %t/merged.c \
// RUN:   | FileCheck %s

// CHECK-DAG: define {{.*}}i32 @a_use()
// CHECK-DAG: define internal i32 @a_same(target("msil.ref") %s, target("msil.ref") %o)
// CHECK-DAG: call i1 @__msil_ref.eq(target("msil.ref") %{{.+}}, target("msil.ref") %{{.+}})
// CHECK-DAG: define {{.*}}i32 @b_use()
// CHECK-DAG: define internal target("msil.ref") @b_pick(target("msil.ref") %t, i32 noundef %c)
// CHECK-DAG: store target("msil.ref") zeroinitializer, ptr %r
// CHECK-DAG: define internal target("msil.ref") @b_down(target("msil.ref") %o)
// CHECK-DAG: call target("msil.ref") @"__msil_mc.<castclass>.__uniq.{{[0-9]+}}"(target("msil.ref") %{{.+}})
// CHECK-DAG: declare target("msil.ref") @__msil_mc.machine()
// CHECK-DAG: declare target("msil.ref") @__msil_mc.boxed()
// CHECK-DAG: declare i1 @__msil_ref.eq(target("msil.ref"), target("msil.ref"))

//--- a.c
typedef struct __attribute__((cli_type("System.String"))) String String;
typedef struct __attribute__((cli_type("System.Object"))) Object Object;
String ^machine(void) __attribute__((msil_method("System.Environment", "get_MachineName", "string()")));
Object ^boxed(void) __attribute__((msil_method("Ns.Objects", "Get", "object()")));
Object ^conflict(void) __attribute__((msil_method("Ns.Objects", "Get", "object()")));
typedef String ^SRef;
static int a_same(String ^s, Object ^o) { return s == o; }
int a_use(void) { return a_same(machine(), boxed()); }

//--- b.c
typedef struct __attribute__((cli_type(" System.String "))) Text Text;
typedef struct __attribute__((cli_type("System.IO.Stream"))) Stream Stream;
typedef struct __attribute__((cli_type("System.Object"))) Object Object;
Text ^machine(void) __attribute__((msil_method("System.Environment", "get_MachineName", "string()")));
Object ^boxed(void) __attribute__((msil_method("Ns.Objects", "Get", "object()")));
Stream ^conflict(void) __attribute__((msil_method("Ns.Objects", "Get", "class System.IO.Stream()")));
typedef Text ^SRef;
static Text ^b_pick(Text ^t, int c) {
  Text ^r;
  if (c)
    r = t;
  return r;
}
static Text ^b_down(Object ^o) { return (Text ^)o; }
int b_use(void) { return b_pick(machine(), 1) == b_down(boxed()); }

//--- merged.c
// expected-warning-re@b.c:6 {{{{^}}external function 'conflict' declared with incompatible types in different translation units ('Stream ^(void)' (aka 'System.IO.Stream ^(void)') vs. 'Object ^(void)' (aka 'System.Object ^(void)')){{$}}}}
// expected-note-re@a.c:5 {{{{^}}declared here with type 'Object ^(void)' (aka 'System.Object ^(void)'){{$}}}}
int main(void) { return 0; }

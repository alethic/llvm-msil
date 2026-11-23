// -ftrivial-auto-var-init and .NET references (ImplementationPlan.md 5.21.5
// RG3, 5.21.4 RS2 and 5.21.14 RV12, milestone R-a). A reference local without
// an initializer always gets RS2's NULL store at its declaration, which is
// not an auto-init store; getAutoVarInitKind answers Uninitialized for
// references, so -ftrivial-auto-var-init=zero and =pattern add nothing for
// them (a pattern is no reference), while an int beside it gets the
// annotated auto-init store. A computed goto makes CodeGen treat every local
// of the function as bypassed (the bypass rule of EmitAutoVarAlloca), which
// moves the auto-init of the int and of the label pointer to the alloca; the
// reference still gets only its NULL store at the declaration. Without the
// option there is no auto-init at all (NONE).

// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -ftrivial-auto-var-init=zero -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,ZERO
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -ftrivial-auto-var-init=zero -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,ZERO
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -ftrivial-auto-var-init=pattern -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,PATTERN
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -ftrivial-auto-var-init=pattern -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,PATTERN
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -ftrivial-auto-var-init=pattern -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,PATTERN
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -ftrivial-auto-var-init=zero -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,ZERO
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -ftrivial-auto-var-init=pattern -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,PATTERN
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -ftrivial-auto-var-init=zero -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,ZERO
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -ftrivial-auto-var-init=pattern -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,PATTERN
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -ftrivial-auto-var-init=zero -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,ZERO
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -ftrivial-auto-var-init=pattern -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,PATTERN
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -ftrivial-auto-var-init=zero -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,ZERO
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -ftrivial-auto-var-init=pattern -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,PATTERN
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -ftrivial-auto-var-init=zero -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,ZERO
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -ftrivial-auto-var-init=pattern -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,PATTERN
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -ftrivial-auto-var-init=zero -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,ZERO
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -ftrivial-auto-var-init=pattern -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,PATTERN
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,NONE --implicit-check-not=auto-init

typedef struct __attribute__((cli_type("System.String"))) String String;

// CHECK-LABEL: define {{.*}}i32 @test(

static int plain(String ^a) {
  String ^r;
  int i;
  r = a;
  i = 0;
  return (r == a) + i;
}
// CHECK-LABEL: define internal i32 @plain(
// CHECK:       %r = alloca target("msil.ref")
// CHECK:       %i = alloca i32
// CHECK:       store target("msil.ref") zeroinitializer, ptr %r, align {{[0-9]+}}{{$}}
// ZERO-NEXT:   store i32 0, ptr %i, align 4, !annotation [[AUTO:![0-9]+]]
// PATTERN-NEXT: store i32 -1431655766, ptr %i, align 4, !annotation [[AUTO:![0-9]+]]
// NONE-NEXT:   load target("msil.ref"), ptr %a.addr
// CHECK-NOT:   store target("msil.ref") {{.*}}!annotation
// CHECK:       ret i32

static int computed(String ^a, int c) {
  String ^r;
  int i;
  void *t = c ? &&L1 : &&L2;
  goto *t;
L1:
  r = a;
  i = 1;
L2:
  return (r == a) + i;
}
// CHECK-LABEL: define internal i32 @computed(
// CHECK:       %r = alloca target("msil.ref")
// CHECK:       %i = alloca i32
// CHECK:       %t = alloca ptr
// The bypass rule: the int and the pointer are initialized at the start ...
// ZERO-NEXT:   store i32 0, ptr %i, align 4, !annotation [[AUTO]]
// ZERO-NEXT:   store ptr null, ptr %t, align 8, !annotation [[AUTO]]
// PATTERN-NEXT: store i32 -1431655766, ptr %i, align 4, !annotation [[AUTO]]
// PATTERN-NEXT: store ptr inttoptr (i{{32|64}} {{-?[0-9]+}} to ptr), ptr %t, align {{[0-9]+}}, !annotation [[AUTO]]
// ... and the reference only at its declaration, with NULL.
// CHECK-NOT:   store target("msil.ref") {{.*}}!annotation
// CHECK:       store target("msil.ref") zeroinitializer, ptr %r, align {{[0-9]+}}{{$}}
// CHECK-NOT:   store target("msil.ref") {{.*}}!annotation
// CHECK:       indirectbr

// ZERO:    [[AUTO]] = !{!"auto-init"}
// PATTERN: [[AUTO]] = !{!"auto-init"}

int test(void) { return plain(0) + computed(0, 1); }

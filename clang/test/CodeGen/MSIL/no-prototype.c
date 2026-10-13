// On MSIL triples, a function declared without a prototype and without a body
// gets the "no-prototype" function attribute (ImplementationPlan.md M1.d, the
// rule of clang/lib/CodeGen/Targets/WebAssembly.cpp). MSILAddMissingPrototypes
// (M9) uses it to give such a function an exact CIL signature. Prototyped
// declarations and definitions (K&R ones included) never get it, and native
// triples never do.
//
// Each attribute group is checked by its own FileCheck run, because the group
// numbers (and so the order of the 'attributes #N' lines) are not fixed.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=gnu17 -Wno-deprecated-non-prototype \
// RUN:   -emit-llvm -o %t.lnx.ll %s
// RUN: FileCheck --input-file=%t.lnx.ll --check-prefixes=CHECK,NOPROTO %s
// RUN: FileCheck --input-file=%t.lnx.ll --check-prefixes=CHECK,PROTO %s
// RUN: FileCheck --input-file=%t.lnx.ll --check-prefixes=CHECK,DEF %s
// RUN: FileCheck --input-file=%t.lnx.ll --check-prefixes=CHECK,KR %s

// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=gnu17 -Wno-deprecated-non-prototype \
// RUN:   -emit-llvm -o %t.win.ll %s
// RUN: FileCheck --input-file=%t.win.ll --check-prefixes=CHECK,NOPROTO %s
// RUN: FileCheck --input-file=%t.win.ll --check-prefixes=CHECK,PROTO %s
// RUN: FileCheck --input-file=%t.win.ll --check-prefixes=CHECK,DEF %s
// RUN: FileCheck --input-file=%t.win.ll --check-prefixes=CHECK,KR %s

// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -std=gnu17 -Wno-deprecated-non-prototype \
// RUN:   -emit-llvm -o %t.a64.ll %s
// RUN: FileCheck --input-file=%t.a64.ll --check-prefixes=CHECK,NOPROTO %s
// RUN: FileCheck --input-file=%t.a64.ll --check-prefixes=CHECK,PROTO %s
// RUN: FileCheck --input-file=%t.a64.ll --check-prefixes=CHECK,DEF %s
// RUN: FileCheck --input-file=%t.a64.ll --check-prefixes=CHECK,KR %s

// Native triples: no "no-prototype" anywhere.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=gnu17 -Wno-deprecated-non-prototype -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=CHECK --implicit-check-not='"no-prototype"' %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -std=gnu17 -Wno-deprecated-non-prototype -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=CHECK --implicit-check-not='"no-prototype"' %s

int f();       // no prototype, no body: "no-prototype"
int h(int);    // prototype
int kr(a) int a; { return a; } // no prototype, but a body (K&R definition)

int g(void) { return f(1) + h(2) + kr(3); }

// CHECK-LABEL: define {{.*}}i32 @kr(i32 noundef %a)
// KR-SAME: [[KR_ATTRS:#[0-9]+]]
// CHECK-LABEL: define {{.*}}i32 @g()
// DEF-SAME: [[G_ATTRS:#[0-9]+]]
// CHECK: declare {{.*}}i32 @f(...)
// NOPROTO-SAME: [[F_ATTRS:#[0-9]+]]
// CHECK: declare {{.*}}i32 @h(i32 noundef)
// PROTO-SAME: [[H_ATTRS:#[0-9]+]]

// NOPROTO: attributes [[F_ATTRS]] = { {{.*}}"no-prototype"{{.*}} }

// PROTO: attributes [[H_ATTRS]] = {
// PROTO-NOT: "no-prototype"
// PROTO-SAME: }

// DEF: attributes [[G_ATTRS]] = {
// DEF-NOT: "no-prototype"
// DEF-SAME: }

// KR: attributes [[KR_ATTRS]] = {
// KR-NOT: "no-prototype"
// KR-SAME: }

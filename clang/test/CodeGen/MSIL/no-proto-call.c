// On every MSIL triple, a call through a function type without a prototype has
// a fixed-arity call type built from the promoted arguments, as on WebAssembly:
// CIL call and calli need exact signatures, there is no native %al convention,
// the backend rejects variadic calls (D19), and MSILFixFunctionBitcasts can
// repair a mismatched call type only for direct calls, so a call through a
// function pointer must already have its exact type. Native x86_64 SysV types
// such calls as variadic (X86_64TargetCodeGenInfo::isNoProtoCallVariadic), and
// the MSIL x86_64 Linux triples must not inherit that. The declarations stay
// 'declare i32 @f(...)' with "no-prototype" (no-prototype.c checks them) for
// MSILAddMissingPrototypes, and calls through a variadic prototype stay
// variadic.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -std=gnu17 -Wno-deprecated-non-prototype \
// RUN:   -emit-llvm -o - %s | FileCheck --check-prefixes=CHECK,FIXED %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -std=gnu17 -Wno-deprecated-non-prototype \
// RUN:   -emit-llvm -o - %s | FileCheck --check-prefixes=CHECK,FIXED %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -std=gnu17 -Wno-deprecated-non-prototype \
// RUN:   -emit-llvm -o - %s | FileCheck --check-prefixes=CHECK,FIXED %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -std=gnu17 -Wno-deprecated-non-prototype \
// RUN:   -emit-llvm -o - %s | FileCheck --check-prefixes=CHECK,FIXED %s

// Native triples are unchanged: variadic call types on x86_64 SysV, fixed-arity
// ones on Win64.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=gnu17 -Wno-deprecated-non-prototype \
// RUN:   -emit-llvm -o - %s | FileCheck --check-prefixes=CHECK,VARIADIC %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -std=gnu17 -Wno-deprecated-non-prototype \
// RUN:   -emit-llvm -o - %s | FileCheck --check-prefixes=CHECK,FIXED %s

int f();             // no prototype
int fd();            // no prototype, called with a float (promoted to double)
int v(int, ...);     // variadic prototype

int add1(int x) { return x + 1; }

// (a) Direct call through a prototype-less declaration.
// CHECK-LABEL: define {{.*}}i32 @direct()
// FIXED: call i32 @f(i32 noundef 1)
// VARIADIC: call i32 (i32, ...) @f(i32 noundef 1)
// CHECK: declare {{.*}}i32 @f(...)
int direct(void) { return f(1); }

// (b) Indirect call through a pointer to an unprototyped function type.
// CHECK-LABEL: define {{.*}}i32 @indirect()
// CHECK: [[FP:%.*]] = load ptr, ptr %fp
// FIXED: call i32 [[FP]](i32 noundef 41)
// VARIADIC: call i32 (i32, ...) [[FP]](i32 noundef 41)
int indirect(void) {
  int (*fp)() = add1;
  return fp(41);
}

// (c) Default argument promotion: the float argument is a double in the call
// type.
// CHECK-LABEL: define {{.*}}i32 @promoted(float noundef %x)
// CHECK: [[D:%.*]] = fpext float {{%.*}} to double
// FIXED: call i32 @fd(double noundef [[D]])
// VARIADIC: call i32 (double, ...) @fd(double noundef [[D]])
// CHECK: declare {{.*}}i32 @fd(...)
int promoted(float x) { return fd(x); }

// A call through a variadic prototype stays variadic on every triple.
// CHECK-LABEL: define {{.*}}i32 @proto_variadic()
// CHECK: call i32 (i32, ...) @v(i32 noundef 1, i32 noundef 2)
// CHECK: declare {{.*}}i32 @v(i32 noundef, ...)
int proto_variadic(void) { return v(1, 2); }

// long double is IEEE double on every MSIL triple (Design.md section 3,
// decision D3; ImplementationPlan.md M1.c): size and alignment 8, 'double' in
// the IR, the native calling convention's treatment of double for parameters,
// results and va_arg, and -mlong-double-80/-128 are rejected. The triples whose
// native long double is wider (x86_64 Linux and MinGW: x87; aarch64 Linux: IEEE
// quad) prove that the wrapper overrides the native TargetInfo.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefixes=CHECK,SYSV --implicit-check-not=x86_fp80 --implicit-check-not=fp128 %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefixes=CHECK,SYSV --implicit-check-not=x86_fp80 --implicit-check-not=fp128 %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefixes=CHECK,WIN --implicit-check-not=x86_fp80 --implicit-check-not=fp128 %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefixes=CHECK,WIN --implicit-check-not=x86_fp80 --implicit-check-not=fp128 %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefixes=CHECK,A64 --implicit-check-not=x86_fp80 --implicit-check-not=fp128 %s

// long double needs no x87: '-target-feature -x87' compiles, where the native
// triple reports that the target does not support 'long double'.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -target-feature -x87 -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefixes=CHECK,SYSV --implicit-check-not=x86_fp80 --implicit-check-not=fp128 %s
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature -x87 -emit-llvm-only -DNATIVE_NO_X87 %s 2>&1 \
// RUN:   | FileCheck --check-prefix=NO-X87 %s
// NO-X87: error: '{{.*}}' requires {{.*}}'long double' type support, but target 'x86_64-unknown-linux-gnu' does not support it

// -mlong-double-64 is what the wrapper does anyway; the other sizes cannot be
// honored and are diagnosed.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -mlong-double-64 -emit-llvm-only %s
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -mlong-double-80 -emit-llvm-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD80 %s
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -mlong-double-128 -emit-llvm-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD128 %s
// RUN: not %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -mlong-double-128 -emit-llvm-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD128 %s
// RUN: not %clang_cc1 -triple x86_64-pc-windows-msvc-msil -mlong-double-80 -emit-llvm-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=BAD80 %s
// BAD80: error: option '-mlong-double-80' cannot be specified on this target
// BAD128: error: option '-mlong-double-128' cannot be specified on this target

// The native control run (NATIVE_NO_X87) has an 80-bit long double.
#ifndef NATIVE_NO_X87
_Static_assert(sizeof(long double) == 8, "long double is double");
_Static_assert(_Alignof(long double) == 8, "long double has double's alignment");
_Static_assert(__LDBL_MANT_DIG__ == 53, "long double has double's precision");
_Static_assert(__SIZEOF_LONG_DOUBLE__ == 8, "");

struct S {
  char c;
  long double d;
};
_Static_assert(sizeof(struct S) == 16, "");
_Static_assert(__builtin_offsetof(struct S, d) == 8, "");
#endif

long double g = 1.0L;
// CHECK: @g = {{.*}}global double 1.000000e+00, align 8

long double add(long double a, long double b) { return a + b; }
// CHECK-LABEL: define {{.*}}double @add(double noundef %a, double noundef %b)
// CHECK: fadd double

_Complex long double cadd(_Complex long double a, _Complex long double b) {
  return a + b;
}
// SYSV-LABEL: define {{.*}}{ double, double } @cadd(double {{.*}}%a.coerce0, double {{.*}}%a.coerce1, double {{.*}}%b.coerce0, double {{.*}}%b.coerce1)
// WIN-LABEL: define {{.*}}void @cadd(ptr {{.*}}sret({ double, double }) {{.*}}, ptr {{.*}}%a, ptr {{.*}}%b)
// A64-LABEL: define {{.*}}{ double, double } @cadd([2 x double] {{.*}}%a.coerce, [2 x double] {{.*}}%b.coerce)
// CHECK: fadd double
// CHECK: fadd double

long double va(int n, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, n);
  long double r = __builtin_va_arg(ap, long double);
  __builtin_va_end(ap);
  return r;
}
// CHECK-LABEL: define {{.*}}double @va(i32 noundef %n, ...)
// SYSV: %fp_offset = load i32
// SYSV: icmp ule i32 %fp_offset, 160
// A64: %vr_offs = load i32
// WIN: %argp.cur = load ptr
// CHECK: load double

// CHECK: !{i32 1, !"long-double-type", !"double"}

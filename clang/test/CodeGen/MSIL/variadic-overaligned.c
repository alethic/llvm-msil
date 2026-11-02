// Over-aligned aggregates as variadic arguments (ImplementationPlan.md 5.15.2,
// M26; the section 12 row '5.15.2's SysV `slotInfo` amended (review r1, slot
// alignment)'). On the x86_64 MSIL triples off Windows, ExpandVariadics packs
// a call's variadic arguments into a buffer that the callee's va_arg reads
// with clang's x86-64 rules: an aggregate whose C alignment exceeds 8 is read
// from a slot aligned to that alignment. A coerced argument ('i64, i64')
// carries no alignment, so clang passes an unnamed (variadic) aggregate whose
// C alignment exceeds 8 'byval' with that alignment instead, and the pass
// aligns its slot to it. Named arguments, aggregates aligned to 8, the Windows
// triples (whose va_arg reads such a value through a pointer) and the native
// triples keep their lowering.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,SYSV
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,SYSV
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,WIN
// The native triple: an unnamed 16-aligned aggregate stays coerced.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=CHECK,NATIVE

struct __attribute__((aligned(16))) S16 { long a, b; };
struct __attribute__((aligned(32))) S32 { long a, b, c; };
struct __attribute__((aligned(8))) S8 { long a, b; };

void vsink(int, ...);
void nsink16(int, struct S16);
void nsink32(int, struct S32);
void nsink8(int, struct S8);

// An unnamed aligned(16) structure: byval align 16 on SysV MSIL triples.
// CHECK-LABEL: define {{.*}}void @call_v16(
// SYSV:        call void (i32, ...) @vsink(i32 noundef 1, ptr noundef byval(%struct.S16) align 16 %{{[^,)]+}})
// NATIVE:      call void (i32, ...) @vsink(i32 noundef 1, i64 %{{[^,]+}}, i64 %{{[^,)]+}})
// WIN:         call void (i32, ...) @vsink(i32 noundef 1, ptr {{.*}}align 16 {{.*}}dereferenceable(16) %byval-temp)
void call_v16(struct S16 *p) { vsink(1, *p); }

// An unnamed aligned(32) structure: byval align 32.
// CHECK-LABEL: define {{.*}}void @call_v32(
// SYSV:        call void (i32, ...) @vsink(i32 noundef 2, ptr noundef byval(%struct.S32) align 32 %{{[^,)]+}})
// NATIVE:      call void (i32, ...) @vsink(i32 noundef 2, ptr noundef byval(%struct.S32) align 32 %{{[^,)]+}})
// WIN:         call void (i32, ...) @vsink(i32 noundef 2, ptr {{.*}}align 32 {{.*}}dereferenceable(32) %byval-temp)
void call_v32(struct S32 *p) { vsink(2, *p); }

// An unnamed aggregate aligned to 8 stays coerced to scalars.
// CHECK-LABEL: define {{.*}}void @call_v8(
// SYSV:        call void (i32, ...) @vsink(i32 noundef 3, i64 %{{[^,]+}}, i64 %{{[^,)]+}})
// NATIVE:      call void (i32, ...) @vsink(i32 noundef 3, i64 %{{[^,]+}}, i64 %{{[^,)]+}})
// WIN:         call void (i32, ...) @vsink(i32 noundef 3, i64 %{{[^,)]+}})
// CHECK-NOT:   byval
// CHECK:       ret void
void call_v8(struct S8 *p) { vsink(3, *p); }

// The same structures as named arguments keep their lowering.
// CHECK-LABEL: define {{.*}}void @call_n16(
// SYSV:        call void @nsink16(i32 noundef 4, i64 %{{[^,]+}}, i64 %{{[^,)]+}})
// NATIVE:      call void @nsink16(i32 noundef 4, i64 %{{[^,]+}}, i64 %{{[^,)]+}})
// WIN:         call void @nsink16(i32 noundef 4, ptr {{.*}}align 16 {{.*}}dereferenceable(16) %byval-temp)
// CHECK-NOT:   byval(
// CHECK:       ret void
// SYSV:        declare void @nsink16(i32 noundef, i64, i64)
void call_n16(struct S16 *p) { nsink16(4, *p); }

// CHECK-LABEL: define {{.*}}void @call_n32(
// SYSV:        call void @nsink32(i32 noundef 5, ptr noundef byval(%struct.S32) align 32 %{{[^,)]+}})
// NATIVE:      call void @nsink32(i32 noundef 5, ptr noundef byval(%struct.S32) align 32 %{{[^,)]+}})
// WIN:         call void @nsink32(i32 noundef 5, ptr {{.*}}align 32 {{.*}}dereferenceable(32) %byval-temp)
// SYSV:        declare void @nsink32(i32 noundef, ptr noundef byval(%struct.S32) align 32)
void call_n32(struct S32 *p) { nsink32(5, *p); }

// CHECK-LABEL: define {{.*}}void @call_n8(
// SYSV:        call void @nsink8(i32 noundef 6, i64 %{{[^,]+}}, i64 %{{[^,)]+}})
// NATIVE:      call void @nsink8(i32 noundef 6, i64 %{{[^,]+}}, i64 %{{[^,)]+}})
// WIN:         call void @nsink8(i32 noundef 6, i64 %{{[^,)]+}})
// CHECK-NOT:   byval
// CHECK:       ret void
// SYSV:        declare void @nsink8(i32 noundef, i64, i64)
void call_n8(struct S8 *p) { nsink8(6, *p); }

// Small aggregates by value on the aarch64 MSIL triples (decision D6;
// Design.md 3 'clang reuses its native TargetInfo and ABI lowering'). clang
// classifies them with its AArch64 ABI; since upstream de7794fcf781 a non-HFA
// aggregate of fewer than 8 bytes is passed as an iN of its exact size
// (i8 .. i56) instead of an i64 on every little-endian non-Darwin AArch64
// triple, so all four Phase-1 aarch64 triples changed. In IL an i8, i16, i24
// or i32 is an int32 and an i40, i48, i56 or i64 a native int. Results of up
// to 8 bytes were already iN. 9-16 bytes are [2 x i64] (two native ints; a
// two-part result is demoted to a hidden first pointer), HFAs are arrays of
// their element type (Linux adds alignstack), and larger aggregates go by
// pointer to a copy plus sret. The IL is the same on all four triples and at
// -O0 and -O2.

// REQUIRES: msil-registered-target

// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=IR,IR-LNX
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=IR,IR-LNX
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=IR,IR-WIN
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefixes=IR,IR-WIN

// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -S -O2 -o %t.gnu.il %s
// RUN: FileCheck %s --check-prefix=IL --implicit-check-not=int64 --input-file=%t.gnu.il
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -S -O2 -o %t.musl.il %s
// RUN: FileCheck %s --check-prefix=IL --implicit-check-not=int64 --input-file=%t.musl.il
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -S -O2 -o %t.msvc.il %s
// RUN: FileCheck %s --check-prefix=IL --implicit-check-not=int64 --input-file=%t.msvc.il
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -S -O2 -o %t.mingw.il %s
// RUN: FileCheck %s --check-prefix=IL --implicit-check-not=int64 --input-file=%t.mingw.il
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -S -o %t.gnu0.il %s
// RUN: FileCheck %s --check-prefix=IL --implicit-check-not=int64 --input-file=%t.gnu0.il
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -S -o %t.msvc0.il %s
// RUN: FileCheck %s --check-prefix=IL --implicit-check-not=int64 --input-file=%t.msvc0.il
// RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t.gnu.dll %t.gnu.il %}
// RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t.msvc0.dll %t.msvc0.il %}

struct S1 { char a; };
struct S2 { short a; };
struct S3 { char a, b, c; };
struct S4 { int a; };
struct S5 { char a[5]; };
struct S6 { short a[3]; };
struct S7 { char a[7]; };
struct S8 { int a, b; };
struct S12 { int a, b, c; };
struct S16 { long long a, b; };
struct SIF { int a; float b; };
struct SCF { char a; float b; };
struct HFA2 { float a, b; };
struct HFA3 { float a, b, c; };
struct HFA4 { double a, b, c, d; };
struct HD1 { double a; };
struct HFAArr { float a[3]; };
struct SP2 { void *a, *b; };
struct S24 { int a[6]; };

// IR-LABEL: define {{.*}}i8 @f1(i8 %x.coerce)
// IL:       {{^}}.method public static int32 'f1'(int32) cil managed{{$}}
struct S1 f1(struct S1 x) { return x; }
// IR-LABEL: define {{.*}}i16 @f2(i16 %x.coerce)
// IL:       {{^}}.method public static int32 'f2'(int32) cil managed{{$}}
struct S2 f2(struct S2 x) { return x; }
// IR-LABEL: define {{.*}}i24 @f3(i24 %x.coerce)
// IL:       {{^}}.method public static int32 'f3'(int32) cil managed{{$}}
struct S3 f3(struct S3 x) { return x; }
// IR-LABEL: define {{.*}}i32 @f4(i32 %x.coerce)
// IL:       {{^}}.method public static int32 'f4'(int32) cil managed{{$}}
struct S4 f4(struct S4 x) { return x; }
// IR-LABEL: define {{.*}}i40 @f5(i40 %x.coerce)
// IL:       {{^}}.method public static native int 'f5'(native int) cil managed{{$}}
struct S5 f5(struct S5 x) { return x; }
// IR-LABEL: define {{.*}}i48 @f6(i48 %x.coerce)
// IL:       {{^}}.method public static native int 'f6'(native int) cil managed{{$}}
struct S6 f6(struct S6 x) { return x; }
// IR-LABEL: define {{.*}}i56 @f7(i56 %x.coerce)
// IL:       {{^}}.method public static native int 'f7'(native int) cil managed{{$}}
struct S7 f7(struct S7 x) { return x; }
// IR-LABEL: define {{.*}}i64 @f8(i64 %x.coerce)
// IL:       {{^}}.method public static native int 'f8'(native int) cil managed{{$}}
struct S8 f8(struct S8 x) { return x; }
// IR-LABEL: define {{.*}}[2 x i64] @f12([2 x i64] %x.coerce)
// IL:       {{^}}.method public static void 'f12'(native int, native int, native int) cil managed{{$}}
struct S12 f12(struct S12 x) { return x; }
// IR-LABEL: define {{.*}}[2 x i64] @f16([2 x i64] %x.coerce)
// IL:       {{^}}.method public static void 'f16'(native int, native int, native int) cil managed{{$}}
struct S16 f16(struct S16 x) { return x; }
// IR-LABEL: define {{.*}}i64 @fif(i64 %x.coerce)
// IL:       {{^}}.method public static native int 'fif'(native int) cil managed{{$}}
struct SIF fif(struct SIF x) { return x; }
// IR-LABEL: define {{.*}}i64 @fcf(i64 %x.coerce)
// IL:       {{^}}.method public static native int 'fcf'(native int) cil managed{{$}}
struct SCF fcf(struct SCF x) { return x; }
// IR-LNX-LABEL: define {{.*}}%struct.HFA2 @fh2([2 x float] alignstack(8) %x.coerce)
// IR-WIN-LABEL: define {{.*}}%struct.HFA2 @fh2([2 x float] %x.coerce)
// IL:       {{^}}.method public static void 'fh2'(native int, float32, float32) cil managed{{$}}
struct HFA2 fh2(struct HFA2 x) { return x; }
// IR-LNX-LABEL: define {{.*}}%struct.HFA3 @fh3([3 x float] alignstack(8) %x.coerce)
// IR-WIN-LABEL: define {{.*}}%struct.HFA3 @fh3([3 x float] %x.coerce)
// IL:       {{^}}.method public static void 'fh3'(native int, float32, float32, float32) cil managed{{$}}
struct HFA3 fh3(struct HFA3 x) { return x; }
// IR-LNX-LABEL: define {{.*}}%struct.HFA4 @fh4([4 x double] alignstack(8) %x.coerce)
// IR-WIN-LABEL: define {{.*}}%struct.HFA4 @fh4([4 x double] %x.coerce)
// IL:       {{^}}.method public static void 'fh4'(native int, float64, float64, float64, float64) cil managed{{$}}
struct HFA4 fh4(struct HFA4 x) { return x; }
// IR-LNX-LABEL: define {{.*}}%struct.HD1 @fhd1([1 x double] alignstack(8) %x.coerce)
// IR-WIN-LABEL: define {{.*}}%struct.HD1 @fhd1([1 x double] %x.coerce)
// IL:       {{^}}.method public static float64 'fhd1'(float64) cil managed{{$}}
struct HD1 fhd1(struct HD1 x) { return x; }
// IR-LNX-LABEL: define {{.*}}%struct.HFAArr @fharr([3 x float] alignstack(8) %x.coerce)
// IR-WIN-LABEL: define {{.*}}%struct.HFAArr @fharr([3 x float] %x.coerce)
// IL:       {{^}}.method public static void 'fharr'(native int, float32, float32, float32) cil managed{{$}}
struct HFAArr fharr(struct HFAArr x) { return x; }
// IR-LABEL: define {{.*}}[2 x i64] @fsp2([2 x ptr] %x.coerce)
// IL:       {{^}}.method public static void 'fsp2'(native int, native int, native int) cil managed{{$}}
struct SP2 fsp2(struct SP2 x) { return x; }
// IR-LABEL: define {{.*}}void @fs24(ptr {{.*}}sret(%struct.S24) align 4 %agg.result, ptr {{.*}}dead_on_return dereferenceable(24) %x)
// IL:       {{^}}.method public static void 'fs24'(native int, native int) cil managed{{$}}
struct S24 fs24(struct S24 x) { return x; }

// The callers pass the same shapes (the stores keep the calls at -O2).
volatile int sink;
// IR-LABEL: define {{.*}}void @g3(i24 %x.coerce)
// IL:       {{^}}.method public static void 'g3'(int32) cil managed{{$}}
__attribute__((noinline)) void g3(struct S3 x) { sink = x.c; }
// IR-LABEL: define {{.*}}void @g5(i40 %x.coerce)
// IL:       {{^}}.method public static void 'g5'(native int) cil managed{{$}}
__attribute__((noinline)) void g5(struct S5 x) { sink = x.a[4]; }
// IR-LABEL: define {{.*}}void @call_small(ptr {{.*}}%p, ptr {{.*}}%q)
// IR:       call void @g3(i24 %{{.*}})
// IR:       call void @g5(i40 %{{.*}})
// IL:       {{^}}.method public static void 'call_small'(native int, native int) cil managed{{$}}
// IL:       call void 'g3'(int32)
// IL:       call void 'g5'(native int)
void call_small(struct S3 *p, struct S5 *q) {
  g3(*p);
  g5(*q);
}

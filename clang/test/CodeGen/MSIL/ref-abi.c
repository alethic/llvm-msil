// .NET references at the C ABI (ImplementationPlan.md 5.21.5 RG1, RG6 and
// 5.21.14 RV2, milestone R-a): a reference is target("msil.ref") in the IR and
// is passed and returned Direct on all ten MSIL triples with no ABI hook,
// never as 'ptr', an integer, 'byval' or 'sret', whatever the other
// parameters are (integers, a double, a _Bool, nine references, more than any
// register set holds), in definitions, calls and a managed call's
// declaration. The experimental ABI lowering library
// (-fexperimental-abi-lowering, QualTypeMapper.cpp) cannot lower the type:
// on the six MSIL triples whose lowering uses it (x86_64 and aarch64 Linux,
// aarch64 Windows), clang stops with a fatal usage error, 'error in backend:
// Type not supported in ABI lowering', and exit status 1, not with an
// internal error (exit status 70 and crash diagnostics, as for the HLSL
// types) (EXPABI). The EXPABI runs go through the driver, which reports
// clang's job failing with any status but 1, or crashing, as 'clang frontend
// command failed'. (On Windows, LLVM's fatal error handler prints the stack
// trace banner after a usage error too, so the banner does not tell the two
// apart.)

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -emit-llvm -o - %s \
// RUN:   | FileCheck %s --implicit-check-not=byval --implicit-check-not=sret
// RUN: %clang_cc1 -triple x86_64-unknown-linux-musl-msil -emit-llvm -o - %s \
// RUN:   | FileCheck %s --implicit-check-not=byval --implicit-check-not=sret
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu-msil -emit-llvm -o - %s \
// RUN:   | FileCheck %s --implicit-check-not=byval --implicit-check-not=sret
// RUN: %clang_cc1 -triple aarch64-unknown-linux-musl-msil -emit-llvm -o - %s \
// RUN:   | FileCheck %s --implicit-check-not=byval --implicit-check-not=sret
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -emit-llvm -o - %s \
// RUN:   | FileCheck %s --implicit-check-not=byval --implicit-check-not=sret
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu-msil -emit-llvm -o - %s \
// RUN:   | FileCheck %s --implicit-check-not=byval --implicit-check-not=sret
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc-msil -emit-llvm -o - %s \
// RUN:   | FileCheck %s --implicit-check-not=byval --implicit-check-not=sret
// RUN: %clang_cc1 -triple aarch64-w64-windows-gnu-msil -emit-llvm -o - %s \
// RUN:   | FileCheck %s --implicit-check-not=byval --implicit-check-not=sret
// RUN: %clang_cc1 -triple msil64-unknown-dotnet -emit-llvm -o - %s \
// RUN:   | FileCheck %s --implicit-check-not=byval --implicit-check-not=sret
// RUN: %clang_cc1 -triple msil32-unknown-dotnet -emit-llvm -o - %s \
// RUN:   | FileCheck %s --implicit-check-not=byval --implicit-check-not=sret
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -O2 -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=O2 --implicit-check-not=byval --implicit-check-not=sret
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -O2 -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=O2 --implicit-check-not=byval --implicit-check-not=sret

// RUN: not %clang --target=x86_64-unknown-linux-gnu-msil -Xclang -fexperimental-abi-lowering -fno-crash-diagnostics -S -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=EXPABI --implicit-check-not="frontend command failed"
// RUN: not %clang --target=x86_64-unknown-linux-musl-msil -Xclang -fexperimental-abi-lowering -fno-crash-diagnostics -S -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=EXPABI --implicit-check-not="frontend command failed"
// RUN: not %clang --target=aarch64-unknown-linux-gnu-msil -Xclang -fexperimental-abi-lowering -fno-crash-diagnostics -S -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=EXPABI --implicit-check-not="frontend command failed"
// RUN: not %clang --target=aarch64-unknown-linux-musl-msil -Xclang -fexperimental-abi-lowering -fno-crash-diagnostics -S -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=EXPABI --implicit-check-not="frontend command failed"
// RUN: not %clang --target=aarch64-pc-windows-msvc-msil -Xclang -fexperimental-abi-lowering -fno-crash-diagnostics -S -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=EXPABI --implicit-check-not="frontend command failed"
// RUN: not %clang --target=aarch64-w64-windows-gnu-msil -Xclang -fexperimental-abi-lowering -fno-crash-diagnostics -S -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=EXPABI --implicit-check-not="frontend command failed"
// EXPABI: error in backend: Type not supported in ABI lowering{{$}}

typedef struct __attribute__((cli_type("System.String"))) String String;
typedef struct __attribute__((cli_type("System.Object"))) Object Object;

String ^m_copy(String ^s) __attribute__((msil_method("System.String", "Copy", "string(string)")));

__attribute__((noinline)) static String ^r_id(String ^s) { return s; }
__attribute__((noinline)) static Object ^r_mixed(int i, String ^s, double d, Object ^o, _Bool b) {
  return b ? o : (i + d > 0 ? s : 0);
}
__attribute__((noinline)) static int r_many(String ^a, String ^b, String ^c, String ^d, String ^e, String ^f,
                                            String ^g, String ^h, String ^i) {
  return (a == b) + (c == d) + (e == f) + (g == h) + (i == 0);
}

// CHECK-LABEL: define {{.*}}i32 @test()
// CHECK:       call target("msil.ref") @__msil_mc.m_copy(target("msil.ref") zeroinitializer)
// CHECK:       call i32 @r_many(target("msil.ref") %{{[0-9]+}}, target("msil.ref") %{{[0-9]+}}, target("msil.ref") %{{[0-9]+}}, target("msil.ref") %{{[0-9]+}}, target("msil.ref") %{{[0-9]+}}, target("msil.ref") %{{[0-9]+}}, target("msil.ref") %{{[0-9]+}}, target("msil.ref") %{{[0-9]+}}, target("msil.ref") %{{[0-9]+}})
// CHECK:       call target("msil.ref") @r_id(target("msil.ref") %{{[0-9]+}})
// CHECK:       call target("msil.ref") @r_mixed(i32 noundef 1, target("msil.ref") %{{[0-9]+}}, double noundef 2.000000e+00, target("msil.ref") %{{[0-9]+}}, i1 noundef {{(zeroext )?}}true)
// CHECK-DAG:   declare target("msil.ref") @__msil_mc.m_copy(target("msil.ref"))
// CHECK-DAG:   define internal target("msil.ref") @r_id(target("msil.ref") %s)
// CHECK-DAG:   define internal target("msil.ref") @r_mixed(i32 noundef %i, target("msil.ref") %s, double noundef %d, target("msil.ref") %o, i1 noundef {{(zeroext )?}}%b)
// CHECK-DAG:   define internal i32 @r_many(target("msil.ref") %a, target("msil.ref") %b, target("msil.ref") %c, target("msil.ref") %d, target("msil.ref") %e, target("msil.ref") %f, target("msil.ref") %g, target("msil.ref") %h, target("msil.ref") %i)

// The optimizer keeps the reference's IR type in a signature it keeps (it may
// change the calling convention of an internal function and fold the other
// calls away).
// O2: define internal {{.*}}i32 @r_many(target("msil.ref") {{.*}}%a, target("msil.ref") {{.*}}%b, {{.*}})

int test(void) {
  String ^s = m_copy(0); // an opaque reference, so -O2 keeps every parameter
  int r = r_many(s, s, s, s, s, s, s, s, s);
  return r + (r_id(s) == r_mixed(1, s, 2.0, s, 1));
}

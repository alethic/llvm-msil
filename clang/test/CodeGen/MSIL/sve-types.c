// The SVE types on the aarch64 MSIL triples are diagnosed, not a crash
// (review-r2 finding 2, M10 fix-loop row for fix-7; ImplementationPlan.md
// section 2, 'Diagnostics leave well-formed output'). D7 accepts every native
// feature string, so -march=armv8-a+sve and __attribute__((target("sve2p1")))
// make clang accept the SVE types: __SVInt32_t is <vscale x 4 x i32> and
// __SVCount_t is target("aarch64.svcount") in the IR. CIL has neither
// (Design.md 3.2 maps only fixed-length vectors), so MSILPrepareIR reports the
// type at the function and deletes its body. clang used to stop with 'fatal
// error: error in backend: Don't know how to legalize this scalable vector
// type' or an assertion for __SVCount_t, with a stack dump and exit code 74,
// and an unused local __SVInt32_t compiled at -O0 with vscale taken as 1.
// arm_sve.h is not in this build's resource headers, so the builtin type names
// are used. Without -march=armv8-a+sve only the target("sve2p1") function is
// compiled (NO_SVE): the other uses of the types need the feature in Sema.

// REQUIRES: msil-registered-target

// DEFINE: %{flags} = -ffreestanding -S -o /dev/null
// DEFINE: %{err} = FileCheck %s --implicit-check-not=error: --implicit-check-not="error in backend" \
// DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not="Stack dump"

// RUN: not %clang --target=aarch64-unknown-linux-gnu-msil -march=armv8-a+sve %{flags} -O0 %s 2>&1 \
// RUN:   | %{err} --check-prefixes=SVE,SVE-O0,SVCOUNT
// RUN: not %clang --target=aarch64-unknown-linux-gnu-msil -march=armv8-a+sve %{flags} -O2 %s 2>&1 \
// RUN:   | %{err} --check-prefixes=SVE,SVCOUNT
// RUN: not %clang --target=aarch64-pc-windows-msvc-msil -march=armv8-a+sve %{flags} -O0 %s 2>&1 \
// RUN:   | %{err} --check-prefixes=SVE,SVE-O0,SVCOUNT
// RUN: not %clang --target=aarch64-pc-windows-msvc-msil -march=armv8-a+sve %{flags} -O2 %s 2>&1 \
// RUN:   | %{err} --check-prefixes=SVE,SVCOUNT

// RUN: not %clang --target=aarch64-unknown-linux-gnu-msil -DNO_SVE %{flags} -O0 %s 2>&1 \
// RUN:   | %{err} --check-prefix=SVCOUNT
// RUN: not %clang --target=aarch64-unknown-linux-gnu-msil -DNO_SVE %{flags} -O2 %s 2>&1 \
// RUN:   | %{err} --check-prefix=SVCOUNT
// RUN: not %clang --target=aarch64-pc-windows-msvc-msil -DNO_SVE %{flags} -O0 %s 2>&1 \
// RUN:   | %{err} --check-prefix=SVCOUNT
// RUN: not %clang --target=aarch64-pc-windows-msvc-msil -DNO_SVE %{flags} -O2 %s 2>&1 \
// RUN:   | %{err} --check-prefix=SVCOUNT

#ifndef NO_SVE
// The type in the signature (and, at -O0, in the alloca, load and store of the
// parameter).
// SVE: {{.*}}sve-types.c:[[@LINE+1]]:{{[0-9]+}}: error: scalable vector type '<vscale x 4 x i32>' is not supported for MSIL{{$}}
__SVInt32_t sve_identity(__SVInt32_t a) { return a; }

// Only in the alloca of the unused local, which -O2 deletes.
// SVE-O0: {{.*}}sve-types.c:[[@LINE+1]]:{{[0-9]+}}: error: scalable vector type '<vscale x 4 x i32>' is not supported for MSIL{{$}}
int sve_local(void) {
  __SVInt32_t v;
  return 0;
}
#endif

// SVCOUNT: {{.*}}sve-types.c:[[@LINE+2]]:{{[0-9]+}}: error: type 'target("aarch64.svcount")' is not supported for MSIL{{$}}
__attribute__((target("sve2p1")))
__SVCount_t svcount_identity(__SVCount_t c) { return c; }

// A function without SVE types compiles.
int after(int a) { return a + 1; }

#ifndef NO_SVE
// The address of a declaration with an SVE type in its signature: the
// AsmPrinter, which computes the CIL signature of an undefined function whose
// address is taken, used to reach 'UNREACHABLE executed' for __SVCount_t. The
// declarations are reported after the definitions.
void sve_ext(__SVInt32_t);
void svcount_ext(__SVCount_t);
void *sve_ext_address(void) { return (void *)sve_ext; }
void *svcount_ext_address(void) { return (void *)svcount_ext; }
// SVE: {{.*}}sve-types.c:[[@LINE-4]]:{{[0-9]+}}: error: scalable vector type '<vscale x 4 x i32>' is not supported for MSIL{{$}}
// SVE: {{.*}}sve-types.c:[[@LINE-4]]:{{[0-9]+}}: error: type 'target("aarch64.svcount")' is not supported for MSIL{{$}}
#endif

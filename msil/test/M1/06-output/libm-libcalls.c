// C math library calls that exist only as library calls made by instruction
// selection (Milestone 8, D14). Nothing here declares a math function: the IR
// declares only puts and LLVM intrinsics, llvm.sqrt, llvm.floor and llvm.fma
// among them (whatever the math-errno default, which is on for both x86_64
// triples), and FSQRT, FFLOOR and FMA have no CIL instruction, so LowerCall
// gets sqrt, sqrtf, floor, floorf, fma and fmaf as external symbols with no IR
// declaration behind them. Each must still become a P/Invoke into the triple's
// math library: ucrtbase on windows-msvc, libm.so.6 on linux-gnu (the WSL leg).
// The other libm tests mostly bind to IR declarations instead: libm-calls.c
// declares the functions it calls, and libm-sqrt.c gets a call of a declared
// sqrt. Inputs are read from volatile globals, so nothing is folded at -O2.
// Every result is exactly specified, and the fma checks fail unless the
// multiply and the add are fused, so a wrong entry point or argument order
// shows. MSIL_EXPECT returns 1 + (line % 255) at the first failed check;
// [exit 0] means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

volatile double in_d[4] = {6.25, -2.5, 1.0 / 1073741824.0, 152399025.0};
volatile float in_f[4] = {2.25f, -0.5f, 1.0f / 4096.0f, 16777216.0f};
volatile int zero = 0;

static int check_sqrt(void) {
  // llvm.sqrt.f64 and llvm.sqrt.f32: library calls of sqrt and sqrtf.
  double d = __builtin_elementwise_sqrt(in_d[0]);
  float f = __builtin_elementwise_sqrt(in_f[0]);
  MSIL_EXPECT(d == 2.5);
  MSIL_EXPECT(f == 1.5f);
  // Exact square roots: 12345^2 and 4096^2.
  MSIL_EXPECT(__builtin_elementwise_sqrt(in_d[3]) == 12345.0);
  MSIL_EXPECT(__builtin_elementwise_sqrt(in_f[3]) == 4096.0f);
  put_i64((long long)(d * 100.0));
  put_i64((long long)(f * 100.0f));
  return 0;
}

static int check_floor(void) {
  // llvm.floor.f64 and llvm.floor.f32: library calls of floor and floorf.
  double d = __builtin_floor(in_d[1]);
  float f = __builtin_floorf(in_f[1]);
  MSIL_EXPECT(d == -3.0);
  MSIL_EXPECT(f == -1.0f);
  MSIL_EXPECT(__builtin_floor(in_d[0]) == 6.0);
  MSIL_EXPECT(__builtin_floorf(in_f[0]) == 2.0f);
  put_i64((long long)d);
  put_i64((long long)f);
  return 0;
}

static int check_fma(void) {
  // llvm.fma.f64 and llvm.fma.f32: library calls of fma and fmaf.
  MSIL_EXPECT(__builtin_fma(in_d[0], 2.0, in_d[1]) == 10.0);
  MSIL_EXPECT(__builtin_fmaf(in_f[0], 2.0f, in_f[1]) == 4.0f);
  // Fused: (1 + 2^-30)^2 - 1 is 2^-29 + 2^-60 only without an intermediate
  // rounding; a multiply and an add would give 2^-29.
  double e30 = in_d[2];
  double r = __builtin_fma(1.0 + e30, 1.0 + e30, -1.0);
  MSIL_EXPECT(r == 2.0 * e30 + e30 * e30);
  MSIL_EXPECT(r != 2.0 * e30);
  // The same in float: (1 + 2^-12)^2 - 1 is 2^-11 + 2^-24, not 2^-11.
  float e12 = in_f[2];
  float rf = __builtin_fmaf(1.0f + e12, 1.0f + e12, -1.0f);
  MSIL_EXPECT(rf == 2.0f * e12 + e12 * e12);
  MSIL_EXPECT(rf != 2.0f * e12);
  // The scaled results: 2^-29 + 2^-60 times 2^60 is 2^31 + 1.
  put_i64((long long)(r * 1152921504606846976.0));
  // 2^-11 + 2^-24 times 2^24 is 2^13 + 1.
  put_i64((long long)(rf * 16777216.0f));
  return 0;
}

int main(void) {
  int r;
  if ((r = check_sqrt()) != 0)
    return r;
  put_str("sqrt ok");
  if ((r = check_floor()) != 0)
    return r;
  put_str("floor ok");
  if ((r = check_fma()) != 0)
    return r;
  put_str("fma ok");
  return zero;
}

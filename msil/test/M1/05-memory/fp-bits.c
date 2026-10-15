// fabs, copysign, negation, sign bits and signed zeros from C, checked
// through bit patterns (Milestone 7, D16: the generic expansions of fabs and
// copysign work on the integer bits through a bitcast and a stack slot, so
// they moved here from M5): __builtin_fabs{,f}, __builtin_copysign{,f}, unary
// minus, __builtin_signbit{,f}, __builtin_fpclassify, __builtin_isnan,
// __builtin_isinf, __builtin_isinf_sign, __builtin_isfinite and
// __builtin_isnormal (-O2 turns several into llvm.is.fpclass) on float64 and
// float32 values built from bit patterns: both zeros, +-1, both infinities,
// quiet NaNs of both signs with payloads (fabs, copysign and negation change
// only the sign bit), subnormals, the smallest normal and the largest finite
// values; plus -0.0 produced by arithmetic (0.0 * -1.0, -0.0 + -0.0,
// -0.0 - 0.0, negation, float/double conversion) and +0.0 where IEEE
// arithmetic gives it (-0.0 + 0.0, x - x). Each result is compared with the
// bits the reference computes with integer operations. Values are derived
// from argc (1 when run without arguments) and pass through noinline helpers,
// so clang cannot fold them at -O2. MSIL_EXPECT returns 1 + (line % 255) at
// the first failed check; [exit 0] means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))
#define TRY(call)                                                              \
  do {                                                                         \
    int r_ = (call);                                                           \
    if (r_ != 0)                                                               \
      return r_;                                                               \
  } while (0)

typedef unsigned u32;
typedef unsigned long long u64;

#define SIGN64 0x8000000000000000ull
#define SIGN32 0x80000000u

// The class numbers passed to __builtin_fpclassify.
enum { C_NAN, C_INF, C_NORMAL, C_SUBNORMAL, C_ZERO };

static NOINLINE u64 d2u(double d) {
  union {
    double d;
    u64 u;
  } x;
  x.d = d;
  return x.u;
}
static NOINLINE double u2d(u64 u) {
  union {
    double d;
    u64 u;
  } x;
  x.u = u;
  return x.d;
}
static NOINLINE u32 f2u(float f) {
  union {
    float f;
    u32 u;
  } x;
  x.f = f;
  return x.u;
}
static NOINLINE float u2f(u32 u) {
  union {
    float f;
    u32 u;
  } x;
  x.u = u;
  return x.f;
}

static NOINLINE double fabs_d(double x) { return __builtin_fabs(x); }
static NOINLINE float fabs_f(float x) { return __builtin_fabsf(x); }
static NOINLINE double copysign_d(double x, double y) {
  return __builtin_copysign(x, y);
}
static NOINLINE float copysign_f(float x, float y) {
  return __builtin_copysignf(x, y);
}
static NOINLINE double neg_d(double x) { return -x; }
static NOINLINE float neg_f(float x) { return -x; }
static NOINLINE int signbit_d(double x) { return __builtin_signbit(x) != 0; }
static NOINLINE int signbit_f(float x) { return __builtin_signbitf(x) != 0; }
static NOINLINE int classify_d(double x) {
  return __builtin_fpclassify(C_NAN, C_INF, C_NORMAL, C_SUBNORMAL, C_ZERO, x);
}
static NOINLINE int classify_f(float x) {
  return __builtin_fpclassify(C_NAN, C_INF, C_NORMAL, C_SUBNORMAL, C_ZERO, x);
}
// Bit 0 isnan, bit 1 isinf, bit 2 isfinite, bit 3 isnormal, bits 4-5
// isinf_sign + 1.
static NOINLINE int flags_d(double x) {
  return !!__builtin_isnan(x) | !!__builtin_isinf(x) << 1 |
         !!__builtin_isfinite(x) << 2 | !!__builtin_isnormal(x) << 3 |
         (__builtin_isinf_sign(x) + 1) << 4;
}
static NOINLINE int flags_f(float x) {
  return !!__builtin_isnan(x) | !!__builtin_isinf(x) << 1 |
         !!__builtin_isfinite(x) << 2 | !!__builtin_isnormal(x) << 3 |
         (__builtin_isinf_sign(x) + 1) << 4;
}

// The class and the flags, from the bits.
static int ref_class(u64 exp, u64 mant, u64 exp_max) {
  if (exp == exp_max)
    return mant ? C_NAN : C_INF;
  if (exp == 0)
    return mant ? C_SUBNORMAL : C_ZERO;
  return C_NORMAL;
}
static int ref_flags(int c, int neg) {
  int inf_sign = c == C_INF ? (neg ? -1 : 1) : 0;
  return (c == C_NAN) | (c == C_INF) << 1 | (c > C_INF) << 2 |
         (c == C_NORMAL) << 3 | (inf_sign + 1) << 4;
}

static NOINLINE int check_double(u64 b) {
  double v = u2d(b);
  u64 sign = b & SIGN64;
  int c = ref_class((b >> 52) & 0x7FF, b & 0xFFFFFFFFFFFFFull, 0x7FF);
  MSIL_EXPECT(d2u(fabs_d(v)) == (b & ~SIGN64));
  MSIL_EXPECT(d2u(neg_d(v)) == (b ^ SIGN64));
  MSIL_EXPECT(d2u(copysign_d(v, 1.0)) == (b & ~SIGN64));
  MSIL_EXPECT(d2u(copysign_d(v, -0.0)) == (b | SIGN64));
  MSIL_EXPECT(d2u(copysign_d(v, v)) == b);
  MSIL_EXPECT(d2u(copysign_d(2.5, v)) == (0x4004000000000000ull | sign));
  MSIL_EXPECT(d2u(copysign_d(-0.0, v)) == sign);
  MSIL_EXPECT(signbit_d(v) == (sign != 0));
  MSIL_EXPECT(classify_d(v) == c);
  MSIL_EXPECT(flags_d(v) == ref_flags(c, sign != 0));
  return 0;
}

static NOINLINE int check_float(u32 b) {
  float v = u2f(b);
  u32 sign = b & SIGN32;
  int c = ref_class((b >> 23) & 0xFF, b & 0x7FFFFFu, 0xFF);
  MSIL_EXPECT(f2u(fabs_f(v)) == (b & ~SIGN32));
  MSIL_EXPECT(f2u(neg_f(v)) == (b ^ SIGN32));
  MSIL_EXPECT(f2u(copysign_f(v, 1.0f)) == (b & ~SIGN32));
  MSIL_EXPECT(f2u(copysign_f(v, -0.0f)) == (b | SIGN32));
  MSIL_EXPECT(f2u(copysign_f(v, v)) == b);
  MSIL_EXPECT(f2u(copysign_f(2.5f, v)) == (0x40200000u | sign));
  MSIL_EXPECT(f2u(copysign_f(-0.0f, v)) == sign);
  MSIL_EXPECT(signbit_f(v) == (sign != 0));
  MSIL_EXPECT(classify_f(v) == c);
  MSIL_EXPECT(flags_f(v) == ref_flags(c, sign != 0));
  return 0;
}

static NOINLINE int check_special_values(int one) {
  u64 d[14];
  d[0] = 0;                      // +0
  d[1] = SIGN64;                 // -0
  d[2] = 0x3FF0000000000000ull;  // 1
  d[3] = 0xBFF0000000000000ull;  // -1
  d[4] = 0x7FF0000000000000ull;  // +inf
  d[5] = 0xFFF0000000000000ull;  // -inf
  d[6] = 0x7FF8000000000001ull;  // +qNaN, payload 1
  d[7] = 0xFFF8000000000123ull;  // -qNaN, payload 0x123
  d[8] = 1;                      // smallest subnormal
  d[9] = 0x800FFFFFFFFFFFFFull;  // largest subnormal, negative
  d[10] = 0x0010000000000000ull; // smallest normal
  d[11] = 0xFFEFFFFFFFFFFFFFull; // -largest finite
  d[12] = 0xC004000000000000ull; // -2.5
  d[13] = 0x7FFC00000000ABCDull; // +qNaN, another payload
  for (int i = 0; i < 14; i++)
    TRY(check_double(d[i] * one));

  u32 f[14];
  f[0] = 0;
  f[1] = SIGN32;
  f[2] = 0x3F800000u;
  f[3] = 0xBF800000u;
  f[4] = 0x7F800000u;
  f[5] = 0xFF800000u;
  f[6] = 0x7FC00001u;
  f[7] = 0xFFC00123u;
  f[8] = 1;
  f[9] = 0x807FFFFFu;
  f[10] = 0x00800000u;
  f[11] = 0xFF7FFFFFu;
  f[12] = 0xC0200000u;
  f[13] = 0x7FE0ABCDu;
  for (int i = 0; i < 14; i++)
    TRY(check_float(f[i] * one));
  return 0;
}

static NOINLINE int check_signed_zeros(int one) {
  double z = 0.0 * one, m1 = -1.0 * one;
  double nz = z * m1;
  MSIL_EXPECT(d2u(nz) == SIGN64);
  MSIL_EXPECT(nz == 0.0 && !(nz < 0.0) && signbit_d(nz));
  MSIL_EXPECT(d2u(-z) == SIGN64 && d2u(-nz) == 0);
  MSIL_EXPECT(d2u(nz + nz) == SIGN64);
  MSIL_EXPECT(d2u(nz + z) == 0 && d2u(z + nz) == 0);
  MSIL_EXPECT(d2u(nz - z) == SIGN64 && d2u(z - z) == 0);
  MSIL_EXPECT(d2u(nz * nz) == 0 && d2u(nz * 3.0) == SIGN64);
  double x = 7.25 * one;
  MSIL_EXPECT(d2u(x - x) == 0 && d2u(-x + x) == 0);
  MSIL_EXPECT((int)nz == 0);
  MSIL_EXPECT(d2u(fabs_d(nz)) == 0 && d2u(copysign_d(z, m1)) == SIGN64);

  float zf = 0.0f * one, m1f = -1.0f * one;
  float nzf = zf * m1f;
  MSIL_EXPECT(f2u(nzf) == SIGN32 && nzf == 0.0f);
  MSIL_EXPECT(f2u(nzf + zf) == 0 && f2u(nzf + nzf) == SIGN32);
  // Conversions keep the sign of zero.
  MSIL_EXPECT(d2u((double)nzf) == SIGN64);
  MSIL_EXPECT(f2u((float)nz) == SIGN32);
  MSIL_EXPECT(f2u((float)u2d(SIGN64 | one)) == SIGN32);
  return 0;
}

int main(int argc, char **argv) {
  (void)argv;
  TRY(check_special_values(argc));
  TRY(check_signed_zeros(argc));
  return 0;
}

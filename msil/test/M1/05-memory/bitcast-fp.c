// Bitcasts between floating-point and integer values from C (Milestone 7,
// D16): through unions (stores and loads of different types at -O0, an LLVM
// bitcast at -O2, which the backend expands through a stack slot) and
// through __builtin_memcpy, for float32 <-> uint32 and float64 <-> uint64, in
// both directions: exact bit patterns of normal, subnormal, infinite, zero
// and quiet NaN values (the NaN payload must survive), round trips of 300
// pseudo-random non-NaN bit patterns, arithmetic on values built from bit
// patterns, the halves of a float64 through a union with a uint32 array
// (little-endian), and float bytes through an unsigned char view. Results are
// compared as integers or with ==, never printed. Values are derived from
// argc (1 when run without arguments) and pass through noinline helpers, so
// clang cannot fold them at -O2. MSIL_EXPECT returns 1 + (line % 255) at the
// first failed check; [exit 0] means every check passed.
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

union fu {
  float f;
  u32 u;
};
union du {
  double d;
  u64 u;
};
union dw {
  double d;
  u32 w[2];
};

static NOINLINE u32 f2u(float f) {
  union fu x;
  x.f = f;
  return x.u;
}
static NOINLINE float u2f(u32 u) {
  union fu x;
  x.u = u;
  return x.f;
}
static NOINLINE u64 d2u(double d) {
  union du x;
  x.d = d;
  return x.u;
}
static NOINLINE double u2d(u64 u) {
  union du x;
  x.u = u;
  return x.d;
}
static NOINLINE u32 f2u_mc(float f) {
  u32 u;
  __builtin_memcpy(&u, &f, sizeof u);
  return u;
}
static NOINLINE float u2f_mc(u32 u) {
  float f;
  __builtin_memcpy(&f, &u, sizeof f);
  return f;
}
static NOINLINE u64 d2u_mc(double d) {
  u64 u;
  __builtin_memcpy(&u, &d, sizeof u);
  return u;
}
static NOINLINE double u2d_mc(u64 u) {
  double d;
  __builtin_memcpy(&d, &u, sizeof d);
  return d;
}

static NOINLINE int check_patterns(int one) {
  float f1 = (float)one;
  double d1 = (double)one;

  MSIL_EXPECT(f2u(f1) == 0x3F800000u && f2u_mc(f1) == 0x3F800000u);
  MSIL_EXPECT(f2u(-2.5f * f1) == 0xC0200000u);
  MSIL_EXPECT(f2u_mc(0.1f * f1) == 0x3DCCCCCDu);
  MSIL_EXPECT(f2u(0.0f * f1) == 0 && f2u_mc(-0.0f * f1) == 0x80000000u);
  MSIL_EXPECT(d2u(d1) == 0x3FF0000000000000ull);
  MSIL_EXPECT(d2u_mc(-2.5 * d1) == 0xC004000000000000ull);
  MSIL_EXPECT(d2u(0.1 * d1) == 0x3FB999999999999Aull);
  MSIL_EXPECT(d2u_mc(-0.0 * d1) == 0x8000000000000000ull);
  MSIL_EXPECT(d2u(1e300 * d1 * 1e300) == 0x7FF0000000000000ull);

  // From bits to values.
  MSIL_EXPECT(u2f(0x3F800000u * one) == 1.0f);
  MSIL_EXPECT(u2f_mc(0xC0490FDBu * one) == -3.14159274101257324f);
  MSIL_EXPECT(u2d(0x400921FB54442D18ull * one) == 3.141592653589793);
  MSIL_EXPECT(u2d_mc(0xC000000000000000ull * one) == -2.0);
  // The smallest subnormals and the largest finite values.
  MSIL_EXPECT(u2f(1u * one) * 0x1p100f * 0x1p49f == 1.0f);
  MSIL_EXPECT(u2d_mc(1ull * one) * 0x1p1000 * 0x1p74 == 1.0);
  MSIL_EXPECT(u2f_mc(0x7F7FFFFFu * one) == 0x1.fffffep127f);
  MSIL_EXPECT(u2d(0x7FEFFFFFFFFFFFFFull * one) == 0x1.fffffffffffffp1023);
  // Infinities, zeros and NaNs.
  MSIL_EXPECT(u2f(0x7F800000u * one) > 0x1.fffffep127f);
  MSIL_EXPECT(u2d(0xFFF0000000000000ull * one) < -0x1.fffffffffffffp1023);
  float nz = u2f(0x80000000u * one);
  MSIL_EXPECT(nz == 0.0f && f2u(nz) == 0x80000000u);
  float qnan = u2f(0x7FC12345u * one);
  MSIL_EXPECT(qnan != qnan);
  MSIL_EXPECT(f2u(qnan) == 0x7FC12345u && f2u_mc(qnan) == 0x7FC12345u);
  double dnan = u2d_mc(0xFFF8000000ABCDEFull * one);
  MSIL_EXPECT(dnan != dnan);
  MSIL_EXPECT(d2u(dnan) == 0xFFF8000000ABCDEFull);
  MSIL_EXPECT(d2u_mc(u2d(0x7FF8000000000001ull * one)) ==
              0x7FF8000000000001ull);
  return 0;
}

static NOINLINE int check_round_trips(int one) {
  u64 seed = 12345ull * one;
  for (int i = 0; i < 300; i++) {
    seed = seed * 6364136223846793005ull + 1442695040888963407ull;
    u64 b = seed;
    // Keep NaN bit patterns out: an all-ones exponent becomes 0x7FE.
    if (((b >> 52) & 0x7FF) == 0x7FF)
      b &= ~(1ull << 52);
    u32 c = (u32)(seed >> 17);
    if (((c >> 23) & 0xFF) == 0xFF)
      c &= ~(1u << 23);
    MSIL_EXPECT(d2u(u2d(b)) == b);
    MSIL_EXPECT(d2u_mc(u2d_mc(b)) == b);
    MSIL_EXPECT(d2u(u2d_mc(b)) == b);
    MSIL_EXPECT(f2u(u2f(c)) == c);
    MSIL_EXPECT(f2u_mc(u2f_mc(c)) == c);
    MSIL_EXPECT(f2u_mc(u2f(c)) == c);
    // The value is the same whichever way it was built.
    MSIL_EXPECT(u2d(b) == u2d_mc(b) && u2f(c) == u2f_mc(c));
  }
  return 0;
}

static NOINLINE int check_arithmetic(int one) {
  // pi as a float32, doubled: the exponent goes up by one.
  float pi = u2f(0x40490FDBu * one);
  MSIL_EXPECT(f2u(pi * 2.0f) == 0x40C90FDBu);
  // Negation and absolute value by flipping or clearing the sign bit.
  double x = -1234.5 * one;
  MSIL_EXPECT(u2d(d2u(x) ^ 0x8000000000000000ull) == 1234.5);
  MSIL_EXPECT(u2d(d2u(x) & ~0x8000000000000000ull) == 1234.5);
  // The next double after 1.0 is 1 + 2^-52.
  MSIL_EXPECT(u2d(d2u(1.0 * one) + 1) - 1.0 == 0x1p-52);
  MSIL_EXPECT(u2f(f2u(1.0f * one) + 1) - 1.0f == 0x1p-23f);
  // floor(log2(v)) from the exponent field.
  MSIL_EXPECT((int)((d2u(1000.0 * one) >> 52) & 0x7FF) - 1023 == 9);
  MSIL_EXPECT((int)((f2u(0.0625f * one) >> 23) & 0xFF) - 127 == -4);
  // float -> double conversion keeps the value, not the bits.
  MSIL_EXPECT(d2u((double)u2f(0x3E000000u * one)) == 0x3FC0000000000000ull);

  union dw h;
  h.d = -1.0 * one;
  MSIL_EXPECT(h.w[1] == 0xBFF00000u && h.w[0] == 0);
  h.w[0] = 1u * one;
  MSIL_EXPECT(h.d == -1.0000000000000002);
  h.w[1] = 0x40000000u * one;
  h.w[0] = 0;
  MSIL_EXPECT(h.d == 2.0);

  float f = -0.75f * one;
  unsigned char b[4];
  __builtin_memcpy(b, &f, 4);
  // -0.75f is 0xBF400000.
  MSIL_EXPECT(b[0] == 0 && b[1] == 0 && b[2] == 0x40 && b[3] == 0xBF);
  b[3] = 0x3F;
  __builtin_memcpy(&f, b, 4);
  MSIL_EXPECT(f == 0.75f);
  return 0;
}

int main(int argc, char **argv) {
  (void)argv;
  TRY(check_patterns(argc));
  TRY(check_round_trips(argc));
  TRY(check_arithmetic(argc));
  return 0;
}

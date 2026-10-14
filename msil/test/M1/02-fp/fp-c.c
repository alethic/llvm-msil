// -O0 needs frames (M7): at -O0 every parameter and local is an alloca (D32).
// float and double from C at -O2 (Milestone 5): arithmetic in both precisions,
// the usual arithmetic conversions, comparisons with NaN, unsigned long long
// to double and float (the D17 sequence), FP to unsigned conversions above the
// signed range, signed zeros and conditional expressions. The noinline helpers
// get inputs derived from argc (1 when run without arguments), so neither
// clang nor llc can fold them; no fabs, copysign, math library or FP bitcasts
// (they need frames or P/Invoke). Each MSIL_CHECK group sets one bit of the
// exit status when one of its checks fails (0 = all passed, 8 bits, D37).
// RUN: %{msil_c_O2}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

static NOINLINE double add_d(double a, double b) { return a + b; }
static NOINLINE double div_d(double a, double b) { return a / b; }
static NOINLINE float add_f(float a, float b) { return a + b; }
static NOINLINE float div_f(float a, float b) { return a / b; }
// f * i is a float multiplication; the sum is a double addition.
static NOINLINE double scale_add(float f, int i, double d) { return f * i + d; }
static NOINLINE float narrow(double d) { return (float)d; }
static NOINLINE double widen(float f) { return f; }
static NOINLINE int less(double a, double b) { return a < b; }
static NOINLINE int greater_eq_f(float a, float b) { return a >= b; }
static NOINLINE int not_equal(double a, double b) { return a != b; }
static NOINLINE double u64_to_d(unsigned long long v) { return (double)v; }
static NOINLINE float u64_to_f(unsigned long long v) { return (float)v; }
static NOINLINE double s64_to_d(long long v) { return (double)v; }
static NOINLINE float u32_to_f(unsigned v) { return (float)v; }
static NOINLINE unsigned long long d_to_u64(double d) {
  return (unsigned long long)d;
}
static NOINLINE unsigned f_to_u32(float f) { return (unsigned)f; }
static NOINLINE int d_to_i32(double d) { return (int)d; }
static NOINLINE long long f_to_i64(float f) { return (long long)f; }
static NOINLINE unsigned char d_to_u8(double d) { return (unsigned char)d; }
static NOINLINE double negate(double x) { return -x; }
static NOINLINE double smaller(double a, double b) { return a < b ? a : b; }
static NOINLINE float pick_f(int c, float a, float b) { return c ? a : b; }

int main(int argc, char **argv) {
  (void)argv;
  unsigned acc = 0;
  int one = argc;
  double zero = (double)(one - 1);
  double nan = zero / zero;
  double inf = 1.0 / zero;

  // Bit 0: double arithmetic.
  MSIL_CHECK(acc, 0, add_d(0.1 * one, 0.2) == 0.30000000000000004);
  MSIL_CHECK(acc, 0, add_d(1e308 * one, 1e308) == inf);
  MSIL_CHECK(acc, 0, div_d(1.0 * one, 3.0) == 0x1.5555555555555p-2);
  MSIL_CHECK(acc, 0, div_d(-one, zero) == -inf);
  MSIL_CHECK(acc, 0, scale_add(1.5f * one, 3, 0.25) == 4.75);

  // Bit 1: float arithmetic stays in float.
  MSIL_CHECK(acc, 1, add_f(16777216.0f * one, 1.0f) == 16777216.0f);
  MSIL_CHECK(acc, 1, add_f(0.1f * one, 0.2f) == 0.3f);
  MSIL_CHECK(acc, 1, div_f(1.0f * one, 3.0f) == 0x1.555556p-2f);
  MSIL_CHECK(acc, 1, scale_add(4097.0f, 4097 * one, 0.0) == 16785408.0);
  MSIL_CHECK(acc, 1, narrow(0.1 * one) == 0.1f);
  MSIL_CHECK(acc, 1, widen(0.1f * one) == 0x1.99999ap-4);

  // Bit 2: comparisons with NaN are false, except !=.
  MSIL_CHECK(acc, 2, (less(nan, 1.0) | less(1.0, nan)) == 0);
  MSIL_CHECK(acc, 2, less(-inf, inf));
  MSIL_CHECK(acc, 2, greater_eq_f((float)nan, 1.0f) == 0);
  MSIL_CHECK(acc, 2, greater_eq_f(2.0f * one, 2.0f));
  MSIL_CHECK(acc, 2, not_equal(nan, nan));
  MSIL_CHECK(acc, 2, not_equal(zero, -zero) == 0);
  MSIL_CHECK(acc, 2, nan != nan);
  MSIL_CHECK(acc, 2, (nan == nan) == 0);
  MSIL_CHECK(acc, 2, (nan <= inf) == 0);

  // Bit 3: unsigned long long to double and float: one rounding (D17).
  MSIL_CHECK(acc, 3, u64_to_d(0x8000000000000401ull * one) == 0x1.0000000000001p63);
  MSIL_CHECK(acc, 3, u64_to_f(0x8000008000000001ull * one) == 0x1.000002p63f);
  MSIL_CHECK(acc, 3, u64_to_d(~0ull * one) == 0x1p64);
  MSIL_CHECK(acc, 3, u64_to_f(~0ull * one) == 0x1p64f);
  MSIL_CHECK(acc, 3, u64_to_d((1ull << 53) + one) == 0x1p53);
  MSIL_CHECK(acc, 3, s64_to_d(-(1ll << 62) * one) == -0x1p62);

  // Bit 4: FP to integer, truncating; unsigned results above the signed range.
  MSIL_CHECK(acc, 4, d_to_u64(1.8e19 * one) == 18000000000000000000ull);
  MSIL_CHECK(acc, 4, d_to_u64(0x1p63 * one) == 0x8000000000000000ull);
  MSIL_CHECK(acc, 4, f_to_u32(4e9f * one) == 4000000000u);
  MSIL_CHECK(acc, 4, d_to_i32(-2.9 * one) == -2);
  MSIL_CHECK(acc, 4, f_to_i64(-7.75f * one) == -7);
  MSIL_CHECK(acc, 4, d_to_u8(255.9 * one) == 255);

  // Bit 5: signed zeros, observed through 1 / x.
  MSIL_CHECK(acc, 5, 1.0 / negate(zero) == -inf);
  MSIL_CHECK(acc, 5, 1.0 / negate(-zero) == inf);
  MSIL_CHECK(acc, 5, 1.0 / (zero * -one) == -inf);
  MSIL_CHECK(acc, 5, 1.0 / add_d(-zero, zero) == inf);

  // Bit 6: conditional expressions on doubles and floats.
  MSIL_CHECK(acc, 6, smaller(-1.0 * one, 2.0) == -1.0);
  MSIL_CHECK(acc, 6, smaller(nan, 2.0 * one) == 2.0);
  MSIL_CHECK(acc, 6, smaller(3.0 * one, nan) != smaller(3.0 * one, nan));
  MSIL_CHECK(acc, 6, pick_f(one, 0.5f, -0.5f) == 0.5f);
  MSIL_CHECK(acc, 6, pick_f(one - 1, 0.5f, -0.5f) == -0.5f);

  // Bit 7: integer to FP: int and unsigned int sources.
  MSIL_CHECK(acc, 7, (float)(16777217 * one) == 16777216.0f);
  MSIL_CHECK(acc, 7, u32_to_f(0xFFFFFF7Fu * one) == 4294967040.0f);
  MSIL_CHECK(acc, 7, (double)(unsigned)-one == 4294967295.0);
  MSIL_CHECK(acc, 7, (double)(-2147483647 - one) == -2147483648.0);

  return (int)acc;
}

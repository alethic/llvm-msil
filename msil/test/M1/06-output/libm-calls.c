// The C math library through P/Invoke (Milestone 8, D14): every double
// function of the Phase-1 import table and the float variants that the C
// libraries of both execution triples export, called two ways in one module.
// (1) Direct calls of declared functions: -ffreestanding implies
// -fno-builtin, so each stays a call of an undefined function that becomes a
// P/Invoke into ucrtbase (windows-msvc) or libm.so.6 (linux-gnu). (2) The
// same operations as LLVM intrinsics (__builtin_floor, __builtin_fma,
// __builtin_frexp, __builtin_modf, __builtin_elementwise_sqrt/sin/pow/...),
// which instruction selection turns into library calls of the same names
// (external symbols, FREXP and FMODF with a result through memory), so one
// P/Invoke declaration must serve both a declared function and a libcall.
// ldexpf, frexpf and hypotf are not called: ucrtbase does not export them
// (the UCRT headers define them inline), and __builtin_ldexpf is a call of
// ldexpf under math-errno; __builtin_frexpf is llvm.frexp, which becomes a
// call of frexp on windows-msvc and of frexpf elsewhere. Exactly specified
// results (sqrt, rounding, fmod, remainder, fma, fmin/fmax, fdim, ldexp,
// frexp, modf) are compared exactly, transcendental ones within a small
// relative error, so the results agree for UCRT and glibc. Inputs are read
// through volatile globals, so nothing is folded at -O2. MSIL_EXPECT returns
// 1 + (line % 255) at the first failed check; [exit 0] means every check
// passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define TRY(call)                                                              \
  do {                                                                         \
    int r_ = (call);                                                           \
    if (r_ != 0)                                                               \
      return r_;                                                               \
  } while (0)

double sqrt(double), cbrt(double), floor(double), ceil(double), trunc(double);
double round(double), rint(double), nearbyint(double), fmod(double, double);
double remainder(double, double), fma(double, double, double);
double fmin(double, double), fmax(double, double), fdim(double, double);
double sin(double), cos(double), tan(double), asin(double), acos(double);
double atan(double), atan2(double, double), sinh(double), cosh(double);
double tanh(double), exp(double), exp2(double), expm1(double), log(double);
double log2(double), log10(double), log1p(double), pow(double, double);
double ldexp(double, int), frexp(double, int *), modf(double, double *);
double hypot(double, double);

float sqrtf(float), cbrtf(float), floorf(float), ceilf(float), truncf(float);
float roundf(float), rintf(float), nearbyintf(float), fmodf(float, float);
float remainderf(float, float), fmaf(float, float, float);
float fminf(float, float), fmaxf(float, float), fdimf(float, float);
float sinf(float), cosf(float), tanf(float), asinf(float), acosf(float);
float atanf(float), atan2f(float, float), sinhf(float), coshf(float);
float tanhf(float), expf(float), exp2f(float), expm1f(float), logf(float);
float log2f(float), log10f(float), log1pf(float), powf(float, float);
float modff(float, float *);

volatile double v_one = 1.0;
volatile float v_onef = 1.0f;

// x, but computed at run time.
static double D(double x) { return x * v_one; }
static float F(float x) { return x * v_onef; }

static int close_d(double got, double want) {
  double d = got - want, m = want;
  if (d < 0)
    d = -d;
  if (m < 0)
    m = -m;
  return d <= m * 1e-14;
}

static int close_f(float got, float want) {
  float d = got - want, m = want;
  if (d < 0)
    d = -d;
  if (m < 0)
    m = -m;
  return d <= m * 1e-5f;
}

static int check_double_calls(void) {
  double nan = D(0.0) / D(0.0);
  MSIL_EXPECT(sqrt(D(2.25)) == 1.5);
  MSIL_EXPECT(close_d(cbrt(D(27.0)), 3.0));
  MSIL_EXPECT(floor(D(-2.5)) == -3.0);
  MSIL_EXPECT(ceil(D(-2.5)) == -2.0);
  MSIL_EXPECT(trunc(D(-2.75)) == -2.0);
  MSIL_EXPECT(round(D(2.5)) == 3.0 && round(D(-2.5)) == -3.0);
  MSIL_EXPECT(rint(D(2.5)) == 2.0 && rint(D(3.5)) == 4.0);
  MSIL_EXPECT(nearbyint(D(-1.5)) == -2.0);
  MSIL_EXPECT(fmod(D(7.5), D(2.0)) == 1.5 && fmod(D(-7.5), D(2.0)) == -1.5);
  MSIL_EXPECT(remainder(D(7.5), D(2.0)) == -0.5);
  MSIL_EXPECT(fma(D(2.0), D(3.0), D(4.0)) == 10.0);
  // Fused: (1 + 2^-30)^2 - 1 is 2^-29 + 2^-60 only without an intermediate
  // rounding; a multiply and an add would give 2^-29.
  double e30 = D(1.0 / 1073741824.0);
  MSIL_EXPECT(fma(1.0 + e30, 1.0 + e30, D(-1.0)) ==
              2.0 * e30 + e30 * e30);
  MSIL_EXPECT(fmin(D(1.0), nan) == 1.0 && fmin(D(-3.0), D(2.0)) == -3.0);
  MSIL_EXPECT(fmax(nan, D(-1.0)) == -1.0 && fmax(D(-1.0), D(2.0)) == 2.0);
  MSIL_EXPECT(fdim(D(5.0), D(3.0)) == 2.0 && fdim(D(3.0), D(5.0)) == 0.0);
  MSIL_EXPECT(close_d(sin(D(0.5)), 0.479425538604203));
  MSIL_EXPECT(close_d(cos(D(0.5)), 0.8775825618903728));
  MSIL_EXPECT(close_d(tan(D(0.5)), 0.5463024898437905));
  MSIL_EXPECT(close_d(asin(D(0.5)), 0.5235987755982989));
  MSIL_EXPECT(close_d(acos(D(0.5)), 1.0471975511965979));
  MSIL_EXPECT(close_d(atan(D(1.0)), 0.7853981633974483));
  MSIL_EXPECT(close_d(atan2(D(1.0), D(-1.0)), 2.356194490192345));
  MSIL_EXPECT(close_d(sinh(D(1.0)), 1.1752011936438014));
  MSIL_EXPECT(close_d(cosh(D(1.0)), 1.5430806348152437));
  MSIL_EXPECT(close_d(tanh(D(0.5)), 0.46211715726000974));
  MSIL_EXPECT(close_d(exp(D(1.0)), 2.718281828459045));
  MSIL_EXPECT(close_d(exp2(D(10.0)), 1024.0));
  // expm1 and log1p keep the precision that exp(x) - 1 and log(1 + x) lose.
  MSIL_EXPECT(close_d(expm1(D(1e-10)), 1.00000000005e-10));
  MSIL_EXPECT(close_d(log(D(10.0)), 2.302585092994046));
  MSIL_EXPECT(close_d(log2(D(1024.0)), 10.0));
  MSIL_EXPECT(close_d(log10(D(1000.0)), 3.0));
  MSIL_EXPECT(close_d(log1p(D(1e-10)), 9.999999999500001e-11));
  MSIL_EXPECT(pow(D(2.0), D(10.0)) == 1024.0);
  MSIL_EXPECT(close_d(pow(D(2.0), D(0.5)), 1.4142135623730951));
  MSIL_EXPECT(ldexp(D(1.5), 4) == 24.0 && ldexp(D(1.0), -3) == 0.125);
  int e = 0;
  MSIL_EXPECT(frexp(D(24.0), &e) == 0.75 && e == 5);
  double ip = 0.0;
  MSIL_EXPECT(modf(D(-3.25), &ip) == -0.25 && ip == -3.0);
  MSIL_EXPECT(close_d(hypot(D(3.0), D(4.0)), 5.0));
  // No overflow in the intermediate square, unlike sqrt(x * x + y * y).
  MSIL_EXPECT(close_d(hypot(D(1e200), D(1e200)), 1.414213562373095e+200));
  return 0;
}

static int check_float_calls(void) {
  float nan = F(0.0f) / F(0.0f);
  MSIL_EXPECT(sqrtf(F(6.25f)) == 2.5f);
  MSIL_EXPECT(close_f(cbrtf(F(-8.0f)), -2.0f));
  MSIL_EXPECT(floorf(F(2.5f)) == 2.0f && ceilf(F(2.25f)) == 3.0f);
  MSIL_EXPECT(truncf(F(-2.75f)) == -2.0f && roundf(F(-0.5f)) == -1.0f);
  MSIL_EXPECT(rintf(F(-2.5f)) == -2.0f && nearbyintf(F(0.5f)) == 0.0f);
  MSIL_EXPECT(fmodf(F(5.5f), F(1.5f)) == 1.0f);
  MSIL_EXPECT(remainderf(F(5.0f), F(2.0f)) == 1.0f);
  MSIL_EXPECT(fmaf(F(1.5f), F(2.0f), F(0.25f)) == 3.25f);
  MSIL_EXPECT(fminf(F(3.0f), F(-4.0f)) == -4.0f && fmaxf(nan, F(5.0f)) == 5.0f);
  MSIL_EXPECT(fdimf(F(1.5f), F(0.5f)) == 1.0f);
  MSIL_EXPECT(close_f(sinf(F(0.5f)), 0.479425538604203f));
  MSIL_EXPECT(close_f(cosf(F(0.5f)), 0.8775825618903728f));
  MSIL_EXPECT(close_f(tanf(F(0.5f)), 0.5463024898437905f));
  MSIL_EXPECT(close_f(asinf(F(0.5f)), 0.5235987755982989f));
  MSIL_EXPECT(close_f(acosf(F(0.5f)), 1.0471975511965979f));
  MSIL_EXPECT(close_f(atanf(F(1.0f)), 0.7853981633974483f));
  MSIL_EXPECT(close_f(atan2f(F(1.0f), F(-1.0f)), 2.356194490192345f));
  MSIL_EXPECT(close_f(sinhf(F(1.0f)), 1.1752011936438014f));
  MSIL_EXPECT(close_f(coshf(F(1.0f)), 1.5430806348152437f));
  MSIL_EXPECT(close_f(tanhf(F(0.5f)), 0.46211715726000974f));
  MSIL_EXPECT(close_f(expf(F(-2.0f)), 0.1353352832366127f));
  MSIL_EXPECT(close_f(exp2f(F(10.0f)), 1024.0f));
  MSIL_EXPECT(close_f(expm1f(F(1e-6f)), 1.0000005e-6f));
  MSIL_EXPECT(close_f(logf(F(0.25f)), -1.3862943611198906f));
  MSIL_EXPECT(close_f(log2f(F(1024.0f)), 10.0f));
  MSIL_EXPECT(close_f(log10f(F(1000.0f)), 3.0f));
  MSIL_EXPECT(close_f(log1pf(F(1e-6f)), 9.999995e-7f));
  MSIL_EXPECT(powf(F(2.0f), F(10.0f)) == 1024.0f);
  float fip = 0.0f;
  MSIL_EXPECT(modff(F(7.75f), &fip) == 0.75f && fip == 7.0f);
  return 0;
}

// The same operations as LLVM intrinsics: library calls made by instruction
// selection, partly of the functions declared above.
static int check_intrinsics(void) {
  MSIL_EXPECT(__builtin_floor(D(-2.5)) == -3.0);
  MSIL_EXPECT(__builtin_ceil(D(-2.5)) == -2.0);
  MSIL_EXPECT(__builtin_trunc(D(2.75)) == 2.0);
  MSIL_EXPECT(__builtin_round(D(-2.5)) == -3.0);
  MSIL_EXPECT(__builtin_rint(D(2.5)) == 2.0);
  MSIL_EXPECT(__builtin_nearbyint(D(3.5)) == 4.0);
  MSIL_EXPECT(__builtin_fma(D(2.0), D(3.0), D(4.0)) == 10.0);
  int e = 0;
  MSIL_EXPECT(__builtin_frexp(D(-40.0), &e) == -0.625 && e == 6);
  double ip = 0.0;
  MSIL_EXPECT(__builtin_modf(D(5.5), &ip) == 0.5 && ip == 5.0);
  MSIL_EXPECT(__builtin_elementwise_sqrt(D(6.25)) == 2.5);
  MSIL_EXPECT(close_d(__builtin_elementwise_sin(D(2.5)), 0.5984721441039564));
  MSIL_EXPECT(close_d(__builtin_elementwise_cos(D(2.5)), -0.8011436155469337));
  MSIL_EXPECT(close_d(__builtin_elementwise_tan(D(0.5)), 0.5463024898437905));
  MSIL_EXPECT(close_d(__builtin_elementwise_asin(D(0.5)), 0.5235987755982989));
  MSIL_EXPECT(close_d(__builtin_elementwise_acos(D(0.5)), 1.0471975511965979));
  MSIL_EXPECT(close_d(__builtin_elementwise_atan(D(1.0)), 0.7853981633974483));
  MSIL_EXPECT(
      close_d(__builtin_elementwise_atan2(D(1.0), D(-1.0)), 2.356194490192345));
  MSIL_EXPECT(close_d(__builtin_elementwise_sinh(D(1.0)), 1.1752011936438014));
  MSIL_EXPECT(close_d(__builtin_elementwise_cosh(D(1.0)), 1.5430806348152437));
  MSIL_EXPECT(close_d(__builtin_elementwise_tanh(D(0.5)), 0.46211715726000974));
  MSIL_EXPECT(close_d(__builtin_elementwise_exp(D(-2.0)), 0.1353352832366127));
  MSIL_EXPECT(close_d(__builtin_elementwise_exp2(D(10.0)), 1024.0));
  MSIL_EXPECT(close_d(__builtin_elementwise_log(D(0.25)), -1.3862943611198906));
  MSIL_EXPECT(close_d(__builtin_elementwise_log2(D(1024.0)), 10.0));
  MSIL_EXPECT(close_d(__builtin_elementwise_log10(D(1000.0)), 3.0));
  MSIL_EXPECT(close_d(__builtin_elementwise_pow(D(2.0), D(0.5)),
                      1.4142135623730951));
  MSIL_EXPECT(__builtin_floorf(F(-0.5f)) == -1.0f);
  MSIL_EXPECT(__builtin_fmaf(F(1.5f), F(2.0f), F(0.25f)) == 3.25f);
  MSIL_EXPECT(__builtin_elementwise_sqrt(F(2.25f)) == 1.5f);
  MSIL_EXPECT(close_f(__builtin_elementwise_sin(F(0.5f)), 0.479425538604203f));
  MSIL_EXPECT(__builtin_elementwise_pow(F(2.0f), F(10.0f)) == 1024.0f);
  int ef = 0;
  MSIL_EXPECT(__builtin_frexpf(F(24.0f), &ef) == 0.75f && ef == 5);
  return 0;
}

int main(void) {
  TRY(check_double_calls());
  put_str("double calls: ok");
  TRY(check_float_calls());
  put_str("float calls: ok");
  TRY(check_intrinsics());
  put_str("intrinsics: ok");
  return 0;
}

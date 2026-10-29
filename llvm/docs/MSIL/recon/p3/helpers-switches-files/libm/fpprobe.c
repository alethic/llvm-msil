// The C counterpart of fpprobe/Program.cs: same lines, C library semantics.
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static double D(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }
static void O(const char *n, int i, double r) { uint64_t u; memcpy(&u, &r, 8); if (i >= 0) printf("%s[%d] %016llx\n", n, i, (unsigned long long)u); else printf("%s %016llx\n", n, (unsigned long long)u); }
static unsigned F(float f) { unsigned u; memcpy(&u, &f, 4); return u; }
int main(void) {
  volatile double nz = D(0x8000000000000000ull), qnan = D(0x7ff8000000000000ull), nnan = D(0xfff8000000000000ull), pl = D(0x7ff4000000000123ull);
  double rin[] = { 0.5, 1.5, 2.5, -0.5, -2.5, 0.49999999999999994, -0.49999999999999994, 4503599627370495.5, 4503599627370497.0, -0.4, nz, 1e300, D(1), qnan, nnan };
  for (int i = 0; i < 15; i++) {
    volatile double x = rin[i];
    O("round_even", i, rint(x)); O("round_away", i, round(x));
    O("floor", i, floor(x)); O("ceil", i, ceil(x)); O("trunc", i, trunc(x));
    O("abs", i, fabs(x)); O("sqrt", i, sqrt(x));
  }
  O("abs_nnan", -1, fabs(nnan)); O("abs_payload", -1, fabs(D(0xfff4000000000123ull)));
  volatile double m1 = -1.0;
  O("sqrt_neg", -1, sqrt(m1)); O("sqrt_payload", -1, sqrt(pl));
  O("min_-0_+0", -1, fmin(nz, 0.0)); O("min_+0_-0", -1, fmin(0.0, nz));
  O("max_-0_+0", -1, fmax(nz, 0.0)); O("max_+0_-0", -1, fmax(0.0, nz));
  O("min_nan_1", -1, fmin(qnan, 1.0)); O("min_1_nan", -1, fmin(1.0, qnan));
  volatile double inf = INFINITY;
  double rp[][2] = { { 5.1, 3.0 }, { -5.1, 3.0 }, { 1e308, 3.0 }, { 1e308, 1e-308 }, { 7.0, 2.0 }, { -7.0, 2.0 }, { 0.0, 0.0 }, { 1.0, 0.0 }, { inf, 2.0 }, { 3.0, inf }, { nz, 1.0 }, { 6.0, 4.0 } };
  for (int i = 0; i < 12; i++) { volatile double a = rp[i][0], b = rp[i][1]; O("rem", i, fmod(a, b)); O("ieeerem", i, remainder(a, b)); }
  double tin[] = { 1e22, D(0x3ff921fb54442d18ull), 1.5707963267948966, 3.141592653589793, 0.5, 2.0, 1e-300, 709.782712893384, -745.1332191019411, 100.0, 1e300, 0.7853981633974483, 12345.6789, D(0x3ff921fb54442d19ull), 0.1 };
  for (int i = 0; i < 15; i++) {
    volatile double x = tin[i];
    O("sin", i, sin(x)); O("cos", i, cos(x)); O("tan", i, tan(x));
    O("exp", i, exp(x)); O("log", i, log(x)); O("log10", i, log10(x));
    O("atan", i, atan(x)); O("sinh", i, sinh(x)); O("tanh", i, tanh(x));
    O("asin", i, asin(x / 1e23)); O("acos", i, acos(x / 1e23));
    O("pow", i, pow(x, 1.0 / 3.0)); O("pow2", i, pow(1.0000001, x));
    O("atan2", i, atan2(x, 3.0));
  }
  volatile double one = 1.0, zero = 0.0, m8 = -8.0;
  O("pow_m1_inf", -1, pow(m1, inf)); O("pow_1_nan", -1, pow(one, qnan)); O("pow_nan_0", -1, pow(qnan, zero));
  O("pow_m8_third", -1, pow(m8, 1.0 / 3.0)); O("log_neg", -1, log(m1)); O("log_0", -1, log(zero));
  volatile double a27 = 27.0, a01 = 0.1, a10 = 10.0, a3 = 3.0, a15 = 1.5, a8 = 8.0;
  O("cbrt27", -1, cbrt(a27)); O("cbrt_m8", -1, cbrt(m8));
  O("fma", -1, fma(a01, a10, -1.0)); O("fma_unfused", -1, a01 * a10 - 1.0);
  O("copysign", -1, copysign(a3, nz)); O("copysign_nan", -1, copysign(qnan, -1.0));
  O("scaleb", -1, scalbn(a15, 1030)); O("log2", -1, log2(a8)); O("bitdec", -1, nextafter(one, 0.0));
  O("maxnumber_nan", -1, fmax(qnan, 1.0)); O("maxnumber_-0+0", -1, fmax(nz, 0.0));
  volatile float fx = 1e10f, fe = 88.7f, fp = 1.0000001f, fq = 1e7f;
  printf("sinf %08x via-double %08x\n", F(sinf(fx)), F((float)sin(fx)));
  printf("expf %08x via-double %08x\n", F(expf(fe)), F((float)exp(fe)));
  printf("powf %08x via-double %08x\n", F(powf(fp, fq)), F((float)pow(fp, fq)));
  return 0;
}

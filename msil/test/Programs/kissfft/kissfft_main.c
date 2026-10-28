/* KISS FFT (kiss_fft_scalar double) from an archive: complex forward and
   inverse FFTs of the mixed-radix sizes 64, 100, 360 and 243 (inverse
   checked against the input) and real FFTs of the even sizes; outputs are
   printed as rounded fixed-point integers. kiss_fft.c calls cos, sin, floor
   and sqrt from the C math library and malloc (the support archive's in the
   MSIL build). The hashes wrap in unsigned arithmetic. The error count is
   also the exit status (at most 255). */
#include "kiss_fft.h"
#include "kiss_fftr.h"
#include "sup.h"
static long long fix(double x) { return (long long)(x >= 0 ? x * 1000.0 + 0.5 : x * 1000.0 - 0.5); }
static kiss_fft_cpx cin[400], cout[400], cback[400];
static kiss_fft_scalar rin[400];
int main(void) {
  static const int sizes[] = {64, 100, 360, 243};
  int errors = 0;
  for (int s = 0; s < 4; ++s) {
    int n = sizes[s];
    sup_reset();
    kiss_fft_cfg f = kiss_fft_alloc(n, 0, 0, 0), b = kiss_fft_alloc(n, 1, 0, 0);
    for (int i = 0; i < n; ++i) {
      cin[i].r = (i * 37 % 101) / 7.0 - 5.0;
      cin[i].i = (i * 13 % 29) / 3.0 - 4.0;
    }
    kiss_fft(f, cin, cout);
    kiss_fft(b, cout, cback);
    unsigned long long h = 0;
    for (int i = 0; i < n; ++i) {
      h = h * 31 + (unsigned long long)fix(cout[i].r) * 7 + (unsigned long long)fix(cout[i].i);
      if (fix(cback[i].r / n) != fix(cin[i].r) || fix(cback[i].i / n) != fix(cin[i].i))
        ++errors;
    }
    sup_put_u("fft", h);
    sup_put_i("  bin1.r", fix(cout[1].r));
    if (n % 2 == 0) {
      kiss_fftr_cfg r = kiss_fftr_alloc(n, 0, 0, 0);
      for (int i = 0; i < n; ++i) rin[i] = (i * 53 % 97) / 9.0 - 3.0;
      kiss_fftr(r, rin, cout);
      unsigned long long g = 0;
      for (int i = 0; i <= n / 2; ++i)
        g = g * 31 + (unsigned long long)fix(cout[i].r) - (unsigned long long)fix(cout[i].i);
      sup_put_u("fftr", g);
    }
  }
  sup_put_u("errors", errors);
  return errors > 255 ? 255 : errors;
}

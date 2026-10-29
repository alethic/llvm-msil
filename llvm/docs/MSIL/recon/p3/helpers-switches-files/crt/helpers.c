// Exercises the compiler-rt helpers that the MSIL legalizer names today.
// Prints bit patterns so that MSIL and native runs compare exactly.

#include <stdint.h>
int puts(const char *);
static char buf[256]; static int bl;
static void s(const char *t) { while (*t) buf[bl++] = *t++; }
static void hx(uint64_t v, int n) { s(" "); for (int i = n - 1; i >= 0; --i) buf[bl++] = "0123456789abcdef"[(v >> (4 * i)) & 15]; }
static void dn(long long v) { char t[24]; int k = 0; unsigned long long u = v < 0 ? -(unsigned long long)v : v; s(" "); if (v < 0) buf[bl++] = '-'; do t[k++] = '0' + u % 10; while (u /= 10); while (k) buf[bl++] = t[--k]; }
static void nl(void) { buf[bl] = 0; puts(buf); bl = 0; }

static uint64_t bits64(double d) { uint64_t u; __builtin_memcpy(&u, &d, 8); return u; }
static uint32_t bits32(float f) { uint32_t u; __builtin_memcpy(&u, &f, 4); return u; }
static uint16_t bits16h(_Float16 h) { uint16_t u; __builtin_memcpy(&u, &h, 2); return u; }
static uint16_t bits16b(__bf16 h) { uint16_t u; __builtin_memcpy(&u, &h, 2); return u; }

volatile double dz = 0.0, dinf, dnan, d1 = 1.5, d2 = -2.25, dbig = 1e300;
volatile float fz = 0.0f, finf, fnan, f1 = 1.5f, f2 = -2.25f;
volatile int ie = 7;
volatile __int128 bigi = (__int128)1 << 100;
volatile unsigned __int128 bigu = ~(unsigned __int128)0;

__attribute__((noinline)) double _Complex cmul(double _Complex a, double _Complex b) { return a * b; }
__attribute__((noinline)) double _Complex cdiv(double _Complex a, double _Complex b) { return a / b; }
__attribute__((noinline)) float _Complex fcmul(float _Complex a, float _Complex b) { return a * b; }
__attribute__((noinline)) float _Complex fcdiv(float _Complex a, float _Complex b) { return a / b; }

static void pc(const char *n, double _Complex z) {
  s(n); hx(bits64(__real__ z), 16); hx(bits64(__imag__ z), 16); nl();
}
static void pfc(const char *n, float _Complex z) {
  s(n); hx(bits32(__real__ z), 8); hx(bits32(__imag__ z), 8); nl();
}

int main(void) {
  dinf = 1.0 / dz; dnan = dz / dz; finf = 1.0f / fz; fnan = fz / fz;
  double _Complex a = d1 + d2 * 1.0i, b = d2 + d1 * 1.0i;
  pc("muldc3", cmul(a, b));
  pc("divdc3", cdiv(a, b));
  // Annex G: (inf + nan i) * (1 + 1i) is an infinity, not NaN + NaN i.
  double _Complex inf_nan; __real__ inf_nan = dinf; __imag__ inf_nan = dnan;
  pc("muldc3-inf", cmul(inf_nan, 1.0 + 1.0i));
  pc("divdc3-zero", cdiv(1.0 + 1.0i, 0.0));
  pc("divdc3-inf", cdiv(1.0 + 1.0i, inf_nan));
  pc("divdc3-big", cdiv(dbig + dbig * 1.0i, dbig + 1.0i));
  float _Complex fa = f1 + f2 * 1.0if, fb = f2 + f1 * 1.0if;
  pfc("mulsc3", fcmul(fa, fb));
  pfc("divsc3", fcdiv(fa, fb));
  float _Complex finf_nan; __real__ finf_nan = finf; __imag__ finf_nan = fnan;
  pfc("mulsc3-inf", fcmul(finf_nan, 1.0f + 1.0if));

  volatile _Float16 h1 = (_Float16)d1, h2 = (_Float16)f2, h3 = (_Float16)65504.0f;
  _Float16 hs = h1 + h2, hm = h1 * h2, hd = h1 / h3, ho = h3 * (_Float16)2.0f;
  s("f16"); hx(bits16h(h1), 4); hx(bits16h(hs), 4); hx(bits16h(hm), 4); hx(bits16h(hd), 4); hx(bits16h(ho), 4); nl();
  s("f16->f32"); hx(bits32((float)hm), 8); s(" f16->f64"); hx(bits64((double)hd), 16); nl();
  volatile double dsub = 1.0e-7; volatile float fhalfway = 1.0009765625f + 0.00048828125f;
  s("f64->f16"); hx(bits16h((_Float16)dsub), 4); s(" f32->f16"); hx(bits16h((_Float16)fhalfway), 4); nl();
  s("f16->i32"); dn((int)hm); s(" i32->f16"); hx(bits16h((_Float16)ie), 4); nl();
  s("f16->i128"); dn((long long)(__int128)hm); s(" i128->f16"); hx(bits16h((_Float16)bigi), 4); nl();

  volatile __bf16 b1 = (__bf16)f1, b2 = (__bf16)d2;
  volatile __bf16 b3 = (__bf16)(float)fhalfway;
  s("bf16"); hx(bits16b(b1), 4); hx(bits16b(b2), 4); hx(bits16b(b3), 4); s(" f32"); hx(bits32((float)b2), 8); nl();
  s("i128->bf16"); hx(bits16b((__bf16)bigi), 4); nl();

  s("powidf2"); hx(bits64(__builtin_powi(d1, ie)), 16); s(" powisf2"); hx(bits32(__builtin_powif(f2, -ie)), 8); nl();

  __int128 r; int o = __builtin_mul_overflow(bigi, (__int128)ie << 30, &r);
  s("muloti4"); dn(o); hx((uint64_t)(r >> 64), 16); hx((uint64_t)r, 16); nl();
  o = __builtin_mul_overflow(bigi, (__int128)3, &r);
  s("muloti4b"); dn(o); hx((uint64_t)(r >> 64), 16); hx((uint64_t)r, 16); nl();
  return 0;
}

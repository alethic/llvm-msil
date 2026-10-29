// Exhaustive _Float16 and __bf16 conversion check: prints hashes of the result bits of
//   A: every half -> float and -> double (65,536 inputs)
//   B: every float -> half (2^32 inputs), split into NaN and non-NaN inputs
//   C: double -> half on the rounding boundaries of every finite half (midpoints, +-1 ulp,
//      double-rounding traps where (half)(float)d != (half)d) plus 2^24 pseudo-random doubles
//   D: every bf16 -> float (65,536) and every float -> bf16 (2^32), double -> bf16 boundaries
//   S: named special cases, printed in full
// argv[1] selects the parts (default "ABCDS"). Only puts is used (no printf before P3-B).
#include <stdint.h>
#define memcpy __builtin_memcpy
int puts(const char *);

static char line[200];
static int ll;
static void s(const char *t) { while (*t) line[ll++] = *t++; }
static void hx(uint64_t v, int n) {
  s(" ");
  for (int i = n - 1; i >= 0; --i) line[ll++] = "0123456789abcdef"[(v >> (4 * i)) & 15];
}
static void nl(void) { line[ll] = 0; puts(line); ll = 0; }

typedef struct { uint64_t h; uint64_t n; } hash_t;
static void hadd(hash_t *x, uint64_t v) { x->h = (x->h ^ v) * 0x100000001b3ull; x->h ^= x->h >> 29; x->n++; }
static void hprint(const char *name, hash_t x) { s(name); hx(x.h, 16); hx(x.n, 12); nl(); }
static hash_t hinit(void) { hash_t x = {0xcbf29ce484222325ull, 0}; return x; }

static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint64_t d2u(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static double u2d(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }
static uint16_t h2u(_Float16 h) { uint16_t u; memcpy(&u, &h, 2); return u; }
static _Float16 u2h(uint16_t u) { _Float16 h; memcpy(&h, &u, 2); return h; }
static uint16_t b2u(__bf16 h) { uint16_t u; memcpy(&u, &h, 2); return u; }
static __bf16 u2b(uint16_t u) { __bf16 h; memcpy(&h, &u, 2); return h; }

// Conversions in noinline functions so that every one is a real conversion (a helper call).
__attribute__((noinline)) float h2f(_Float16 h) { return (float)h; }
__attribute__((noinline)) double h2d(_Float16 h) { return (double)h; }
__attribute__((noinline)) _Float16 f2h(float f) { return (_Float16)f; }
__attribute__((noinline)) _Float16 d2h(double d) { return (_Float16)d; }
__attribute__((noinline)) float b2f(const __bf16 *p) { return (float)*p; }
__attribute__((noinline)) void f2b(__bf16 *p, float f) { *p = (__bf16)f; }
__attribute__((noinline)) void d2b(__bf16 *p, double d) { *p = (__bf16)d; }

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint64_t next(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

static void partA(void) {
  hash_t hf = hinit(), hd = hinit();
  for (uint32_t u = 0; u < 65536; ++u) {
    _Float16 h = u2h((uint16_t)u);
    hadd(&hf, f2u(h2f(h)));
    hadd(&hd, d2u(h2d(h)));
  }
  hprint("A h->f32", hf);
  hprint("A h->f64", hd);
}

static void partB(void) {
  hash_t hn = hinit(), hv = hinit();
  uint32_t u = 0;
  do {
    float f = u2f(u);
    uint16_t r = h2u(f2h(f));
    if ((u & 0x7f800000u) == 0x7f800000u && (u & 0x7fffffu))
      hadd(&hn, r);
    else
      hadd(&hv, r);
  } while (++u != 0);
  hprint("B f32->h nonNaN", hv);
  hprint("B f32->h NaN", hn);
}

static void partC(void) {
  hash_t hb = hinit(), hr = hinit(), ht = hinit();
  for (uint32_t u = 0; u < 0x7c00; ++u) {  // finite non-negative halves
    double lo = h2d(u2h((uint16_t)u)), hi = h2d(u2h((uint16_t)(u + 1)));
    double mid = (lo + hi) / 2;             // exact in double
    uint64_t m = d2u(mid);
    for (int k = -2; k <= 2; ++k) {
      double d = u2d(m + (int64_t)k);
      hadd(&hb, h2u(d2h(d)));
      hadd(&hb, h2u(d2h(-d)));
    }
    // double-rounding trap: just above/below the midpoint by less than half a float ulp
    float mf = (float)mid;
    if ((double)mf == mid) {
      double up = u2d(m + 1), dn = u2d(m - 1);
      hadd(&ht, h2u(d2h(up)) | ((uint32_t)h2u(f2h((float)up)) << 16));
      hadd(&ht, h2u(d2h(dn)) | ((uint32_t)h2u(f2h((float)dn)) << 16));
    }
  }
  for (uint32_t i = 0; i < (1u << 24); ++i) {
    uint64_t r = next();
    // exponents around the half range: 2^-26 .. 2^17
    uint64_t e = 1023 - 26 + (r >> 58) % 44;
    double d = u2d((r & 0x800fffffffffffffull) | (e << 52));
    hadd(&hr, h2u(d2h(d)));
  }
  hprint("C f64->h bounds", hb);
  hprint("C f64->h dbl-round", ht);
  hprint("C f64->h random", hr);
}

static void partD(void) {
  hash_t he = hinit(), hn = hinit(), hv = hinit(), hd = hinit();
  for (uint32_t u = 0; u < 65536; ++u) {
    __bf16 b = u2b((uint16_t)u);
    hadd(&he, f2u(b2f(&b)));
  }
  uint32_t u = 0;
  do {
    __bf16 b;
    f2b(&b, u2f(u));
    if ((u & 0x7f800000u) == 0x7f800000u && (u & 0x7fffffu))
      hadd(&hn, b2u(b));
    else
      hadd(&hv, b2u(b));
  } while (++u != 0);
  for (uint32_t i = 0; i < (1u << 22); ++i) {
    uint64_t r = next();
    double d = u2d(r);
    __bf16 b;
    d2b(&b, d);
    hadd(&hd, b2u(b));
    // and a bf16 midpoint +- 1 double ulp
    uint32_t fb = (uint32_t)(r >> 32) & 0xffff0000u;
    double mid = (double)u2f(fb | 0x8000u);
    d2b(&b, u2d(d2u(mid) + 1)); hadd(&hd, b2u(b));
    d2b(&b, u2d(d2u(mid) - 1)); hadd(&hd, b2u(b));
  }
  hprint("D bf16->f32", he);
  hprint("D f32->bf16 nonNaN", hv);
  hprint("D f32->bf16 NaN", hn);
  hprint("D f64->bf16", hd);
}

static void partS(void) {
  static const uint16_t hs[] = {0x0001, 0x03ff, 0x0400, 0x3c00, 0x7bff, 0x7c00, 0x7c01, 0x7d00,
                                0x7e00, 0x7fff, 0xfc01, 0xfe00, 0x8000};
  for (unsigned i = 0; i < sizeof hs / sizeof hs[0]; ++i) {
    _Float16 h = u2h(hs[i]);
    s("S h"); hx(hs[i], 4); s(" ->f32"); hx(f2u(h2f(h)), 8); s(" ->f64"); hx(d2u(h2d(h)), 16); nl();
  }
  static const uint32_t fs[] = {0x7f800001u, 0x7fa00000u, 0x7fc00000u, 0x7fc00001u, 0x7fffe000u,
                                0xff812345u, 0x477ff000u, 0x477fefffu, 0x477ff001u, 0x33000000u,
                                0x33000001u, 0x32ffffffu, 0x38800000u, 0x387fe000u, 0x3f801000u,
                                0x3f803000u};
  for (unsigned i = 0; i < sizeof fs / sizeof fs[0]; ++i) {
    __bf16 b;
    f2b(&b, u2f(fs[i]));
    s("S f"); hx(fs[i], 8); s(" ->h"); hx(h2u(f2h(u2f(fs[i]))), 4); s(" ->bf"); hx(b2u(b), 4); nl();
  }
  static const uint64_t ds[] = {0x7ff0000000000001ull, 0x7ff8000000000001ull, 0x7ff4000000000000ull,
                                0x3f30040000000001ull, 0x40effe0000000000ull, 0x3e70000000000000ull,
                                0x3e70000000000001ull};
  for (unsigned i = 0; i < sizeof ds / sizeof ds[0]; ++i) {
    __bf16 b;
    d2b(&b, u2d(ds[i]));
    s("S d"); hx(ds[i], 16); s(" ->h"); hx(h2u(d2h(u2d(ds[i]))), 4); s(" ->bf"); hx(b2u(b), 4); nl();
  }
}

int main(int argc, char **argv) {
  const char *parts = argc > 1 ? argv[1] : "ABCDS";
  for (const char *p = parts; *p; ++p) {
    switch (*p) {
    case 'A': partA(); break;
    case 'B': partB(); break;
    case 'C': partC(); break;
    case 'D': partD(); break;
    case 'S': partS(); break;
    }
  }
  return 0;
}

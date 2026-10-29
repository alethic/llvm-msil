// C complex multiply and divide (clang emits __muldc3/__divdc3/__mulsc3/__divsc3) on named Annex G
// cases and on a 14^4 matrix of special operands. Prints result bit patterns ("named", "matrix") or
// hashes ("hash"). Only puts is used.
#include <stdint.h>
int puts(const char *);
#define memcpy __builtin_memcpy

static char line[200];
static int ll;
static void s(const char *t) { while (*t) line[ll++] = *t++; }
static void hx(uint64_t v, int n) {
  s(" ");
  for (int i = n - 1; i >= 0; --i) line[ll++] = "0123456789abcdef"[(v >> (4 * i)) & 15];
}
static void nl(void) { line[ll] = 0; puts(line); ll = 0; }
static uint64_t d2u(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

__attribute__((noinline)) double _Complex cmul(double _Complex a, double _Complex b) { return a * b; }
__attribute__((noinline)) double _Complex cdiv(double _Complex a, double _Complex b) { return a / b; }
__attribute__((noinline)) float _Complex fmul(float _Complex a, float _Complex b) { return a * b; }
__attribute__((noinline)) float _Complex fdiv(float _Complex a, float _Complex b) { return a / b; }

static volatile double zero = 0.0;
static double V[14];
static void initV(void) {
  double inf = 1.0 / zero, nan = zero / zero;
  double v[14] = {0.0, -0.0, 1.0, -1.0, 1.5, -2.25, 1e-310, 1e300, -1e300, inf, -inf, nan, 3.0, 1e-20};
  for (int i = 0; i < 14; ++i) V[i] = v[i];
}
static double _Complex mk(double re, double im) { double _Complex z; __real__ z = re; __imag__ z = im; return z; }
static float _Complex mkf(float re, float im) { float _Complex z; __real__ z = re; __imag__ z = im; return z; }

static void named(void) {
  double inf = 1.0 / zero, nan = zero / zero;
  struct { const char *n; double a, b, c, d; } cs[] = {
      {"ordinary", 1.5, -2.25, -2.25, 1.5},
      {"(inf+nan i)*(1+1i)", inf, nan, 1, 1},
      {"(nan+inf i)*(2+0i)", nan, inf, 2, 0},
      {"(inf+0i)*(0+0i)", inf, 0, 0, 0},
      {"(1e300+1e300i)^2", 1e300, 1e300, 1e300, 1e300},
      {"(0+inf i)*(0+inf i)", 0, inf, 0, inf},
      {"(inf+inf i)*(nan+nan i)", inf, inf, nan, nan},
      {"(1+1i)/(0+0i)", 1, 1, 0, 0},
      {"(1+1i)/(-0+0i)", 1, 1, -0.0, 0},
      {"(1+1i)/(inf+nan i)", 1, 1, inf, nan},
      {"(1+1i)/(inf+inf i)", 1, 1, inf, inf},
      {"(inf+inf i)/(1+1i)", inf, inf, 1, 1},
      {"(inf+nan i)/(1+1i)", inf, nan, 1, 1},
      {"(1e300+1e300i)/(1e300+1i)", 1e300, 1e300, 1e300, 1},
      {"(1e-300+1e-300i)/(1e-300+1e-300i)", 1e-300, 1e-300, 1e-300, 1e-300},
      {"(1+1i)/(1e-310+1e-310i)", 1, 1, 1e-310, 1e-310},
      {"(3+4i)/(1e308+1e308i)", 3, 4, 1e308, 1e308},
      {"(1e308+1e308i)/(1e-308+1e-308i)", 1e308, 1e308, 1e-308, 1e-308},
      {"(nan+0i)/(0+0i)", nan, 0, 0, 0},
      {"(0+0i)/(0+0i)", 0, 0, 0, 0},
  };
  for (unsigned i = 0; i < sizeof cs / sizeof cs[0]; ++i) {
    double _Complex x = mk(cs[i].a, cs[i].b), y = mk(cs[i].c, cs[i].d);
    int isdiv = 0;
    for (const char *q = cs[i].n; *q; ++q) isdiv |= *q == '/';
    double _Complex r = isdiv ? cdiv(x, y) : cmul(x, y);
    s(cs[i].n); s(" ="); hx(d2u(__real__ r), 16); hx(d2u(__imag__ r), 16); nl();
  }
}

static uint64_t H = 0xcbf29ce484222325ull;
static void hadd(uint64_t v) { H = (H ^ v) * 0x100000001b3ull; H ^= H >> 29; }

static void matrix(int print) {
  const char *ops[4] = {"dmul", "ddiv", "fmul", "fdiv"};
  for (int op = 0; op < 4; ++op) {
    H = 0xcbf29ce484222325ull;
    for (int a = 0; a < 14; ++a)
      for (int b = 0; b < 14; ++b)
        for (int c = 0; c < 14; ++c)
          for (int d = 0; d < 14; ++d) {
            uint64_t r1, r2;
            if (op < 2) {
              double _Complex x = mk(V[a], V[b]), y = mk(V[c], V[d]);
              double _Complex r = op == 0 ? cmul(x, y) : cdiv(x, y);
              r1 = d2u(__real__ r); r2 = d2u(__imag__ r);
            } else {
              float _Complex x = mkf((float)V[a], (float)V[b]), y = mkf((float)V[c], (float)V[d]);
              float _Complex r = op == 2 ? fmul(x, y) : fdiv(x, y);
              r1 = f2u(__real__ r); r2 = f2u(__imag__ r);
            }
            hadd(r1); hadd(r2);
            if (print) {
              s(ops[op]); hx((uint64_t)(((a * 14 + b) * 14 + c) * 14 + d), 5); hx(r1, 16); hx(r2, 16); nl();
            }
          }
    if (!print) { s(ops[op]); s(" hash"); hx(H, 16); nl(); }
  }
}

int main(int argc, char **argv) {
  initV();
  const char *m = argc > 1 ? argv[1] : "named";
  if (m[0] == 'n') named();
  else if (m[0] == 'm') matrix(1);
  else matrix(0);
  return 0;
}

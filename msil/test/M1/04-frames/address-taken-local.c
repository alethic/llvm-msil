// Address-taken scalar locals and parameters (Milestone 7): locals of every
// scalar width whose address escapes into a callee live in the localloc frame
// at -O0 and -O2. The callees store values that use the sign bit and every
// byte, so a store of the wrong width or two locals sharing an offset clobber
// a neighbour; then they read, modify and write each local back (sign and zero
// extension of narrow loads). Also pointers to pointers, a local reached two
// calls down, a local of the middle frame, parameters whose address is taken
// (in functions with int, float and double results), and a local updated
// through its pointer in a loop. The helpers have external linkage so that
// the optimizer cannot promote their pointer parameters to values (argument
// promotion only rewrites internal functions); inputs are derived from argc
// (1 when run without arguments). MSIL_EXPECT returns 1 + (line % 255) at the
// first failed check; main returns 0 when every check passes.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

// v is 1 when run without arguments; the comments give the values for v = 1.
NOINLINE void at_fill(signed char *c, unsigned char *uc, short *s,
                      unsigned short *us, int *i, unsigned *u, long long *ll,
                      unsigned long long *ull, float *f, double *d, _Bool *b,
                      int v) {
  *c = (signed char)(-128 + v);                       // -127
  *uc = (unsigned char)(255 - v);                     // 254
  *s = (short)(-32768 + v);                           // -32767
  *us = (unsigned short)(65535 - v);                  // 65534
  *i = -2147483647 - 1 + v;                           // INT_MIN + 1
  *u = 0xFFFFFFFFu - (unsigned)v;                     // 0xFFFFFFFE
  *ll = -9223372036854775807LL - 1 + v;               // LLONG_MIN + 1
  *ull = 0xFFFFFFFFFFFFFFFFull - (unsigned)v;         // 2^64 - 2
  *f = 0.5f + (float)v;                               // 1.5
  *d = -0.25 * v;                                     // -0.25
  *b = v & 1;                                         // 1
}

// Reads each local, changes it and writes it back.
NOINLINE void at_bump(signed char *c, unsigned char *uc, short *s,
                      unsigned short *us, int *i, unsigned *u, long long *ll,
                      unsigned long long *ull, float *f, double *d,
                      _Bool *b) {
  *c = (signed char)(*c + 1);
  *uc = (unsigned char)(*uc + 1);
  *s = (short)(*s + 1);
  *us = (unsigned short)(*us + 1);
  *i += 1;
  *u += 1;
  *ll += 1;
  *ull += 1;
  *f *= 2.0f;
  *d += 1.0;
  *b = !*b;
}

NOINLINE void at_point(int **pp, int *target) { *pp = target; }
NOINLINE int at_deref2(int *const *pp) { return **pp; }
NOINLINE int *at_pick(int *x, int *y, int c) { return c ? x : y; }
NOINLINE int at_same(const void *p, const void *q) { return p == q; }
NOINLINE void at_add(int *p, int v) { *p += v; }

// *p grows by 2 * v + 1; 'mid' is a local of this middle frame.
NOINLINE void at_add_twice(int *p, int v) {
  int mid = v;
  at_add(p, v);
  at_add(&mid, 1);
  at_add(p, mid);
}

NOINLINE void at_scale(long long *b, double *c) {
  *b *= 3;
  *c *= 0.5;
}

NOINLINE void at_negate8(signed char *d) { *d = (signed char)-*d; }

// Parameters whose address is taken get frame slots of their own.
NOINLINE int at_param_addr(int a, long long b, double c, signed char d) {
  at_add(&a, 5);
  at_scale(&b, &c);
  at_negate8(&d);
  return a + (int)b + (int)c + d;
}

NOINLINE void at_twice_d(double *p) { *p *= 2.0; }
NOINLINE void at_twice_f(float *p) { *p *= 2.0f; }

// Floating-point results of functions with a frame.
static NOINLINE double at_fp_params(double x, float y) {
  at_twice_d(&x);
  at_twice_f(&y);
  return x + y;
}

static NOINLINE float at_fp_result(float y) {
  float z = y + 0.5f;
  at_twice_f(&z);
  return z;
}

NOINLINE void at_step(unsigned *state) {
  *state = *state * 1103515245u + 12345u;
}

int main(int argc, char **argv) {
  (void)argv;
  int one = argc;

  signed char c;
  unsigned char uc;
  short s;
  unsigned short us;
  int i;
  unsigned u;
  long long ll;
  unsigned long long ull;
  float f;
  double d;
  _Bool b;
  at_fill(&c, &uc, &s, &us, &i, &u, &ll, &ull, &f, &d, &b, one);
  MSIL_EXPECT(c == -127);
  MSIL_EXPECT(uc == 254);
  MSIL_EXPECT(s == -32767);
  MSIL_EXPECT(us == 65534);
  MSIL_EXPECT(i == -2147483647);
  MSIL_EXPECT(u == 0xFFFFFFFEu);
  MSIL_EXPECT(ll == -9223372036854775807LL);
  MSIL_EXPECT(ull == 0xFFFFFFFFFFFFFFFEull);
  MSIL_EXPECT(f == 1.5f);
  MSIL_EXPECT(d == -0.25);
  MSIL_EXPECT(b == 1);

  at_bump(&c, &uc, &s, &us, &i, &u, &ll, &ull, &f, &d, &b);
  MSIL_EXPECT(c == -126);
  MSIL_EXPECT(uc == 255);
  MSIL_EXPECT(s == -32766);
  MSIL_EXPECT(us == 65535);
  MSIL_EXPECT(i == -2147483646);
  MSIL_EXPECT(u == 0xFFFFFFFFu);
  MSIL_EXPECT(ll == -9223372036854775806LL);
  MSIL_EXPECT(ull == 0xFFFFFFFFFFFFFFFFull);
  MSIL_EXPECT(f == 3.0f);
  MSIL_EXPECT(d == 0.75);
  MSIL_EXPECT(b == 0);

  // A second bump wraps the unsigned locals to 0.
  at_bump(&c, &uc, &s, &us, &i, &u, &ll, &ull, &f, &d, &b);
  MSIL_EXPECT(c == -125 && uc == 0 && s == -32765 && us == 0);
  MSIL_EXPECT(i == -2147483645 && u == 0 && ll == -9223372036854775805LL);
  MSIL_EXPECT(ull == 0 && f == 6.0f && d == 1.75 && b == 1);

  // Pointers to pointers, and stores through a pointer that names a local.
  int x = 10 * one, y = 20 * one;
  int *p;
  at_point(&p, &x);
  MSIL_EXPECT(at_deref2(&p) == 10);
  *p = 11;
  MSIL_EXPECT(x == 11 && y == 20);
  at_point(&p, &y);
  *p += 5;
  MSIL_EXPECT(x == 11 && y == 25);
  MSIL_EXPECT(at_deref2(&p) == 25);
  MSIL_EXPECT(at_same(p, &y) && !at_same(p, &x) && !at_same(&x, &y));
  *at_pick(&x, &y, one & 1) = 99;
  *at_pick(&x, &y, one - 1) += 1000;
  MSIL_EXPECT(x == 99 && y == 1025);

  // Two calls down, through a local of the middle frame as well.
  int acc = 100 * one;
  at_add_twice(&acc, 7 * one);
  MSIL_EXPECT(acc == 115);
  at_add_twice(&acc, -50 * one);
  MSIL_EXPECT(acc == 16);

  // Parameters whose address is taken: (3 + 5) + 1000 * 3 + 10.0 * 0.5 + 100,
  // then (-5 + 5) + -7 * 3 + (int)(-3.0 * 0.5) + 127.
  MSIL_EXPECT(at_param_addr(3 * one, 1000LL * one, 10.0 * one,
                            (signed char)(-100 * one)) == 3113);
  MSIL_EXPECT(at_param_addr(-5 * one, -7LL * one, -3.0 * one,
                            (signed char)(-127 * one)) == 105);

  // 1.25 * 2 + -0.75 * 2, and (3.25 + 0.5) * 2.
  MSIL_EXPECT(at_fp_params(1.25 * one, -0.75f * one) == 1.0);
  MSIL_EXPECT(at_fp_result(3.25f * one) == 7.5f);

  // A local updated through its pointer in a loop: 100 LCG steps from 7.
  unsigned state = 7u * one;
  for (int k = 0; k < 100 * one; k++)
    at_step(&state);
  MSIL_EXPECT(state == 0xD6780943u);

  return 0;
}

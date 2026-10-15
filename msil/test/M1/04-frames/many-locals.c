// Many locals and large frames (Milestone 7): a function with 320 scalar
// locals that are all live at once (at -O0 each is a frame slot; at -O2 each
// needs its own CIL local, so local indices pass 255 and the long ldloc and
// stloc forms are used); a 70000-byte local array between two other locals,
// so frame offsets need more than 16 bits; and a function with twelve
// parameters of mixed types whose addresses are all taken. The 320 values come
// from a generator that advances a local through its pointer and are folded in
// the reverse order of their definition, and the same values folded from an
// array must give the same result. The helpers have external linkage so that
// the optimizer keeps their pointer parameters; inputs are derived from argc
// (1 when run without arguments). MSIL_EXPECT returns 1 + (line % 255) at the
// first failed check; main returns 0 when every check passes.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

NOINLINE unsigned ml_next(unsigned *state) {
  *state = *state * 1103515245u + 12345u;
  return *state >> 3;
}

// M applied to the names p0..p3, p00..p33, ... in order (R*) and in reverse
// order (RR*).
#define R4(M, p) M(p##0) M(p##1) M(p##2) M(p##3)
#define R16(M, p) R4(M, p##0) R4(M, p##1) R4(M, p##2) R4(M, p##3)
#define R64(M, p) R16(M, p##0) R16(M, p##1) R16(M, p##2) R16(M, p##3)
#define R256(M, p) R64(M, p##0) R64(M, p##1) R64(M, p##2) R64(M, p##3)
#define RR4(M, p) M(p##3) M(p##2) M(p##1) M(p##0)
#define RR16(M, p) RR4(M, p##3) RR4(M, p##2) RR4(M, p##1) RR4(M, p##0)
#define RR64(M, p) RR16(M, p##3) RR16(M, p##2) RR16(M, p##1) RR16(M, p##0)
#define RR256(M, p) RR64(M, p##3) RR64(M, p##2) RR64(M, p##1) RR64(M, p##0)

#define DEFINE(name) unsigned name = ml_next(&state);
#define FOLD(name) h = h * 31u + name;

// 320 locals defined in order (a0000..a3333, then b000..b333) and folded
// last to first.
static NOINLINE unsigned fold320(unsigned seed) {
  unsigned state = seed;
  R256(DEFINE, a)
  R64(DEFINE, b)
  unsigned h = 0;
  RR64(FOLD, b)
  RR256(FOLD, a)
  return h;
}

// The same fold from an array.
static NOINLINE unsigned fold320_array(unsigned seed) {
  unsigned state = seed;
  unsigned v[320];
  for (int i = 0; i < 320; i++)
    v[i] = ml_next(&state);
  unsigned h = 0;
  for (int i = 319; i >= 0; i--)
    h = h * 31u + v[i];
  return h;
}

// Reads the four marked bytes and both neighbours, then changes them.
NOINLINE long long ml_touch(unsigned char *huge, int *before, long long *after,
                            int p1, int p2, int p3) {
  long long s = huge[0] + huge[p1] + huge[p2] + huge[p3] + *before + *after;
  *before += 10;
  *after += 20;
  huge[p3] ^= 0xFF;
  return s;
}

// Frame objects beyond offset 65535.
static NOINLINE long long big_offsets(int one) {
  int before = 1 * one;
  unsigned char huge[70000];
  long long after = (1LL << 33) * one;
  int p1 = 32767 * one, p2 = 65536 * one, p3 = 69999 * one;
  huge[0] = 1;
  huge[p1] = 2;
  huge[p2] = 3;
  huge[p3] = 4;
  long long s = ml_touch(huge, &before, &after, p1, p2, p3);
  // s = 1 + 2 + 3 + 4 + 1 + 2^33; then before = 11, after = 2^33 + 20 and
  // huge[69999] = 0xFB.
  return s + (before == 11) * 1000 + (after == (1LL << 33) + 20) * 10000 +
         (huge[69999] == 0xFB) * 100000 + (huge[0] == 1 && huge[p2] == 3) *
                                              1000000;
}

NOINLINE void ml_inc(int *p) { *p += 1; }
NOINLINE void ml_inc_u(unsigned *p) { *p += 1; }
NOINLINE void ml_inc_ll(long long *p) { *p += 1; }
NOINLINE void ml_inc_ull(unsigned long long *p) { *p += 1; }
NOINLINE void ml_twice(double *p) { *p *= 2; }
NOINLINE void ml_twice_f(float *p) { *p *= 2; }
NOINLINE void ml_neg_s(short *p) { *p = (short)-*p; }
NOINLINE void ml_neg_c(signed char *p) { *p = (signed char)-*p; }

// Twelve parameters whose addresses are taken.
static NOINLINE long long many_params(int a0, long long a1, double a2,
                                      float a3, short a4, signed char a5,
                                      unsigned a6, unsigned long long a7,
                                      int a8, int a9, double a10, int a11) {
  ml_inc(&a0);
  ml_inc_ll(&a1);
  ml_twice(&a2);
  ml_twice_f(&a3);
  ml_neg_s(&a4);
  ml_neg_c(&a5);
  ml_inc_u(&a6);
  ml_inc_ull(&a7);
  ml_inc(&a8);
  ml_inc(&a9);
  ml_twice(&a10);
  ml_inc(&a11);
  return a0 + a1 + (long long)a2 + (long long)a3 + a4 + a5 + a6 +
         (long long)a7 + a8 * 1000 + a9 * 100000 + (long long)a10 + a11 * 7;
}

int main(int argc, char **argv) {
  (void)argv;
  int one = argc;

  MSIL_EXPECT(fold320(1u * one) == 0x5258F810u);
  MSIL_EXPECT(fold320(77u * one) == 0x876C1AD0u);
  MSIL_EXPECT(fold320_array(77u * one) == 0x876C1AD0u);

  MSIL_EXPECT(big_offsets(one) == 1111000 + 11 + (1LL << 33));

  // 2 + (2^40 + 1) + 5 + 2 - 300 + 5 + 4000000001 + (2^50 + 1) + 9 * 1000
  // + 10 * 100000 - 1 + 12 * 7.
  MSIL_EXPECT(many_params(1 * one, (1LL << 40) * one, 2.5 * one, 1.25f * one,
                          (short)(300 * one), (signed char)(-5 * one),
                          4000000000u * one, (1ull << 50) * one, 8 * one,
                          9 * one, -0.5 * one, 11 * one) ==
              (1LL << 40) + (1LL << 50) + 4001008800LL);
  return 0;
}

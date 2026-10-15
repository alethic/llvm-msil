// Calls with many arguments (Milestone 9): CIL passes every argument as a
// typed method parameter, so long argument lists of mixed types must keep
// their order and types in the method signature, the call site and ldarg
// numbering (ldarg.s for indexes 4 and up). Covered: 12 mixed int, long
// long and double arguments (the native ABIs put most of them on the
// stack); 20 arguments of 7 types including float, char, short and
// unsigned; 16 doubles; the same functions through function pointers
// (calli); results of calls used as arguments of other many-argument
// calls; and a by-value struct among the scalars. Each callee weights its
// arguments by position, so a swapped or dropped argument changes the
// printed result. MSIL_EXPECT returns 1 + (line % 255) at the first failed
// check; [exit 0] means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

volatile int zero = 0;

NOINLINE long long twelve(int a, long long b, double c, int d, double e,
                          long long f, int g, double h, long long i, int j,
                          double k, long long l) {
  return a * 1LL + b * 2 + (long long)(c * 4) * 3 + d * 5LL +
         (long long)(e * 4) * 7 + f * 11 + g * 13LL + (long long)(h * 4) * 17 +
         i * 19 + j * 23LL + (long long)(k * 4) * 29 + l * 31;
}

NOINLINE long long twenty(int a0, float a1, char a2, long long a3, short a4,
                          double a5, unsigned a6, int a7, float a8, char a9,
                          long long a10, short a11, double a12, unsigned a13,
                          int a14, float a15, signed char a16, long long a17,
                          unsigned short a18, double a19) {
  long long h = 0;
  h = h * 3 + a0;
  h = h * 3 + (long long)(a1 * 2);
  h = h * 3 + a2;
  h = h * 3 + a3;
  h = h * 3 + a4;
  h = h * 3 + (long long)(a5 * 2);
  h = h * 3 + a6;
  h = h * 3 + a7;
  h = h * 3 + (long long)(a8 * 2);
  h = h * 3 + a9;
  h = h * 3 + a10;
  h = h * 3 + a11;
  h = h * 3 + (long long)(a12 * 2);
  h = h * 3 + a13;
  h = h * 3 + a14;
  h = h * 3 + (long long)(a15 * 2);
  h = h * 3 + a16;
  h = h * 3 + a17;
  h = h * 3 + a18;
  h = h * 3 + (long long)(a19 * 2);
  return h;
}

NOINLINE double sixteen(double d0, double d1, double d2, double d3, double d4,
                        double d5, double d6, double d7, double d8, double d9,
                        double d10, double d11, double d12, double d13,
                        double d14, double d15) {
  return d0 - d1 + 2 * d2 - 3 * d3 + 4 * d4 - 5 * d5 + 6 * d6 - 7 * d7 +
         8 * d8 - 9 * d9 + 10 * d10 - 11 * d11 + 12 * d12 - 13 * d13 +
         14 * d14 - 15 * d15;
}

struct pair {
  long long x;
  int y;
};

NOINLINE long long with_struct(int a, double b, struct pair p, long long c,
                               float d, struct pair q, int e) {
  return a + (long long)(b * 2) * 10 + p.x * 100 + p.y * 1000LL + c * 7 +
         (long long)(d * 4) * 3 + q.x * 5 + q.y * 9LL + e * 11LL;
}

int main(void) {
  long long r12 = twelve(1, 1LL << 33, 0.25, -4, 1.5, -(1LL << 40), 7, -2.75,
                         123456789012LL, -10 + zero, 100.5, 3);
  put_kv("twelve", r12);
  MSIL_EXPECT(r12 == 1 + (1LL << 34) + 3 - 20 + 42 - 11 * (1LL << 40) + 91 -
                         187 + 19 * 123456789012LL - 230 + 402 * 29 + 93);

  long long r20 = twenty(-1, 1.5f, 'c', -(1LL << 35), -300, 2.25, 4000000000u,
                         zero + 8, -0.5f, 'z', 1LL << 40, 32767, -1.75, 7u, -9,
                         3.5f, (signed char)-128, -5, 65535, 0.5);
  put_kv("twenty", r20);

  double r16 =
      sixteen(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 + zero);
  put_kv("sixteen", (long long)r16);
  MSIL_EXPECT(r16 == -127.0);

  struct pair p = {1LL << 36, -77}, q = {-5, 1 << 20};
  long long rs = with_struct(3, 0.5, p, -2, 2.25f, q, 6 + zero);
  put_kv("with struct", rs);

  // The same through pointers.
  long long (*volatile p12)(int, long long, double, int, double, long long, int,
                            double, long long, int, double, long long) = twelve;
  MSIL_EXPECT(p12(1, 1LL << 33, 0.25, -4, 1.5, -(1LL << 40), 7, -2.75,
                  123456789012LL, -10, 100.5, 3) == r12);
  long long (*volatile p20)(int, float, char, long long, short, double,
                            unsigned, int, float, char, long long, short,
                            double, unsigned, int, float, signed char,
                            long long, unsigned short, double) = twenty;
  MSIL_EXPECT(p20(-1, 1.5f, 'c', -(1LL << 35), -300, 2.25, 4000000000u, 8,
                  -0.5f, 'z', 1LL << 40, 32767, -1.75, 7u, -9, 3.5f,
                  (signed char)-128, -5, 65535, 0.5) == r20);
  double (*volatile p16)(double, double, double, double, double, double, double,
                         double, double, double, double, double, double, double,
                         double, double) = sixteen;
  MSIL_EXPECT(p16(16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1) ==
              8.0);
  long long (*volatile ps)(int, double, struct pair, long long, float,
                           struct pair, int) = with_struct;
  MSIL_EXPECT(ps(3, 0.5, p, -2, 2.25f, q, 6) == rs);

  // Calls as arguments of calls.
  long long nested =
      twelve((int)sixteen(1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, zero),
             twelve(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1), 0.5, 1, 0.25, 2, 3,
             0.75, 4, 5, 1.25, p12(0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0));
  put_kv("nested", nested);
  return 0;
}

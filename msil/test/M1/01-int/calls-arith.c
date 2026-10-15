// Direct calls and integer arithmetic from C (Milestone 4): noinline
// helpers get inputs derived from argc (1 when run without arguments), so
// neither clang nor llc can fold them; each MSIL_CHECK group sets one bit of
// the exit status when one of its checks fails (0 = all passed, 8 bits, D37).
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

static NOINLINE int add3(int a, int b, int c) { return a + b + c; }
NOINLINE long long mul_add(long long a, long long b, long long c) {
  return a * b + c;
}
static NOINLINE long long widen_s(int v) { return v; }
static NOINLINE long long widen_u(unsigned v) { return v; }
static NOINLINE int narrow(long long v) { return (int)v; }
static NOINLINE int sdiv(int a, int b) { return a / b; }
static NOINLINE int smod(int a, int b) { return a % b; }
static NOINLINE unsigned udiv(unsigned a, unsigned b) { return a / b; }
static NOINLINE unsigned umod(unsigned a, unsigned b) { return a % b; }
static NOINLINE long long sdiv64(long long a, long long b) { return a / b; }
static NOINLINE unsigned long long umod64(unsigned long long a,
                                          unsigned long long b) {
  return a % b;
}
static NOINLINE signed char add_s8(signed char a, signed char b) {
  return (signed char)(a + b);
}
static NOINLINE unsigned short mul_u16(unsigned short a, unsigned short b) {
  return (unsigned short)((unsigned)a * b);
}
static NOINLINE unsigned char to_u8(int v) { return (unsigned char)v; }
static NOINLINE int from_s8(signed char v) { return v; }
static NOINLINE _Bool is_neg(long long v) { return v < 0; }
static NOINLINE int pick(_Bool c, int a, int b) { return c ? a : b; }
static NOINLINE int max_i(int a, int b) { return a > b ? a : b; }
static NOINLINE unsigned min_u(unsigned a, unsigned b) { return a < b ? a : b; }
static NOINLINE unsigned long long shr64(unsigned long long v, int n) {
  return v >> n;
}
static NOINLINE long long sar64(long long v, int n) { return v >> n; }
static NOINLINE unsigned shl32(unsigned v, int n) { return v << n; }
static NOINLINE int cmp3(long long a, long long b) { return (a > b) - (a < b); }

int main(int argc, char **argv) {
  (void)argv;
  unsigned acc = 0;
  int one = argc;
  int seven = 7 * one;
  long long big = (long long)one << 40;

  // Bit 0: int32 calls and wrap-free sums.
  MSIL_CHECK(acc, 0, add3(seven, -one, 100 * one) == 106);
  MSIL_CHECK(acc, 0, add3(0x7fffffff * one, -0x7fffffff * one, seven) == 7);

  // Bit 1: 64-bit multiply-add and int32 <-> long long conversions.
  MSIL_CHECK(acc, 1, mul_add(big, big >> 20, seven) == (1LL << 60) + 7);
  MSIL_CHECK(acc, 1, mul_add(-big, 3 * one, -one) == -3 * (1LL << 40) - 1);
  MSIL_CHECK(acc, 1, widen_s(-seven) == -7);
  MSIL_CHECK(acc, 1, widen_u((unsigned)-seven) == 0xfffffff9LL);
  MSIL_CHECK(acc, 1, narrow(big + seven) == 7);

  // Bit 2: int32 division, signed and unsigned.
  MSIL_CHECK(acc, 2, sdiv(-seven * 3, 2 * one) == -10);
  MSIL_CHECK(acc, 2, smod(-seven * 3, 2 * one) == -1);
  MSIL_CHECK(acc, 2, udiv(0xffffffffu * one, 3u * one) == 0x55555555u);
  MSIL_CHECK(acc, 2, umod(0x80000000u * one, seven) == 2u);

  // Bit 3: 64-bit division.
  MSIL_CHECK(acc, 3, sdiv64(-big - seven, 1000 * one) == -1099511627);
  MSIL_CHECK(acc, 3, umod64(~0ull * one, 10 * one) == 5);

  // Bit 4: char and short parameters and results.
  MSIL_CHECK(acc, 4, add_s8(100 * one, 100 * one) == -56);
  MSIL_CHECK(acc, 4, mul_u16(300 * one, 300 * one) == 24464);
  MSIL_CHECK(acc, 4, to_u8(-one) == 255);
  MSIL_CHECK(acc, 4, from_s8(-seven) == -7);

  // Bit 5: _Bool results and parameters, conditional expressions.
  MSIL_CHECK(acc, 5, pick(is_neg(-big), seven, -seven) == 7);
  MSIL_CHECK(acc, 5, pick(is_neg(big), seven, -seven) == -7);
  MSIL_CHECK(acc, 5, max_i(-seven, one) == 1);
  MSIL_CHECK(acc, 5, min_u((unsigned)-seven, (unsigned)one) == 1u);

  // Bit 6: shifts by variable amounts.
  MSIL_CHECK(acc, 6, shr64(0x8000000000000000ull * one, 63 * one) == 1);
  MSIL_CHECK(acc, 6, sar64(-big, 40 * one) == -1);
  MSIL_CHECK(acc, 6, shl32(0x80000001u * one, one) == 2u);

  // Bit 7: compares as values.
  MSIL_CHECK(acc, 7, cmp3(big, -big) == 1);
  MSIL_CHECK(acc, 7, cmp3(-big, big) == -1);
  MSIL_CHECK(acc, 7, cmp3(big, big * one) == 0);
  MSIL_CHECK(acc, 7, ((unsigned)-one > (unsigned)one) + (-one < one) == 2);

  return (int)acc;
}

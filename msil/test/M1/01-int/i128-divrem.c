// __int128 division and remainder (Milestone 12, plan Q36 and the section 12
// row 'D14/D33 note and P3-B text'): MSILTargetLowering sets
// setMaxDivRemBitWidthSupported(64), so ExpandIRInsts expands udiv, urem, sdiv
// and srem of 128-bit integers inline and no compiler-rt helper (__udivti3,
// __umodti3, __divti3, __modti3) is called; before M12 each of them was a
// Phase-3 helper error. The native oracle calls those helpers: the VS clang's
// clang_rt.builtins on Windows (msil_native_rtlib) and libgcc_s.so.1 under the
// E7 Linux oracle (plan 5.12.8). The noinline helpers get the operands of
// backlog i128.c's 128-bit xorshift sequence, shifted right by 0..127 bits so
// that the divisors have every width from 1 to 128 bits, negated so that every
// sign combination of dividend and divisor occurs (the negation is done in
// unsigned arithmetic, so -2^127 never overflows), and a table of edge cases:
// all ones, 2^127, -2^127 (never divided by -1, which C leaves undefined), 1,
// quotients of 0 and 1, divisors just above 2^64 and of a single bit, all
// multiplied by argc (1 when run without arguments) so that -O2 cannot fold
// them. Every result is checked with a == q * b + r (in unsigned arithmetic,
// so no check can overflow), |r| < |b| and r having the sign of a (C truncates
// toward zero); the edge-case results and a checksum over all quotients and
// remainders are printed as 32 hex digits. MSIL_EXPECT returns
// 1 + (line % 255) at the first failed check; [exit 0] means every check
// passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

typedef unsigned __int128 u128;
typedef __int128 i128;

#define U128(hi, lo) (((u128)(hi) << 64) | (u128)(lo))
#define I128_MIN ((i128)U128(0x8000000000000000ull, 0))
#define I128_MAX ((i128)U128(0x7FFFFFFFFFFFFFFFull, ~0ull))

NOINLINE u128 udiv(u128 a, u128 b) { return a / b; }
NOINLINE u128 urem(u128 a, u128 b) { return a % b; }
NOINLINE i128 sdiv(i128 a, i128 b) { return a / b; }
NOINLINE i128 srem(i128 a, i128 b) { return a % b; }

// 0x followed by all 32 hex digits.
static void put_hex128(u128 v) {
  static const char digits[] = "0123456789abcdef";
  char buf[35];
  buf[0] = '0';
  buf[1] = 'x';
  for (int i = 0; i < 32; i++)
    buf[2 + i] = digits[(unsigned)(v >> (124 - 4 * i)) & 15];
  buf[34] = 0;
  puts(buf);
}

// Two's complement negation without signed overflow.
static i128 neg(i128 v) { return (i128)(0 - (u128)v); }

static u128 abs_u(i128 v) { return v < 0 ? 0 - (u128)v : (u128)v; }

static u128 seed = U128(0x9E3779B97F4A7C15ull, 0xD1B54A32D192ED03ull);

static u128 next(void) {
  seed ^= seed << 13;
  seed ^= seed >> 7;
  seed ^= seed << 17;
  return seed;
}

static u128 checksum;

static void mix(u128 v) { checksum = checksum * 31 + v; }

// b != 0.
static int check_unsigned(u128 a, u128 b) {
  u128 q = udiv(a, b);
  u128 r = urem(a, b);
  MSIL_EXPECT(r < b);
  MSIL_EXPECT(q * b + r == a);
  MSIL_EXPECT(q <= a);
  mix(q);
  mix(r);
  return 0;
}

// b != 0, and not a == -2^127 with b == -1.
static int check_signed(i128 a, i128 b) {
  i128 q = sdiv(a, b);
  i128 r = srem(a, b);
  MSIL_EXPECT((u128)q * (u128)b + (u128)r == (u128)a);
  MSIL_EXPECT(abs_u(r) < abs_u(b));
  MSIL_EXPECT(r == 0 || (r < 0) == (a < 0));
  // The quotient is negative exactly when the signs differ and |a| >= |b|.
  MSIL_EXPECT((q < 0) == ((a < 0) != (b < 0) && abs_u(a) >= abs_u(b)));
  mix((u128)q);
  mix((u128)r);
  return 0;
}

// Every sign combination of a and b. Callers never pass a == -2^127, so
// neither a nor -a is -2^127 and no quotient overflows.
static int check_signs(i128 a, i128 b) {
  int e;
  if ((e = check_signed(a, b)) != 0)
    return e;
  if ((e = check_signed(neg(a), b)) != 0)
    return e;
  if ((e = check_signed(a, neg(b))) != 0)
    return e;
  if ((e = check_signed(neg(a), neg(b))) != 0)
    return e;
  return 0;
}

struct pair {
  unsigned long long ahi, alo, bhi, blo;
};

static const struct pair edges[] = {
    {~0ull, ~0ull, 0, 7},                                  // all ones / 7
    {~0ull, ~0ull, 0, 1},                                  // all ones / 1
    {~0ull, ~0ull, ~0ull, ~0ull},                          // quotient 1
    {0x0123456789ABCDEFull, 0, 0x0123456789ABCDEFull, 1},  // quotient 0
    {0x8000000000000000ull, 0, 1, 0},                      // 2^127 / 2^64
    {3, 5, 1, 1},                                          // just above 2^64
    {0xFEDCBA9876543210ull, 0x0F1E2D3C4B5A6978ull, 0, 3},  // small divisor
    {0x8000000000000000ull, 0, 0x8000000000000000ull, 0},  // single bit
    {0x7FFFFFFFFFFFFFFFull, ~0ull, 0, 0x100000000ull},     // 2^32
    {0, 0xFFFFFFFFFFFFFFFFull, 0, 0xFFFFFFFFull},          // 64-bit dividend
    {0x0000000000001234ull, 0x5678000000000000ull, 0x10, 0x8000000000000001ull},
};

int main(int argc, char **argv) {
  (void)argv;
  int e;
  // 1 when run without arguments; keeps -O2 from folding the edge cases.
  u128 one = (u128)argc;

  for (int i = 0; i < (int)(sizeof edges / sizeof edges[0]); i++) {
    u128 a = U128(edges[i].ahi, edges[i].alo) * one;
    u128 b = U128(edges[i].bhi, edges[i].blo) * one;
    put_hex128(udiv(a, b));
    put_hex128(urem(a, b));
    put_hex128((u128)sdiv((i128)a, (i128)b));
    put_hex128((u128)srem((i128)a, (i128)b));
    if ((e = check_unsigned(a, b)) != 0)
      return e;
    if ((i128)a != I128_MIN && (e = check_signs((i128)a, (i128)b)) != 0)
      return e;
  }

  // The signed extremes: -2^127 by 1, -2, 2^127 - 1 and 3; 2^127 - 1 by -1.
  i128 min = I128_MIN * (i128)one, max = I128_MAX * (i128)one;
  put_hex128((u128)sdiv(min, 1));
  put_hex128((u128)sdiv(min, -2));
  put_hex128((u128)srem(min, -2));
  put_hex128((u128)sdiv(min, max));
  put_hex128((u128)srem(min, max));
  put_hex128((u128)sdiv(min, 3));
  put_hex128((u128)srem(min, 3));
  put_hex128((u128)sdiv(max, -1));
  if ((e = check_signed(min, 1)) != 0)
    return e;
  if ((e = check_signed(min, -2)) != 0)
    return e;
  if ((e = check_signed(min, max)) != 0)
    return e;
  if ((e = check_signed(min, 3)) != 0)
    return e;
  if ((e = check_signed(max, -1)) != 0)
    return e;
  // -10^24 / 7 and 10^24 % -7: quotients round toward zero.
  i128 big = (i128)1000000000000ll * 1000000000000ll * (i128)one;
  put_hex128((u128)sdiv(neg(big), 7));
  put_hex128((u128)srem(big, -7));
  // -7 / 2, -7 % 2, 7 / -2 and 7 % -2.
  put_hex128((u128)sdiv(-7 * (i128)one, 2));
  put_hex128((u128)srem(-7 * (i128)one, 2));
  put_hex128((u128)sdiv(7 * (i128)one, -2));
  put_hex128((u128)srem(7 * (i128)one, -2));

  // The xorshift operands: divisors of every width and both signs.
  for (int i = 0; i < 1024; i++) {
    u128 a = next();
    u128 b = next() >> (i % 128);
    if (b == 0)
      b = 3;
    if ((e = check_unsigned(a, b)) != 0)
      return e;
    // Dividends of every width too.
    if ((e = check_unsigned(a >> ((i * 7) % 128), b)) != 0)
      return e;
    if ((i128)a != I128_MIN && (e = check_signs((i128)a, (i128)b)) != 0)
      return e;
    // A non-negative dividend of every width by an odd divisor.
    i128 sa = (i128)(a >> 1) >> ((i * 5) % 127);
    if ((e = check_signs(sa, (i128)(b >> 1) | 1)) != 0)
      return e;
  }
  put_hex128(checksum);
  return 0;
}

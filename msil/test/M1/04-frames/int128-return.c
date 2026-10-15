// __int128 and unsigned __int128 returned by value (Milestone 7): clang gives
// these returns a type that the target cannot return directly, <2 x i64> on
// x86_64-pc-windows-msvc-msil (the Win64 shape: XMM0 natively) and i128 on
// x86_64-unknown-linux-gnu-msil, so CanLowerReturn demotes both to a hidden
// pointer into a 16-aligned slot in the caller's frame; the slot exists only
// with frames. Gates 5 (Windows) and 6 (WSL, linux-gnu triple) of M7 run the
// two shapes. The noinline helpers return a value built from two 64-bit
// halves, the full 64 x 64 -> 128-bit product, a negation, their own __int128
// argument (Win64 passes __int128 arguments by reference) and one of two
// __int128 arguments chosen at run time; main checks both halves of every
// result ((uint64_t)x and (uint64_t)(x >> 64), see LO and HI), results kept
// alive across later calls, a result stored through a pointer into one
// element of an array (the other element keeps its value), results passed
// straight into another call, and chains of recursive calls that each return
// through their caller's slot. At -O2 on the linux-gnu triple clang marks
// i128_ident's argument 'returned' and its callers use the argument instead of
// the result, so i128_pick carries the argument-to-result check there. No
// i128 division or remainder: they are compiler-rt calls (__divti3,
// __udivti3), errors under plan D14, and the MSVC native oracle has no such
// functions. No aggregate or string initializers (plan 5.5, until M8). Inputs
// are derived from argc (1 when run without arguments), and no function is
// called twice with the same arguments, so that -O2 cannot merge the calls.
// MSIL_EXPECT returns 1 + (line % 255) at the first failed check; main
// returns 0 when every check passes.
// RUN: %{msil_c}
#include <stdint.h>

#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

typedef unsigned __int128 u128;
typedef __int128 i128;

// The halves of a 128-bit value, through the unsigned type so that the shift
// of a negative __int128 is not implementation-defined.
#define LO(x) ((uint64_t)(u128)(x))
#define HI(x) ((uint64_t)((u128)(x) >> 64))

NOINLINE u128 i128_make(uint64_t hi, uint64_t lo) {
  return (u128)hi << 64 | lo;
}

NOINLINE u128 i128_mul(uint64_t a, uint64_t b) { return (u128)a * b; }

NOINLINE i128 i128_neg(i128 a) { return -a; }

NOINLINE i128 i128_ident(i128 a) { return a; }

NOINLINE i128 i128_pick(i128 a, i128 b, int first) { return first ? a : b; }

// Stores the value returned by i128_make through p.
NOINLINE void i128_put(u128 *p, uint64_t hi, uint64_t lo) {
  *p = i128_make(hi, lo);
}

// base * 2^k by k doublings; every level returns through its caller's slot.
NOINLINE u128 i128_pow2(int k, uint64_t base) {
  if (k == 0)
    return base;
  u128 r = i128_pow2(k - 1, base);
  return r + r;
}

int main(int argc, char **argv) {
  (void)argv;
  int one = argc;
  uint64_t ones = 0ull - (uint64_t)one; // ~0ull

  // Built from two halves.
  u128 x = i128_make(0x0123456789ABCDEFull * one, 0xFEDCBA9876543210ull * one);
  MSIL_EXPECT(LO(x) == 0xFEDCBA9876543210ull);
  MSIL_EXPECT(HI(x) == 0x0123456789ABCDEFull);
  u128 x2 = i128_make(0, 5ull * one);
  MSIL_EXPECT(LO(x2) == 5ull);
  MSIL_EXPECT(HI(x2) == 0ull);

  // 64 x 64 -> 128-bit products.
  u128 p = i128_mul(ones, ones);
  MSIL_EXPECT(LO(p) == 1ull);
  MSIL_EXPECT(HI(p) == 0xFFFFFFFFFFFFFFFEull);
  u128 p2 = i128_mul(0x0123456789ABCDEFull * one, 0xFEDCBA9876543210ull * one);
  MSIL_EXPECT(LO(p2) == 0x2236D88FE5618CF0ull);
  MSIL_EXPECT(HI(p2) == 0x0121FA00AD77D742ull);

  // Negations: a carry out of the low half, a zero low half, and both halves.
  i128 n = i128_neg((i128)x2);
  MSIL_EXPECT(LO(n) == 0xFFFFFFFFFFFFFFFBull);
  MSIL_EXPECT(HI(n) == 0xFFFFFFFFFFFFFFFFull);
  i128 n2 = i128_neg((i128)i128_make(1ull * one, 0));
  MSIL_EXPECT(LO(n2) == 0ull);
  MSIL_EXPECT(HI(n2) == 0xFFFFFFFFFFFFFFFFull);
  i128 n3 = i128_neg((i128)x);
  MSIL_EXPECT(LO(n3) == 0x0123456789ABCDF0ull);
  MSIL_EXPECT(HI(n3) == 0xFEDCBA9876543210ull);

  // The argument returned unchanged, positive and negative, and one of two
  // arguments.
  i128 id = i128_ident((i128)x);
  MSIL_EXPECT(LO(id) == 0xFEDCBA9876543210ull);
  MSIL_EXPECT(HI(id) == 0x0123456789ABCDEFull);
  i128 id2 = i128_ident(n);
  MSIL_EXPECT(LO(id2) == 0xFFFFFFFFFFFFFFFBull);
  MSIL_EXPECT(HI(id2) == 0xFFFFFFFFFFFFFFFFull);
  i128 pk = i128_pick((i128)x, n, one);
  MSIL_EXPECT(LO(pk) == 0xFEDCBA9876543210ull);
  MSIL_EXPECT(HI(pk) == 0x0123456789ABCDEFull);
  i128 pk2 = i128_pick((i128)x, n, one - 1);
  MSIL_EXPECT(LO(pk2) == 0xFFFFFFFFFFFFFFFBull);
  MSIL_EXPECT(HI(pk2) == 0xFFFFFFFFFFFFFFFFull);

  // The earlier results are unchanged by the later calls.
  MSIL_EXPECT(LO(x) == 0xFEDCBA9876543210ull);
  MSIL_EXPECT(HI(x) == 0x0123456789ABCDEFull);
  MSIL_EXPECT(LO(p) == 1ull);
  MSIL_EXPECT(HI(p) == 0xFFFFFFFFFFFFFFFEull);
  MSIL_EXPECT(LO(n) == 0xFFFFFFFFFFFFFFFBull);
  MSIL_EXPECT(HI(n) == 0xFFFFFFFFFFFFFFFFull);

  // Stored through a pointer: by a callee into slots[1], and here into
  // slots[one - 1] (slots[0]) through an index the compiler cannot fold.
  u128 slots[2];
  slots[0] = x2;
  slots[1] = x2;
  i128_put(&slots[one], 0x1122334455667788ull * one,
           0x99AABBCCDDEEFF00ull * one);
  MSIL_EXPECT(LO(slots[0]) == 5ull);
  MSIL_EXPECT(HI(slots[0]) == 0ull);
  MSIL_EXPECT(LO(slots[1]) == 0x99AABBCCDDEEFF00ull);
  MSIL_EXPECT(HI(slots[1]) == 0x1122334455667788ull);
  slots[one - 1] = i128_mul(ones, 3ull * one);
  MSIL_EXPECT(LO(slots[0]) == 0xFFFFFFFFFFFFFFFDull);
  MSIL_EXPECT(HI(slots[0]) == 2ull);
  MSIL_EXPECT(LO(slots[1]) == 0x99AABBCCDDEEFF00ull);
  MSIL_EXPECT(HI(slots[1]) == 0x1122334455667788ull);

  // Passed straight into another call: -(5 * (2^64 - 1)), -(-(2^65)), and
  // the second of -9 and 7.
  i128 c = i128_ident(i128_neg((i128)i128_mul(ones, 5ull * one)));
  MSIL_EXPECT(LO(c) == 5ull);
  MSIL_EXPECT(HI(c) == 0xFFFFFFFFFFFFFFFBull);
  i128 c2 = i128_neg(i128_ident(i128_neg((i128)i128_make(2ull * one, 0))));
  MSIL_EXPECT(LO(c2) == 0ull);
  MSIL_EXPECT(HI(c2) == 2ull);
  i128 c3 = i128_pick(i128_neg((i128)i128_make(0, 9ull * one)),
                      i128_ident((i128)i128_make(0, 7ull * one)), one - 1);
  MSIL_EXPECT(LO(c3) == 7ull);
  MSIL_EXPECT(HI(c3) == 0ull);

  // Recursion: 2^100, and 0xF000000000000001 * 8 (carries into the high half).
  u128 r = i128_pow2(100 * one, 1ull * one);
  MSIL_EXPECT(LO(r) == 0ull);
  MSIL_EXPECT(HI(r) == 0x0000001000000000ull);
  u128 r2 = i128_pow2(3 * one, 0xF000000000000001ull * one);
  MSIL_EXPECT(LO(r2) == 0x8000000000000008ull);
  MSIL_EXPECT(HI(r2) == 7ull);

  return 0;
}

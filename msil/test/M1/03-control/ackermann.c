// Ackermann's function from C (Milestone 6): recursion whose depth and
// call count grow fast (ack(3, n) = 2^(n + 3) - 3; ack(3, 8) makes about 2.8
// million calls and goes 2047 frames deep), with a recursive call as the
// argument of another, in the classic three-case form on int, in a form
// whose outer tail call is written as a loop, and on unsigned long long. The
// inputs are derived from argc (1 when run without arguments). MSIL_EXPECT
// returns 1 + (line % 255) at the first failed check; when every check
// passes, main returns a checksum of the results with all four bytes folded
// into the low 8 bits (D37). That status is not 1 + (line % 255) for any
// MSIL_EXPECT line of this file, so a failed check cannot pass for it
// (re-check after edits that move lines).
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

static NOINLINE int ack(int m, int n) {
  if (m == 0)
    return n + 1;
  if (n == 0)
    return ack(m - 1, 1);
  return ack(m - 1, ack(m, n - 1));
}

static NOINLINE unsigned ack_loop(unsigned m, unsigned n) {
  while (m != 0) {
    if (n == 0)
      n = 1;
    else
      n = ack_loop(m, n - 1);
    m--;
  }
  return n + 1;
}

static NOINLINE unsigned long long ack64(unsigned long long m,
                                         unsigned long long n) {
  if (m == 0)
    return n + 1;
  if (n == 0)
    return ack64(m - 1, 1);
  return ack64(m - 1, ack64(m, n - 1));
}

int main(int argc, char **argv) {
  (void)argv;
  int one = argc;
  unsigned h = 0;
#define MIX(v) (h = h * 31u + (unsigned)(v))

  MSIL_EXPECT(ack(0 * one, 0 * one) == 1);
  MSIL_EXPECT(ack(1 * one, 2 * one) == 4);
  MSIL_EXPECT(ack(2 * one, 3 * one) == 9);
  MSIL_EXPECT(ack(3 * one, 3 * one) == 61);
  int a = ack(3 * one, 5 + one);
  MSIL_EXPECT(a == 509);
  MIX(a);
  for (int m = 0; m <= 3 * one; m++)
    for (int n = 0; n <= 4 * one; n++)
      MIX(ack(m, n));

  unsigned b = ack_loop(3u * one, 7u * one);
  MSIL_EXPECT(b == 1021);
  MSIL_EXPECT(ack_loop(2u * one, 10u * one) == 23);
  MSIL_EXPECT(ack_loop(0u * one, 41u * one) == 42);
  MIX(b);

  unsigned long long c = ack64(3ull * one, 8ull * one);
  MSIL_EXPECT(c == 2045);
  MSIL_EXPECT(ack64(0ull * one, 0xFFFFFFFFull * one) == 0x100000000ull);
  MSIL_EXPECT(ack64(1ull * one, 5ull * one) == 7);
  MIX(c);

  // Fold all four bytes of h into the low byte before masking: h & 0xff
  // alone depends only on the low byte of each mixed value.
  h ^= h >> 16;
  h ^= h >> 8;
  return (int)(h & 0xff);
}

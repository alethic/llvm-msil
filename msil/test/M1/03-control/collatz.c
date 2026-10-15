// Collatz sequences from C (Milestone 6): data-dependent while loops
// with an even/odd branch on unsigned long long, the longest chain below a
// bound (a loop nest with a running maximum and its argmax), the peak value
// of a chain, a recursive formulation, and a loop with two exits (reaching 1
// or a step limit). The bounds are derived from argc (1 when run without
// arguments). MSIL_EXPECT returns 1 + (line % 255) at the first failed check;
// when every check passes, main returns a checksum of the results with all
// four bytes folded into the low 8 bits (D37). That status is not
// 1 + (line % 255) for any MSIL_EXPECT line of this file, so a failed check
// cannot pass for it (re-check after edits that move lines).
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

static NOINLINE int steps(unsigned long long n) {
  int k = 0;
  while (n != 1) {
    if (n & 1)
      n = 3 * n + 1;
    else
      n >>= 1;
    k++;
  }
  return k;
}

static NOINLINE unsigned long long peak(unsigned long long n) {
  unsigned long long p = n;
  while (n != 1) {
    n = (n & 1) ? 3 * n + 1 : n / 2;
    if (n > p)
      p = n;
  }
  return p;
}

static NOINLINE int steps_rec(unsigned long long n) {
  if (n == 1)
    return 0;
  return 1 + steps_rec((n & 1) ? 3 * n + 1 : n / 2);
}

// -1 when the chain is longer than cap steps.
static NOINLINE int steps_capped(unsigned n, int cap) {
  int k = 0;
  while (n != 1) {
    if (k == cap)
      return -1;
    n = n % 2 ? 3 * n + 1 : n / 2;
    k++;
  }
  return k;
}

int main(int argc, char **argv) {
  (void)argv;
  int one = argc;
  unsigned h = 1;
#define MIX(v) (h = h * 31u + (unsigned)(v))

  MSIL_EXPECT(steps(1ull * one) == 0);
  MSIL_EXPECT(steps(27ull * one) == 111);
  MSIL_EXPECT(steps(837799ull * one) == 524);
  MSIL_EXPECT(peak(27ull * one) == 9232);
  MSIL_EXPECT(peak(77671ull * one) == 1570824736ull);
  MSIL_EXPECT(steps_rec(97ull * one) == 118);
  MSIL_EXPECT(steps_capped(27u * one, 50 * one) == -1);
  MSIL_EXPECT(steps_capped(27u * one, 111 * one) == 111);
  MSIL_EXPECT(steps_capped(27u * one, 110 * one) == -1);

  // The longest chain below 1000 and the sum of all chain lengths below 3000.
  int best = 0;
  unsigned long long best_n = 0;
  for (unsigned long long n = 1; n < 1000ull * one; n++) {
    int s = steps(n);
    if (s > best) {
      best = s;
      best_n = n;
    }
  }
  MSIL_EXPECT(best == 178);
  MSIL_EXPECT(best_n == 871);
  MIX(best);
  MIX(best_n);

  long long total = 0;
  for (unsigned n = 1; n < 3000u * one; n++)
    total += steps_rec(n);
  MIX(total);
  MIX(total >> 8);

  unsigned long long maxpeak = 0;
  for (unsigned long long n = 1; n <= 10000ull * one; n++) {
    unsigned long long p = peak(n);
    if (p > maxpeak)
      maxpeak = p;
  }
  MSIL_EXPECT(maxpeak == 27114424);
  MIX(maxpeak);

  // Fold all four bytes of h into the low byte before masking: h & 0xff
  // alone depends only on the low byte of each mixed value.
  h ^= h >> 16;
  h ^= h >> 8;
  return (int)(h & 0xff);
}

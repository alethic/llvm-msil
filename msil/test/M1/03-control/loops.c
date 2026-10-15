// Loops from C (Milestone 6): for, while and do-while loops, break
// and continue, nested loops, loops with several exits, early returns from
// inside a loop, and loops over int, unsigned, long long and double values
// with calls in their bodies. The noinline helpers get inputs derived from
// argc (1 when run without arguments), so clang can neither fold them nor
// propagate constants into them. MSIL_EXPECT returns 1 + (line % 255) at the
// first failed check; when every check passes, main returns a checksum of
// the results with all four bytes folded into the low 8 bits (D37). That
// status is not 1 + (line % 255) for any MSIL_EXPECT line of this file, so a
// failed check cannot pass for it (re-check after edits that move lines).
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

static NOINLINE unsigned gcd(unsigned a, unsigned b) {
  while (b != 0) {
    unsigned t = a % b;
    a = b;
    b = t;
  }
  return a;
}

static NOINLINE int bit_count(unsigned long long v) {
  int n = 0;
  while (v) {
    v &= v - 1;
    n++;
  }
  return n;
}

// Binary search for floor(sqrt(n)); mid * mid cannot overflow (mid < 2^16).
static NOINLINE unsigned isqrt(unsigned n) {
  unsigned lo = 0, hi = 65536;
  while (hi - lo > 1) {
    unsigned mid = lo + (hi - lo) / 2;
    if (mid * mid <= n)
      lo = mid;
    else
      hi = mid;
  }
  return lo;
}

static NOINLINE int is_prime(int n) {
  if (n < 2)
    return 0;
  for (int d = 2; d * d <= n; d++)
    if (n % d == 0)
      return 0;
  return 1;
}

static NOINLINE int count_primes(int limit) {
  int c = 0;
  for (int i = 0; i < limit; i++)
    c += is_prime(i);
  return c;
}

static NOINLINE unsigned reverse_digits(unsigned v) {
  unsigned r = 0;
  do {
    r = r * 10 + v % 10;
    v /= 10;
  } while (v != 0);
  return r;
}

static NOINLINE unsigned long long powmod(unsigned long long b, unsigned e,
                                          unsigned long long m) {
  unsigned long long r = 1;
  b %= m;
  while (e) {
    if (e & 1)
      r = r * b % m;
    b = b * b % m;
    e >>= 1;
  }
  return r;
}

// Sum of i in [0, n) that are divisible by neither 3 nor 5 (continue).
static NOINLINE int sum_skip(int n) {
  int s = 0;
  for (int i = 0; i < n; i++) {
    if (i % 3 == 0)
      continue;
    if (i % 5 == 0)
      continue;
    s += i;
  }
  return s;
}

// The first i with i * i * i > limit (break).
static NOINLINE int first_cube_above(int limit) {
  int i = 0;
  for (;;) {
    if (i * i * i > limit)
      break;
    i++;
  }
  return i;
}

// Pythagorean triples a < b < c <= n (three nested loops with continue).
static NOINLINE int pythagorean(int n) {
  int count = 0;
  for (int a = 1; a <= n; a++)
    for (int b = a + 1; b <= n; b++) {
      int s = a * a + b * b;
      if (s > n * n)
        break;
      for (int c = b + 1; c <= n; c++) {
        if (c * c < s)
          continue;
        if (c * c == s)
          count++;
        break;
      }
    }
  return count;
}

// The first pair a < b with a * b == target, as a * 1000 + b; -1 if none.
static NOINLINE int factor_pair(int target, int limit) {
  for (int a = 2; a < limit; a++)
    for (int b = a + 1; b < limit; b++)
      if (a * b == target)
        return a * 1000 + b;
  return -1;
}

static NOINLINE double quarter_sum(int n) {
  double s = 0;
  for (int k = 0; k < n; k++)
    s += k * 0.25;
  return s;
}

// Counts down an unsigned value with the 'i-- > 0' idiom.
static NOINLINE unsigned countdown_xor(unsigned n) {
  unsigned x = 0;
  for (unsigned i = n; i-- > 0;)
    x ^= i * 2654435761u;
  return x;
}

static NOINLINE long long step64(long long v, int i) { return v * 3 + i; }

// A long long accumulator updated through a call in a while loop with two
// exits: the value passes 2^40, or the count reaches the limit.
static NOINLINE int grow(int limit) {
  long long v = 1;
  int i = 0;
  while (i < limit) {
    v = step64(v, i);
    i++;
    if (v > (1LL << 40))
      return 1000 + i;
  }
  return i;
}

int main(int argc, char **argv) {
  (void)argv;
  int one = argc;
  unsigned h = 0;
#define MIX(v) (h = h * 31u + (unsigned)(v))

  unsigned g1 = gcd(1071u * one, 462u * one);
  MSIL_EXPECT(g1 == 21);
  unsigned g2 = gcd(17u * one, 0u * one);
  MSIL_EXPECT(g2 == 17);
  MIX(g1);
  MIX(g2);

  int bc = bit_count(0xF0F0F0F0F0F0F0F0ull * one);
  MSIL_EXPECT(bc == 32);
  MSIL_EXPECT(bit_count(0ull * one) == 0);
  MIX(bc);

  unsigned r1 = isqrt(99999999u * one);
  MSIL_EXPECT(r1 == 9999);
  unsigned r2 = isqrt(0xFFFFFFFFu * one);
  MSIL_EXPECT(r2 == 65535);
  MIX(r1);
  MIX(r2);

  int primes = count_primes(200 * one);
  MSIL_EXPECT(primes == 46);
  MIX(primes);

  unsigned rev = reverse_digits(123456789u * one);
  MSIL_EXPECT(rev == 987654321u);
  MSIL_EXPECT(reverse_digits(0u * one) == 0);
  MIX(rev);

  unsigned long long pm = powmod(3ull * one, 200u * one, 1000000007ull * one);
  MSIL_EXPECT(pm == 136318165ull);
  MIX(pm);
  MIX(pm >> 32);

  int sk = sum_skip(100 * one);
  MSIL_EXPECT(sk == 2632);
  MIX(sk);

  int cube = first_cube_above(100000 * one);
  MSIL_EXPECT(cube == 47);
  MIX(cube);

  int py = pythagorean(50 * one);
  MSIL_EXPECT(py == 20);
  MIX(py);

  int fp1 = factor_pair(391 * one, 100 * one);
  MSIL_EXPECT(fp1 == 17023);
  int fp2 = factor_pair(97 * one, 100 * one);
  MSIL_EXPECT(fp2 == -1);
  MIX(fp1);
  MIX(fp2);

  double qs = quarter_sum(100 * one);
  MSIL_EXPECT(qs == 1237.5);
  MIX((int)qs);

  unsigned cx = countdown_xor(1000u * one);
  MIX(cx);
  MSIL_EXPECT(countdown_xor(0u * one) == 0);

  int gr1 = grow(10 * one);
  MSIL_EXPECT(gr1 == 10);
  int gr2 = grow(100 * one);
  MSIL_EXPECT(gr2 == 1026);
  MIX(gr1);
  MIX(gr2);

  // Fold all four bytes of h into the low byte before masking: h & 0xff
  // alone depends only on the low byte of each mixed value.
  h ^= h >> 16;
  h ^= h >> 8;
  return (int)(h & 0xff);
}

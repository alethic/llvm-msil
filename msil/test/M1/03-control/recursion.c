// Recursion from C (Milestone 6): doubly recursive Fibonacci, mutual
// recursion (is_even/is_odd, 1001 frames deep), binomial coefficients through
// Pascal's rule, the Takeuchi function and McCarthy's 91 function (recursive
// calls as arguments of recursive calls), Towers of Hanoi, recursive GCD and
// exponentiation by squaring on unsigned long long, a recursive float64 sum,
// and a non-tail recursion 3000 frames deep. clang -O2 turns some of these
// into loops (tail calls, accumulators) and keeps the others as calls. The
// noinline functions get inputs derived from argc (1 when run without
// arguments). MSIL_EXPECT returns 1 + (line % 255) at the first failed check;
// when every check passes, main returns a checksum of the results with all
// four bytes folded into the low 8 bits (D37). That status is not
// 1 + (line % 255) for any MSIL_EXPECT line of this file, so a failed check
// cannot pass for it (re-check after edits that move lines).
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

static NOINLINE int fib(int n) {
  if (n < 2)
    return n;
  return fib(n - 1) + fib(n - 2);
}

static NOINLINE int is_odd(unsigned n);

static NOINLINE int is_even(unsigned n) {
  if (n == 0)
    return 1;
  return is_odd(n - 1);
}

static NOINLINE int is_odd(unsigned n) {
  if (n == 0)
    return 0;
  return is_even(n - 1);
}

static NOINLINE int binom(int n, int k) {
  if (k == 0 || k == n)
    return 1;
  return binom(n - 1, k - 1) + binom(n - 1, k);
}

static NOINLINE int tak(int x, int y, int z) {
  if (y < x)
    return tak(tak(x - 1, y, z), tak(y - 1, z, x), tak(z - 1, x, y));
  return z;
}

static NOINLINE int m91(int n) {
  if (n > 100)
    return n - 10;
  return m91(m91(n + 11));
}

// Towers of Hanoi: each move contributes 1 plus its disk and pegs shifted
// left by 20, and the second recursive call is weighted by 3, so a lost
// call, swapped pegs or swapped calls change the result.
static NOINLINE unsigned long long hanoi(int n, int from, int to, int via) {
  if (n == 0)
    return 0;
  unsigned long long a = hanoi(n - 1, from, via, to);
  unsigned long long move =
      1 + ((unsigned long long)(n * 9 + from * 3 + to) << 20);
  unsigned long long b = hanoi(n - 1, via, to, from);
  return a + move + b * 3;
}

static NOINLINE unsigned gcd_rec(unsigned a, unsigned b) {
  if (b == 0)
    return a;
  return gcd_rec(b, a % b);
}

static NOINLINE unsigned long long pow_rec(unsigned long long b, unsigned e) {
  if (e == 0)
    return 1;
  unsigned long long half = pow_rec(b, e / 2);
  unsigned long long r = half * half;
  if (e & 1)
    r *= b;
  return r;
}

static NOINLINE double sum_halves(int n) {
  if (n == 0)
    return 0.0;
  return n * 0.5 + sum_halves(n - 1);
}

// Not a tail call and not an accumulator clang can turn into a loop: every
// level needs its own frame.
static NOINLINE unsigned long long deep(unsigned n) {
  if (n == 0)
    return 7;
  return deep(n - 1) * 3 + n;
}

int main(int argc, char **argv) {
  (void)argv;
  int one = argc;
  unsigned h = 0;
#define MIX(v) (h = h * 31u + (unsigned)(v))

  int f20 = fib(20 * one);
  MSIL_EXPECT(f20 == 6765);
  MSIL_EXPECT(fib(0 * one) == 0);
  MSIL_EXPECT(fib(1 * one) == 1);
  MIX(f20);

  MSIL_EXPECT(is_even(1000u * one) == 1);
  MSIL_EXPECT(is_odd(1001u * one) == 1);
  MSIL_EXPECT(is_even(1001u * one) == 0);
  MIX(is_even(777u * one));

  int b = binom(16 * one, 8 * one);
  MSIL_EXPECT(b == 12870);
  MIX(b);
  MIX(binom(20 * one, 3 * one));

  int t = tak(18 * one, 12 * one, 6 * one);
  MSIL_EXPECT(t == 7);
  MIX(t);
  MIX(tak(12 * one, 8 * one, 4 * one));

  MSIL_EXPECT(m91(1 * one) == 91);
  MSIL_EXPECT(m91(100 * one) == 91);
  MSIL_EXPECT(m91(150 * one) == 140);
  MIX(m91(42 * one));

  unsigned long long hn = hanoi(10 * one, 1, 3, 2);
  MSIL_EXPECT(hn == 7324165297493ull);
  MIX(hn);
  MIX(hn >> 32);

  MSIL_EXPECT(gcd_rec(1071u * one, 462u * one) == 21);
  MIX(gcd_rec(832040u * one, 514229u * one));

  unsigned long long p = pow_rec(3ull * one, 40u * one);
  MSIL_EXPECT(p == 12157665459056928801ull);
  MIX(p);
  MIX(pow_rec(7ull * one, 100u * one) >> 40);

  double s = sum_halves(100 * one);
  MSIL_EXPECT(s == 2525.0);
  MIX((int)s);

  unsigned long long d = deep(3000u * one);
  MIX(d);
  MIX(d >> 32);
  MSIL_EXPECT(deep(0u * one) == 7);
  MSIL_EXPECT(deep(2u * one) == 7 * 9 + 1 * 3 + 2);

  // Fold all four bytes of h into the low byte before masking: h & 0xff
  // alone depends only on the low byte of each mixed value.
  h ^= h >> 16;
  h ^= h >> 8;
  return (int)(h & 0xff);
}

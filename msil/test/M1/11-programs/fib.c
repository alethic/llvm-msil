// Fibonacci numbers four ways (Design.md section 12, "fib.c runs on
// dotnet"): naive recursion, a loop in unsigned long long, a memo table in
// global arrays filled on demand, and fast doubling (F(2k) = F(k)(2F(k+1) -
// F(k)), F(2k+1) = F(k)^2 + F(k+1)^2, through an address-taken local), each
// checked against the others up to F(92), the largest that fits in 63 bits.
// Prints F(0..24) from the recursion, every tenth value up to F(90), the
// Pisano periods of F mod m for m = 2..12, and the Zeckendorf
// representations (sums of non-consecutive Fibonacci numbers) of a few
// integers. The recursion bound is read from a volatile global, so -O2 keeps
// the calls. MSIL_EXPECT returns 1 + (line % 255) at the first failed check;
// on success the exit status is F(13) = 233, which is not 1 + (line % 255)
// for any MSIL_EXPECT line of this file.
// RUN: %{msil_c}
#include "msil_test.h"

volatile int limit = 25;

static int fib_rec(int n) {
  return n < 2 ? n : fib_rec(n - 1) + fib_rec(n - 2);
}

static unsigned long long fib_iter(int n) {
  unsigned long long a = 0, b = 1;
  for (int i = 0; i < n; i++) {
    unsigned long long t = a + b;
    a = b;
    b = t;
  }
  return a;
}

static unsigned long long memo[93];
static unsigned char known[93];

static unsigned long long fib_memo(int n) {
  if (n < 2)
    return (unsigned long long)n;
  if (!known[n]) {
    memo[n] = fib_memo(n - 1) + fib_memo(n - 2);
    known[n] = 1;
  }
  return memo[n];
}

// Returns F(n) and stores F(n + 1) in *next (exact for n <= 92).
static unsigned long long fib_doubling(int n, unsigned long long *next) {
  if (n == 0) {
    *next = 1;
    return 0;
  }
  unsigned long long b;
  unsigned long long a = fib_doubling(n / 2, &b); // F(k), F(k + 1)
  unsigned long long even = a * (2 * b - a);       // F(2k)
  unsigned long long odd = a * a + b * b;          // F(2k + 1)
  if (n % 2 == 0) {
    *next = odd;
    return even;
  }
  *next = even + odd;
  return odd;
}

// The period of F(n) mod m (at most 6m).
static int pisano(int m) {
  int a = 0, b = 1;
  for (int p = 1; p <= 6 * m; p++) {
    int t = (a + b) % m;
    a = b;
    b = t;
    if (a == 0 && b == 1)
      return p;
  }
  return 0;
}

static char line[256];
static int len;

static void add_str(const char *s) {
  while (*s && len < 250)
    line[len++] = *s++;
}

static void add_u64(unsigned long long v) {
  char buf[24];
  buf[23] = 0;
  add_str(msil_fmt_u64(v, buf + 23));
}

static void flush(void) {
  line[len] = 0;
  puts(line);
  len = 0;
}

// Prints n = F(i) + F(j) + ... greedily (largest first, which never picks
// two consecutive Fibonacci numbers) and returns the number of terms.
static int zeckendorf(unsigned long long n) {
  int terms = 0;
  add_u64(n);
  add_str(" =");
  for (int k = 91; k >= 2 && n; k--) {
    unsigned long long f = fib_memo(k);
    if (f <= n) {
      n -= f;
      add_str(terms ? " + " : " ");
      add_u64(f);
      terms++;
      k--; // the next term is at most F(k - 2)
    }
  }
  flush();
  return n == 0 ? terms : -1;
}

int main(void) {
  int n = limit;
  MSIL_EXPECT(n >= 14 && n <= 30);
  int rec[30];
  put_str("recursive");
  for (int i = 0; i < n; i++) {
    rec[i] = fib_rec(i);
    put_u64((unsigned)rec[i]);
  }

  for (int i = 0; i <= 92; i++) {
    unsigned long long next;
    unsigned long long f = fib_iter(i);
    MSIL_EXPECT(fib_memo(i) == f);
    MSIL_EXPECT(fib_doubling(i, &next) == f);
    MSIL_EXPECT(next == fib_iter(i + 1));
    MSIL_EXPECT(i >= n || f == (unsigned)rec[i]);
  }
  MSIL_EXPECT(fib_iter(92) == 7540113804746346429ull);

  put_str("every tenth");
  for (int i = 0; i <= 90; i += 10) {
    add_str("F(");
    add_u64((unsigned)i);
    add_str(") = ");
    add_u64(fib_memo(i));
    flush();
  }

  put_str("pisano periods");
  for (int m = 2; m <= 12; m++) {
    add_str("mod ");
    add_u64((unsigned)m);
    add_str(": ");
    add_u64((unsigned)pisano(m));
    flush();
  }
  MSIL_EXPECT(pisano(10) == 60);

  put_str("zeckendorf");
  MSIL_EXPECT(zeckendorf(100) == 3);
  MSIL_EXPECT(zeckendorf(1000) == 2);
  MSIL_EXPECT(zeckendorf(12345) > 0);
  MSIL_EXPECT(zeckendorf(1000000) > 0);
  MSIL_EXPECT(zeckendorf(fib_memo(40) - 1) == 19);

  unsigned long long next;
  return (int)(fib_doubling(n - 12, &next) & 0xff);
}

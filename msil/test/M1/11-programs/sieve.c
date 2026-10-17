// The sieve of Eratosthenes over global arrays (Design.md section 12): one
// byte per number in zero-initialized global memory (BSS) up to 20000,
// cross-checked against a bit-packed sieve of the odd numbers in a global
// uint32 array (shifts, masks and word indexes) and against trial division
// below 500. Prints the primes below 100, the prime count of each block of
// 2000, pi(10^k) for k = 1..4, the number of twin-prime pairs, the largest
// prime gap and where it starts, the largest prime and the sum of the
// primes. The bound is read from a volatile global, so -O2 keeps every
// loop. MSIL_EXPECT returns 1 + (line % 255) at the first failed check; on
// success the exit status is pi(20000) = 2262 modulo 256 (214), which is
// not 1 + (line % 255) for any MSIL_EXPECT line of this file.
// RUN: %{msil_c}
#include "msil_test.h"

#define MAX 20000

volatile int bound = MAX;
static unsigned char composite[MAX + 1];
static unsigned odd_bits[MAX / 64 + 1]; // bit i: 2i + 1 is composite

static void sieve_bytes(int n) {
  composite[0] = composite[1] = 1;
  for (int i = 2; i * i <= n; i++)
    if (!composite[i])
      for (int j = i * i; j <= n; j += i)
        composite[j] = 1;
}

static int bit_get(int i) { return (int)(odd_bits[i >> 5] >> (i & 31)) & 1; }
static void bit_set(int i) { odd_bits[i >> 5] |= 1u << (i & 31); }

static void sieve_bits(int n) {
  bit_set(0); // 1 is not a prime
  for (int i = 1; (2 * i + 1) * (2 * i + 1) <= n; i++)
    if (!bit_get(i)) {
      int p = 2 * i + 1;
      for (int j = p * p; j <= n; j += 2 * p)
        bit_set(j / 2);
    }
}

static int is_prime_bits(int k) {
  if (k < 2)
    return 0;
  if (k % 2 == 0)
    return k == 2;
  return !bit_get(k / 2);
}

static int is_prime_trial(int k) {
  if (k < 2)
    return 0;
  for (int d = 2; d * d <= k; d++)
    if (k % d == 0)
      return 0;
  return 1;
}

static int count_range(int lo, int hi) { // primes in [lo, hi)
  int c = 0;
  for (int k = lo; k < hi; k++)
    c += !composite[k];
  return c;
}

static char line[400];
static int len;

static void add_str(const char *s) {
  while (*s && len < 390)
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

int main(void) {
  int n = bound;
  MSIL_EXPECT(n >= 10000 && n <= MAX);
  sieve_bytes(n);
  sieve_bits(n);

  int count = 0, twins = 0, gap = 0, gap_at = 0, last = 0;
  unsigned long long sum = 0;
  for (int k = 0; k <= n; k++) {
    int prime = !composite[k];
    MSIL_EXPECT(prime == is_prime_bits(k));
    MSIL_EXPECT(k >= 500 || prime == is_prime_trial(k));
    if (!prime)
      continue;
    count++;
    sum += (unsigned)k;
    if (last && k - last == 2)
      twins++;
    if (last && k - last > gap) {
      gap = k - last;
      gap_at = last;
    }
    last = k;
  }

  put_str("primes below 100");
  for (int k = 2; k < 100; k++)
    if (!composite[k]) {
      if (len)
        add_str(" ");
      add_u64((unsigned)k);
    }
  flush();

  put_str("primes per block of 2000");
  int total = 0;
  for (int lo = 0; lo + 2000 <= n; lo += 2000) {
    int c = count_range(lo, lo + 2000);
    total += c;
    add_u64((unsigned)lo);
    add_str("..");
    add_u64((unsigned)(lo + 1999));
    add_str(": ");
    add_u64((unsigned)c);
    flush();
  }
  MSIL_EXPECT(n % 2000 != 0 || total == count);

  put_str("pi(10^k)");
  for (int lim = 10; lim <= n; lim *= 10) {
    add_str("pi(");
    add_u64((unsigned)lim);
    add_str(") = ");
    add_u64((unsigned)count_range(0, lim + 1));
    flush();
  }
  MSIL_EXPECT(count_range(0, 101) == 25 && count_range(0, 10001) == 1229);

  put_kv("pi", count);
  put_kv("twin prime pairs", twins);
  put_kv("largest gap", gap);
  put_kv("largest gap after", gap_at);
  put_kv("largest prime", last);
  put_kv("sum", (long long)sum);
  return count & 0xff;
}

// A small number-theory and 128-bit arithmetic workload with the __int128 division idioms that real
// C code uses: (u128)a*b % m (mulmod), Miller-Rabin and Pollard rho on u64, u128 <-> decimal text,
// exact rationals over i128 (gcd, normalization), nanosecond timestamps split by 1e9, isqrt by
// Newton steps, binomials, and a 64.64 fixed-point divide. Prints checksums; argv[1] = repeat count.
#include <stdint.h>
int puts(const char *);
typedef unsigned __int128 u128;
typedef __int128 i128;

static char line[256];
static int ll;
static void s(const char *t) { while (*t) line[ll++] = *t++; }
static void hx(uint64_t v) {
  s(" ");
  for (int i = 15; i >= 0; --i) line[ll++] = "0123456789abcdef"[(v >> (4 * i)) & 15];
}
static void nl(void) { line[ll] = 0; puts(line); ll = 0; }

// ---- modular arithmetic on u64 through u128
static uint64_t mulmod(uint64_t a, uint64_t b, uint64_t m) { return (uint64_t)((u128)a * b % m); }
static uint64_t powmod(uint64_t b, uint64_t e, uint64_t m) {
  uint64_t r = 1 % m;
  b %= m;
  while (e) {
    if (e & 1) r = mulmod(r, b, m);
    b = mulmod(b, b, m);
    e >>= 1;
  }
  return r;
}
static int is_prime(uint64_t n) {
  if (n < 2) return 0;
  static const uint64_t small[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37};
  for (int i = 0; i < 12; ++i) {
    if (n % small[i] == 0) return n == small[i];
  }
  uint64_t d = n - 1;
  int r = 0;
  while ((d & 1) == 0) { d >>= 1; ++r; }
  for (int i = 0; i < 12; ++i) {
    uint64_t x = powmod(small[i], d, n);
    if (x == 1 || x == n - 1) continue;
    int comp = 1;
    for (int j = 1; j < r && comp; ++j) {
      x = mulmod(x, x, n);
      if (x == n - 1) comp = 0;
    }
    if (comp) return 0;
  }
  return 1;
}
static uint64_t gcd64(uint64_t a, uint64_t b) { while (b) { uint64_t t = a % b; a = b; b = t; } return a; }
static uint64_t rho(uint64_t n, uint64_t c) {
  uint64_t x = 2, y = 2, d = 1;
  while (d == 1) {
    x = (mulmod(x, x, n) + c) % n;
    y = (mulmod(y, y, n) + c) % n;
    y = (mulmod(y, y, n) + c) % n;
    d = gcd64(x > y ? x - y : y - x, n);
  }
  return d;
}

// ---- u128 <-> decimal
static int u128_to_dec(u128 v, char *out) {
  char t[48];
  int k = 0;
  do { t[k++] = (char)('0' + (int)(v % 10)); v /= 10; } while (v);
  for (int i = 0; i < k; ++i) out[i] = t[k - 1 - i];
  out[k] = 0;
  return k;
}
static u128 dec_to_u128(const char *p) {
  u128 v = 0;
  while (*p) v = v * 10 + (u128)(*p++ - '0');
  return v;
}
static int i128_to_dec(i128 v, char *out) {
  if (v < 0) { out[0] = '-'; return 1 + u128_to_dec(-(u128)v, out + 1); }
  return u128_to_dec((u128)v, out);
}

// ---- exact rationals over i128
typedef struct { i128 n, d; } rat;
static i128 gcd128(i128 a, i128 b) {
  if (a < 0) a = -a;
  if (b < 0) b = -b;
  while (b) { i128 t = a % b; a = b; b = t; }
  return a;
}
static rat rnorm(i128 n, i128 d) {
  i128 g = gcd128(n, d);
  if (g == 0) g = 1;
  rat r = {n / g, d / g};
  if (r.d < 0) { r.n = -r.n; r.d = -r.d; }
  return r;
}
static rat radd(rat a, rat b) {
  i128 g = gcd128(a.d, b.d);
  return rnorm(a.n * (b.d / g) + b.n * (a.d / g), a.d / g * b.d);
}
static rat rmul(rat a, rat b) {
  i128 g1 = gcd128(a.n, b.d), g2 = gcd128(b.n, a.d);
  if (!g1) g1 = 1;
  if (!g2) g2 = 1;
  return rnorm((a.n / g1) * (b.n / g2), (a.d / g2) * (b.d / g1));
}

// ---- timestamps, isqrt, binomials, fixed point
typedef struct { int64_t sec; int32_t nsec; } ts;
static ts ns_split(i128 ns) {
  ts t;
  t.sec = (int64_t)(ns / 1000000000);
  t.nsec = (int32_t)(ns % 1000000000);
  if (t.nsec < 0) { t.nsec += 1000000000; t.sec -= 1; }
  return t;
}
static i128 ms_of(i128 ns) { return ns / 1000000; }
static u128 isqrt128(u128 n) {
  if (n < 2) return n;
  u128 x = n, y = x / 2 + 1;
  while (y < x) { x = y; y = (x + n / x) / 2; }
  return x;
}
static u128 binom(unsigned n, unsigned k) {
  u128 r = 1;
  if (k > n - k) k = n - k;
  for (unsigned i = 1; i <= k; ++i) r = r * (n - k + i) / i;
  return r;
}
static u128 fixdiv(u128 a, u128 b) { return (a << 32) / b; }  // 96.32 / 96.32 fixed point
static i128 sdiv_round(i128 a, i128 b) {                       // round half away from zero
  i128 q = a / b, r = a % b;
  if ((r < 0 ? -r : r) * 2 >= (b < 0 ? -b : b)) q += ((a < 0) != (b < 0)) ? -1 : 1;
  return q;
}

static uint64_t H = 0xcbf29ce484222325ull;
static void hadd(uint64_t v) { H = (H ^ v) * 0x100000001b3ull; H ^= H >> 29; }
static void hadd128(u128 v) { hadd((uint64_t)v); hadd((uint64_t)(v >> 64)); }

static void work(void) {
  // primes and factors
  uint64_t seed = 0x123456789abcdefull;
  int primes = 0;
  for (int i = 0; i < 400; ++i) {
    seed = seed * 6364136223846793005ull + 1442695040888963407ull;
    uint64_t n = (seed >> 3) | 1;
    if (is_prime(n)) ++primes;
    uint64_t a = 1000003 + 2 * (uint64_t)i, b = 999999937;
    uint64_t semi = a * b;
    uint64_t f = rho(semi, 1);
    hadd(f < semi ? f : 0);
  }
  hadd((uint64_t)primes);
  // decimal round trips
  char buf[64];
  u128 v = 1;
  for (int i = 0; i < 2000; ++i) {
    v = v * 0x9E3779B97F4A7C15ull + (u128)i;
    int k = u128_to_dec(v, buf);
    u128 w = dec_to_u128(buf);
    hadd128(w ^ v);
    hadd((uint64_t)k);
    k = i128_to_dec((i128)v, buf);
    hadd((uint64_t)k);
  }
  // rationals: harmonic numbers H(1..60)
  rat h = {0, 1};
  for (int i = 1; i <= 60; ++i) {
    rat t = {1, i};
    h = radd(h, t);
    rat sq = rmul(t, t);
    hadd128((u128)sq.d);
  }
  hadd128((u128)h.n);
  hadd128((u128)h.d);
  // timestamps, isqrt, binomials, fixed point, rounding division
  i128 ns = (i128)1700000000 * 1000000000 + 123456789;
  for (int i = 0; i < 3000; ++i) {
    ts t = ns_split(ns - (i128)i * 987654321987);
    hadd((uint64_t)t.sec);
    hadd((uint64_t)t.nsec);
    hadd128((u128)ms_of(ns + i));
    hadd128(isqrt128(((u128)ns << 20) + (u128)i));
    hadd128(fixdiv((u128)ns + (u128)i, (u128)(i + 7)));
    hadd128((u128)sdiv_round(-ns + i, 1000 + i));
  }
  for (unsigned n = 1; n < 120; ++n) hadd128(binom(n, n / 2));
}

int main(int argc, char **argv) {
  int reps = 1;
  if (argc > 1) { reps = 0; for (const char *p = argv[1]; *p; ++p) reps = reps * 10 + (*p - '0'); }
  for (int r = 0; r < reps; ++r) work();
  char buf[64];
  u128_to_dec(isqrt128((u128)-1), buf);
  s("isqrt(2^128-1) = "); s(buf); nl();
  u128_to_dec(binom(127, 63), buf);
  s("C(127,63) = "); s(buf); nl();
  s("hash"); hx(H); nl();
  return 0;
}

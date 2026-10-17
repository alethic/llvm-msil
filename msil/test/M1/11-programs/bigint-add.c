// Arbitrary-precision unsigned integers as arrays of 32-bit limbs
// (Milestone 10 program corpus, Design.md section 12): addition with the
// carry kept in 64-bit arithmetic and, independently, in 32-bit arithmetic
// with carries detected by unsigned compares; subtraction with borrow;
// comparison; multiplication by a small number both directly and by repeated
// addition; division by a small number (64-bit div.un/rem.un) for decimal
// output. Programs: Fibonacci numbers up to F(302) with the identity
// F(0) + ... + F(n) = F(n + 2) - 1, powers of two by doubling, a carry
// chain through sixteen all-ones limbs and back, the carry out of the full
// width, and factorials up to 100!. A 100-byte struct is passed by value to
// a function that divides its own copy down to zero (byval on SysV, a
// pointer to a caller-made copy on Win64; the caller's value must not
// change). Numbers are printed in decimal; MSIL_EXPECT returns
// 1 + (line % 255) at the first failed check; when every check passes, the
// exit status is the sum of all printed digits modulo 256 (D37).
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

typedef unsigned u32;
typedef unsigned long long u64;

#define LIMBS 24
// Limbs at index n and above are always zero.
struct big {
  int n;
  u32 d[LIMBS];
};

volatile int zero = 0;
static unsigned digit_sum;

static void big_norm(struct big *a) {
  while (a->n > 0 && a->d[a->n - 1] == 0)
    a->n--;
}

static NOINLINE void big_set(struct big *a, u64 v) {
  for (int i = 0; i < LIMBS; i++)
    a->d[i] = 0;
  a->n = 0;
  while (v) {
    a->d[a->n++] = (u32)v;
    v >>= 32;
  }
}

// r = a + b; r may alias a or b. Returns the carry out of LIMBS limbs.
static NOINLINE int big_add(struct big *r, const struct big *a,
                            const struct big *b) {
  int n = a->n > b->n ? a->n : b->n;
  u64 carry = 0;
  for (int i = 0; i < n; i++) {
    u64 s = (u64)a->d[i] + b->d[i] + carry;
    r->d[i] = (u32)s;
    carry = s >> 32;
  }
  for (int i = n; i < LIMBS; i++)
    r->d[i] = 0;
  if (carry && n < LIMBS) {
    r->d[n++] = (u32)carry;
    carry = 0;
  }
  r->n = n;
  big_norm(r);
  return (int)carry;
}

// The same sum in 32-bit arithmetic over every limb.
static NOINLINE int big_add32(struct big *r, const struct big *a,
                              const struct big *b) {
  u32 carry = 0;
  for (int i = 0; i < LIMBS; i++) {
    u32 x = a->d[i];
    u32 s = x + b->d[i];
    u32 c1 = s < x;
    s += carry;
    u32 c2 = s < carry;
    r->d[i] = s;
    carry = c1 | c2;
  }
  r->n = LIMBS;
  big_norm(r);
  return (int)carry;
}

// r = a - b for a >= b; r may alias a or b.
static NOINLINE void big_sub(struct big *r, const struct big *a,
                             const struct big *b) {
  u64 borrow = 0;
  for (int i = 0; i < LIMBS; i++) {
    u64 t = (u64)a->d[i] - b->d[i] - borrow;
    r->d[i] = (u32)t;
    borrow = t >> 63;
  }
  r->n = LIMBS;
  big_norm(r);
}

static NOINLINE int big_cmp(const struct big *a, const struct big *b) {
  if (a->n != b->n)
    return a->n < b->n ? -1 : 1;
  for (int i = a->n - 1; i >= 0; i--)
    if (a->d[i] != b->d[i])
      return a->d[i] < b->d[i] ? -1 : 1;
  return 0;
}

static NOINLINE void big_mul_small(struct big *a, u32 m) {
  u64 carry = 0;
  for (int i = 0; i < a->n; i++) {
    u64 p = (u64)a->d[i] * m + carry;
    a->d[i] = (u32)p;
    carry = p >> 32;
  }
  if (carry)
    a->d[a->n++] = (u32)carry;
  if (m == 0)
    big_set(a, 0);
}

// r = a * m as m additions; r may alias a.
static NOINLINE void big_mul_by_adding(struct big *r, const struct big *a,
                                       u32 m) {
  struct big acc;
  big_set(&acc, 0);
  for (u32 i = 0; i < m; i++)
    big_add(&acc, &acc, a);
  *r = acc;
}

// a /= m, returns a % m.
static NOINLINE u32 big_divmod_small(struct big *a, u32 m) {
  u64 rem = 0;
  for (int i = a->n - 1; i >= 0; i--) {
    u64 cur = rem << 32 | a->d[i];
    a->d[i] = (u32)(cur / m);
    rem = cur % m;
  }
  big_norm(a);
  return (u32)rem;
}

// Decimal digits of a, written into out; every digit goes into digit_sum.
static NOINLINE char *big_to_dec(const struct big *a, char *out) {
  struct big t = *a;
  u32 chunk[LIMBS * 2];
  int nchunks = 0;
  do
    chunk[nchunks++] = big_divmod_small(&t, 1000000000u);
  while (t.n);
  char *o = out;
  for (int c = nchunks - 1; c >= 0; c--) {
    char tmp[9];
    u32 v = chunk[c];
    for (int k = 8; k >= 0; k--) {
      tmp[k] = (char)('0' + v % 10);
      v /= 10;
    }
    int k = 0;
    if (c == nchunks - 1)
      while (k < 8 && tmp[k] == '0')
        k++;
    for (; k < 9; k++)
      *o++ = tmp[k];
  }
  *o = 0;
  for (char *p = out; *p; p++)
    digit_sum += (unsigned)(*p - '0');
  return out;
}

// Counts decimal digits by dividing its own copy down to zero.
static NOINLINE int big_digits(struct big a) {
  int n = 0;
  do {
    big_divmod_small(&a, 10);
    n++;
  } while (a.n);
  return n;
}

static int str_eq(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a == *b;
}

static char text[400];
static char line[480];

static void show(const char *label, const struct big *a) {
  char *o = line;
  while (*label)
    *o++ = *label++;
  *o++ = ' ';
  big_to_dec(a, text);
  for (const char *p = text; *p; p++)
    *o++ = *p;
  *o = 0;
  put_str(line);
}

static struct big fib[3], fib32[3], sum, tmp;

int main(void) {
  // Fibonacci: fib[i % 3] holds F(i); sum holds F(0) + ... + F(i).
  big_set(&fib[0], 0);
  big_set(&fib[1], 1 + (u64)zero);
  fib32[0] = fib[0];
  fib32[1] = fib[1];
  big_set(&sum, 1);
  for (int i = 2; i <= 302; i++) {
    struct big *f = &fib[i % 3];
    MSIL_EXPECT(big_add(f, &fib[(i + 1) % 3], &fib[(i + 2) % 3]) == 0);
    MSIL_EXPECT(big_add32(&fib32[i % 3], &fib32[(i + 1) % 3],
                          &fib32[(i + 2) % 3]) == 0);
    MSIL_EXPECT(big_cmp(f, &fib32[i % 3]) == 0);
    if (i == 10 || i == 50 || i == 93 || i == 94 || i == 100 || i == 200 ||
        i == 300) {
      char label[12] = "F(";
      label[2] = (char)('0' + i / 100);
      label[3] = (char)('0' + i / 10 % 10);
      label[4] = (char)('0' + i % 10);
      label[5] = ')';
      label[6] = 0;
      show(label, f);
    }
    if (i == 93)
      MSIL_EXPECT(f->n == 2 && ((u64)f->d[1] << 32 | f->d[0]) ==
                                   12200160415121876738ull);
    if (i == 94)
      MSIL_EXPECT(f->n == 3);
    if (i == 100)
      MSIL_EXPECT(str_eq(text, "354224848179261915075"));
    if (i <= 300)
      big_add(&sum, &sum, f);
  }
  // sum = F(0) + ... + F(300) = F(302) - 1.
  big_set(&tmp, 1);
  big_add(&tmp, &tmp, &sum);
  MSIL_EXPECT(big_cmp(&tmp, &fib[302 % 3]) == 0);
  show("sum F(0..300)", &sum);
  put_kv("digits of F(302)", big_digits(fib[302 % 3]));
  MSIL_EXPECT(big_cmp(&tmp, &fib[302 % 3]) == 0);

  // Powers of two by doubling.
  struct big p;
  big_set(&p, 1);
  for (int k = 1; k <= 512; k++) {
    big_add(&p, &p, &p);
    if (k == 64 || k == 100 || k == 128 || k == 256 || k == 512) {
      char label[8] = "2^";
      label[2] = (char)('0' + k / 100);
      label[3] = (char)('0' + k / 10 % 10);
      label[4] = (char)('0' + k % 10);
      label[5] = 0;
      show(label, &p);
      if (k == 128)
        MSIL_EXPECT(str_eq(text, "340282366920938463463374607431768211456"));
    }
  }
  MSIL_EXPECT(p.n == 17 && p.d[16] == 1 && p.d[15] == 0 && p.d[0] == 0);

  // A carry through sixteen all-ones limbs, and the borrow back.
  struct big ones, one, back;
  big_set(&ones, 0);
  for (int i = 0; i < 16; i++)
    ones.d[i] = 0xffffffffu + (u32)zero;
  ones.n = 16;
  big_set(&one, 1);
  big_add(&tmp, &ones, &one);
  MSIL_EXPECT(big_cmp(&tmp, &p) == 0);
  big_sub(&back, &tmp, &one);
  MSIL_EXPECT(big_cmp(&back, &ones) == 0);
  show("2^512-1", &back);

  // The carry out of the full width.
  for (int i = 0; i < LIMBS; i++)
    ones.d[i] = 0xffffffffu;
  ones.n = LIMBS;
  MSIL_EXPECT(big_add(&tmp, &ones, &one) == 1 && tmp.n == 0);
  MSIL_EXPECT(big_add32(&tmp, &one, &ones) == 1 && tmp.n == 0);

  // Factorials, directly and by repeated addition.
  struct big fact, fact2;
  big_set(&fact, 1);
  big_set(&fact2, 1);
  for (u32 k = 2; k <= 100; k++) {
    big_mul_small(&fact, k + (u32)zero);
    big_mul_by_adding(&fact2, &fact2, k);
    MSIL_EXPECT(big_cmp(&fact, &fact2) == 0);
    if (k == 20 || k == 25 || k == 50 || k == 100) {
      char label[8];
      label[0] = (char)('0' + k / 100);
      label[1] = (char)('0' + k / 10 % 10);
      label[2] = (char)('0' + k % 10);
      label[3] = '!';
      label[4] = 0;
      show(label, &fact);
    }
  }
  MSIL_EXPECT(big_digits(fact) == 158);
  big_mul_small(&fact2, 0);
  MSIL_EXPECT(fact2.n == 0);

  put_kv("digit sum", digit_sum);
  return (int)(digit_sum & 0xff);
}

// memcpy and memset from C (Milestone 7, D15): copies and fills of constant
// length (cpblk and initblk, or loads and stores where LLVM expands a small
// one), copies of variable length (System.Buffer::MemoryCopy) and fills of
// variable length (initblk with an int32 length), struct assignment (an
// llvm.memcpy even at -O0) and zeroing, __builtin_memcpy_inline and
// __builtin_memset_inline (llvm.memcpy.inline and llvm.memset.inline, which
// never become calls), fill values outside 0..255 (C stores the value converted
// to unsigned char), zero lengths, misaligned source and destination addresses,
// and the pointer memcpy and memset return. Every destination has guard bytes
// on both sides, so a copy or fill that is one byte too long or too short
// fails. Sizes, values and seeds are derived from argc (1 when run without
// arguments) and reach the copies through noinline helpers, so clang cannot
// fold them at -O2. MSIL_EXPECT returns 1 + (line % 255) at the first failed
// check; [exit 0] means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))
#define TRY(call)                                                              \
  do {                                                                         \
    int r_ = (call);                                                           \
    if (r_ != 0)                                                               \
      return r_;                                                               \
  } while (0)

// Destinations are CAP bytes with GUARD bytes of 0xEE on each side.
#define CAP 1100
#define GUARD 16
#define BUF (CAP + 2 * GUARD)

typedef unsigned char u8;
typedef unsigned long long u64;

static u8 pattern(int seed, int i) {
  return (u8)(seed * 37 + i * 11 + (i >> 4));
}

static NOINLINE void fill_pattern(u8 *p, int n, int seed) {
  for (int i = 0; i < n; i++)
    p[i] = pattern(seed, i);
}

static NOINLINE void fill_byte(u8 *p, int n, u8 c) {
  for (int i = 0; i < n; i++)
    p[i] = c;
}

// 1 when p[0..n) are all c.
static NOINLINE int all_byte(const u8 *p, int n, u8 c) {
  for (int i = 0; i < n; i++)
    if (p[i] != c)
      return 0;
  return 1;
}

// 1 when p[0..n) holds pattern(seed, from + i).
static NOINLINE int has_pattern(const u8 *p, int n, int seed, int from) {
  for (int i = 0; i < n; i++)
    if (p[i] != pattern(seed, from + i))
      return 0;
  return 1;
}

// 1 when buf holds pattern(seed, from ...) for n bytes at GUARD + at and 0xEE
// everywhere else.
static NOINLINE int copied(const u8 *buf, int at, int n, int seed, int from) {
  return all_byte(buf, GUARD + at, 0xEE) &&
         has_pattern(buf + GUARD + at, n, seed, from) &&
         all_byte(buf + GUARD + at + n, BUF - GUARD - at - n, 0xEE);
}

// 1 when buf holds n bytes of c at GUARD + at and 0xEE everywhere else.
static NOINLINE int filled(const u8 *buf, int at, int n, u8 c) {
  return all_byte(buf, GUARD + at, 0xEE) && all_byte(buf + GUARD + at, n, c) &&
         all_byte(buf + GUARD + at + n, BUF - GUARD - at - n, 0xEE);
}

// Constant lengths: at -O2, 1 and 8 become a load and a store; the others
// stay llvm.memcpy (cpblk, or loads and stores where LLVM expands them).
static NOINLINE void copy1(void *d, const void *s) {
  __builtin_memcpy(d, s, 1);
}
static NOINLINE void copy3(void *d, const void *s) {
  __builtin_memcpy(d, s, 3);
}
static NOINLINE void copy8(void *d, const void *s) {
  __builtin_memcpy(d, s, 8);
}
static NOINLINE void copy13(void *d, const void *s) {
  __builtin_memcpy(d, s, 13);
}
static NOINLINE void copy64(void *d, const void *s) {
  __builtin_memcpy(d, s, 64);
}
static NOINLINE void copy1000(void *d, const void *s) {
  __builtin_memcpy(d, s, 1000);
}
static NOINLINE void copy_inline24(void *d, const void *s) {
  __builtin_memcpy_inline(d, s, 24);
}

// Variable lengths: System.Buffer::MemoryCopy.
static NOINLINE void *copy_var(void *d, const void *s, u64 n) {
  return __builtin_memcpy(d, s, n);
}
static NOINLINE void copy_var32(void *d, const void *s, unsigned n) {
  __builtin_memcpy(d, s, n);
}

static NOINLINE void set37(void *d) { __builtin_memset(d, 0x5A, 37); }
static NOINLINE void set_zero100(void *d) { __builtin_memset(d, 0, 100); }
static NOINLINE void set300(void *d, int c) { __builtin_memset(d, c, 300); }
static NOINLINE void set_inline19(void *d, int c) {
  __builtin_memset_inline(d, c, 19);
}
static NOINLINE void *set_var(void *d, int c, u64 n) {
  return __builtin_memset(d, c, n);
}
static NOINLINE void set_var32(void *d, int c, unsigned n) {
  __builtin_memset(d, c, n);
}

static NOINLINE int check_const_copies(int one) {
  u8 src[CAP];
  u8 dst[BUF];
  int seed = 3 * one;
  fill_pattern(src, CAP, seed);

  fill_byte(dst, BUF, 0xEE);
  copy1(dst + GUARD, src + 4);
  MSIL_EXPECT(copied(dst, 0, 1, seed, 4));

  fill_byte(dst, BUF, 0xEE);
  copy3(dst + GUARD + 5, src + 1);
  MSIL_EXPECT(copied(dst, 5, 3, seed, 1));

  fill_byte(dst, BUF, 0xEE);
  copy8(dst + GUARD, src + 8);
  MSIL_EXPECT(copied(dst, 0, 8, seed, 8));

  fill_byte(dst, BUF, 0xEE);
  copy8(dst + GUARD + 3, src + 7);
  MSIL_EXPECT(copied(dst, 3, 8, seed, 7));

  fill_byte(dst, BUF, 0xEE);
  copy13(dst + GUARD + 2, src + 5);
  MSIL_EXPECT(copied(dst, 2, 13, seed, 5));

  fill_byte(dst, BUF, 0xEE);
  copy64(dst + GUARD, src + 64);
  MSIL_EXPECT(copied(dst, 0, 64, seed, 64));

  fill_byte(dst, BUF, 0xEE);
  copy1000(dst + GUARD + 7, src + 3);
  MSIL_EXPECT(copied(dst, 7, 1000, seed, 3));

  fill_byte(dst, BUF, 0xEE);
  copy_inline24(dst + GUARD + 1, src + 2);
  MSIL_EXPECT(copied(dst, 1, 24, seed, 2));
  return 0;
}

static NOINLINE int check_var_copies(int one) {
  u8 src[CAP];
  u8 dst[BUF];
  int seed = 5 * one;
  fill_pattern(src, CAP, seed);

  // Every length from 0 to 70, at varying offsets of both buffers.
  for (int n = 0; n <= 70; n++) {
    fill_byte(dst, BUF, 0xEE);
    void *r = copy_var(dst + GUARD + n % 9, src + n % 7, (u64)n * one);
    MSIL_EXPECT(r == dst + GUARD + n % 9);
    MSIL_EXPECT(copied(dst, n % 9, n, seed, n % 7));
  }

  fill_byte(dst, BUF, 0xEE);
  copy_var(dst + GUARD, src, 1024ull * one);
  MSIL_EXPECT(copied(dst, 0, 1024, seed, 0));

  fill_byte(dst, BUF, 0xEE);
  copy_var(dst + GUARD + 1, src + 2, (u64)(CAP - 3) * one);
  MSIL_EXPECT(copied(dst, 1, CAP - 3, seed, 2));

  fill_byte(dst, BUF, 0xEE);
  copy_var32(dst + GUARD + 3, src + 50, 999u * one);
  MSIL_EXPECT(copied(dst, 3, 999, seed, 50));

  fill_byte(dst, BUF, 0xEE);
  copy_var32(dst + GUARD, src, 0u * one);
  MSIL_EXPECT(all_byte(dst, BUF, 0xEE));
  return 0;
}

static NOINLINE int check_fills(int one) {
  u8 buf[BUF];

  fill_byte(buf, BUF, 0xEE);
  set37(buf + GUARD + 1);
  MSIL_EXPECT(filled(buf, 1, 37, 0x5A));

  fill_byte(buf, BUF, 0xEE);
  set_zero100(buf + GUARD + 4);
  MSIL_EXPECT(filled(buf, 4, 100, 0));

  fill_byte(buf, BUF, 0xEE);
  set300(buf + GUARD + 9, 0x3C * one);
  MSIL_EXPECT(filled(buf, 9, 300, 0x3C));

  fill_byte(buf, BUF, 0xEE);
  set_inline19(buf + GUARD + 2, 0xC3 * one);
  MSIL_EXPECT(filled(buf, 2, 19, 0xC3));

  // The fill value is converted to unsigned char.
  fill_byte(buf, BUF, 0xEE);
  void *r = set_var(buf + GUARD + 3, 0x1AB * one, 77ull * one);
  MSIL_EXPECT(r == buf + GUARD + 3);
  MSIL_EXPECT(filled(buf, 3, 77, 0xAB));

  fill_byte(buf, BUF, 0xEE);
  set_var(buf + GUARD, -1 * one, 1000ull * one);
  MSIL_EXPECT(filled(buf, 0, 1000, 0xFF));

  fill_byte(buf, BUF, 0xEE);
  set_var(buf + GUARD + 5, 0x100 * one, 10ull * one);
  MSIL_EXPECT(filled(buf, 5, 10, 0));

  fill_byte(buf, BUF, 0xEE);
  set_var(buf + GUARD + 6, -2 * one, (u64)CAP - 6);
  MSIL_EXPECT(filled(buf, 6, CAP - 6, 0xFE));

  fill_byte(buf, BUF, 0xEE);
  set_var32(buf + GUARD + 1, 0x7F * one, 1024u * one);
  MSIL_EXPECT(filled(buf, 1, 1024, 0x7F));

  // Every length from 0 to 40.
  for (int n = 0; n <= 40; n++) {
    fill_byte(buf, BUF, 0xEE);
    set_var(buf + GUARD + n % 5, n * 7 + one, (u64)n * one);
    MSIL_EXPECT(filled(buf, n % 5, n, (u8)(n * 7 + one)));
  }

  fill_byte(buf, BUF, 0xEE);
  set_var32(buf + GUARD, 0x11 * one, 0u * one);
  MSIL_EXPECT(all_byte(buf, BUF, 0xEE));
  return 0;
}

struct rec {
  int a;
  short b;
  signed char c;
  long long d;
  double e;
  u8 tag[13];
  float f;
};

static NOINLINE void set_rec(struct rec *r, int k) {
  r->a = -1000003 * k;
  r->b = (short)(-30000 * k);
  r->c = (signed char)(-100 * k);
  r->d = 0x123456789ABCDEFll * k;
  r->e = -0.375 * k;
  for (int i = 0; i < 13; i++)
    r->tag[i] = (u8)(0xF0 + i * k);
  r->f = 2.5f * k;
}

static NOINLINE int rec_is(const struct rec *r, int k) {
  if (r->a != -1000003 * k || r->b != (short)(-30000 * k) ||
      r->c != (signed char)(-100 * k) || r->d != 0x123456789ABCDEFll * k ||
      r->e != -0.375 * k || r->f != 2.5f * k)
    return 0;
  for (int i = 0; i < 13; i++)
    if (r->tag[i] != (u8)(0xF0 + i * k))
      return 0;
  return 1;
}

static NOINLINE int rec_is_zero(const struct rec *r) {
  if (r->a != 0 || r->b != 0 || r->c != 0 || r->d != 0 || r->e != 0.0 ||
      r->f != 0.0f)
    return 0;
  return all_byte(r->tag, 13, 0);
}

static NOINLINE void copy_rec(struct rec *d, const struct rec *s) { *d = *s; }
static NOINLINE void zero_rec(struct rec *r) {
  __builtin_memset(r, 0, sizeof *r);
}

static NOINLINE int check_structs(int one) {
  struct rec x, y, arr[4];
  set_rec(&x, 7 * one);
  MSIL_EXPECT(rec_is(&x, 7));
  set_rec(&y, 3 * one);
  zero_rec(&y);
  MSIL_EXPECT(rec_is_zero(&y));
  copy_rec(&y, &x);
  MSIL_EXPECT(rec_is(&y, 7));

  // Struct assignment between locals and array elements.
  arr[0] = x;
  for (int i = 1; i < 4; i++) {
    arr[i] = arr[i - 1];
    arr[i].a += i;
  }
  MSIL_EXPECT(arr[3].a == x.a + 6);
  arr[3].a = x.a;
  MSIL_EXPECT(rec_is(&arr[3], 7));
  y = arr[2];
  MSIL_EXPECT(y.a == x.a + 3 && y.d == x.d && y.tag[12] == x.tag[12]);

  // Arrays of doubles and long longs survive a byte copy unchanged.
  double ds[8], dd[8];
  long long ls[8], ld[8];
  for (int i = 0; i < 8; i++) {
    ds[i] = i * 0.5 * one - 1.25;
    ls[i] = (long long)i * -1234567891011ll * one;
    dd[i] = 99.0;
    ld[i] = 99;
  }
  copy_var(dd, ds, sizeof ds);
  copy64(ld, ls);
  for (int i = 0; i < 8; i++) {
    MSIL_EXPECT(dd[i] == ds[i]);
    MSIL_EXPECT(ld[i] == ls[i]);
  }
  return 0;
}

int main(int argc, char **argv) {
  (void)argv;
  TRY(check_const_copies(argc));
  TRY(check_var_copies(argc));
  TRY(check_fills(argc));
  TRY(check_structs(argc));
  return 0;
}

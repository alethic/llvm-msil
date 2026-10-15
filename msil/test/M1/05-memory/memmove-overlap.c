// memmove from C with overlapping source and destination (Milestone 7,
// D15): every memmove calls System.Buffer::MemoryCopy, which has memmove
// semantics on every runtime, never cpblk, whose result for an overlapping
// copy of constant size differs between .NET and .NET Framework
// (llvm/docs/MSIL/recon/cil-runtime.md, e04 and e19). Covered: the e19 case
// memmove(p + 1, p, 7) on the bytes 01 02 .. 08 and its mirror image,
// constant lengths 7, 16 and 100 and variable lengths moved forwards and
// backwards by 1, 2, 3, 8 and 100 bytes, every length from 0 to 40, a move
// onto itself, a zero length, a disjoint move, and memmove inside an
// insertion sort and an array deletion. Each result is compared byte for
// byte, the bytes around the destination included, with a reference move
// through a temporary buffer. Lengths and data are derived from argc (1 when
// run without arguments) and reach the moves through noinline helpers, so
// clang cannot fold them at -O2. MSIL_EXPECT returns 1 + (line % 255) at the
// first failed check; [exit 0] means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))
#define TRY(call)                                                              \
  do {                                                                         \
    int r_ = (call);                                                           \
    if (r_ != 0)                                                               \
      return r_;                                                               \
  } while (0)

#define N 1200

typedef unsigned char u8;
typedef unsigned long long u64;

static NOINLINE void move7(void *d, const void *s) {
  __builtin_memmove(d, s, 7);
}
static NOINLINE void move16(void *d, const void *s) {
  __builtin_memmove(d, s, 16);
}
static NOINLINE void move100(void *d, const void *s) {
  __builtin_memmove(d, s, 100);
}
static NOINLINE void *move_var(void *d, const void *s, u64 n) {
  return __builtin_memmove(d, s, n);
}

static NOINLINE void fill(u8 *p, int n, int seed) {
  for (int i = 0; i < n; i++)
    p[i] = (u8)(seed + i * 13 + (i >> 8));
}

static NOINLINE int same(const u8 *a, const u8 *b, int n) {
  for (int i = 0; i < n; i++)
    if (a[i] != b[i])
      return 0;
  return 1;
}

// The reference: moves n bytes of buf from offset from to offset to through
// a temporary buffer.
static NOINLINE void ref_move(u8 *buf, int to, int from, int n) {
  u8 tmp[N];
  for (int i = 0; i < n; i++)
    tmp[i] = buf[from + i];
  for (int i = 0; i < n; i++)
    buf[to + i] = tmp[i];
}

// 1 when memmove (variable length) of n bytes from offset from to offset to
// gives the same N bytes as the reference.
static NOINLINE int var_move_ok(int to, int from, int n, int seed) {
  u8 a[N], b[N];
  fill(a, N, seed);
  fill(b, N, seed);
  void *r = move_var(a + to, a + from, (u64)n);
  ref_move(b, to, from, n);
  return r == a + to && same(a, b, N);
}

// The same for the constant lengths 7 (kind 0), 16 (kind 1) and 100.
static NOINLINE int const_move_ok(int kind, int to, int from, int seed) {
  u8 a[N], b[N];
  fill(a, N, seed);
  fill(b, N, seed);
  int n;
  if (kind == 0) {
    move7(a + to, a + from);
    n = 7;
  } else if (kind == 1) {
    move16(a + to, a + from);
    n = 16;
  } else {
    move100(a + to, a + from);
    n = 100;
  }
  ref_move(b, to, from, n);
  return same(a, b, N);
}

// The little-endian value of p[0..8).
static NOINLINE u64 le64(const u8 *p) {
  u64 v = 0;
  for (int i = 7; i >= 0; i--)
    v = v << 8 | p[i];
  return v;
}

static NOINLINE int check_e19(int one) {
  u8 p[8];
  for (int i = 0; i < 8; i++)
    p[i] = (u8)(i + one);
  move7(p + 1, p);
  MSIL_EXPECT(le64(p) == 0x0706050403020101ull);

  for (int i = 0; i < 8; i++)
    p[i] = (u8)(i + one);
  move7(p, p + 1);
  MSIL_EXPECT(le64(p) == 0x0808070605040302ull);

  for (int i = 0; i < 8; i++)
    p[i] = (u8)(i + one);
  move_var(p + 1, p, 7ull * one);
  MSIL_EXPECT(le64(p) == 0x0706050403020101ull);
  return 0;
}

static NOINLINE int check_const_lengths(int one) {
  for (int kind = 0; kind < 3; kind++) {
    MSIL_EXPECT(const_move_ok(kind, 1 * one, 0, 21 + kind));
    MSIL_EXPECT(const_move_ok(kind, 0, 1 * one, 22 + kind));
    MSIL_EXPECT(const_move_ok(kind, 3 * one + 10, 10, 23 + kind));
    MSIL_EXPECT(const_move_ok(kind, 10, 3 * one + 10, 24 + kind));
    MSIL_EXPECT(const_move_ok(kind, 8 * one + 5, 5, 25 + kind));
    MSIL_EXPECT(const_move_ok(kind, 5, 8 * one + 5, 26 + kind));
    MSIL_EXPECT(const_move_ok(kind, 100 * one, 0, 27 + kind));
    MSIL_EXPECT(const_move_ok(kind, 0, 100 * one, 28 + kind));
    MSIL_EXPECT(const_move_ok(kind, 33 * one, 33, 29 + kind));
    MSIL_EXPECT(const_move_ok(kind, 600 * one + 1, 2, 30 + kind));
  }
  return 0;
}

static NOINLINE int check_var_lengths(int one) {
  MSIL_EXPECT(var_move_ok(1 * one, 0, 1000, 41));
  MSIL_EXPECT(var_move_ok(0, 1 * one, 1000, 42));
  MSIL_EXPECT(var_move_ok(3 * one, 0, 997, 43));
  MSIL_EXPECT(var_move_ok(0, 3 * one, 997, 44));
  MSIL_EXPECT(var_move_ok(8 * one + 5, 5, 1001, 45));
  MSIL_EXPECT(var_move_ok(5, 8 * one + 5, 1001, 46));
  MSIL_EXPECT(var_move_ok(100 * one, 0, 1100, 47));
  MSIL_EXPECT(var_move_ok(0, 100 * one, 1100, 48));
  // Onto itself, a zero length, and disjoint ranges.
  MSIL_EXPECT(var_move_ok(37, 37 * one, 500, 49));
  MSIL_EXPECT(var_move_ok(10 * one, 20, 0, 50));
  MSIL_EXPECT(var_move_ok(600 * one, 0, 600, 51));
  MSIL_EXPECT(var_move_ok(0, 600 * one, 600, 52));
  // Every length from 0 to 40, forwards and backwards by 1, 2 and 3.
  for (int n = 0; n <= 40; n++) {
    MSIL_EXPECT(var_move_ok(2 + n % 3 + one, 2, n, n));
    MSIL_EXPECT(var_move_ok(2, 2 + n % 3 + one, n, n + 100));
  }
  return 0;
}

// Sorts v[0..n) by inserting each element after one memmove of the larger
// elements before it.
static NOINLINE void insertion_sort(int *v, int n) {
  for (int i = 1; i < n; i++) {
    int x = v[i];
    int j = i;
    while (j > 0 && v[j - 1] > x)
      j--;
    move_var(v + j + 1, v + j, (u64)(i - j) * sizeof(int));
    v[j] = x;
  }
}

// Removes v[k] from v[0..n) by moving the rest down; returns n - 1.
static NOINLINE int erase_at(double *v, int n, int k) {
  move_var(v + k, v + k + 1, (u64)(n - k - 1) * sizeof(double));
  return n - 1;
}

static NOINLINE int check_arrays(int one) {
  int v[64];
  // 37 is coprime to 64, so this is a permutation of 0..63.
  for (int i = 0; i < 64; i++)
    v[i] = (i * 37 * one + 11) % 64 - 20;
  insertion_sort(v, 64);
  for (int i = 0; i < 64; i++)
    MSIL_EXPECT(v[i] == i - 20);

  double d[10];
  for (int i = 0; i < 10; i++)
    d[i] = i * 1.5 * one;
  int n = erase_at(d, 10, 3);
  n = erase_at(d, n, 0);
  n = erase_at(d, n, n - 1);
  MSIL_EXPECT(n == 7);
  // Left: 1, 2, 4, 5, 6, 7, 8 (times 1.5).
  MSIL_EXPECT(d[0] == 1.5 && d[1] == 3.0 && d[2] == 6.0 && d[3] == 7.5);
  MSIL_EXPECT(d[4] == 9.0 && d[5] == 10.5 && d[6] == 12.0);
  return 0;
}

int main(int argc, char **argv) {
  (void)argv;
  TRY(check_e19(argc));
  TRY(check_const_lengths(argc));
  TRY(check_var_lengths(argc));
  TRY(check_arrays(argc));
  return 0;
}

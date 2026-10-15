// Pointer arithmetic from C (Milestone 7): array indexing with int, unsigned,
// long long and signed char indexes (negative int32 indexes must be sign
// extended to native int), pointer differences of int, double and 12-byte
// struct elements (an exact division), pointer increments and decrements in
// loops, pointer comparisons within an array, two-dimensional arrays and
// pointers to rows, arrays of pointers and pointers to pointers (native int
// loads and stores), a linked list in a local array of nodes reversed in
// place, pointers returned from calls and compared with NULL, uintptr_t
// round trips, offsets beyond 32 KiB in a large local array, and loads of
// one location before and after a store through a pointer that may alias it
// (int, long long and a char view), which no JIT may merge (the optimizing
// JIT leg, D38). Indexes and data are derived from argc (1 when run without
// arguments) and pass through noinline helpers, so clang cannot fold them at
// -O2. MSIL_EXPECT returns 1 + (line % 255) at the first failed check;
// [exit 0] means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"
#include <stddef.h>
#include <stdint.h>

#define NOINLINE __attribute__((noinline))
#define TRY(call)                                                              \
  do {                                                                         \
    int r_ = (call);                                                           \
    if (r_ != 0)                                                               \
      return r_;                                                               \
  } while (0)

struct trio {
  int a, b, c;
};
_Static_assert(sizeof(struct trio) == 12, "trio");

struct node {
  int value;
  struct node *next;
};

static NOINLINE int at(const int *p, int k) { return p[k]; }
static NOINLINE int at_u(const int *p, unsigned k) { return p[k]; }
static NOINLINE int at_ll(const int *p, long long k) { return p[k]; }
static NOINLINE int at_sc(const int *p, signed char k) { return p[k]; }
static NOINLINE ptrdiff_t diff_int(const int *x, const int *y) { return x - y; }
static NOINLINE ptrdiff_t diff_dbl(const double *x, const double *y) {
  return x - y;
}
static NOINLINE ptrdiff_t diff_trio(const struct trio *x,
                                    const struct trio *y) {
  return x - y;
}
static NOINLINE const int *advance(const int *p, int k) { return p + k; }

// Bit 0: x < y, bit 1: x <= y, bit 2: x > y, bit 3: x >= y, bit 4: x == y,
// bit 5: x != y.
static NOINLINE int compare(const int *x, const int *y) {
  return (x < y) | (x <= y) << 1 | (x > y) << 2 | (x >= y) << 3 |
         (x == y) << 4 | (x != y) << 5;
}

// The first element equal to key, or NULL.
static NOINLINE int *find(int *p, int n, int key) {
  for (int *q = p; q != p + n; q++)
    if (*q == key)
      return q;
  return NULL;
}

static NOINLINE int check_indexing(int one) {
  int a[20];
  for (int i = 0; i < 20; i++)
    a[i] = i * i * one - 7;
  const int *p = a + 10 * one;
  MSIL_EXPECT(at(p, -3 * one) == 42);
  MSIL_EXPECT(at(p, -10 * one) == -7);
  MSIL_EXPECT(at(p, 9 * one) == 354);
  MSIL_EXPECT(at_u(a, 7u * one) == 42);
  MSIL_EXPECT(at_ll(p, -1ll * one) == 74);
  MSIL_EXPECT(at_ll(a, 19ll * one) == 354);
  MSIL_EXPECT(at_sc(p, (signed char)(-5 * one)) == 18);
  MSIL_EXPECT(at_sc(a, (signed char)(12 * one)) == 137);

  MSIL_EXPECT(diff_int(&a[17], &a[4]) == 13);
  MSIL_EXPECT(diff_int(&a[4], &a[17]) == -13);
  MSIL_EXPECT(diff_int(advance(a, 19 * one), a) == 19);
  MSIL_EXPECT(*advance(p, -9 * one) == -6);

  double d[9];
  for (int i = 0; i < 9; i++)
    d[i] = i * 0.25 * one;
  MSIL_EXPECT(diff_dbl(&d[8], &d[1]) == 7 && diff_dbl(d, &d[8]) == -8);

  struct trio t[7];
  MSIL_EXPECT(diff_trio(&t[6], t) == 6 && diff_trio(&t[1], &t[5]) == -4);
  MSIL_EXPECT((char *)&t[2] - (char *)&t[0] == 24);
  MSIL_EXPECT((char *)&t[3].c - (char *)t == 36 + offsetof(struct trio, c));
  return 0;
}

static NOINLINE int check_walks(int one) {
  int a[50];
  for (int i = 0; i < 50; i++)
    a[i] = (i * 7 + 3) * one;
  // Forwards, with a pointer bound.
  int s = 0;
  for (const int *q = a; q < a + 50; q++)
    s += *q;
  MSIL_EXPECT(s == 7 * 1225 + 150);
  // Backwards, with predecrement.
  int t = 0, k = 0;
  for (const int *q = a + 50; q != a;) {
    t = t * 3 + *--q;
    if (++k == 5)
      break;
  }
  // a[49..45] = 346, 339, 332, 325, 318.
  MSIL_EXPECT(t == (((346 * 3 + 339) * 3 + 332) * 3 + 325) * 3 + 318);
  // Every second element through a double step, writing through pointers.
  for (int *q = a + 1; q < a + 50; q += 2)
    *q = -*q;
  MSIL_EXPECT(a[0] == 3 && a[1] == -10 && a[48] == 339 && a[49] == -346);

  MSIL_EXPECT(compare(&a[3], &a[9]) == 0x23);
  MSIL_EXPECT(compare(&a[9], &a[3]) == 0x2C);
  MSIL_EXPECT(compare(&a[5], a + 5 * one) == 0x1A);

  int *f = find(a, 50, 339 * one);
  MSIL_EXPECT(f != NULL && f - a == 48);
  MSIL_EXPECT(find(a, 50, 340 * one) == NULL);
  *f = 1;
  MSIL_EXPECT(a[48] == 1);

  // A conditional pointer.
  int x = 10 * one, y = 20 * one;
  int *m = x > y ? &x : &y;
  *m += 5;
  MSIL_EXPECT(x == 10 && y == 25);
  return 0;
}

static NOINLINE int sum_row(int (*row)[5], int r) {
  int s = 0;
  for (int j = 0; j < 5; j++)
    s += row[r][j];
  return s;
}

static NOINLINE int check_2d(int one) {
  int m[4][5];
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 5; j++)
      m[i][j] = (i * 10 + j) * one;
  int (*row)[5] = m + 2 * one;
  MSIL_EXPECT(row[1][3] == 33 && row[-2][4] == 4);
  MSIL_EXPECT((char *)&m[2][3] - (char *)m == 13 * sizeof(int) * one);
  MSIL_EXPECT(sum_row(m, 3 * one) == 160);
  MSIL_EXPECT(sum_row(row, -one) == 60);
  MSIL_EXPECT((char *)&m[3] - (char *)m == 60);
  return 0;
}

// Swaps two pointers through pointers to pointers.
static NOINLINE void swap_ptr(int **x, int **y) {
  int *t = *x;
  *x = *y;
  *y = t;
}

static NOINLINE int check_pointer_arrays(int one) {
  int vals[6];
  int *ptrs[6];
  for (int i = 0; i < 6; i++) {
    vals[i] = (100 + i) * one;
    ptrs[i] = &vals[5 - i];
  }
  MSIL_EXPECT(*ptrs[0] == 105 && *ptrs[5] == 100);
  swap_ptr(&ptrs[0], &ptrs[5]);
  MSIL_EXPECT(*ptrs[0] == 100 && *ptrs[5] == 105 && ptrs[0] == vals);
  int **pp = ptrs + 2 * one;
  **pp += 1000;
  MSIL_EXPECT(vals[3] == 1103);
  pp[1] = pp[-1];
  MSIL_EXPECT(ptrs[3] == &vals[4] && *ptrs[3] == 104);
  int s = 0;
  for (int **q = ptrs; q < ptrs + 6; q++)
    s += **q;
  MSIL_EXPECT(s == 100 + 104 + 1103 + 104 + 101 + 105);
  return 0;
}

// Reverses a list in place; returns the new head.
static NOINLINE struct node *reverse(struct node *head) {
  struct node *prev = NULL;
  while (head) {
    struct node *next = head->next;
    head->next = prev;
    prev = head;
    head = next;
  }
  return prev;
}

static NOINLINE int check_list(int one) {
  struct node nodes[8];
  // Link the nodes in the order 0, 3, 6, 1, 4, 7, 2, 5 (3 is coprime to 8).
  for (int i = 0; i < 8; i++) {
    int k = i * 3 % 8;
    nodes[k].value = (k + 1) * one;
    nodes[k].next = i == 7 ? NULL : &nodes[(i + 1) * 3 % 8];
  }
  struct node *head = &nodes[0];
  int count = 0, h = 0;
  for (struct node *n = head; n; n = n->next) {
    h = h * 10 + n->value;
    count++;
  }
  MSIL_EXPECT(count == 8 && h == 14725836);
  head = reverse(head);
  h = 0;
  for (struct node *n = head; n; n = n->next)
    h = h * 10 + n->value;
  MSIL_EXPECT(head == &nodes[5] && h == 63852741);
  MSIL_EXPECT(nodes[0].next == NULL);
  return 0;
}

static NOINLINE int check_uintptr(int one) {
  int a[8];
  double d[4];
  for (int i = 0; i < 8; i++)
    a[i] = i * one;
  uintptr_t u = (uintptr_t)&a[1];
  MSIL_EXPECT(u - (uintptr_t)&a[0] == sizeof(int));
  int *back = (int *)(u + 2 * sizeof(int) * one);
  MSIL_EXPECT(back == &a[3] && *back == 3);
  MSIL_EXPECT((uintptr_t)a % _Alignof(int) == 0);
  MSIL_EXPECT((uintptr_t)d % _Alignof(double) == 0);
  // Zero and sign extension of 32-bit values added to an address.
  unsigned big = 0x80000000u * one;
  int neg = -8 * one;
  MSIL_EXPECT((uintptr_t)a + big - (uintptr_t)a == 0x80000000ull);
  MSIL_EXPECT((uintptr_t)((char *)&a[4] + neg) == (uintptr_t)&a[2]);
  MSIL_EXPECT((intptr_t)&a[0] - (intptr_t)&a[7] == -28);
  return 0;
}

// Offsets that do not fit an int16, in a 40000-byte frame.
static NOINLINE int check_large_offsets(int one) {
  unsigned char big[40000];
  for (int i = 0; i < 40000; i += 4000)
    big[i] = (unsigned char)(i / 4000 * one);
  big[39999] = (unsigned char)(0xAB * one);
  big[32768] = (unsigned char)(0x80 * one);
  MSIL_EXPECT(big[36000] == 9 && big[4000] == 1 && big[0] == 0);
  MSIL_EXPECT(big[39999] == 0xAB && big[32768] == 0x80);
  unsigned char *p = big + 39999 * one;
  MSIL_EXPECT(p - big == 39999 && *p == 0xAB && p[-7231] == 0x80);
  return 0;
}

// Reads *p, writes through q, reads *p again: when p == q the second load
// must see the store.
static NOINLINE int alias_rw(int *p, int *q) {
  int a = *p;
  *q = a + 1;
  return *p * 100 + a;
}

// The same with a store of one byte of *p through a char pointer.
static NOINLINE int alias_bytes(int *p, unsigned char *b) {
  int a = *p;
  b[1] = 0x12;
  return *p - a;
}

static NOINLINE long long alias_ll(long long *p, long long *q, long long v) {
  *p = v;
  *q = v + 1;
  return *p;
}

static NOINLINE int check_aliasing(int one) {
  int x = 5 * one, y = 7 * one;
  MSIL_EXPECT(alias_rw(&x, &x) == 605 && x == 6);
  MSIL_EXPECT(alias_rw(&x, &y) == 606 && x == 6 && y == 7);
  int z = 0x01000000 * one;
  MSIL_EXPECT(alias_bytes(&z, (unsigned char *)&z) == 0x1200);
  MSIL_EXPECT(z == 0x01001200);
  long long l[2];
  l[0] = 0;
  l[1] = 0;
  MSIL_EXPECT(alias_ll(&l[0], &l[0], 41ll * one) == 42 && l[1] == 0);
  MSIL_EXPECT(alias_ll(&l[0], &l[1], 41ll * one) == 41 && l[1] == 42);
  return 0;
}

int main(int argc, char **argv) {
  (void)argv;
  TRY(check_indexing(argc));
  TRY(check_walks(argc));
  TRY(check_2d(argc));
  TRY(check_pointer_arrays(argc));
  TRY(check_list(argc));
  TRY(check_uintptr(argc));
  TRY(check_large_offsets(argc));
  TRY(check_aliasing(argc));
  return 0;
}

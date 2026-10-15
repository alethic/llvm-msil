// Relocations at under-aligned sites of the data segment (Milestone 8, D12):
// pointers in packed global structs, which <Module>::.cctor stores after
// copying the data image. The data segment base is aligned to its maximum
// alignment and every global here is 8-aligned (aligned(8)), so a field's
// address modulo 8 is its offset in its global modulo 8; a pointer stored at an
// address that is not 8-aligned gets 'unaligned. N' before its stind.i, with
// N = 1 (odd address), 2 or 4 (plan section 12, M8.c alignment prefixes).
// Covered: __attribute__((packed)) structs with data pointers, a string literal
// pointer and pointers to a function of the module and to the puts P/Invoke at
// offsets that are 0, 1, 2, 3 and 4 modulo 8; an array of them whose 61-byte
// elements put the same fields at the other residues (5, 6 and 7 too); and
// structs under #pragma pack(2) and pack(4). Pointers are read back through the
// packed fields (code accesses them with alignment prefixes of its own),
// written through, retargeted at run time and compared with the addresses taken
// in code (functions are compared, not called: indirect calls come in M9). The
// globals are writable and externally visible and table indexes come from a
// volatile global, so the fields are loaded at run time at -O2 too. MSIL_EXPECT
// returns 1 + (line % 255) at the first failed check; [exit 0] means every
// check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))
#define OFFSET(type, field) ((int)__builtin_offsetof(type, field))

int x = 10;
int y = 20;
int arr[8] = {0, 1, 2, 3, 4, 5, 6, 7};
long long wide = -5;

static NOINLINE int inc(int v) { return v + 1; }

// Pointer field offsets (modulo 8 in parentheses): p_odd 1, p_two 10 (2),
// f_four 20 (4), p_eight 32 (0), s_odd 41 (1), out_three 51 (3). Size 61.
struct __attribute__((packed)) pk {
  char a;
  int *p_odd;
  char b;
  int *p_two;
  short c;
  int (*f_four)(int);
  int d;
  int *p_eight;
  char e;
  const char *s_odd;
  short g;
  int (*out_three)(const char *);
  char z;
  char w;
};

struct pk one __attribute__((aligned(8))) = {
    'a', &x, 'b', &arr[2], -3, inc, 0x12345678,
    &y, 'e', "odd", 7, puts, 'z', 'w'};

// Element i starts at 61 * i, so element 1 has its pointer fields at
// 62 (6), 71 (7), 81 (1), 93 (5), 102 (6), 112 (0) and element 2 at 123 (3),
// 132 (4), 142 (6), 154 (2), 163 (3), 173 (5).
struct pk table[3] __attribute__((aligned(8))) = {
    {1, &arr[0], 2, &arr[1], 3, inc, 4, &arr[2], 5, "zero", 6, puts, 7, 8},
    {11, &arr[3], 12, &arr[4], 13, inc, 14, &x, 15, "one", 16, puts, 17, 18},
    {21, &arr[7], 22, (int *)&wide, 23, 0, 24, &y, 25, "two", 26, 0, 27, 28},
};

// #pragma pack(2): p at 2, q at 14 and r at 22 (2, 6 and 6 modulo 8).
#pragma pack(push, 2)
struct p2 {
  short a;
  int *p;
  int b;
  int *q;
  int *r;
  short c;
};
#pragma pack(pop)
struct p2 two __attribute__((aligned(8))) = {-7, &x, 8, &arr[5], &y, 9};

// #pragma pack(4): p at 4, q at 28 and l at 36 (4 modulo 8); f at 16.
#pragma pack(push, 4)
struct p4 {
  int a;
  int *p;
  int b;
  int (*f)(int);
  int c;
  long long *q;
  long long *l;
};
#pragma pack(pop)
struct p4 four __attribute__((aligned(8))) = {
    1, &arr[6], 2, inc, 3, &wide, &wide};

volatile int zero = 0;

static int check_layout(void) {
  // The offsets the comments above rely on (the same natively and on MSIL).
  MSIL_EXPECT(sizeof(struct pk) == 61);
  MSIL_EXPECT(OFFSET(struct pk, p_odd) == 1 && OFFSET(struct pk, p_two) == 10);
  MSIL_EXPECT(OFFSET(struct pk, f_four) == 20);
  MSIL_EXPECT(OFFSET(struct pk, p_eight) == 32);
  MSIL_EXPECT(OFFSET(struct pk, s_odd) == 41);
  MSIL_EXPECT(OFFSET(struct pk, out_three) == 51);
  MSIL_EXPECT(sizeof(struct p2) == 32 && OFFSET(struct p2, p) == 2 &&
              OFFSET(struct p2, q) == 14 && OFFSET(struct p2, r) == 22);
  MSIL_EXPECT(sizeof(struct p4) == 44 && OFFSET(struct p4, p) == 4 &&
              OFFSET(struct p4, f) == 16 && OFFSET(struct p4, q) == 28 &&
              OFFSET(struct p4, l) == 36);
  MSIL_EXPECT(((unsigned long long)&one & 7) == 0);
  MSIL_EXPECT(((unsigned long long)table & 7) == 0);
  MSIL_EXPECT(((unsigned long long)&two & 7) == 0);
  MSIL_EXPECT(((unsigned long long)&four & 7) == 0);
  return 0;
}

static int check_one(void) {
  MSIL_EXPECT(one.p_odd == &x && *one.p_odd == 10);
  MSIL_EXPECT(one.p_two == &arr[2] && *one.p_two == 2);
  MSIL_EXPECT(one.f_four == inc);
  MSIL_EXPECT(one.p_eight == &y && *one.p_eight == 20);
  MSIL_EXPECT(one.s_odd[0] == 'o' && one.s_odd[1] == 'd' &&
              one.s_odd[2] == 'd' && one.s_odd[3] == 0);
  MSIL_EXPECT(one.out_three == puts);
  // The bytes around the pointers survive the relocation stores.
  MSIL_EXPECT(one.a == 'a' && one.b == 'b' && one.c == -3);
  MSIL_EXPECT(one.d == 0x12345678 && one.e == 'e' && one.g == 7);
  MSIL_EXPECT(one.z == 'z' && one.w == 'w');
  put_str(one.s_odd);
  return 0;
}

static int check_table(void) {
  int sum = 0;
  for (int i = 0; i < 3; i++) {
    const struct pk *t = &table[i + zero];
    MSIL_EXPECT(t->a == 10 * i + 1 && t->b == 10 * i + 2);
    MSIL_EXPECT(t->c == 10 * i + 3 && t->d == 10 * i + 4);
    MSIL_EXPECT(t->e == 10 * i + 5 && t->g == 10 * i + 6);
    MSIL_EXPECT(t->z == 10 * i + 7 && t->w == 10 * i + 8);
    sum = sum * 100 + *t->p_odd * 10 + *t->p_eight;
    put_str(t->s_odd);
  }
  MSIL_EXPECT(table[0 + zero].p_odd == &arr[0]);
  MSIL_EXPECT(table[0 + zero].p_two == &arr[1]);
  MSIL_EXPECT(table[1 + zero].p_odd == &arr[3]);
  MSIL_EXPECT(table[1 + zero].p_two == &arr[4]);
  MSIL_EXPECT(table[1 + zero].p_eight == &x);
  MSIL_EXPECT(table[2 + zero].p_odd == &arr[7]);
  MSIL_EXPECT(table[2 + zero].p_two == (int *)&wide);
  MSIL_EXPECT(table[2 + zero].p_eight == &y);
  MSIL_EXPECT(table[0 + zero].f_four == inc && table[1 + zero].f_four == inc);
  MSIL_EXPECT(table[2 + zero].f_four == 0);
  MSIL_EXPECT(table[0 + zero].out_three == puts &&
              table[1 + zero].out_three == puts);
  MSIL_EXPECT(table[2 + zero].out_three == 0);
  put_i64(sum);
  return 0;
}

static int check_pack2_pack4(void) {
  MSIL_EXPECT(two.p == &x && two.q == &arr[5] && two.r == &y);
  MSIL_EXPECT(two.a == -7 && two.b == 8 && two.c == 9);
  MSIL_EXPECT(*two.p + *two.q + *two.r == 35);
  MSIL_EXPECT(four.p == &arr[6] && *four.p == 6);
  MSIL_EXPECT(four.f == inc);
  MSIL_EXPECT(four.q == &wide && four.l == &wide && *four.q == -5);
  MSIL_EXPECT(four.a == 1 && four.b == 2 && four.c == 3);
  return 0;
}

static int check_writes(void) {
  // Writes through relocated pointers land in their targets.
  *one.p_odd = 11 + zero;
  MSIL_EXPECT(x == 11);
  *table[1 + zero].p_two = 40 + zero;
  MSIL_EXPECT(arr[4] == 40);
  *four.l = 1ll << 40;
  MSIL_EXPECT(wide == 1ll << 40 && *four.q == 1ll << 40);
  // Retarget the under-aligned fields at run time; the neighbours keep their
  // values.
  one.p_odd = &arr[7];
  one.p_two = &x;
  two.q = &y;
  table[2 + zero].f_four = inc;
  MSIL_EXPECT(*one.p_odd == 7 && *one.p_two == 11 && *two.q == 20);
  MSIL_EXPECT(table[2 + zero].f_four == inc);
  MSIL_EXPECT(one.a == 'a' && one.b == 'b' && one.c == -3);
  MSIL_EXPECT(two.p == &x && two.b == 8 && two.r == &y);
  MSIL_EXPECT(table[2 + zero].c == 23 && table[2 + zero].d == 24);
  // inc still works when called directly.
  MSIL_EXPECT(inc(one.d + zero) == 0x12345679);
  return 0;
}

int main(void) {
  int r;
  if ((r = check_layout()) != 0)
    return r;
  put_str("layout ok");
  if ((r = check_one()) != 0)
    return r;
  put_str("packed ok");
  if ((r = check_table()) != 0)
    return r;
  put_str("table ok");
  if ((r = check_pack2_pack4()) != 0)
    return r;
  put_str("pack(2) and pack(4) ok");
  if ((r = check_writes()) != 0)
    return r;
  put_str("writes ok");
  return 0;
}

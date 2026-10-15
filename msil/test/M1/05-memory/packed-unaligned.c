// Unaligned memory access from C (Milestone 7, D24): fields of packed
// structs (#pragma pack(1), #pragma pack(2) and __attribute__((packed)))
// written and read at odd and 2-aligned offsets, so loads and stores of every
// width (int8 through native int, pointers, float32, float64) need an
// 'unaligned. 1' or 'unaligned. 2' prefix; sign and zero extension of
// unaligned narrow fields; arrays of packed structs with odd strides; packed
// struct copies; volatile packed fields ('unaligned.' and 'volatile.'
// together); and the memcpy idiom for unaligned loads and stores (a load or
// store with align 1 at -O2) at every offset 0..7 of a byte buffer. The layout
// is checked with _Static_assert and, at run time, through byte views of the
// little-endian memory. Values are derived from argc (1 when run without
// arguments) and pass through noinline helpers, so clang cannot fold them at
// -O2. MSIL_EXPECT returns 1 + (line % 255) at the first failed check;
// [exit 0] means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"
#include <stddef.h>

#define NOINLINE __attribute__((noinline))
#define TRY(call)                                                              \
  do {                                                                         \
    int r_ = (call);                                                           \
    if (r_ != 0)                                                               \
      return r_;                                                               \
  } while (0)

typedef unsigned char u8;
typedef unsigned long long u64;

#pragma pack(push, 1)
struct p1 {
  u8 c;
  int i;
  short s;
  long long ll;
  double d;
  float f;
  unsigned short us;
  signed char sc;
  int *ptr;
  u8 tail;
};
#pragma pack(pop)
_Static_assert(offsetof(struct p1, i) == 1, "p1.i");
_Static_assert(offsetof(struct p1, s) == 5, "p1.s");
_Static_assert(offsetof(struct p1, ll) == 7, "p1.ll");
_Static_assert(offsetof(struct p1, d) == 15, "p1.d");
_Static_assert(offsetof(struct p1, f) == 23, "p1.f");
_Static_assert(offsetof(struct p1, us) == 27, "p1.us");
_Static_assert(offsetof(struct p1, ptr) == 30, "p1.ptr");
_Static_assert(sizeof(struct p1) == 39, "p1 size");

#pragma pack(push, 2)
struct p2 {
  u8 c;
  int i;
  long long ll;
  short s;
  double d;
  float f;
};
#pragma pack(pop)
_Static_assert(offsetof(struct p2, i) == 2, "p2.i");
_Static_assert(offsetof(struct p2, ll) == 6, "p2.ll");
_Static_assert(offsetof(struct p2, d) == 16, "p2.d");
_Static_assert(sizeof(struct p2) == 28 && _Alignof(struct p2) == 2, "p2");

struct __attribute__((packed)) p3 {
  u8 c;
  unsigned long long u;
  unsigned u32;
  signed char sc;
  unsigned short u16;
};
_Static_assert(offsetof(struct p3, u32) == 9, "p3.u32");
_Static_assert(offsetof(struct p3, u16) == 14, "p3.u16");
_Static_assert(sizeof(struct p3) == 16 && _Alignof(struct p3) == 1, "p3");

static NOINLINE void p1_store(struct p1 *p, int k, int *target) {
  p->c = (u8)(0xA0 + k);
  p->i = -123456789 * k;
  p->s = (short)(-12345 * k);
  p->ll = 0x0102030405060708ll * k;
  p->d = -2.75 * k;
  p->f = 1.5f * k;
  p->us = (unsigned short)(0xFEDC + k);
  p->sc = (signed char)(-100 + k);
  p->ptr = target;
  p->tail = (u8)(0x5B + k);
}

static NOINLINE int p1_check(const struct p1 *p, int k, int *target) {
  MSIL_EXPECT(p->c == (u8)(0xA0 + k));
  MSIL_EXPECT(p->i == -123456789 * k);
  MSIL_EXPECT(p->s == (short)(-12345 * k));
  MSIL_EXPECT(p->ll == 0x0102030405060708ll * k);
  MSIL_EXPECT(p->d == -2.75 * k);
  MSIL_EXPECT(p->f == 1.5f * k);
  MSIL_EXPECT(p->us == (unsigned short)(0xFEDC + k));
  MSIL_EXPECT(p->sc == (signed char)(-100 + k));
  MSIL_EXPECT(p->ptr == target);
  MSIL_EXPECT(p->tail == (u8)(0x5B + k));
  return 0;
}

// Widened loads of single unaligned fields (sign and zero extension).
static NOINLINE long long p1_s_wide(const struct p1 *p) { return p->s; }
static NOINLINE u64 p1_us_wide(const struct p1 *p) { return p->us; }
static NOINLINE long long p1_i_wide(const struct p1 *p) { return p->i; }
static NOINLINE double p1_f_wide(const struct p1 *p) { return p->f; }
static NOINLINE int p1_load_through(const struct p1 *p) { return *p->ptr; }

static NOINLINE void p1_copy(struct p1 *d, const struct p1 *s) { *d = *s; }

static NOINLINE int check_p1(int one) {
  struct p1 x, y, arr[5];
  int target = 4242 * one;
  p1_store(&x, one, &target);
  TRY(p1_check(&x, 1, &target));

  // The bytes of the fields, little-endian, at their packed offsets.
  const u8 *b = (const u8 *)&x;
  MSIL_EXPECT(b[0] == 0xA1);
  // -123456789 is 0xF8A432EB.
  MSIL_EXPECT(b[1] == 0xEB && b[2] == 0x32 && b[3] == 0xA4 && b[4] == 0xF8);
  // -12345 is 0xCFC7.
  MSIL_EXPECT(b[5] == 0xC7 && b[6] == 0xCF);
  for (int i = 0; i < 8; i++)
    MSIL_EXPECT(b[7 + i] == 8 - i);
  // -2.75 is 0xC006000000000000 and 1.5f is 0x3FC00000.
  MSIL_EXPECT(b[22] == 0xC0 && b[21] == 0x06 && b[15] == 0);
  MSIL_EXPECT(b[26] == 0x3F && b[25] == 0xC0 && b[23] == 0);
  MSIL_EXPECT(b[27] == 0xDD && b[28] == 0xFE && b[29] == 0x9D);
  MSIL_EXPECT(b[38] == 0x5C);

  MSIL_EXPECT(p1_s_wide(&x) == -12345);
  MSIL_EXPECT(p1_us_wide(&x) == 0xFEDD);
  MSIL_EXPECT(p1_i_wide(&x) == -123456789);
  MSIL_EXPECT(p1_f_wide(&x) == 1.5);
  MSIL_EXPECT(p1_load_through(&x) == 4242);

  p1_copy(&y, &x);
  TRY(p1_check(&y, 1, &target));
  y.ll = -1;
  y.i = 0x7FFFFFFF;
  MSIL_EXPECT(x.ll == 0x0102030405060708ll && x.i == -123456789);

  // An array of packed structs: element k starts at byte 39 * k.
  for (int k = 0; k < 5; k++)
    p1_store(&arr[k], k + one, &target);
  for (int k = 0; k < 5; k++)
    TRY(p1_check(&arr[k], k + 1, &target));
  MSIL_EXPECT((const u8 *)&arr[3] - (const u8 *)arr == 117);
  MSIL_EXPECT(((const u8 *)arr)[117 + 7] == 32);
  arr[2] = arr[4];
  TRY(p1_check(&arr[2], 5, &target));
  TRY(p1_check(&arr[1], 2, &target));
  TRY(p1_check(&arr[3], 4, &target));
  return 0;
}

static NOINLINE void p2_store(struct p2 *p, int k) {
  p->c = (u8)k;
  p->i = 0x10203040 * k;
  p->ll = -0x0706050403020100ll * k;
  p->s = (short)(0x7FFF * k);
  p->d = 1e300 * k;
  p->f = -0.25f * k;
}

static NOINLINE int check_p2(int one) {
  struct p2 v[3];
  for (int k = 0; k < 3; k++)
    p2_store(&v[k], k + one);
  for (int k = 0; k < 3; k++) {
    MSIL_EXPECT(v[k].c == k + 1);
    MSIL_EXPECT(v[k].i == 0x10203040 * (k + 1));
    MSIL_EXPECT(v[k].ll == -0x0706050403020100ll * (k + 1));
    MSIL_EXPECT(v[k].s == (short)(0x7FFF * (k + 1)));
    MSIL_EXPECT(v[k].d == 1e300 * (k + 1));
    MSIL_EXPECT(v[k].f == -0.25f * (k + 1));
  }
  const u8 *b = (const u8 *)&v[1];
  // v[1].i = 0x20406080 at offset 2, v[1].ll = -0x0E0C0A0806040200 at 6.
  MSIL_EXPECT(b[2] == 0x80 && b[3] == 0x60 && b[4] == 0x40 && b[5] == 0x20);
  MSIL_EXPECT(b[6] == 0 && b[7] == 0xFE && b[13] == 0xF1);
  return 0;
}

static NOINLINE void p3_store(struct p3 *p, u64 u, unsigned u32, int sc,
                              unsigned short u16) {
  p->c = 0x77;
  p->u = u;
  p->u32 = u32;
  p->sc = (signed char)sc;
  p->u16 = u16;
}

static NOINLINE int check_p3(int one) {
  struct p3 a[4];
  for (int k = 0; k < 4; k++)
    p3_store(&a[k], 0xFEDCBA9876543210ull * one + k, 0x80000000u + k, -k,
             (unsigned short)(0x8000 + k));
  for (int k = 0; k < 4; k++) {
    MSIL_EXPECT(a[k].c == 0x77);
    MSIL_EXPECT(a[k].u == 0xFEDCBA9876543210ull + k);
    // Zero extension of an unaligned uint32 to 64 bits.
    u64 w = a[k].u32;
    MSIL_EXPECT(w == 0x80000000ull + k);
    long long sc = a[k].sc;
    MSIL_EXPECT(sc == -k);
    unsigned u16 = a[k].u16;
    MSIL_EXPECT(u16 == 0x8000u + k);
  }
  MSIL_EXPECT(((const u8 *)a)[16 * 2 + 1] == 0x12);
  return 0;
}

// Unaligned loads and stores through the memcpy idiom.
static NOINLINE unsigned short load16(const u8 *p) {
  unsigned short v;
  __builtin_memcpy(&v, p, 2);
  return v;
}
static NOINLINE unsigned load32(const u8 *p) {
  unsigned v;
  __builtin_memcpy(&v, p, 4);
  return v;
}
static NOINLINE u64 load64(const u8 *p) {
  u64 v;
  __builtin_memcpy(&v, p, 8);
  return v;
}
static NOINLINE float loadf(const u8 *p) {
  float v;
  __builtin_memcpy(&v, p, 4);
  return v;
}
static NOINLINE double loadd(const u8 *p) {
  double v;
  __builtin_memcpy(&v, p, 8);
  return v;
}
static NOINLINE void store16(u8 *p, unsigned short v) {
  __builtin_memcpy(p, &v, 2);
}
static NOINLINE void store32(u8 *p, unsigned v) { __builtin_memcpy(p, &v, 4); }
static NOINLINE void store64(u8 *p, u64 v) { __builtin_memcpy(p, &v, 8); }
static NOINLINE void storef(u8 *p, float v) { __builtin_memcpy(p, &v, 4); }
static NOINLINE void stored(u8 *p, double v) { __builtin_memcpy(p, &v, 8); }

static NOINLINE int check_memcpy_idiom(int one) {
  u8 buf[32];
  for (int i = 0; i < 32; i++)
    buf[i] = (u8)(i + one);
  MSIL_EXPECT(load16(buf + 7) == 0x0908);
  MSIL_EXPECT(load32(buf + 1) == 0x05040302u);
  MSIL_EXPECT(load32(buf + 3) == 0x07060504u);
  MSIL_EXPECT(load64(buf + 5) == 0x0D0C0B0A09080706ull);

  for (int off = 0; off < 8; off++) {
    for (int i = 0; i < 32; i++)
      buf[i] = (u8)(0xC0 + one);
    store64(buf + 8 + off, 0x1122334455667788ull * one);
    MSIL_EXPECT(buf[8 + off - 1] == 0xC1 && buf[16 + off] == 0xC1);
    MSIL_EXPECT(buf[8 + off] == 0x88 && buf[15 + off] == 0x11);
    MSIL_EXPECT(load64(buf + 8 + off) == 0x1122334455667788ull);
    MSIL_EXPECT(load32(buf + 9 + off) == 0x44556677u);

    store32(buf + 8 + off, 0xDEADBEEFu * one);
    MSIL_EXPECT(buf[8 + off] == 0xEF && buf[11 + off] == 0xDE);
    MSIL_EXPECT(buf[12 + off] == 0x44 && buf[7 + off] == 0xC1);
    MSIL_EXPECT(load32(buf + 8 + off) == 0xDEADBEEFu);

    store16(buf + 1 + off, (unsigned short)(0xA55A * one));
    MSIL_EXPECT(buf[1 + off] == 0x5A && buf[2 + off] == 0xA5);
    MSIL_EXPECT(load16(buf + 1 + off) == 0xA55A);

    stored(buf + 16 + off, -0.1 * one);
    MSIL_EXPECT(loadd(buf + 16 + off) == -0.1);
    // -0.1 is 0xBFB999999999999A.
    MSIL_EXPECT(buf[16 + off] == 0x9A && buf[23 + off] == 0xBF);

    storef(buf + 3 + off, 3.25f * one);
    MSIL_EXPECT(loadf(buf + 3 + off) == 3.25f);
    // 3.25f is 0x40500000.
    MSIL_EXPECT(buf[5 + off] == 0x50 && buf[6 + off] == 0x40);
  }
  return 0;
}

static NOINLINE void vstore(volatile struct p1 *p, int i, long long ll,
                            double d) {
  p->i = i;
  p->ll = ll;
  p->d = d;
  p->s = (short)i;
}

static NOINLINE long long vsum(volatile struct p1 *p) {
  return p->i + p->ll + p->s + (long long)p->d;
}

static NOINLINE int check_volatile_packed(int one) {
  volatile struct p1 v;
  vstore(&v, -7 * one, 1000000000000ll * one, 0.5 * one);
  MSIL_EXPECT(v.i == -7 && v.ll == 1000000000000ll && v.d == 0.5);
  MSIL_EXPECT(vsum(&v) == 1000000000000ll - 14);
  v.us = (unsigned short)(65535 * one);
  v.ll += 5;
  MSIL_EXPECT(v.us == 65535 && v.ll == 1000000000005ll);
  return 0;
}

int main(int argc, char **argv) {
  (void)argv;
  TRY(check_p1(argc));
  TRY(check_p2(argc));
  TRY(check_p3(argc));
  TRY(check_memcpy_idiom(argc));
  TRY(check_volatile_packed(argc));
  return 0;
}

// Extending loads and truncating stores from C (Milestone 7): int8, uint8,
// int16 and uint16 loads widened to int32 (ldind.i1, ldind.u1, ldind.i2,
// ldind.u2) and to 64 bits, int32 and uint32 loads widened to 64 bits (an
// unsigned load of 0x80000000 widened to 64 bits must stay 0x80000000: in
// CIL 'ldind.u4; conv.i8' sign-extends, cil-runtime.md section 5), float32
// loads widened to float64, and stores that truncate int32 and 64-bit values
// to 8, 16 and 32 bits and float64 values to float32 (rounding to nearest,
// ties to even), each through a pointer in a noinline helper, at the
// boundary values of every width; the stored bytes and their neighbours are
// checked through an unsigned char view. Also loops that widen and narrow
// whole arrays. Values are derived from argc (1 when run without arguments),
// so clang cannot fold them at -O2. MSIL_EXPECT returns 1 + (line % 255) at
// the first failed check; [exit 0] means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))
#define TRY(call)                                                              \
  do {                                                                         \
    int r_ = (call);                                                           \
    if (r_ != 0)                                                               \
      return r_;                                                               \
  } while (0)

typedef unsigned char u8;
typedef unsigned long long u64;

static NOINLINE int ld_s8(const signed char *p) { return *p; }
static NOINLINE int ld_u8(const u8 *p) { return *p; }
static NOINLINE int ld_s16(const short *p) { return *p; }
static NOINLINE int ld_u16(const unsigned short *p) { return *p; }
static NOINLINE long long ld_s8_64(const signed char *p) { return *p; }
static NOINLINE u64 ld_u8_64(const u8 *p) { return *p; }
static NOINLINE long long ld_s16_64(const short *p) { return *p; }
static NOINLINE u64 ld_u16_64(const unsigned short *p) { return *p; }
static NOINLINE long long ld_s32_64(const int *p) { return *p; }
static NOINLINE u64 ld_u32_64(const unsigned *p) { return *p; }
static NOINLINE long long ld_u32_s64(const unsigned *p) {
  return (long long)*p;
}
static NOINLINE double ld_f32_64(const float *p) { return *p; }

static NOINLINE void st_8(u8 *p, int v) { *p = (u8)v; }
static NOINLINE void st_16(unsigned short *p, int v) { *p = (unsigned short)v; }
static NOINLINE void st_8_64(signed char *p, long long v) {
  *p = (signed char)v;
}
static NOINLINE void st_16_64(short *p, u64 v) { *p = (short)v; }
static NOINLINE void st_32_64(unsigned *p, long long v) { *p = (unsigned)v; }
static NOINLINE void st_f32_64(float *p, double v) { *p = (float)v; }

static NOINLINE int check_loads(int one) {
  signed char s8[3];
  u8 u8v[3];
  short s16[3];
  unsigned short u16[3];
  int s32[3];
  unsigned u32[3];
  float f[3];
  s8[0] = (signed char)(-128 * one);
  s8[1] = (signed char)(127 * one);
  s8[2] = (signed char)(-1 * one);
  u8v[0] = (u8)(255 * one);
  u8v[1] = (u8)(128 * one);
  u8v[2] = (u8)(127 * one);
  s16[0] = (short)(-32768 * one);
  s16[1] = (short)(32767 * one);
  s16[2] = (short)(-2 * one);
  u16[0] = (unsigned short)(65535 * one);
  u16[1] = (unsigned short)(32768 * one);
  u16[2] = (unsigned short)(1 * one);
  s32[0] = -2147483647 * one - 1;
  s32[1] = 2147483647 * one;
  s32[2] = -3 * one;
  u32[0] = 0x80000000u * one;
  u32[1] = 0xFFFFFFFFu * one;
  u32[2] = 0x7FFFFFFFu * one;
  f[0] = -0.1f * one;
  f[1] = 16777217.0f * one;
  f[2] = 0x1p-149f * one;

  MSIL_EXPECT(ld_s8(&s8[0]) == -128 && ld_s8(&s8[1]) == 127);
  MSIL_EXPECT(ld_s8(&s8[2]) == -1);
  MSIL_EXPECT(ld_u8(&u8v[0]) == 255 && ld_u8(&u8v[1]) == 128);
  MSIL_EXPECT(ld_s16(&s16[0]) == -32768 && ld_s16(&s16[1]) == 32767);
  MSIL_EXPECT(ld_s16(&s16[2]) == -2);
  MSIL_EXPECT(ld_u16(&u16[0]) == 65535 && ld_u16(&u16[1]) == 32768);

  MSIL_EXPECT(ld_s8_64(&s8[0]) == -128 && ld_s8_64(&s8[2]) == -1);
  MSIL_EXPECT(ld_u8_64(&u8v[0]) == 255 && ld_u8_64(&u8v[2]) == 127);
  MSIL_EXPECT(ld_s16_64(&s16[0]) == -32768 && ld_s16_64(&s16[1]) == 32767);
  MSIL_EXPECT(ld_u16_64(&u16[0]) == 65535 && ld_u16_64(&u16[2]) == 1);
  MSIL_EXPECT(ld_s32_64(&s32[0]) == -2147483647ll - 1);
  MSIL_EXPECT(ld_s32_64(&s32[1]) == 2147483647 && ld_s32_64(&s32[2]) == -3);
  MSIL_EXPECT(ld_u32_64(&u32[0]) == 0x80000000ull);
  MSIL_EXPECT(ld_u32_64(&u32[1]) == 0xFFFFFFFFull);
  MSIL_EXPECT(ld_u32_64(&u32[2]) == 0x7FFFFFFFull);
  MSIL_EXPECT(ld_u32_s64(&u32[1]) == 4294967295ll);
  // The widened values compare as 64-bit numbers.
  MSIL_EXPECT(ld_u32_64(&u32[0]) > 0x7FFFFFFFull);
  MSIL_EXPECT(ld_u32_s64(&u32[0]) > 0 && ld_s32_64(&s32[0]) < 0);

  MSIL_EXPECT(ld_f32_64(&f[0]) == (double)-0.1f);
  MSIL_EXPECT(ld_f32_64(&f[0]) != -0.1);
  MSIL_EXPECT(ld_f32_64(&f[1]) == 16777216.0);
  MSIL_EXPECT(ld_f32_64(&f[2]) == 0x1p-149);
  return 0;
}

static NOINLINE int all_byte(const u8 *p, int n, u8 c) {
  for (int i = 0; i < n; i++)
    if (p[i] != c)
      return 0;
  return 1;
}

static NOINLINE int check_stores(int one) {
  u8 b[8];
  for (int i = 0; i < 8; i++)
    b[i] = 0xEE;
  st_8(&b[3], 0x1FF80 * one);
  MSIL_EXPECT(b[3] == 0x80 && all_byte(b, 3, 0xEE) && all_byte(b + 4, 4, 0xEE));
  st_8(&b[3], -1 * one);
  MSIL_EXPECT(b[3] == 0xFF && b[2] == 0xEE && b[4] == 0xEE);

  unsigned short h[4];
  for (int i = 0; i < 4; i++)
    h[i] = 0xEEEE;
  st_16(&h[1], 0x7654ABCD * one);
  MSIL_EXPECT(h[1] == 0xABCD && h[0] == 0xEEEE && h[2] == 0xEEEE);
  const u8 *hb = (const u8 *)h;
  MSIL_EXPECT(hb[2] == 0xCD && hb[3] == 0xAB && hb[1] == 0xEE && hb[4] == 0xEE);

  signed char c[4];
  for (int i = 0; i < 4; i++)
    c[i] = 0x11;
  st_8_64(&c[2], -0x123456789ABCD80ll * one);
  MSIL_EXPECT(c[2] == -128 && c[1] == 0x11 && c[3] == 0x11);
  st_8_64(&c[1], 0x7FFFFFFFFFFFFF7Fll * one);
  MSIL_EXPECT(c[1] == 127);

  short s[4];
  for (int i = 0; i < 4; i++)
    s[i] = 0x1111;
  st_16_64(&s[2], 0xFFFFFFFF00018000ull * one);
  MSIL_EXPECT(s[2] == -32768 && s[1] == 0x1111 && s[3] == 0x1111);

  unsigned w[4];
  for (int i = 0; i < 4; i++)
    w[i] = 0x11111111u;
  st_32_64(&w[1], -0x100000001ll * one);
  MSIL_EXPECT(w[1] == 0xFFFFFFFFu && w[0] == 0x11111111u &&
              w[2] == 0x11111111u);
  st_32_64(&w[2], 0x123456789ll * one);
  MSIL_EXPECT(w[2] == 0x23456789u && w[3] == 0x11111111u);
  const u8 *wb = (const u8 *)w;
  MSIL_EXPECT(wb[8] == 0x89 && wb[11] == 0x23 && wb[12] == 0x11);

  float f[3];
  for (int i = 0; i < 3; i++)
    f[i] = 7.0f;
  st_f32_64(&f[1], 0.1 * one);
  MSIL_EXPECT(f[1] == 0.1f && f[0] == 7.0f && f[2] == 7.0f);
  st_f32_64(&f[1], 16777217.0 * one);
  MSIL_EXPECT(f[1] == 16777216.0f);
  // Halfway cases round to even.
  st_f32_64(&f[1], (1.0 + 0x1p-24) * one);
  MSIL_EXPECT(f[1] == 1.0f);
  st_f32_64(&f[1], (-1.0 - 0x3p-24) * one);
  MSIL_EXPECT(f[1] == -1.0f - 0x1p-22f);
  return 0;
}

static NOINLINE void widen(long long *d, const signed char *s, int n) {
  for (int i = 0; i < n; i++)
    d[i] = s[i];
}
static NOINLINE void widen_u(u64 *d, const unsigned *s, int n) {
  for (int i = 0; i < n; i++)
    d[i] = s[i];
}
static NOINLINE void narrow(short *d, const long long *s, int n) {
  for (int i = 0; i < n; i++)
    d[i] = (short)s[i];
}

static NOINLINE int check_arrays(int one) {
  signed char s8[40];
  unsigned u32[40];
  long long l[40];
  u64 ul[40];
  short h[40];
  for (int i = 0; i < 40; i++) {
    s8[i] = (signed char)(i * 7 * one - 140);
    u32[i] = 0xFFFFFF00u + (unsigned)i * 7 * one;
  }
  widen(l, s8, 40);
  widen_u(ul, u32, 40);
  for (int i = 0; i < 40; i++) {
    MSIL_EXPECT(l[i] == (signed char)(i * 7 - 140));
    MSIL_EXPECT(ul[i] == (u64)(0xFFFFFF00u + (unsigned)i * 7));
    l[i] = (long long)i * 0x10001 * one - 0x8000;
  }
  narrow(h, l, 40);
  for (int i = 0; i < 40; i++)
    MSIL_EXPECT(h[i] == (short)(i * 0x10001 - 0x8000));
  return 0;
}

int main(int argc, char **argv) {
  (void)argv;
  TRY(check_loads(argc));
  TRY(check_stores(argc));
  TRY(check_arrays(argc));
  return 0;
}

// Bit-manipulation builtins from C (Milestone 7, D16: LLVM's generic
// integer expansions, no BCL intrinsics): __builtin_popcount{,ll,g},
// __builtin_parity{,ll}, __builtin_clz{,ll}, __builtin_ctz{,ll} (nonzero
// inputs), __builtin_clzg and __builtin_ctzg with a fallback for zero on
// 8-, 16-, 32- and 64-bit types, __builtin_ffs{,ll}, __builtin_clrsb{,ll},
// __builtin_bswap16/32/64, __builtin_bitreverse8/16/32/64,
// __builtin_rotateleft and __builtin_rotateright on 8, 16, 32 and 64 bits
// with any amount (taken modulo the width), and the C rotate idiom
// (x << s) | (x >> (-s & (w - 1))). Each result is compared with a plain
// bit-by-bit reference loop for 16 edge values (0, 1, single high bits, all
// ones, bit 32 alone, ...) and 400 pseudo-random values of varying
// magnitude, and a few with constants. The values are derived from argc (1
// when run without arguments) and reach the builtins through a noinline
// function, so clang cannot fold them at -O2. MSIL_EXPECT returns
// 1 + (line % 255) at the first failed check; [exit 0] means every check
// passed.
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
typedef unsigned short u16;
typedef unsigned u32;
typedef unsigned long long u64;

static u64 mask(int w) { return w == 64 ? ~0ull : (1ull << w) - 1; }

static NOINLINE int ref_popcount(u64 x) {
  int n = 0;
  for (; x; x >>= 1)
    n += (int)(x & 1);
  return n;
}

// Leading zeros of the low w bits of x (w for 0).
static NOINLINE int ref_clz(u64 x, int w) {
  int n = 0;
  for (int b = w - 1; b >= 0 && !((x >> b) & 1); b--)
    n++;
  return n;
}

// Trailing zeros of the low w bits of x (w for 0).
static NOINLINE int ref_ctz(u64 x, int w) {
  int n = 0;
  while (n < w && !((x >> n) & 1))
    n++;
  return n;
}

// Leading bits equal to the sign bit of the low w bits, minus one.
static NOINLINE int ref_clrsb(u64 x, int w) {
  u64 m = mask(w);
  x &= m;
  if ((x >> (w - 1)) & 1)
    x = ~x & m;
  return ref_clz(x, w) - 1;
}

static NOINLINE u64 ref_bswap(u64 x, int w) {
  u64 r = 0;
  for (int i = 0; i < w / 8; i++)
    r = r << 8 | ((x >> (8 * i)) & 0xFF);
  return r;
}

static NOINLINE u64 ref_bitreverse(u64 x, int w) {
  u64 r = 0;
  for (int i = 0; i < w; i++)
    r = r << 1 | ((x >> i) & 1);
  return r;
}

// Rotates the low w bits of x left by r modulo w.
static NOINLINE u64 ref_rotl(u64 x, unsigned r, int w) {
  u64 m = mask(w);
  x &= m;
  r %= (unsigned)w;
  if (r == 0)
    return x;
  return ((x << r) | (x >> (w - r))) & m;
}

static NOINLINE int check_value(u64 x, unsigned r) {
  u32 x32 = (u32)x;
  u16 x16 = (u16)x;
  u8 x8 = (u8)x;

  MSIL_EXPECT(__builtin_popcountll(x) == ref_popcount(x));
  MSIL_EXPECT(__builtin_popcount(x32) == ref_popcount(x32));
  MSIL_EXPECT(__builtin_popcountg(x16) == ref_popcount(x16));
  MSIL_EXPECT(__builtin_popcountg(x8) == ref_popcount(x8));
  MSIL_EXPECT(__builtin_parityll(x) == (ref_popcount(x) & 1));
  MSIL_EXPECT(__builtin_parity(x32) == (ref_popcount(x32) & 1));

  if (x != 0) {
    MSIL_EXPECT(__builtin_clzll(x) == ref_clz(x, 64));
    MSIL_EXPECT(__builtin_ctzll(x) == ref_ctz(x, 64));
  }
  if (x32 != 0) {
    MSIL_EXPECT(__builtin_clz(x32) == ref_clz(x32, 32));
    MSIL_EXPECT(__builtin_ctz(x32) == ref_ctz(x32, 32));
  }
  MSIL_EXPECT(__builtin_clzg(x, 64) == ref_clz(x, 64));
  MSIL_EXPECT(__builtin_ctzg(x, 64) == ref_ctz(x, 64));
  MSIL_EXPECT(__builtin_clzg(x32, -1) == (x32 ? ref_clz(x32, 32) : -1));
  MSIL_EXPECT(__builtin_ctzg(x32, 32) == ref_ctz(x32, 32));
  MSIL_EXPECT(__builtin_clzg(x16, 16) == ref_clz(x16, 16));
  MSIL_EXPECT(__builtin_ctzg(x16, 16) == ref_ctz(x16, 16));
  MSIL_EXPECT(__builtin_clzg(x8, 8) == ref_clz(x8, 8));
  MSIL_EXPECT(__builtin_ctzg(x8, 99) == (x8 ? ref_ctz(x8, 8) : 99));
  MSIL_EXPECT(__builtin_ffsll((long long)x) == (x ? ref_ctz(x, 64) + 1 : 0));
  MSIL_EXPECT(__builtin_ffs((int)x32) == (x32 ? ref_ctz(x32, 32) + 1 : 0));
  MSIL_EXPECT(__builtin_clrsbll((long long)x) == ref_clrsb(x, 64));
  MSIL_EXPECT(__builtin_clrsb((int)x32) == ref_clrsb(x32, 32));

  MSIL_EXPECT(__builtin_bswap64(x) == ref_bswap(x, 64));
  MSIL_EXPECT(__builtin_bswap32(x32) == ref_bswap(x32, 32));
  MSIL_EXPECT(__builtin_bswap16(x16) == ref_bswap(x16, 16));
  MSIL_EXPECT(__builtin_bitreverse64(x) == ref_bitreverse(x, 64));
  MSIL_EXPECT(__builtin_bitreverse32(x32) == ref_bitreverse(x32, 32));
  MSIL_EXPECT(__builtin_bitreverse16(x16) == ref_bitreverse(x16, 16));
  MSIL_EXPECT(__builtin_bitreverse8(x8) == ref_bitreverse(x8, 8));

  MSIL_EXPECT(__builtin_rotateleft64(x, r) == ref_rotl(x, r, 64));
  MSIL_EXPECT(__builtin_rotateright64(x, r) == ref_rotl(x, 64 - r % 64, 64));
  MSIL_EXPECT(__builtin_rotateleft32(x32, r) == ref_rotl(x32, r, 32));
  MSIL_EXPECT(__builtin_rotateright32(x32, r) ==
              ref_rotl(x32, 32 - r % 32, 32));
  MSIL_EXPECT(__builtin_rotateleft16(x16, (u16)r) == ref_rotl(x16, (u16)r, 16));
  MSIL_EXPECT(__builtin_rotateright16(x16, (u16)r) ==
              ref_rotl(x16, 16 - (u16)r % 16, 16));
  MSIL_EXPECT(__builtin_rotateleft8(x8, (u8)r) == ref_rotl(x8, (u8)r, 8));
  MSIL_EXPECT(__builtin_rotateright8(x8, (u8)r) ==
              ref_rotl(x8, 8 - (u8)r % 8, 8));

  unsigned s32 = r & 31;
  MSIL_EXPECT(((x32 << s32) | (x32 >> (-s32 & 31))) == ref_rotl(x32, r, 32));
  unsigned s64 = r & 63;
  MSIL_EXPECT(((x << s64) | (x >> (-s64 & 63))) == ref_rotl(x, r, 64));
  return 0;
}

static NOINLINE int check_constants(int one) {
  MSIL_EXPECT(__builtin_bswap32(0x12345678u * one) == 0x78563412u);
  MSIL_EXPECT(__builtin_bswap64(0x0102030405060708ull * one) ==
              0x0807060504030201ull);
  MSIL_EXPECT(__builtin_bswap16((u16)(0xABCD * one)) == 0xCDAB);
  MSIL_EXPECT(__builtin_popcount(0xF0F0F0F0u * one) == 16);
  MSIL_EXPECT(__builtin_popcountll(~0ull * one) == 64);
  MSIL_EXPECT(__builtin_clz(1u * one) == 31);
  MSIL_EXPECT(__builtin_clzll(0x100000000ull * one) == 31);
  MSIL_EXPECT(__builtin_ctz(0x80000000u * one) == 31);
  MSIL_EXPECT(__builtin_ctzll(0x100000000ull * one) == 32);
  MSIL_EXPECT(__builtin_rotateleft32(0x80000001u * one, 1) == 3);
  MSIL_EXPECT(__builtin_rotateright64(1ull * one, 1) == 0x8000000000000000ull);
  MSIL_EXPECT(__builtin_bitreverse32(1u * one) == 0x80000000u);
  MSIL_EXPECT(__builtin_bitreverse64(0x00000000000000F1ull * one) ==
              0x8F00000000000000ull);
  return 0;
}

int main(int argc, char **argv) {
  (void)argv;
  int one = argc;
  TRY(check_constants(one));

  u64 edges[16];
  edges[0] = 0;
  edges[1] = 1;
  edges[2] = 0x80;
  edges[3] = 0x8000;
  edges[4] = 0x80000000ull;
  edges[5] = 0xFFFFFFFFull;
  edges[6] = 0x100000000ull;
  edges[7] = 0x8000000000000000ull;
  edges[8] = ~0ull;
  edges[9] = 0x0123456789ABCDEFull;
  edges[10] = 0xFEDCBA9876543210ull;
  edges[11] = 0x00000001FFFFFFFEull;
  edges[12] = 0x7FFFFFFFFFFFFFFFull;
  edges[13] = 0xFFFFFFFF00000000ull;
  edges[14] = 0x5555555555555555ull;
  edges[15] = 0x00FF00FF00FF8001ull;
  for (int i = 0; i < 16; i++)
    for (unsigned r = 0; r < 140; r += 9)
      TRY(check_value(edges[i] * one, r * one));

  // A 64-bit LCG; each value is shifted right by a pseudo-random amount, so
  // its leading-zero count varies over 0..63.
  u64 seed = 0x9E3779B97F4A7C15ull * one;
  for (int i = 0; i < 400; i++) {
    seed = seed * 6364136223846793005ull + 1442695040888963407ull;
    u64 x = seed >> (seed >> 58);
    TRY(check_value(x, (unsigned)(seed >> 32) % 200));
  }
  return 0;
}

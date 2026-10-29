// Conversions between float/double and __int128 (Milestone 12, plan Q36 and
// the section 12 row 'D14/D33 note and P3-B text'): MSILTargetLowering sets
// setMaxLargeFPConvertBitWidthSupported(64), so ExpandIRInsts expands the
// eight plain conversions inline (fptosi and fptoui to i128 from float and
// double, sitofp and uitofp from i128 to float and double) and no compiler-rt
// helper (__fixsfti, __fixdfti, __fixunssfti, __fixunsdfti, __floattisf,
// __floattidf, __floatuntisf, __floatuntidf) is called; before M12 each was a
// Phase-3 helper error. The native oracle calls the helpers (clang_rt.builtins
// on Windows, libgcc_s.so.1 under the E7 Linux oracle, plan 5.12.8). The
// inputs: exact values (small integers, powers of two up to 2^127, values with
// 24 and 53 significant bits), integers that round to nearest with ties to
// even (exactly at a halfway point next to an even and next to an odd
// significand, and one above it), values next to 2^127 and 2^128, negative
// values, both zeros, and fractions, which truncate toward zero. Every input
// is in range: FP values truncate into the range of the integer type (for
// unsigned, (-1, 2^128); C11 6.3.1.4), and no integer converted to float
// exceeds FLT_MAX. Inputs come from tables and pass through noinline helpers,
// so neither clang nor llc can fold the conversions at -O2; every result is
// printed as its bit pattern (32 hex digits for the integers, 8 for float, 16
// for double), next to the input's bit pattern.
// ExpandIRInsts also expands __bf16 to __int128 and to unsigned __int128
// inline, on the value's 16-bit pattern (the section 12 row of 2026-10-21 that
// corrects 'D14/D33 note and P3-B text'), where the native build calls
// __fixsfti and __fixunssfti. A __bf16 parameter needed the __truncsfbf2
// helper on MSIL until the upstream merge at 1838cefa99c0 (upstream
// 5110131a330f), so b2i and b2u take a pointer, and main fills each __bf16
// from its bit pattern (4 hex digits) with __builtin_memcpy.
// Their inputs: exact values, fractions, both zeros, powers of two up to
// 2^127, the largest finite __bf16 values below 2^127 and below 2^128, and
// -2^127, each in range.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

typedef unsigned __int128 u128;
typedef __int128 i128;
typedef unsigned long long u64;

#define U128(hi, lo) (((u128)(u64)(hi) << 64) | (u128)(u64)(lo))

NOINLINE i128 f2i(float f) { return (i128)f; }
NOINLINE i128 d2i(double d) { return (i128)d; }
NOINLINE u128 f2u(float f) { return (u128)f; }
NOINLINE u128 d2u(double d) { return (u128)d; }
NOINLINE float i2f(i128 v) { return (float)v; }
NOINLINE double i2d(i128 v) { return (double)v; }
NOINLINE float u2f(u128 v) { return (float)v; }
NOINLINE double u2d(u128 v) { return (double)v; }
NOINLINE i128 b2i(const __bf16 *p) { return (i128)*p; }
NOINLINE u128 b2u(const __bf16 *p) { return (u128)*p; }

static const char digits[] = "0123456789abcdef";

// Appends the low n hex digits of v to p and returns the new end.
static char *hex(char *p, u128 v, int n) {
  for (int i = n - 1; i >= 0; i--)
    *p++ = digits[(unsigned)(v >> (4 * i)) & 15];
  return p;
}

// "<name> <input bits> -> <result bits>".
static void put_conv(const char *name, u128 in, int in_digits, u128 out,
                     int out_digits) {
  char buf[96];
  char *p = buf;
  while (*name)
    *p++ = *name++;
  *p++ = ' ';
  p = hex(p, in, in_digits);
  *p++ = ' ';
  *p++ = '-';
  *p++ = '>';
  *p++ = ' ';
  p = hex(p, out, out_digits);
  *p = 0;
  puts(buf);
}

static u64 bits_d(double d) {
  union {
    double d;
    u64 u;
  } x;
  x.d = d;
  return x.u;
}

static unsigned bits_f(float f) {
  union {
    float f;
    unsigned u;
  } x;
  x.f = f;
  return x.u;
}

// To signed __int128: truncation toward zero, the extremes -2^127 and the
// largest values below 2^127.
static const double to_i128_d[] = {
    0.0,
    -0.0,
    1.0,
    -1.0,
    0.5,
    -0.75,
    1.9999999999999998,
    -2.5,
    123456789.987654321,
    9007199254740991.0,            // 2^53 - 1
    -9007199254740993.5,           // rounded at compile time to -2^53 - 2
    0x1p63,
    0x1p64,
    -0x1.8p64,
    0x1.23456789abcdep90,
    -0x1.fedcba9876543p100,
    1e30,
    -1e30,
    0x1.fffffffffffffp126,         // 2^127 - 2^74
    -0x1.fffffffffffffp126,
    -0x1p127,                      // INT128_MIN
};

static const float to_i128_f[] = {
    0.0f,          -0.0f,        1.0f,           -1.0f,
    0.25f,         -0.999f,      16777215.0f,    -16777216.0f,
    0x1.8p40f,     0x1p64f,      -0x1.abcdeep80f, 1e30f,
    -3.4e37f,      0x1.fffffep126f, /* 2^127 - 2^103 */
    -0x1p127f,     /* INT128_MIN */
};

// To unsigned __int128: (-1, 2^128).
static const double to_u128_d[] = {
    0.0,
    -0.0,
    -0.75,
    0.999,
    3.5,
    4294967296.25,
    0x1p64,
    0x1.0000000000001p64,
    0x1p127,
    0x1.8p127,
    1e38,
    0x1.fffffffffffffp127,         // 2^128 - 2^75
};

static const float to_u128_f[] = {
    0.0f,        -0.5f,        7.75f,          1e20f,
    0x1p63f,     0x1p64f,      0x1.000002p64f, 0x1p127f,
    0x1.fffffep127f, /* FLT_MAX = 2^128 - 2^104 */
};

// From signed __int128 (hi, lo): around the halfway points of float (24-bit
// significand) and double (53-bit significand).
static const u64 from_i128[][2] = {
    {0, 0},
    {0, 1},
    {~0ull, ~0ull},                               // -1
    {0, 0x1000001},                               // 2^24 + 1: tie, even down
    {0, 0x1000003},                               // 2^24 + 3: tie, even up
    {~0ull, (u64)-0x1000001ll},                   // -(2^24 + 1)
    {0, 0x20000000000001ull},                     // 2^53 + 1: tie, even down
    {0, 0x20000000000003ull},                     // 2^53 + 3: tie, even up
    {0, 0x20000000000002ull},                     // 2^53 + 2: exact
    {0x1000000000ull, 0x0000800000000000ull},     // 2^100 + 2^47: tie (double)
    {0x1000000000ull, 0x0001800000000000ull},     // 2^100 + 3 * 2^47
    {0x1000000000ull, 0x0000800000000001ull},     // 2^100 + 2^47 + 1
    {0x1000000000ull, 0x0000000000001000ull},     // 2^100 + 2^12
    {0x1000000000ull, 0},                         // 2^100
    {0x1000001000ull, 0},                         // 2^100 + 2^76: tie (float)
    {0x1000003000ull, 0},                         // 2^100 + 3 * 2^76
    {0x1000001000ull, 1},                         // 2^100 + 2^76 + 1
    {0x1000002000ull, 0},                         // 2^100 + 2^77: exact (float)
    {0xFFFFFFF000000000ull, 0},                   // -2^100
    {0xFFFFFFEFFFFF8000ull, 0},                   // -(2^100 + 2^79)
    {0xFFFFFFEFFFFFFFFFull, 0xFFFF800000000000ull}, // -(2^100 + 2^47)
    {0x0123456789ABCDEFull, 0xFEDCBA9876543210ull},
    {0x7FFFFFFFFFFFFFFFull, ~0ull},               // INT128_MAX
    {0x7FFFFFFFFFFFFC00ull, 0},                   // 2^127 - 2^74
    {0x7FFFFFFFFFFFFE00ull, 0},                   // 2^127 - 2^73: tie
    {0x8000000000000000ull, 0},                   // INT128_MIN
    {0x8000000000000000ull, 1},                   // INT128_MIN + 1
};

// From unsigned __int128 (hi, lo): float inputs stay at or below FLT_MAX.
static const u64 from_u128[][2] = {
    {0, 0},
    {0, 1},
    {0, ~0ull},                                   // 2^64 - 1
    {1, 1},                                       // 2^64 + 1
    {0, 0x20000000000001ull},                     // 2^53 + 1
    {0x8000000000000000ull, 0},                   // 2^127
    {0x8000000000000400ull, 0},                   // 2^127 + 2^74: tie (double)
    {0x8000000000000C00ull, 0},                   // 2^127 + 3 * 2^74
    {0x8000000000000400ull, 1},                   // 2^127 + 2^74 + 1
    {0x8000008000000000ull, 0},                   // 2^127 + 2^103: tie (float)
    {0x8000018000000000ull, 0},                   // 2^127 + 3 * 2^103
    {0x8000008000000000ull, 1},                   // 2^127 + 2^103 + 1
    {0xFFFFFF0000000000ull, 0},                   // FLT_MAX
    {0xFFFFFEC000000000ull, 0},                   // FLT_MAX - 2^102
    {0x0123456789ABCDEFull, 0xFEDCBA9876543210ull},
};

// The u128 inputs above FLT_MAX convert to double only.
static const u64 from_u128_double_only[][2] = {
    {0xFFFFFFFFFFFFF800ull, 0},                   // 2^128 - 2^75
    {0xFFFFFFFFFFFFFC00ull, 0},                   // 2^128 - 2^74: tie, up
    {0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull}, // 2^128 - 1
};

// __bf16 bit patterns (sign, 8 exponent bits, 7 significand bits) to signed
// __int128: (-2^127 - 1, 2^127).
static const unsigned short to_i128_bf[] = {
    0x0000, // 0
    0x8000, // -0.0
    0x3F80, // 1
    0xBF80, // -1
    0x3F00, // 0.5
    0xBF00, // -0.5
    0x4049, // 3.140625
    0xC049, // -3.140625
    0x42F7, // 123.5
    0xC2F7, // -123.5
    0x4B80, // 2^24
    0x5F00, // 2^63
    0x5F80, // 2^64
    0x6000, // 2^65
    0x7EFF, // 2^126 + 127 * 2^119, the largest __bf16 below 2^127
    0xFEFF, // -(2^126 + 127 * 2^119)
    0xFF00, // -2^127 = INT128_MIN
};

// To unsigned __int128: (-1, 2^128).
static const unsigned short to_u128_bf[] = {
    0x0000, // 0
    0x8000, // -0.0
    0xBF00, // -0.5
    0x3F80, // 1
    0x3F00, // 0.5
    0x4049, // 3.140625
    0x42F7, // 123.5
    0x4B80, // 2^24
    0x5F00, // 2^63
    0x6000, // 2^65
    0x7EFF, // 2^126 + 127 * 2^119
    0x7F00, // 2^127
    0x7F7F, // 2^127 + 127 * 2^120, the largest finite __bf16
};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

int main(void) {
  for (int i = 0; i < COUNT(to_i128_d); i++)
    put_conv("d2i", bits_d(to_i128_d[i]), 16, (u128)d2i(to_i128_d[i]), 32);
  for (int i = 0; i < COUNT(to_i128_f); i++)
    put_conv("f2i", bits_f(to_i128_f[i]), 8, (u128)f2i(to_i128_f[i]), 32);
  for (int i = 0; i < COUNT(to_u128_d); i++)
    put_conv("d2u", bits_d(to_u128_d[i]), 16, d2u(to_u128_d[i]), 32);
  for (int i = 0; i < COUNT(to_u128_f); i++)
    put_conv("f2u", bits_f(to_u128_f[i]), 8, f2u(to_u128_f[i]), 32);
  for (int i = 0; i < COUNT(from_i128); i++) {
    u128 v = U128(from_i128[i][0], from_i128[i][1]);
    put_conv("i2f", v, 32, bits_f(i2f((i128)v)), 8);
    put_conv("i2d", v, 32, bits_d(i2d((i128)v)), 16);
  }
  for (int i = 0; i < COUNT(from_u128); i++) {
    u128 v = U128(from_u128[i][0], from_u128[i][1]);
    put_conv("u2f", v, 32, bits_f(u2f(v)), 8);
    put_conv("u2d", v, 32, bits_d(u2d(v)), 16);
  }
  for (int i = 0; i < COUNT(from_u128_double_only); i++) {
    u128 v = U128(from_u128_double_only[i][0], from_u128_double_only[i][1]);
    put_conv("u2d", v, 32, bits_d(u2d(v)), 16);
  }
  for (int i = 0; i < COUNT(to_i128_bf); i++) {
    __bf16 b;
    __builtin_memcpy(&b, &to_i128_bf[i], sizeof(b));
    put_conv("b2i", to_i128_bf[i], 4, (u128)b2i(&b), 32);
  }
  for (int i = 0; i < COUNT(to_u128_bf); i++) {
    __bf16 b;
    __builtin_memcpy(&b, &to_u128_bf[i], sizeof(b));
    put_conv("b2u", to_u128_bf[i], 4, b2u(&b), 32);
  }
  return 0;
}

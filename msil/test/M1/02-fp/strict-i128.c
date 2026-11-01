// Strict conversions between __int128 and float/double (Milestone 24): under
// '#pragma STDC FENV_ACCESS ON' clang emits the constrained intrinsics
// llvm.experimental.constrained.sitofp and uitofp from i128 and fptosi and
// fptoui to i128 instead of the plain instructions that i128-fp-conv.c
// tests. Before M24 they became calls of the compiler-rt helpers
// (__floattisf, __floattidf, __floatuntisf, __floatuntidf, __fixsfti,
// __fixdfti, __fixunssfti, __fixunsdfti), 'runtime helper ... is not
// available' errors in -S for all eight and in -c for the four to i128; from
// M24 MSIL::relaxStrictInt128Conversions makes them plain at slot P0 (plan
// 5.13.6), before ExpandIRInsts, which then expands them inline as it expands
// the plain ones (CIL has one rounding mode and no FP exceptions). The native
// oracle calls the helpers (clang_rt.builtins on Windows, libgcc_s.so.1 under
// the E7 Linux oracle). Every input is m * 2^e, in range for its integer
// type, with m below 2^24 (the tables exact in float and double) or with 25
// to 53 significant bits (the tables exact in double only), so no conversion
// rounds and the results are the same in every rounding mode. Each input goes
// from __int128 or unsigned __int128 to every FP type it is exact in and back
// through noinline helpers, one per conversion, so neither clang nor llc can
// fold them at -O2. The bit patterns of the FP results and the two halves of
// the integer results are printed with put_hex, and each round trip must give
// the input back: the exit status has one bit per table and FP type whose
// round trip failed (0 = all passed). The pragma comes first, so every
// function of the file is strictfp.
// RUN: %{msil_c}
#pragma STDC FENV_ACCESS ON
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

typedef __int128 i128;
typedef unsigned __int128 u128;
typedef unsigned long long u64;

NOINLINE float i2f(i128 v) { return (float)v; }
NOINLINE double i2d(i128 v) { return (double)v; }
NOINLINE float u2f(u128 v) { return (float)v; }
NOINLINE double u2d(u128 v) { return (double)v; }
NOINLINE i128 f2i(float f) { return (i128)f; }
NOINLINE i128 d2i(double d) { return (i128)d; }
NOINLINE u128 f2u(float f) { return (u128)f; }
NOINLINE u128 d2u(double d) { return (u128)d; }

static unsigned bits_f(float f) {
  unsigned u;
  __builtin_memcpy(&u, &f, sizeof(u));
  return u;
}

static u64 bits_d(double d) {
  u64 u;
  __builtin_memcpy(&u, &d, sizeof(u));
  return u;
}

// The high half, then the low half.
static void put_u128(u128 v) {
  put_hex((u64)(v >> 64));
  put_hex((u64)v);
}

// m * 2^e (POS) and -(m * 2^e) (NEG) as {high 64 bits, low 64 bits}.
#define POS(m, e) {(u64)(((u128)(m) << (e)) >> 64), (u64)((u128)(m) << (e))}
#define NEG(m, e)                                                              \
  {(u64)((0 - ((u128)(m) << (e))) >> 64), (u64)(0 - ((u128)(m) << (e)))}
#define U128(p) (((u128)(p)[0] << 64) | (u128)(p)[1])
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

// Signed, exact in float and double: small values, 2^24, -2^31, the largest
// float below 2^64, +-2^63, 2^64, 24-bit significands across the two halves,
// +-2^100, +-(the largest float below 2^127) and INT128_MIN.
static const u64 i128_fd[][2] = {
    POS(0, 0),          POS(1, 0),          NEG(1, 0),
    POS(2, 0),          NEG(3, 0),          POS(0xff, 0),
    POS(0xabcdef, 0),   NEG(0xabcdef, 0),   POS(0xffffff, 0),
    POS(1, 24),         NEG(1, 31),         POS(0xffffff, 40),
    POS(1, 63),         NEG(1, 63),         POS(1, 64),
    POS(0xffffff, 52),  NEG(0xffffff, 52),  POS(0x800001, 60),
    POS(1, 100),        NEG(1, 100),        POS(0xabcdef, 100),
    POS(0xffffff, 103), NEG(0xffffff, 103), NEG(1, 127),
};

// Signed, exact in double only (25 to 53 significant bits): +-(2^24 + 1),
// 2^32 - 1, +-(2^53 - 1), the largest double below 2^64, 53-bit significands
// across the two halves and in the high half, and +-(the largest double below
// 2^127).
static const u64 i128_d[][2] = {
    POS(0x1000001, 0),         NEG(0x1000001, 0),
    POS(0xffffffff, 0),        POS(0x1fffffffffffff, 0),
    NEG(0x1fffffffffffff, 0),  POS(0x1fffffffffffff, 11),
    POS(0x1fffffffffffff, 40), NEG(0x1fffffffffffff, 40),
    POS(0x10000000000001, 64), POS(0x123456789abcd, 70),
    POS(0x1fffffffffffff, 74), NEG(0x1fffffffffffff, 74),
};

// Unsigned, exact in float and double: small values, the largest float below
// 2^64, 2^64, 24-bit significands across the two halves, 2^126, then 2^127
// and above (the top bit set) up to FLT_MAX = 2^128 - 2^104.
static const u64 u128_fd[][2] = {
    POS(0, 0),          POS(1, 0),          POS(0xffffff, 0),
    POS(1, 32),         POS(0xffffff, 40),  POS(1, 64),
    POS(0xffffff, 52),  POS(1, 126),        POS(1, 127),
    POS(0x800001, 104), POS(0xabcdef, 104), POS(0xfffffe, 104),
    POS(0xffffff, 104),
};

// Unsigned, exact in double only: 2^53 + 2, 2^53 - 1, the largest double
// below 2^64, 53-bit significands across the two halves and in the high half,
// then 2^127 and above up to 2^128 - 2^75, the largest double below 2^128.
static const u64 u128_d[][2] = {
    POS(0x20000000000002, 0),  POS(0x1fffffffffffff, 0),
    POS(0x1fffffffffffff, 11), POS(0x1fffffffffffff, 40),
    POS(0x1fffffffffffff, 64), POS(0x123456789abcd, 79),
    POS(0x10000000000001, 75), POS(0x1fffffffffffff, 75),
};

int main(void) {
  unsigned acc = 0;
  put_str("i128 <-> float, double");
  for (int i = 0; i < COUNT(i128_fd); i++) {
    i128 v = (i128)U128(i128_fd[i]);
    float f = i2f(v);
    double d = i2d(v);
    i128 vf = f2i(f);
    i128 vd = d2i(d);
    put_hex(bits_f(f));
    put_hex(bits_d(d));
    put_u128((u128)vf);
    put_u128((u128)vd);
    MSIL_CHECK(acc, 0, vf == v);
    MSIL_CHECK(acc, 1, vd == v);
  }
  put_str("i128 <-> double");
  for (int i = 0; i < COUNT(i128_d); i++) {
    i128 v = (i128)U128(i128_d[i]);
    double d = i2d(v);
    i128 vd = d2i(d);
    put_hex(bits_d(d));
    put_u128((u128)vd);
    MSIL_CHECK(acc, 2, vd == v);
  }
  put_str("u128 <-> float, double");
  for (int i = 0; i < COUNT(u128_fd); i++) {
    u128 v = U128(u128_fd[i]);
    float f = u2f(v);
    double d = u2d(v);
    u128 vf = f2u(f);
    u128 vd = d2u(d);
    put_hex(bits_f(f));
    put_hex(bits_d(d));
    put_u128(vf);
    put_u128(vd);
    MSIL_CHECK(acc, 3, vf == v);
    MSIL_CHECK(acc, 4, vd == v);
  }
  put_str("u128 <-> double");
  for (int i = 0; i < COUNT(u128_d); i++) {
    u128 v = U128(u128_d[i]);
    double d = u2d(v);
    u128 vd = d2u(d);
    put_hex(bits_d(d));
    put_u128(vd);
    MSIL_CHECK(acc, 5, vd == v);
  }
  return (int)acc;
}

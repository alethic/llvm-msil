// switch from C (Milestone 6): a dense switch in an interpreter loop
// (a jump table, which is the CIL 'switch' on an int32 index), a sparse
// switch (a tree of compares), a switch on long long with cases above 2^32
// (range-checked at full width before the index is truncated, D35), a
// switch on signed char with negative cases, cases that share code,
// fallthrough, a case after the default, and a switch nested in a switch.
// Every case calls a different noinline function or changes a scalar by a
// different operation, never only a constant: a switch whose cases only
// produce constants is a lookup-table candidate (tables are disabled by TTI,
// D8, but this test must not depend on that) and a table would need loads
// (M7) and globals (M8). Inputs are derived from argc (1 when run without
// arguments); unsigned arithmetic keeps everything free of overflow UB.
// MSIL_EXPECT returns 1 + (line % 255) at the first failed check; when every
// check passes, main returns a checksum of the results with all four bytes
// folded into the low 8 bits (D37). That status is not 1 + (line % 255) for
// any MSIL_EXPECT line of this file, so a failed check cannot pass for it
// (re-check after edits that move lines).
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

static NOINLINE unsigned op0(unsigned a) { return a + 7; }
static NOINLINE unsigned op1(unsigned a) { return a * 3; }
static NOINLINE unsigned op2(unsigned a) { return a ^ 0x5555u; }
static NOINLINE unsigned op3(unsigned a) { return a - 1000; }
static NOINLINE unsigned op4(unsigned a) { return a << 2; }
static NOINLINE unsigned op5(unsigned a) { return a >> 3; }
static NOINLINE unsigned op6(unsigned a) { return a * a; }
static NOINLINE unsigned op7(unsigned a) { return ~a; }
static NOINLINE unsigned op8(unsigned a) { return a | 0x10001u; }
static NOINLINE unsigned op9(unsigned a) { return a & 0xFFFF00u; }
static NOINLINE unsigned op10(unsigned a) { return a + (a >> 16); }
static NOINLINE unsigned op11(unsigned a) { return a * 2654435761u; }

// A bytecode-style interpreter: the opcode comes from a linear congruential
// generator, and a dense switch (cases 0..11 and the default for 12)
// dispatches it.
static NOINLINE unsigned interp(unsigned seed, int steps) {
  unsigned acc = 1;
  for (int i = 0; i < steps; i++) {
    seed = seed * 1103515245u + 12345u;
    switch ((seed >> 16) % 13) {
    case 0:
      acc = op0(acc);
      break;
    case 1:
      acc = op1(acc);
      break;
    case 2:
      acc = op2(acc);
      break;
    case 3:
      acc = op3(acc);
      break;
    case 4:
      acc = op4(acc);
      break;
    case 5:
      acc = op5(acc);
      break;
    case 6:
      acc = op6(acc);
      break;
    case 7:
      acc = op7(acc);
      break;
    case 8:
      acc = op8(acc);
      break;
    case 9:
      acc = op9(acc);
      break;
    case 10:
      acc = op10(acc);
      break;
    case 11:
      acc = op11(acc);
      break;
    default:
      acc += 0x9E3779B9u;
      break;
    }
  }
  return acc;
}

// Far-apart cases: a tree of compares.
static NOINLINE unsigned sparse(int x) {
  unsigned u = (unsigned)x;
  switch (x) {
  case -2147483647 - 1:
    return op0(u);
  case -100:
    return op1(u);
  case -1:
    return op2(u);
  case 0:
    return op3(u);
  case 7:
    return op4(u);
  case 1000:
    return op5(u);
  case 65536:
    return op6(u);
  case 2147483647:
    return op7(u);
  default:
    return op8(u);
  }
}

// long long cases: a dense cluster 0..5 and cases at 2^32 + 1 and -2^32.
static NOINLINE unsigned wide(long long x) {
  unsigned u = (unsigned)x;
  switch (x) {
  case 0:
    return op0(u);
  case 1:
    return op1(u);
  case 2:
    return op2(u);
  case 3:
    return op3(u);
  case 4:
    return op4(u);
  case 5:
    return op5(u);
  case 0x100000001LL:
    return op6(u);
  case -0x100000000LL:
    return op7(u);
  default:
    return op9(u + 0x123456u);
  }
}

// signed char cases: -128, a cluster -5..-2, 0, 'A' and 127.
static NOINLINE unsigned chars(signed char c) {
  unsigned u = (unsigned)c;
  switch (c) {
  case -128:
    return op0(u);
  case -5:
    return op1(u);
  case -4:
    return op2(u);
  case -3:
    return op3(u);
  case -2:
    return op4(u);
  case 0:
    return op5(u);
  case 'A':
    return op6(u);
  case 127:
    return op7(u);
  default:
    return op10(u);
  }
}

// Fallthrough, shared cases and a case after the default.
static NOINLINE unsigned fallthrough(int x, unsigned v) {
  switch (x) {
  case 0:
    v = op1(v);
    // fall through
  case 1:
    v = op0(v);
    // fall through
  case 2:
    v = op2(v);
    break;
  case 3:
  case 4:
  case 5:
    v = op5(v) + (unsigned)x;
    break;
  default:
    v = op7(v);
    break;
  case 6:
    v = op6(v);
    break;
  }
  return v;
}

// A switch nested in a switch, with scalar updates instead of calls in the
// inner one (each case changes v by a different operation).
static NOINLINE unsigned nested(unsigned x, unsigned v) {
  switch (x & 3) {
  case 0:
    switch ((x >> 2) & 7) {
    case 0:
      v += 11;
      break;
    case 1:
      v *= 13;
      break;
    case 2:
      v ^= 0xF0F0u;
      break;
    case 3:
      v -= 17;
      break;
    case 4:
      v <<= 3;
      break;
    case 5:
      v >>= 1;
      break;
    default:
      v = ~v;
      break;
    }
    break;
  case 1:
    v = op11(v);
    break;
  case 2:
    v = op10(v);
    break;
  default:
    v = op9(v) + x;
    break;
  }
  return v;
}

int main(int argc, char **argv) {
  (void)argv;
  int one = argc;
  unsigned h = 0;
#define MIX(v) (h = h * 31u + (unsigned)(v))

  // The interpreter for a few seeds; the steps argument covers 0 and 1.
  MSIL_EXPECT(interp(12345u * one, 0 * one) == 1);
  MIX(interp(12345u * one, 1 * one));
  MIX(interp(12345u * one, 1000 * one));
  MIX(interp(42u * one, 777 * one));

  // Every sparse case, its neighbours and the default.
  MSIL_EXPECT(sparse(7 * one) == 28);
  MSIL_EXPECT(sparse(8 * one) == (8u | 0x10001u));
  MSIL_EXPECT(sparse(-1 * one) == (0xFFFFFFFFu ^ 0x5555u));
  MIX(sparse((-2147483647 - 1) * one));
  MIX(sparse(-2147483647 * one));
  MIX(sparse(-101 * one));
  MIX(sparse(-100 * one));
  MIX(sparse(-99 * one));
  MIX(sparse(-1 * one));
  MIX(sparse(0 * one));
  MIX(sparse(1 * one));
  MIX(sparse(6 * one));
  MIX(sparse(7 * one));
  MIX(sparse(999 * one));
  MIX(sparse(1000 * one));
  MIX(sparse(1001 * one));
  MIX(sparse(65535 * one));
  MIX(sparse(65536 * one));
  MIX(sparse(65537 * one));
  MIX(sparse(2147483646 * one));
  MIX(sparse(2147483647 * one));

  // long long cases: values whose low 32 bits are a case value must still go
  // to the default (2^32 + 2 is not case 2, 2^32 is not case 0).
  long long big = 0x100000000LL * one;
  MSIL_EXPECT(wide(big + 2) == ((2u + 0x123456u) & 0xFFFF00u));
  MSIL_EXPECT(wide(big) == (0x123456u & 0xFFFF00u));
  MSIL_EXPECT(wide(2LL * one) == (2u ^ 0x5555u));
  MSIL_EXPECT(wide(big + 1) == 1u);
  MSIL_EXPECT(wide(-big) == 0xFFFFFFFFu);
  for (long long x = -3 * one; x <= 8; x++)
    MIX(wide(x));
  for (long long x = big - 3; x <= big + 8; x++)
    MIX(wide(x));
  MIX(wide(-big + 3));
  MIX(wide(-big - 1));

  // signed char: every value from -128 to 127.
  for (int c = -128 * one; c <= 127; c++)
    MIX(chars((signed char)c));
  MSIL_EXPECT(chars((signed char)(-128 * one)) == 0xFFFFFF87u);
  MSIL_EXPECT(chars((signed char)(65 * one)) == 4225u);

  // Fallthrough: x from -2 to 8.
  for (int x = -2 * one; x <= 8; x++)
    MIX(fallthrough(x, 100u * one));
  MSIL_EXPECT(fallthrough(0 * one, 10u) == ((10u * 3 + 7) ^ 0x5555u));
  MSIL_EXPECT(fallthrough(1 * one, 10u) == ((10u + 7) ^ 0x5555u));
  MSIL_EXPECT(fallthrough(4 * one, 80u) == 14u);

  // Nested switches: x from 0 to 40.
  for (unsigned x = 0; x <= 40u * one; x++)
    MIX(nested(x, 1000u * one + x));

  // Fold all four bytes of h into the low byte before masking: h & 0xff
  // alone depends only on the low byte of each mixed value.
  h ^= h >> 16;
  h ^= h >> 8;
  return (int)(h & 0xff);
}

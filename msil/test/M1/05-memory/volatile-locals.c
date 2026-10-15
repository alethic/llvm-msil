// Volatile objects from C (Milestone 7, D24): volatile locals of every scalar
// type (signed and unsigned char and short, int, long long, float, double, a
// pointer) stay in memory even at -O2, so every access is a load or store with
// the 'volatile.' prefix in a method that has a localloc frame; sign and zero
// extension of volatile narrow loads, truncating volatile stores,
// read-modify-write operators, a volatile loop counter, a volatile array, a
// volatile struct copied to and from a plain one (a volatile llvm.memcpy at
// -O0, volatile field copies at -O2), a volatile sink overwritten in a loop,
// and volatile accesses through pointers in noinline helpers. Values are
// derived from argc (1 when run without arguments), so clang cannot fold them
// at -O2. MSIL_EXPECT returns 1 + (line % 255) at the first failed check;
// [exit 0] means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))
#define TRY(call)                                                              \
  do {                                                                         \
    int r_ = (call);                                                           \
    if (r_ != 0)                                                               \
      return r_;                                                               \
  } while (0)

struct pair {
  long long a;
  int b;
  short c;
  double d;
};

static NOINLINE void bump(volatile int *p, int k) { *p += k; }
static NOINLINE long long twice(volatile long long *p) { return *p + *p; }
static NOINLINE void put_double(volatile double *p, double v) { *p = v; }

static NOINLINE int check_scalars(int one) {
  volatile signed char sc = (signed char)(-128 * one);
  volatile unsigned char uc = (unsigned char)(255 * one);
  volatile short ss = (short)(-32768 * one);
  volatile unsigned short us = (unsigned short)(65535 * one);
  volatile int i = -2147483647 * one - 1;
  volatile unsigned u = 0x80000000u * one;
  volatile long long ll = -0x7FFFFFFFFFFFFFFFll * one - 1;
  volatile float f = 0.5f * one;
  volatile double d = -1e-300 * one;
  int target = 99 * one;
  int *volatile p = &target;

  // Narrow loads extend by the type's signedness.
  int wide_sc = sc, wide_uc = uc, wide_ss = ss, wide_us = us;
  MSIL_EXPECT(wide_sc == -128 && wide_uc == 255);
  MSIL_EXPECT(wide_ss == -32768 && wide_us == 65535);
  long long l_sc = sc, l_ss = ss, l_i = i;
  unsigned long long l_uc = uc, l_us = us, l_u = u;
  MSIL_EXPECT(l_sc == -128 && l_ss == -32768 && l_i == -2147483647 - 1);
  MSIL_EXPECT(l_uc == 255 && l_us == 65535 && l_u == 0x80000000ull);
  MSIL_EXPECT(ll == -0x7FFFFFFFFFFFFFFFll - 1);
  MSIL_EXPECT(f == 0.5f && d == -1e-300);
  double fd = f;
  MSIL_EXPECT(fd == 0.5);
  MSIL_EXPECT(*p == 99);

  // Truncating stores.
  long long big = 0x123456789ABCDEFll * one;
  sc = (signed char)big;
  uc = (unsigned char)big;
  ss = (short)big;
  us = (unsigned short)big;
  i = (int)big;
  f = (float)(d * -1e300);
  MSIL_EXPECT(sc == (signed char)0xEF && uc == 0xEF);
  MSIL_EXPECT(ss == (short)0xCDEF && us == 0xCDEF);
  MSIL_EXPECT(i == (int)0x89ABCDEF && f == 1.0f);

  // Read-modify-write.
  sc += 1;
  uc++;
  ss -= 2;
  us *= 2;
  i ^= -1;
  u >>= 3;
  ll += 2;
  d *= -2.0;
  f /= 4.0f;
  MSIL_EXPECT(sc == -16 && uc == 0xF0 && ss == (short)0xCDED);
  MSIL_EXPECT(us == 0x9BDE && i == 0x76543210 && u == 0x10000000u);
  MSIL_EXPECT(ll == -0x7FFFFFFFFFFFFFFFll + 1 && d == 2e-300 && f == 0.25f);

  int other = -5 * one;
  p = &other;
  *p *= 3;
  MSIL_EXPECT(other == -15 && target == 99);
  return 0;
}

static NOINLINE int check_loops(int one) {
  // A volatile loop counter and accumulator.
  volatile int sum = 0;
  for (volatile int k = 0; k < 100 * one; k++)
    sum += k;
  MSIL_EXPECT(sum == 4950);

  // A volatile array, written and read with variable indexes.
  volatile int arr[16];
  for (int k = 0; k < 16; k++)
    arr[k] = k * k * one;
  int s = 0;
  for (int k = 15; k >= 0; k--)
    s = s * 2 + arr[k] % 7;
  // k * k % 7 for k = 15 .. 0, as binary digits weighted by powers of two.
  int expect = 0;
  for (int k = 15; k >= 0; k--)
    expect = expect * 2 + k * k % 7;
  MSIL_EXPECT(s == expect);

  // A sink that only the last store survives in.
  volatile long long sink = -1;
  for (int k = 0; k < 10 * one; k++)
    sink = (long long)k << 40;
  MSIL_EXPECT(sink == 9ll << 40);

  volatile int counter = 3 * one;
  for (int k = 0; k < 5; k++)
    bump(&counter, k);
  MSIL_EXPECT(counter == 13);
  volatile long long v = 0x100000000ll * one;
  MSIL_EXPECT(twice(&v) == 0x200000000ll);
  volatile double vd;
  put_double(&vd, 0.125 * one);
  MSIL_EXPECT(vd == 0.125);
  return 0;
}

static NOINLINE int check_structs(int one) {
  struct pair plain;
  plain.a = -9 * one;
  plain.b = 1000 * one;
  plain.c = (short)(-300 * one);
  plain.d = 6.5 * one;
  volatile struct pair vol;
  vol = plain;
  MSIL_EXPECT(vol.a == -9 && vol.b == 1000 && vol.c == -300 && vol.d == 6.5);
  vol.b += 1;
  vol.d = -vol.d;
  struct pair back = vol;
  MSIL_EXPECT(back.a == -9 && back.b == 1001 && back.c == -300);
  MSIL_EXPECT(back.d == -6.5 && plain.b == 1000 && plain.d == 6.5);
  return 0;
}

int main(int argc, char **argv) {
  (void)argv;
  TRY(check_scalars(argc));
  TRY(check_loops(argc));
  TRY(check_structs(argc));
  return 0;
}

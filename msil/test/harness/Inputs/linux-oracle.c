// Input of harness/linux-oracle.test, the E7 Linux oracle (plan 5.12.8): a C
// constructor, which start.c must run from .init_array before main; output
// through puts (libc.so.6 through the dynamic loader); an unsigned __int128
// division, for which a native x86_64 build calls __udivti3 (libgcc_s.so.1
// under E7, the compiler-rt builtins in the Windows oracle); and a non-zero
// exit status, which exit(main(...)) must pass on. linux-oracle.expected is
// the Windows oracle's transcript (update-expected.py), so the two native
// oracles agree on it.
#include "msil_test.h"

static int initialized;

__attribute__((constructor)) static void init(void) {
  initialized = 40;
  put_str("constructor ran");
}

// Opaque operands: the division stays a call at every level.
static volatile unsigned long long hi = 0x0123456789abcdefull;
static volatile unsigned long long lo = 0xfedcba9876543210ull;
static volatile unsigned long long divisor = 0x00000001000003d1ull;

int main(void) {
  put_str(initialized == 40 ? "main sees the constructor" : "main ran first");
  unsigned __int128 n = ((unsigned __int128)hi << 64) | lo;
  unsigned __int128 q = n / divisor;
  unsigned __int128 r = n % divisor;
  put_hex((unsigned long long)(q >> 64));
  put_hex((unsigned long long)q);
  put_hex((unsigned long long)r);
  MSIL_EXPECT(q * divisor + r == n);
  return initialized + 2;
}

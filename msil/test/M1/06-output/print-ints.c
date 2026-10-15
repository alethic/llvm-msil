// Integer printing through the msil_test.h helpers (Milestone 8): put_str,
// put_u64, put_i64 (LLONG_MIN included, negated in unsigned arithmetic),
// put_hex (lowercase, no leading zeros; its digit table is a static local, so
// a global of the data segment) and put_kv (a name truncated at 64
// characters, then '=' and the value). The values are boundary cases of
// every width (0, +-1, the limits of signed char, short, int and long long
// and of their unsigned types, powers of ten up to 10^19) and come once as
// constants and once from a mutable global table read in a loop, so they are
// formatted at run time at every optimization level. Exit status 0.
// RUN: %{msil_c}
#include <limits.h>

#include "msil_test.h"

long long signed_values[] = {
    0,         1,          -1,         9,         -10,       SCHAR_MIN,
    SCHAR_MAX, SHRT_MIN,   SHRT_MAX,   INT_MIN,   INT_MAX,   -2147483649ll,
    LLONG_MAX, LLONG_MIN,  -LLONG_MAX, 1000000007, -999999999999ll,
};

unsigned long long unsigned_values[] = {
    0,
    1,
    UCHAR_MAX,
    USHRT_MAX,
    UINT_MAX,
    4294967296ull,
    0x8000000000000000ull,
    ULLONG_MAX,
    ULLONG_MAX - 1,
    12345678901234567890ull,
};

int main(void) {
  put_str("constants");
  put_i64(0);
  put_i64(-1);
  put_i64(LLONG_MIN);
  put_i64(LLONG_MAX);
  put_u64(ULLONG_MAX);
  put_hex(0);
  put_hex(255);
  put_hex(0xdeadbeef);
  put_hex(ULLONG_MAX);
  put_kv("answer", 42);
  put_kv("negative", -7);
  put_kv("min", LLONG_MIN);
  put_kv("", 0);
  put_kv("a_name_that_is_longer_than_sixty_four_characters_and_is_truncated_"
         "here",
         1);

  put_str("signed");
  for (unsigned i = 0; i < sizeof signed_values / sizeof signed_values[0]; i++)
    put_i64(signed_values[i]);

  put_str("unsigned and hex");
  for (unsigned i = 0; i < sizeof unsigned_values / sizeof unsigned_values[0];
       i++) {
    put_u64(unsigned_values[i]);
    put_hex(unsigned_values[i]);
  }

  put_str("powers of ten");
  unsigned long long p = 1;
  for (int i = 0; i <= 19; i++) {
    put_u64(p);
    put_i64(-(long long)(p / 2));
    p *= 10;
  }

  put_str("hex digits");
  unsigned long long h = 0;
  for (int i = 0; i < 16; i++) {
    h = h << 4 | (unsigned long long)(15 - i);
    put_hex(h);
  }

  put_str("key=value");
  for (unsigned i = 0; i < sizeof signed_values / sizeof signed_values[0]; i++)
    put_kv("signed_values[i]", signed_values[i]);
  return 0;
}

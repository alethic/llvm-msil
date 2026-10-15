// Local arrays in the localloc frame (Milestone 7): arrays of every scalar
// width declared next to each other and indexed by values the compiler cannot
// see (a store of the wrong width or a wrong element offset clobbers a
// neighbour), a sieve on a byte array, an insertion sort, a 2-D matrix product,
// long long prefix sums, double and float arrays, an array of pointers to
// other locals, an array reversed by a callee and read back through a pointer
// into its middle (negative offsets), and two arrays in disjoint scopes, which
// StackColoring may give the same frame slot at -O2. Arrays are filled element
// by element: brace initializers become constant globals (M8). The helpers
// have external linkage so that the optimizer keeps their pointer parameters;
// inputs are derived from argc (1 when run without arguments). MSIL_EXPECT
// returns 1 + (line % 255) at the first failed check; main returns 0 when
// every check passes.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

NOINLINE void la_reverse(int *a, int n) {
  for (int i = 0, j = n - 1; i < j; i++, j--) {
    int t = a[i];
    a[i] = a[j];
    a[j] = t;
  }
}

NOINLINE unsigned la_hash(const int *a, int n) {
  unsigned h = 0;
  for (int i = 0; i < n; i++)
    h = h * 31u + (unsigned)a[i];
  return h;
}

// Number of primes below n (n <= 1000).
static NOINLINE int sieve_count(int n) {
  unsigned char composite[1000];
  for (int i = 0; i < n; i++)
    composite[i] = 0;
  int count = 0;
  for (int i = 2; i < n; i++) {
    if (composite[i])
      continue;
    count++;
    for (int j = i * i; j < n; j += i)
      composite[j] = 1;
  }
  return count;
}

// Sorts n <= 64 pseudo-random values in [-2^23, 2^23) with insertion sort and
// returns the hash of the sorted array, or 0 when the result is not ascending
// or is not a permutation of the input (sum and xor).
static NOINLINE unsigned sort_hash(unsigned seed, int n) {
  int a[64];
  long long sum = 0;
  unsigned x = 0;
  for (int i = 0; i < n; i++) {
    seed = seed * 1103515245u + 12345u;
    a[i] = (int)(seed >> 8) - (1 << 23);
    sum += a[i];
    x ^= (unsigned)a[i];
  }
  for (int i = 1; i < n; i++) {
    int v = a[i];
    int j = i - 1;
    while (j >= 0 && a[j] > v) {
      a[j + 1] = a[j];
      j--;
    }
    a[j + 1] = v;
  }
  int ok = 1;
  for (int i = 1; i < n; i++)
    ok &= a[i - 1] <= a[i];
  for (int i = 0; i < n; i++) {
    sum -= a[i];
    x ^= (unsigned)a[i];
  }
  return ok && sum == 0 && x == 0 ? la_hash(a, n) : 0;
}

// r = m * n with m[i][j] = (i + 1) + j and n[i][j] = 2 on the diagonal,
// i - j elsewhere; returns the hash of the rows (each row hashed on its own).
static NOINLINE unsigned matrix_hash(int one, int *trace) {
  int m[4][4], n[4][4], r[4][4];
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) {
      m[i][j] = (i + 1) * one + j;
      n[i][j] = i == j ? 2 * one : i - j;
    }
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) {
      int s = 0;
      for (int k = 0; k < 4; k++)
        s += m[i][k] * n[k][j];
      r[i][j] = s;
    }
  unsigned h = 0;
  *trace = 0;
  for (int i = 0; i < 4; i++) {
    h = h * 7u + la_hash(r[i], 4);
    *trace += r[i][i];
  }
  return h;
}

// Two arrays whose lifetimes do not overlap.
static NOINLINE unsigned scoped(int one) {
  unsigned h;
  {
    int a[64];
    for (int i = 0; i < 64; i++)
      a[i] = i * 3 * one;
    la_reverse(a, 64);
    h = la_hash(a, 64);
  }
  {
    int b[64];
    for (int i = 0; i < 64; i++)
      b[i] = i * 5 * one + 1;
    la_reverse(b, 64);
    h = h * 7u + la_hash(b, 64);
  }
  return h;
}

int main(int argc, char **argv) {
  (void)argv;
  int one = argc;

  MSIL_EXPECT(sieve_count(1000 * one) == 168);
  MSIL_EXPECT(sieve_count(100 * one) == 25);
  MSIL_EXPECT(sieve_count(2 * one) == 0);

  MSIL_EXPECT(sort_hash(12345u * one, 64 * one) == 0x9379AAFDu);
  MSIL_EXPECT(sort_hash(99u * one, 1 * one) == 0xFFEFB084u);
  MSIL_EXPECT(sort_hash(7u * one, 2 * one) == 0xF56FE7E6u);

  int trace;
  MSIL_EXPECT(matrix_hash(one, &trace) == 0x10E760F0u);
  MSIL_EXPECT(trace == 32);

  MSIL_EXPECT(scoped(one) == 0xCADB98C0u);

  // Neighbouring arrays of every width, written in one interleaved loop.
  short sh[8];
  signed char sc[8];
  unsigned char ub[8];
  unsigned short uw[8];
  long long ps[41];
  double dv[10];
  float fv[10];
  for (int i = 0; i < 8; i++) {
    sh[i] = (short)(-32768 + i * 9000 * one);
    sc[i] = (signed char)(i * 20 * one - 75);
    ub[i] = (unsigned char)(i * 37 * one); // 7 * 37 = 259 wraps to 3
    uw[i] = (unsigned short)(65535 - i * 1000 * one);
  }
  ps[0] = 0;
  for (int i = 0; i < 40; i++)
    ps[i + 1] = ps[i] + ((long long)one << i);
  for (int i = 0; i < 10; i++) {
    dv[i] = i * 0.5 * one;
    fv[i] = (float)i * 0.25f;
  }
  int ssh = 0, ssc = 0, sub = 0, suw = 0;
  for (int i = 0; i < 8; i++) {
    ssh += sh[i];
    ssc += sc[i];
    sub += ub[i];
    suw += uw[i];
  }
  MSIL_EXPECT(ssh == -10144);
  MSIL_EXPECT(ssc == -40);
  MSIL_EXPECT(sub == 780);
  MSIL_EXPECT(suw == 496280);
  MSIL_EXPECT(sh[7 * one] == 30232 && sc[0] == -75 && ub[7] == 3);
  MSIL_EXPECT(uw[one] == 64535);
  MSIL_EXPECT(ps[40] == (1LL << 40) - 1);
  MSIL_EXPECT(ps[33 * one] - ps[31] == 3LL << 31);
  double sdv = 0;
  float sfv = 0;
  for (int i = 0; i < 10; i++) {
    sdv += dv[i];
    sfv += fv[i];
  }
  MSIL_EXPECT(sdv == 22.5 && sfv == 11.25f);
  MSIL_EXPECT(dv[9 * one] == 4.5 && fv[3 * one] == 0.75f);

  // An array of pointers to other locals, stored through at variable indices.
  int x0 = -1, x1 = -1, x2 = -1, x3 = -1;
  int *ptrs[4];
  ptrs[0] = &x0;
  ptrs[1] = &x1;
  ptrs[2] = &x2;
  ptrs[3] = &x3;
  for (int k = 0; k < 4; k++)
    *ptrs[(k + one) & 3] = k * 11;
  MSIL_EXPECT(x0 == 33 && x1 == 0 && x2 == 11 && x3 == 22);

  // Reversed by a callee, then read through a pointer into the middle.
  int arr[50];
  for (int i = 0; i < 50; i++)
    arr[i] = i * i * one - 100;
  la_reverse(arr, 50 * one);
  MSIL_EXPECT(arr[0] == 2301 && arr[49] == -100);
  int *mid = &arr[25 * one];
  MSIL_EXPECT(mid[-5] == 741 && mid[-5 * one] == arr[20]);
  MSIL_EXPECT(mid[24] == -100 && mid[-25 * one] == 2301);
  MSIL_EXPECT(arr[7 * one] == 1664);

  return 0;
}

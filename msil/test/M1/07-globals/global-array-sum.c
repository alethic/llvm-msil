// Global arrays (Milestone 8, D12): initialized arrays of int, long long,
// short, unsigned char and double in the data image, partially initialized
// arrays whose tail is zero (emitted as zero fill inside the image), a large
// sparse array with designated initializers, a two-dimensional array, an
// array of structs with padding, and static (assembly-field) arrays. Each is
// summed element by element, in loops in main and through noinline helpers
// that receive the array's address, so the address fields and constant
// element offsets (ldsfld + ldc.i4 + add) are both used; then elements are
// modified and the arrays are summed again. Sums are printed; FP sums are
// scaled to integers. Exit status: a checksum of every sum, modulo 256.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))
#define COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))

int primes[] = {2,  3,  5,  7,  11, 13, 17, 19, 23, 29, 31, 37, 41,
                43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97};
long long big[] = {1000000000000ll, -2000000000000ll, 3, -4,
                   0x7fffffff00000000ll, -0x7fffffff00000000ll};
short shorts[64] = {1, -2, 3, -4, 5, -6, 7, -8};
unsigned char bytes[300] = {255, 254, 253, [150] = 128, [299] = 1};
double weights[] = {0.5, 0.25, 0.125, 2.0, -1.5};
int sparse[10000] = {[0] = 1, [4999] = 2, [5000] = 3, [9999] = 4};
int matrix[4][5] = {
    {1, 2, 3, 4, 5}, {6, 7, 8, 9, 10}, {11, 12, 13, 14, 15}, {16, 17, 18, 19}};

struct item {
  char tag;
  int count;
  short weight;
  long long total;
};
struct item items[] = {
    {'a', 1, 10, 100}, {'b', 2, 20, 200}, {'c', 3, 30, 300}, {'d', 4, 40, 400}};

static int s_fib[16] = {0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233};
static unsigned s_mask[] = {0x1u, 0x10u, 0x100u, 0x1000u, 0x80000000u};

volatile int one = 1;
static unsigned checksum;

static void report(const char *name, long long v) {
  put_kv(name, v);
  checksum = checksum * 31u + (unsigned)v;
}

static NOINLINE long long sum_int(const int *a, int n) {
  long long s = 0;
  for (int i = 0; i < n; i++)
    s += a[i];
  return s;
}

static NOINLINE long long sum_ll(const long long *a, int n) {
  long long s = 0;
  for (int i = 0; i < n; i++)
    s += a[i] / 1000;
  return s;
}

static NOINLINE long long sum_items(const struct item *it, int n) {
  long long s = 0;
  for (int i = 0; i < n; i++)
    s += it[i].tag * 1000000ll + it[i].count * 10000ll + it[i].weight * 100ll +
         it[i].total;
  return s;
}

static void sums(const char *title) {
  put_str(title);
  report("primes", sum_int(primes, COUNT(primes)));
  long long p = 0;
  for (int i = 0; i < COUNT(primes); i++)
    p += (long long)primes[i] * (i + 1);
  report("primes_weighted", p);
  report("big", sum_ll(big, COUNT(big)));
  long long s = 0;
  for (int i = 0; i < COUNT(shorts); i++)
    s += shorts[i] * (i + 1);
  report("shorts", s);
  unsigned long long b = 0;
  for (int i = 0; i < COUNT(bytes); i++)
    b += (unsigned long long)bytes[i] * (unsigned)(i + 1);
  report("bytes", (long long)b);
  double w = 0;
  for (int i = 0; i < COUNT(weights); i++)
    w += weights[i] * (i + 1);
  report("weights*1000", (long long)(w * 1000.0));
  report("sparse", sum_int(sparse, COUNT(sparse)));
  long long sp = 0;
  for (int i = 0; i < COUNT(sparse); i++)
    sp += (long long)sparse[i] * i;
  report("sparse_weighted", sp);
  long long m = 0;
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 5; c++)
      m += matrix[r][c] * (r * 5 + c + 1);
  report("matrix", m);
  report("matrix_row2", sum_int(matrix[2], 5));
  report("items", sum_items(items, COUNT(items)));
  report("s_fib", sum_int(s_fib, COUNT(s_fib)));
  unsigned long long mk = 0;
  for (int i = 0; i < COUNT(s_mask); i++)
    mk = mk * 3 + s_mask[i];
  report("s_mask", (long long)mk);
}

int main(void) {
  sums("initial");
  // Modify a few elements of every array; one is 1 at run time.
  primes[0] = 1000 * one;
  primes[24] = -primes[24];
  big[5] = big[4] / (2 * one);
  shorts[63] = (short)(-32768 * one);
  shorts[8] = 9;
  bytes[299] = (unsigned char)(bytes[0] + one);
  weights[4] = weights[4] * -2.0 * one;
  sparse[1] = 10 * one;
  sparse[9998] = sparse[9999] * 100;
  matrix[3][4] = 20 * one;
  items[2].weight = (short)(items[2].weight * -one);
  items[3].total += items[0].total * one;
  s_fib[14] = s_fib[13] + s_fib[12];
  s_fib[15] = s_fib[14] + s_fib[13];
  s_mask[4] >>= one;
  sums("modified");
  return (int)(checksum & 0xff);
}

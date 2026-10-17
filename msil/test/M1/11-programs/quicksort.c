// Quicksort over a global array (Design.md section 12): 3000 pseudo-random
// ints from a 32-bit LCG (negative values and duplicates included) are
// sorted in place in the global array by Hoare partitioning around a
// median of three, with insertion sort below 12 elements and recursion into
// the smaller part only, so the depth stays below log2(n). The same code
// then sorts sorted, reversed, all-equal, organ-pipe and sawtooth inputs.
// Each result is checked to be ordered and a permutation of its input (the
// sum, the xor and an order-independent hash are unchanged, compared as
// structs returned by value); the program prints the comparisons, swaps and
// recursion depth for each shape, and for the random input the first and
// last values, the quartiles and the number of distinct values. The seed is
// read from a volatile global. MSIL_EXPECT returns 1 + (line % 255) at the
// first failed check; on success the exit status is the comparison count
// for the random input modulo 256 (re-check that it is not 1 + (line % 255)
// for an MSIL_EXPECT line after edits that move lines).
// RUN: %{msil_c}
#include "msil_test.h"

#define N 3000
#define CUTOFF 12

volatile unsigned seed = 20261015u;
int data[N];
static unsigned long long compares, swaps;
static int depth, max_depth;

static int less(int x, int y) {
  compares++;
  return x < y;
}

static void swap(int i, int j) {
  int t = data[i];
  data[i] = data[j];
  data[j] = t;
  swaps++;
}

static void insertion_sort(int lo, int hi) { // data[lo..hi]
  for (int i = lo + 1; i <= hi; i++) {
    int x = data[i];
    int j = i - 1;
    while (j >= lo && less(x, data[j])) {
      data[j + 1] = data[j];
      j--;
    }
    data[j + 1] = x;
  }
}

static void quicksort(int lo, int hi) { // data[lo..hi]
  if (++depth > max_depth)
    max_depth = depth;
  while (hi - lo >= CUTOFF) {
    int mid = lo + (hi - lo) / 2;
    if (less(data[mid], data[lo]))
      swap(mid, lo);
    if (less(data[hi], data[lo]))
      swap(hi, lo);
    if (less(data[hi], data[mid]))
      swap(hi, mid);
    int pivot = data[mid];
    int i = lo - 1, j = hi + 1;
    for (;;) {
      do
        i++;
      while (less(data[i], pivot));
      do
        j--;
      while (less(pivot, data[j]));
      if (i >= j)
        break;
      swap(i, j);
    }
    // data[lo..j] <= pivot <= data[j+1..hi]; lo <= j < hi.
    if (j - lo < hi - j) {
      quicksort(lo, j);
      lo = j + 1;
    } else {
      quicksort(j + 1, hi);
      hi = j;
    }
  }
  insertion_sort(lo, hi);
  depth--;
}

enum { RANDOM, SORTED, REVERSED, EQUAL, ORGAN_PIPE, SAWTOOTH, SHAPES };
static const char *const shape_names[SHAPES] = {
    "random", "sorted", "reversed", "all equal", "organ pipe", "sawtooth"};

static void fill(int shape) {
  unsigned x = seed;
  for (int i = 0; i < N; i++) {
    int v;
    switch (shape) {
    case RANDOM:
      x = x * 1103515245u + 12345u;
      v = (int)((x >> 16) % 2001u) - 1000;
      break;
    case SORTED:
      v = 3 * i - 4000;
      break;
    case REVERSED:
      v = N - i;
      break;
    case EQUAL:
      v = 7;
      break;
    case ORGAN_PIPE:
      v = i < N / 2 ? i : N - i;
      break;
    default:
      v = i % 17 - 8;
      break;
    }
    data[i] = v;
  }
}

struct summary {
  long long sum;
  unsigned bits;
  unsigned hash;
};

static unsigned mix(int v) {
  unsigned x = (unsigned)v * 0x9e3779b1u;
  x ^= x >> 15;
  x *= 0x85ebca77u;
  return x ^ (x >> 13);
}

static struct summary summarize(void) {
  struct summary s = {0, 0, 0};
  for (int i = 0; i < N; i++) {
    s.sum += data[i];
    s.bits ^= (unsigned)data[i];
    s.hash += mix(data[i]);
  }
  return s;
}

static void show_values(const char *name, int from, int count) {
  put_str(name);
  for (int i = from; i < from + count; i++)
    put_i64(data[i]);
}

int main(void) {
  unsigned random_compares = 0;
  for (int s = 0; s < SHAPES; s++) {
    fill(s);
    struct summary before = summarize();
    compares = swaps = 0;
    depth = max_depth = 0;
    quicksort(0, N - 1);
    struct summary after = summarize();
    MSIL_EXPECT(before.sum == after.sum && before.bits == after.bits &&
                before.hash == after.hash);
    for (int i = 1; i < N; i++)
      MSIL_EXPECT(data[i - 1] <= data[i]);
    MSIL_EXPECT(depth == 0 && max_depth <= 12);

    put_str(shape_names[s]);
    put_kv("compares", (long long)compares);
    put_kv("swaps", (long long)swaps);
    put_kv("max depth", max_depth);
    if (s != RANDOM)
      continue;
    random_compares = (unsigned)compares;
    show_values("smallest", 0, 8);
    show_values("largest", N - 8, 8);
    put_kv("q1", data[N / 4]);
    put_kv("median", data[N / 2]);
    put_kv("q3", data[3 * N / 4]);
    int distinct = 1;
    for (int i = 1; i < N; i++)
      distinct += data[i] != data[i - 1];
    put_kv("distinct", distinct);
    put_kv("sum", before.sum);
  }
  return (int)(random_compares & 0xff);
}

// Merge sort, top-down and bottom-up (Milestone 1 program corpus): 1500
// records of (key, sequence number) in a global array of structs are sorted
// by key with a recursive top-down merge sort through a global scratch
// buffer, and a copy with an iterative bottom-up merge sort that alternates
// between two buffers (struct copies throughout). Both must be stable (equal
// keys keep their sequence order) and agree record by record. The inversion
// count of a pseudo-random permutation of 0..999, computed while merge
// sorting it, is checked against the quadratic count, and two sorted local
// arrays initialized from braces are merged into a third. Prints sample
// records, the number of distinct keys, the longest run of equal keys, the
// inversion count and the merged array. The seed is read from a volatile
// global. MSIL_EXPECT returns 1 + (line % 255) at the first failed check; on
// success the exit status is the inversion count modulo 256 (re-check that
// it is not 1 + (line % 255) for an MSIL_EXPECT line after edits that move
// lines).
// RUN: %{msil_c}
#include "msil_test.h"

#define N 1500
#define P 1000

struct rec {
  int key;
  int seq;
};

volatile unsigned seed = 12345u;
static struct rec recs[N], copy[N], scratch[N];
static int perm[P], tmp[P];

static unsigned next_rand(unsigned *x) {
  *x = *x * 1664525u + 1013904223u;
  return *x >> 8;
}

// Merges a[lo..mid) and a[mid..hi) into out[lo..hi); ties take the left
// record, which makes the sort stable.
static void merge(const struct rec *a, int lo, int mid, int hi,
                  struct rec *out) {
  int i = lo, j = mid, k = lo;
  while (i < mid && j < hi)
    out[k++] = a[j].key < a[i].key ? a[j++] : a[i++];
  while (i < mid)
    out[k++] = a[i++];
  while (j < hi)
    out[k++] = a[j++];
}

static void sort_top_down(int lo, int hi) { // recs[lo..hi)
  if (hi - lo < 2)
    return;
  int mid = lo + (hi - lo) / 2;
  sort_top_down(lo, mid);
  sort_top_down(mid, hi);
  merge(recs, lo, mid, hi, scratch);
  for (int k = lo; k < hi; k++)
    recs[k] = scratch[k];
}

// Sorts copy[] and returns the buffer that holds the result.
static const struct rec *sort_bottom_up(void) {
  struct rec *src = copy, *dst = scratch;
  for (int width = 1; width < N; width *= 2) {
    for (int lo = 0; lo < N; lo += 2 * width) {
      int mid = lo + width < N ? lo + width : N;
      int hi = lo + 2 * width < N ? lo + 2 * width : N;
      merge(src, lo, mid, hi, dst);
    }
    struct rec *t = src;
    src = dst;
    dst = t;
  }
  return src;
}

static unsigned long long count_inversions(int lo, int hi) { // perm[lo..hi)
  if (hi - lo < 2)
    return 0;
  int mid = lo + (hi - lo) / 2;
  unsigned long long inv = count_inversions(lo, mid);
  inv += count_inversions(mid, hi);
  int i = lo, j = mid, k = lo;
  while (i < mid && j < hi) {
    if (perm[j] < perm[i]) {
      inv += (unsigned)(mid - i);
      tmp[k++] = perm[j++];
    } else {
      tmp[k++] = perm[i++];
    }
  }
  while (i < mid)
    tmp[k++] = perm[i++];
  while (j < hi)
    tmp[k++] = perm[j++];
  for (k = lo; k < hi; k++)
    perm[k] = tmp[k];
  return inv;
}

static void show(const char *name, struct rec r) {
  put_str(name);
  put_kv("key", r.key);
  put_kv("seq", r.seq);
}

int main(void) {
  unsigned x = seed;
  for (int i = 0; i < N; i++) {
    recs[i].key = (int)(next_rand(&x) % 100u) - 50;
    recs[i].seq = i;
    copy[i] = recs[i];
  }
  sort_top_down(0, N);
  const struct rec *bu = sort_bottom_up();
  int distinct = 1, run = 1, best_run = 1, best_key = recs[0].key;
  for (int i = 0; i < N; i++) {
    MSIL_EXPECT(recs[i].key == bu[i].key && recs[i].seq == bu[i].seq);
    if (i == 0)
      continue;
    MSIL_EXPECT(recs[i - 1].key <= recs[i].key);
    if (recs[i - 1].key == recs[i].key) {
      MSIL_EXPECT(recs[i - 1].seq < recs[i].seq);
      if (++run > best_run) {
        best_run = run;
        best_key = recs[i].key;
      }
    } else {
      distinct++;
      run = 1;
    }
  }
  show("first", recs[0]);
  show("middle", recs[N / 2]);
  show("last", recs[N - 1]);
  put_kv("distinct keys", distinct);
  put_kv("longest run", best_run);
  put_kv("longest run key", best_key);

  for (int i = 0; i < P; i++)
    perm[i] = i;
  for (int i = P - 1; i > 0; i--) {
    int j = (int)(next_rand(&x) % (unsigned)(i + 1));
    int t = perm[i];
    perm[i] = perm[j];
    perm[j] = t;
  }
  unsigned long long slow = 0;
  for (int i = 0; i < P; i++)
    for (int j = i + 1; j < P; j++)
      slow += perm[i] > perm[j];
  put_kv("perm[0]", perm[0]);
  put_kv("perm[999]", perm[P - 1]);
  unsigned long long inversions = count_inversions(0, P);
  MSIL_EXPECT(inversions == slow);
  for (int i = 0; i < P; i++)
    MSIL_EXPECT(perm[i] == i);
  put_kv("inversions", (long long)inversions);

  int a[] = {-7, 1, 4, 9, 16, 25, 36};
  int b[] = {-9, 2, 3, 5, 7, 11, 13, 17, 19, 23, 29};
  int c[18];
  int i = 0, j = 0, k = 0;
  while (i < 7 && j < 11)
    c[k++] = b[j] < a[i] ? b[j++] : a[i++];
  while (i < 7)
    c[k++] = a[i++];
  while (j < 11)
    c[k++] = b[j++];
  put_str("merged");
  for (k = 0; k < 18; k++) {
    MSIL_EXPECT(k == 0 || c[k - 1] <= c[k]);
    put_i64(c[k]);
  }
  return (int)(inversions & 0xff);
}

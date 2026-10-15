// A qsort and a bsearch written in C that call a comparator through a
// pointer (Milestone 9): the comparator is a parameter, so every comparison
// is a managed calli from inside the sorting function. Covered: ints
// ascending and descending (two comparators for one sort routine), long
// long keys, structs ordered by two keys, an array of string pointers
// compared through a pointer to a pointer, a comparator that counts its
// calls through a global, and bsearch with the same comparators (hits and
// misses). The sort is a deterministic quicksort with insertion sort for
// short ranges, so the number of comparisons is the same on every platform.
// Exit status: the number of comparisons of the first sort, modulo 256.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))
#define COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))

typedef int (*cmp_fn)(const void *, const void *);

static void swap_bytes(char *a, char *b, unsigned size) {
  for (unsigned i = 0; i < size; i++) {
    char t = a[i];
    a[i] = b[i];
    b[i] = t;
  }
}

NOINLINE void my_qsort(void *base, int n, unsigned size, cmp_fn cmp) {
  char *b = (char *)base;
  while (n > 6) {
    // Median of three moved to the front, then a Hoare-style partition.
    char *mid = b + (unsigned)(n / 2) * size;
    char *last = b + (unsigned)(n - 1) * size;
    if (cmp(mid, b) < 0)
      swap_bytes(mid, b, size);
    if (cmp(last, mid) < 0) {
      swap_bytes(last, mid, size);
      if (cmp(mid, b) < 0)
        swap_bytes(mid, b, size);
    }
    swap_bytes(b, mid, size);
    int i = 1, j = n - 1;
    for (;;) {
      while (i <= j && cmp(b + (unsigned)i * size, b) < 0)
        i++;
      while (cmp(b + (unsigned)j * size, b) > 0)
        j--;
      if (i >= j)
        break;
      swap_bytes(b + (unsigned)i * size, b + (unsigned)j * size, size);
      i++;
      j--;
    }
    swap_bytes(b, b + (unsigned)j * size, size);
    // Recurse into the smaller part, loop on the larger one.
    if (j < n - 1 - j) {
      my_qsort(b, j, size, cmp);
      b += (unsigned)(j + 1) * size;
      n -= j + 1;
    } else {
      my_qsort(b + (unsigned)(j + 1) * size, n - j - 1, size, cmp);
      n = j;
    }
  }
  for (int i = 1; i < n; i++)
    for (int k = i;
         k > 0 && cmp(b + (unsigned)(k - 1) * size, b + (unsigned)k * size) > 0;
         k--)
      swap_bytes(b + (unsigned)(k - 1) * size, b + (unsigned)k * size, size);
}

NOINLINE const void *my_bsearch(const void *key, const void *base, int n,
                                unsigned size, cmp_fn cmp) {
  const char *b = (const char *)base;
  int lo = 0, hi = n - 1;
  while (lo <= hi) {
    int mid = lo + (hi - lo) / 2;
    int c = cmp(key, b + (unsigned)mid * size);
    if (c == 0)
      return b + (unsigned)mid * size;
    if (c < 0)
      hi = mid - 1;
    else
      lo = mid + 1;
  }
  return 0;
}

static int calls;

static int int_up(const void *a, const void *b) {
  int x = *(const int *)a, y = *(const int *)b;
  calls++;
  return (x > y) - (x < y);
}
static int int_down(const void *a, const void *b) { return int_up(b, a); }

static int ll_up(const void *a, const void *b) {
  long long x = *(const long long *)a, y = *(const long long *)b;
  return (x > y) - (x < y);
}

struct person {
  const char *name;
  int age;
  int id;
};

static int str_cmp(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

// Age descending, then name ascending, then id.
static int person_cmp(const void *a, const void *b) {
  const struct person *p = (const struct person *)a;
  const struct person *q = (const struct person *)b;
  if (p->age != q->age)
    return q->age - p->age;
  int c = str_cmp(p->name, q->name);
  return c ? c : p->id - q->id;
}

static int pstr_cmp(const void *a, const void *b) {
  return str_cmp(*(const char *const *)a, *(const char *const *)b);
}

static void put_ints(const int *v, int n) {
  for (int i = 0; i < n; i++)
    put_i64(v[i]);
}

int main(void) {
  int v[40];
  unsigned seed = 12345;
  for (int i = 0; i < COUNT(v); i++) {
    seed = seed * 1103515245u + 12345u;
    v[i] = (int)(seed >> 16) % 1000 - 500;
  }
  v[7] = v[21]; // a duplicate
  calls = 0;
  my_qsort(v, COUNT(v), sizeof v[0], int_up);
  int first_calls = calls;
  put_kv("comparisons", first_calls);
  put_ints(v, COUNT(v));
  for (int i = 1; i < COUNT(v); i++)
    MSIL_EXPECT(v[i - 1] <= v[i]);

  int key = v[13];
  const int *hit =
      (const int *)my_bsearch(&key, v, COUNT(v), sizeof v[0], int_up);
  MSIL_EXPECT(hit && *hit == key);
  key = 501;
  MSIL_EXPECT(my_bsearch(&key, v, COUNT(v), sizeof v[0], int_up) == 0);

  my_qsort(v, COUNT(v), sizeof v[0], int_down);
  for (int i = 1; i < COUNT(v); i++)
    MSIL_EXPECT(v[i - 1] >= v[i]);
  put_kv("max", v[0]);
  put_kv("min", v[COUNT(v) - 1]);

  long long w[9];
  for (int i = 0; i < COUNT(w); i++)
    w[i] = (long long)((i * 7) % 9 - 4) * (1LL << 36) + i;
  my_qsort(w, COUNT(w), sizeof w[0], ll_up);
  for (int i = 0; i < COUNT(w); i++)
    put_i64(w[i]);

  struct person people[8];
  static const char *const names[] = {"mia", "leo", "ava", "eli",
                                      "zoe", "ava", "kai", "ivy"};
  static const int ages[] = {31, 25, 31, 40, 25, 31, 19, 40};
  for (int i = 0; i < COUNT(people); i++) {
    people[i].name = names[i];
    people[i].age = ages[i];
    people[i].id = i;
  }
  my_qsort(people, COUNT(people), sizeof people[0], person_cmp);
  for (int i = 0; i < COUNT(people); i++)
    put_kv(people[i].name, people[i].age * 10 + people[i].id);
  struct person probe;
  probe.name = "ava";
  probe.age = 31;
  probe.id = 5;
  const struct person *found = (const struct person *)my_bsearch(
      &probe, people, COUNT(people), sizeof people[0], person_cmp);
  MSIL_EXPECT(found && found->id == 5 && found == &people[3]);

  const char *words[] = {"pear",  "apple", "fig",    "banana", "cherry",
                         "apple", "kiwi",  "grape",  "date",   "elder",
                         "lime",  "mango", "papaya", "quince"};
  my_qsort(words, COUNT(words), sizeof words[0], pstr_cmp);
  for (int i = 0; i < COUNT(words); i++)
    put_str(words[i]);
  const char *want = "kiwi";
  const char *const *w_hit = (const char *const *)my_bsearch(
      &want, words, COUNT(words), sizeof words[0], pstr_cmp);
  MSIL_EXPECT(w_hit && str_cmp(*w_hit, "kiwi") == 0);

  return first_calls & 0xff;
}

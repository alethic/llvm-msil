// Tables of string pointers (Milestone 8, D12): 'const char *names[]' is data
// whose elements are relocations to string literals (private '.str' globals
// on Linux, linkonce_odr '??_C@...' COMDAT globals on windows-msvc), applied
// by <Module>::.cctor with stind.i. Covered: a writable pointer table sorted
// in place by swapping pointers, a 'const char *const' table, an empty string
// and literals with quotes, backslashes and tabs, pointers into the middle
// of a literal (relocation addends), a struct table mixing strings and
// integers, a table of pointers to global char arrays, and the same literal
// used by two tables and by code. Indexes come from a volatile global, so
// every pointer is loaded from the data segment at -O2 too. Exit status: the
// total length of the strings printed, modulo 256.
// RUN: %{msil_c}
#include "msil_test.h"

#define COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))

const char *names[] = {"delta", "alpha", "echo",   "charlie", "bravo",
                       "golf",  "india", "hotel",  "foxtrot", "juliett",
                       "kilo",  "alpha", "lima",   "mike"};

static const char *const digits[] = {"zero", "one", "two",   "three", "four",
                                     "five", "six", "seven", "eight", "nine"};

const char *special[] = {
    "", "tab\there", "quote \" inside", "backslash \\ inside",
    "apostrophe ' inside", "percent % and braces {}"};

// Pointers into the middle of literals: relocations with addends.
const char *suffixes[] = {&"prefix:suffix"[7], &"0123456789"[5], &"xyz"[3]};

struct entry {
  const char *key;
  int value;
  const char *description;
};

struct entry entries[] = {
    {"one", 1, "the first entry"},
    {"two", 2, "the second entry"},
    {"three", 3, "the third entry"},
    {"zero", 0, "an entry whose key is also in digits[]"},
};

char buf_a[16] = "buffer a";
char buf_b[16] = "buffer b";
static char buf_c[8];
char *buffers[] = {buf_a, buf_b, buf_c, buf_a + 7};

volatile int zero = 0;
static unsigned total;

static int length(const char *s) {
  int n = 0;
  while (s[n] != '\0')
    n++;
  return n;
}

static void show(const char *s) {
  total += (unsigned)length(s) + 1;
  puts(s);
}

static int compare(const char *a, const char *b) {
  while (*a != '\0' && *a == *b) {
    a++;
    b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

int main(void) {
  put_str("names");
  for (int i = 0; i < COUNT(names); i++)
    show(names[i + zero]);

  // Insertion sort of the pointers; the strings are not moved.
  for (int i = 1; i < COUNT(names); i++) {
    const char *key = names[i + zero];
    int j = i - 1;
    while (j >= 0 && compare(names[j + zero], key) > 0) {
      names[j + 1 + zero] = names[j + zero];
      j--;
    }
    names[j + 1 + zero] = key;
  }
  put_str("sorted");
  for (int i = 0; i < COUNT(names); i++)
    show(names[i + zero]);

  put_str("digits");
  for (int i = 0; i < COUNT(digits); i++)
    show(digits[(i * 7 + zero) % 10]);

  put_str("special");
  for (int i = 0; i < COUNT(special); i++)
    show(special[i + zero]);

  put_str("suffixes");
  for (int i = 0; i < COUNT(suffixes); i++)
    show(suffixes[i + zero]);

  put_str("entries");
  for (int i = 0; i < COUNT(entries); i++) {
    const struct entry *e = &entries[i + zero];
    show(e->key);
    put_kv(e->key, e->value);
    show(e->description);
    // The literal "zero" is shared by digits[] and entries[]; whether the two
    // pointers are equal is unspecified, but the contents are equal.
    if (e->value == 0)
      put_kv("same text as digits[0]", compare(e->key, digits[zero]) == 0);
  }

  put_str("buffers");
  buf_c[0] = 'c';
  buf_c[1] = '!';
  buffers[0][7] = 'A';
  for (int i = 0; i < COUNT(buffers); i++)
    show(buffers[i + zero]);
  put_kv("buffers[3] aliases buf_a", buffers[3] == buf_a + 7);
  return (int)(total & 0xff);
}

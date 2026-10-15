// Functions that return one of their arguments (Milestone 9): at -O2 the
// optimizer marks such a parameter 'returned', and MSILOptimizeReturned
// then lets later uses of the argument read the call's result instead
// (copied from WebAssemblyOptimizeReturned). The values must not change.
// Covered: a pointer returned by a string-building function and used after
// the call (chained calls, writes through the original pointer), an int
// returned unchanged by a function with side effects, a long long returned
// from the second parameter, and a pointer returned from a recursive
// function. Values are printed; MSIL_EXPECT returns 1 + (line % 255) at the
// first failed check; [exit 0] means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

volatile int zero = 0;
static int seen;

// Appends s at the end of the string in dst; returns dst.
NOINLINE char *append(char *dst, const char *s) {
  char *p = dst;
  while (*p)
    p++;
  while ((*p++ = *s++) != 0)
    ;
  return dst;
}

NOINLINE int observe(int v) {
  seen = seen * 10 + v % 10;
  return v;
}

NOINLINE long long second(int tag, long long v) {
  seen += tag;
  return v;
}

// Reverses s in place, recursively from both ends; returns s.
NOINLINE char *reverse(char *s, int lo, int hi) {
  if (lo >= hi)
    return s;
  char t = s[lo];
  s[lo] = s[hi];
  s[hi] = t;
  return reverse(s, lo + 1, hi - 1);
}

int main(void) {
  char buf[64];
  buf[0] = 0;
  char *r = append(append(append(buf, "returned"), " "), "argument");
  MSIL_EXPECT(r == buf);
  put_str(buf);
  buf[zero] = 'R';
  put_str(r);
  char *again = append(r, "!");
  again[1] = 'E';
  put_str(buf);

  int x = 40 + zero;
  int a1 = observe(x);
  int a2 = observe(x + 2);
  int y = a1 + a2 + x;
  MSIL_EXPECT(y == 122 && seen == 2);
  put_kv("observe", observe(y));

  long long big = (1LL << 44) + zero;
  long long z = second(3, big) - big;
  MSIL_EXPECT(z == 0 && seen == 25);
  put_kv("second", second(4, big + 1));

  char word[16];
  word[0] = 0;
  append(word, "recursion");
  char *w = reverse(word, zero, 8);
  MSIL_EXPECT(w == word);
  put_str(word);
  put_str(reverse(append(word, "-x"), 0, 10));
  return 0;
}

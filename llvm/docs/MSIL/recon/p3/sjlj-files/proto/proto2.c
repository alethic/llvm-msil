/* E1's cost on the normal path: a call after setjmp through the wrapper (plus the status check)
 * against the same call made directly.  patch-proto.py turns __msil_sjlj_invoke_add into try/filter. */
#include <stdint.h>
#include <stdio.h>
#define MSIL_SJLJ_MAGIC 0x314A534Du
struct msil_sjlj_status { int label; int val; };
static volatile intptr_t stub_sink;
__attribute__((noinline)) void __msil_sjlj_throw(struct msil_sjlj_status *st) { stub_sink = (intptr_t)st; }
__attribute__((noinline)) int __msil_sjlj_match(int hresult, struct msil_sjlj_status *st) {
  return hresult == (int)(intptr_t)st && st->label != 0;
}
__attribute__((noinline)) static long add(long a, long b) { return a + (b ^ 3); }
__attribute__((noinline)) static long __msil_sjlj_invoke_add(long a, long b, struct msil_sjlj_status *st) {
  stub_sink = (intptr_t)st;
  return add(a, b);
}
__attribute__((noinline)) static long wrapped(long n) {
  struct msil_sjlj_status st; st.label = 0;
  long s = 0;
  for (long i = 0; i < n; i++) {
    s = __msil_sjlj_invoke_add(s, i, &st);
    if (st.label) return -1;
  }
  return s;
}
__attribute__((noinline)) static long direct(long n) {
  long s = 0;
  for (long i = 0; i < n; i++) s = add(s, i);
  return s;
}
static long parse(const char *s) { long v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return v; }
int main(int argc, char **argv) {
  long n = argc > 2 ? parse(argv[2]) : 100000000;
  long r = (argc > 1 && argv[1][0] == 'w') ? wrapped(n) : direct(n);
  puts(r == 0x5a5a5a5a ? "?" : "done");
  return 0;
}

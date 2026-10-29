/* As bench.c, but the jmp_buf is a local of the setjmp caller (a localloc frame), as in Lua. */
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
static volatile int sink;
__attribute__((noinline)) static void thrower(jmp_buf *e, int v) { sink++; longjmp(*e, v); }
__attribute__((noinline)) static int one(int i) {
  jmp_buf env;
  int r = setjmp(env);
  if (r == 0) { thrower(&env, (i & 7) + 1); return -1; }
  return r;
}
int main(int argc, char **argv) {
  long n = argc > 1 ? atol(argv[1]) : 1000000, s = 0;
  for (long i = 0; i < n; i++) s += one((int)i);
  puts(s == n / 8 * 36 ? "ok" : "bad");
  return s == n / 8 * 36 ? 0 : 1;
}

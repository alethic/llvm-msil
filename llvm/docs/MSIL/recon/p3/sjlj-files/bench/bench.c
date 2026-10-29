/* 1M setjmp/longjmp round trips: setjmp in one(), longjmp one frame deeper. */
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
static jmp_buf env;
static volatile int sink;
__attribute__((noinline)) static void thrower(int v) { sink++; longjmp(env, v); }
__attribute__((noinline)) static int one(int i) {
  int r = setjmp(env);
  if (r == 0) { thrower((i & 7) + 1); return -1; }
  return r;
}
int main(int argc, char **argv) {
  long n = argc > 1 ? atol(argv[1]) : 1000000, s = 0;
  for (long i = 0; i < n; i++) s += one((int)i);
  /* sum of (i&7)+1 over n iterations, n a multiple of 8 */
  puts(s == n / 8 * 36 ? "ok" : "bad");
  return s == n / 8 * 36 ? 0 : 1;
}

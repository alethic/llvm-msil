/* Windows: today's native binding forced past clang's _setjmp builtin (frame argument 0 or the real one). */
#include <stdlib.h>
#include <stdio.h>
typedef struct { _Alignas(16) unsigned char b[256]; } jb;
extern int sj(jb *buf, void *frame) __asm__("_setjmp") __attribute__((returns_twice));
extern void lj(jb *buf, int v) __asm__("longjmp") __attribute__((noreturn));
static jb env;
static volatile int sink;
__attribute__((noinline)) static void thrower(int v) { sink++; lj(&env, v); }
__attribute__((noinline)) static int one(int i) {
  int r = sj(&env, 0);
  if (r == 0) { thrower((i & 7) + 1); return -1; }
  return r;
}
int main(int argc, char **argv) {
  long n = argc > 1 ? atol(argv[1]) : 1000000, s = 0;
  for (long i = 0; i < n; i++) s += one((int)i);
  puts(s == n / 8 * 36 ? "ok" : "bad");
  return s == n / 8 * 36 ? 0 : 1;
}

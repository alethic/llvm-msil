#include <setjmp.h>
#include <stdio.h>
static jmp_buf env;
static int depth;
__attribute__((noinline)) static void thrower(int v) { depth++; longjmp(env, v); }
int main(void) {
  volatile int count = 0;
  int r = setjmp(env);
  if (r < 3) { count++; thrower(r + 1); }
  puts(r == 3 && count == 3 ? "ok" : "bad");
  return 0;
}

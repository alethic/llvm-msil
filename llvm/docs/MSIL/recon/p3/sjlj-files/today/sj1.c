#include <setjmp.h>
#include <stdio.h>
static jmp_buf env;
static void thrower(int v) { longjmp(env, v); }
int main(void) {
  volatile int count = 0;
  int r = setjmp(env);
  if (r < 3) { count++; thrower(r + 1); }
  printf("r=%d count=%d sizeof(jmp_buf)=%zu\n", r, count, sizeof(jmp_buf));
  return 0;
}

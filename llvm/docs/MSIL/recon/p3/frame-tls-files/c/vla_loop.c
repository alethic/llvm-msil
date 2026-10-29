/* A VLA in a loop: clang brackets each iteration's VLA with llvm.stacksave and
   llvm.stackrestore, so a native build reuses the same stack bytes every iteration. */
#include <stdio.h>
#include <stdint.h>
__attribute__((noinline)) static unsigned fill(unsigned char *p, int n, int i) {
  for (int k = 0; k < n; k++) p[k] = (unsigned char)(i + k);
  return p[0] + p[n - 1];
}
int main(int argc, char **argv) {
  int n = 64 + argc;            /* not a constant */
  unsigned sum = 0; uintptr_t first = 0, last = 0;
  for (int i = 0; i < 1000000; i++) {
    unsigned char buf[n];       /* VLA: dynamic alloca between stacksave/stackrestore */
    sum += fill(buf, n, i);
    if (i == 0) first = (uintptr_t)buf;
    last = (uintptr_t)buf;
  }
  printf("sum=%u same_address=%d\n", sum, first == last);
  return 0;
}

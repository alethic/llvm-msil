#include <stdio.h>
int sum(int n, ...);
double dsum(const char *fmt, ...);
int (*fp)(int, ...) = sum;
int main(void) {
  printf("sum(3, 10, 20, 12) = %d\n", sum(3, 10, 20, 12));
  printf("dsum = %g\n", dsum("ddiid", 0.5, 1.25, 3, 4, 100.0));
  printf("fp = %d\n", fp(2, 40, 2));
  return 0;
}

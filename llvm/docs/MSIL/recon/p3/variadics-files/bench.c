#include <stdarg.h>
#include <stdio.h>
#include <time.h>
__attribute__((noinline)) int sum(int n, ...) {
  va_list ap;
  int s = 0;
  va_start(ap, n);
  for (int i = 0; i < n; i++) s += va_arg(ap, int);
  va_end(ap);
  return s;
}
__attribute__((noinline)) int sum_fixed(int n, int a, int b, int c, int d) { return n + a + b + c + d; }
int main(void) {
  long long s = 0;
  clock_t t = clock();
  for (int i = 0; i < 20000000; i++) s += sum(4, i, 1, 2, 3);
  double ms1 = (clock() - t) * 1000.0 / CLOCKS_PER_SEC;
  t = clock();
  for (int i = 0; i < 20000000; i++) s += sum_fixed(4, i, 1, 2, 3);
  double ms2 = (clock() - t) * 1000.0 / CLOCKS_PER_SEC;
  char b[64];
  t = clock();
  for (int i = 0; i < 1000000; i++) s += snprintf(b, sizeof b, "%d %f", i, i * 0.5);
  double ms3 = (clock() - t) * 1000.0 / CLOCKS_PER_SEC;
  printf("checksum %lld\nvariadic sum x20M: %.0f ms\nfixed sum x20M: %.0f ms\nsnprintf x1M: %.0f ms\n", s, ms1, ms2, ms3);
  return 0;
}

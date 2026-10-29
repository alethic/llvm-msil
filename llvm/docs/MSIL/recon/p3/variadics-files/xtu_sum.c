#include <stdarg.h>
int sum(int n, ...) {
  va_list ap;
  int s = 0;
  va_start(ap, n);
  for (int i = 0; i < n; ++i)
    s += va_arg(ap, int);
  va_end(ap);
  return s;
}
double dsum(const char *fmt, ...) {
  va_list ap;
  double s = 0;
  va_start(ap, fmt);
  for (; *fmt; fmt++)
    s += *fmt == 'd' ? va_arg(ap, double) : va_arg(ap, int);
  va_end(ap);
  return s;
}

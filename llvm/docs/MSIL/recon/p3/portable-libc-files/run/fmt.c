#include <stddef.h>
#include <stdarg.h>
int vsnprintf(char *, size_t, const char *, va_list);
int strcmp(const char *, const char *);
struct sysv_va { unsigned gp_offset, fp_offset; void *overflow_arg_area, *reg_save_area; };
static int fmt(char *buf, size_t n, const char *f, unsigned long long *slots) {
  struct sysv_va v[1] = {{48, 176, slots, 0}};
  return vsnprintf(buf, n, f, *(va_list *)&v);
}
static unsigned long long D(double d) { union { double d; unsigned long long u; } x = {d}; return x.u; }
int main(void) {
  char b[128]; int r = 0;
  unsigned long long s1[] = {D(1234.5678), D(0.000123), D(1.0/3), (unsigned long long)-5LL, 18446744073709551615ULL};
  fmt(b, sizeof b, "%e|%g|%.10a|%lld|%llu", s1);
  if (strcmp(b, "1.234568e+03|0.000123|0x1.5555555555p-2|-5|18446744073709551615")) r |= 1;
  unsigned long long s2[] = {D(2.5), 77, (unsigned long long)(size_t)"x", 255};
  fmt(b, sizeof b, "%Lf|%zu|%-3s|%#o", s2);
  if (strcmp(b, "2.500000|77|x  |0377")) r |= 2;
  int n = fmt(b, 5, "%s", (unsigned long long[]){(unsigned long long)(size_t)"truncate"});
  if (n != 8 || strcmp(b, "trun")) r |= 4;
  return r ? r : 42;
}

/* qsort through an explicit import (exempt from the callback check): today's run-time behaviour. */
typedef __SIZE_TYPE__ size_t;
int puts(const char *);
#ifdef _WIN32
#define LIBC "api-ms-win-crt-utility-l1-1-0.dll"
#else
#define LIBC "libc.so.6"
#endif
void qsort(void *, size_t, size_t, int (*)(const void *, const void *))
    __attribute__((import_module(LIBC), import_name("qsort")));
static int by_value(const void *a, const void *b) {
  int x = *(const int *)a, y = *(const int *)b;
  return (x > y) - (x < y);
}
static void put_int(int v) {
  char buf[16], *p = buf + 15; int neg = v < 0; unsigned u = neg ? -(unsigned)v : (unsigned)v;
  *p = 0; do { *--p = (char)('0' + u % 10); u /= 10; } while (u); if (neg) *--p = '-'; puts(p);
}
int (*volatile keep)(const void *, const void *) = by_value;
int main(void) {
  int v[8] = {42, -7, 19, 0, 1000, -7, 3, 256};
  qsort(v, 8, sizeof v[0], by_value);
  for (int i = 0; i < 8; ++i) put_int(v[i]);
  return keep == by_value ? 0 : 1;
}

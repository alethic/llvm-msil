/* Function pointers in every direction: C -> native (qsort comparator), native -> C (dlsym/GetProcAddress
   result called from C), C -> C through a pointer stored in data, and pointer equality. */
typedef __SIZE_TYPE__ size_t;
int puts(const char *);
#ifdef _WIN32
#define LIBC "api-ms-win-crt-utility-l1-1-0.dll"
void *LoadLibraryA(const char *) __attribute__((import_module("kernel32.dll"), import_name("LoadLibraryA")));
void *GetProcAddress(void *, const char *) __attribute__((import_module("kernel32.dll"), import_name("GetProcAddress")));
static void *native_sym(const char *n) { return GetProcAddress(LoadLibraryA("ucrtbase.dll"), n); }
#else
#define LIBC "libc.so.6"
void *dlsym(void *, const char *) __attribute__((import_module("libc.so.6"), import_name("dlsym")));
static void *native_sym(const char *n) { return dlsym((void *)0, n); }
#endif
void qsort(void *, size_t, size_t, int (*)(const void *, const void *))
    __attribute__((import_module(LIBC), import_name("qsort")));
static int by_value(const void *a, const void *b) {
  int x = *(const int *)a, y = *(const int *)b;
  return (x > y) - (x < y);
}
static int twice(int x) { return 2 * x; }
static int thrice(int x) { return 3 * x; }
static void put_int(int v) {
  char buf[16], *p = buf + 15; int neg = v < 0; unsigned u = neg ? -(unsigned)v : (unsigned)v;
  *p = 0; do { *--p = (char)('0' + u % 10); u /= 10; } while (u); if (neg) *--p = '-'; puts(p);
}
/* A table of function pointers in data (relocations). */
int (*const table[3])(int) = {twice, thrice, twice};
int (*volatile pick)(int);
int main(int argc, char **argv) {
  int v[8] = {42, -7, 19, 0, 1000, -7, 3, 256};
  qsort(v, 8, sizeof v[0], by_value);
  int sum = 0;
  for (int i = 0; i < 8; ++i) sum = sum * 3 + v[i];
  put_int(sum);
  pick = table[argc & 1];                       /* C -> C through data */
  put_int(pick(7));
  put_int(table[0] == table[2] && table[0] == twice && table[1] != twice);  /* identity */
  int (*nabs)(int) = (int (*)(int))native_sym("abs");    /* native -> called from C */
  put_int(nabs(-5));
  return 0;
}

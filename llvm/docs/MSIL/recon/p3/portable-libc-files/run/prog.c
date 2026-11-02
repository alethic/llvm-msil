/* P-0 libc recon: program linked against the scratch c.dll (host MSIL triple, dotnet approximation). */
#include <stddef.h>
#include <stdarg.h>
double strtod(const char *, char **);
long strtol(const char *, char **, int);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
size_t strlen(const char *);
int strcmp(const char *, const char *);
char *strstr(const char *, const char *);
void *memcpy(void *, const void *, size_t);
int isalpha(int);
float sinf(float);
double exp(double);
double pow(double, double);
double sqrt(double);
int vsnprintf(char *, size_t, const char *, va_list);
int vsscanf(const char *, const char *, va_list);
int *__llvm_libc_errno(void);

static int cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

/* SysV x86-64 va_list built by hand: every register slot marked used, all
   arguments in the overflow area (8 bytes each). Probe only: the portable
   triples will use a char* va_list (M26's MSILCharPtr). */
struct sysv_va { unsigned gp_offset, fp_offset; void *overflow_arg_area, *reg_save_area; };
static int fmt2(char *buf, size_t n, const char *f, unsigned long long *slots) {
  struct sysv_va v[1] = {{48, 176, slots, 0}};
  return vsnprintf(buf, n, f, *(va_list *)&v);
}

int main(void) {
  int fails = 0;
  int a[6] = {5, 3, 6, 1, 4, 2};
  qsort(a, 6, sizeof(int), cmp);
  for (int i = 0; i < 6; i++) if (a[i] != i + 1) fails |= 1;
  char *end;
  double d = strtod("2.5e1xyz", &end);
  if (d != 25.0 || *end != 'x') fails |= 2;
  if (strtol("-0x1F", 0, 16) != -31) fails |= 4;
  float s = sinf(0.5f);
  if (!(s > 0.4794255f && s < 0.4794256f)) fails |= 8;
  double e = exp(1.0);
  if (e != 2.718281828459045) fails |= 16;
  if (pow(2.5, 2.0) != 6.25 || sqrt(2.0) != 1.4142135623730951) fails |= 32;
  if (strlen("hello") != 5 || strcmp("abc", "abd") >= 0 || strstr("haystack", "st") == 0 || !isalpha('q')) fails |= 64;
  strtod("1e999", 0);
  if (*__llvm_libc_errno() != 34) fails |= 128; /* ERANGE through the errno cell */
#ifdef WITH_VSNPRINTF
  {
    char buf[96];
    union { double d; unsigned long long u; } pi = {3.14159};
    unsigned long long slots[4] = {42, (unsigned long long)(size_t)"str", pi.u, 0};
    int n = fmt2(buf, sizeof buf, "%d|%s|%.3f|%x", slots);
    /* doubles in the overflow area too (fp_offset=176 means fp regs used) */
    const char *want = "42|str|3.142|0";
    if (n != 14 || strcmp(buf, want) != 0) fails |= 256;
  }
#endif
  return fails ? (fails > 255 ? 255 : fails) : 42;
}

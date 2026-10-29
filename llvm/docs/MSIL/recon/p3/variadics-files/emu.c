/* emu.c: emulates, in plain C, what the proposed MSIL lowering of variadic
 * functions produces, so that today's MSIL toolchain (which compiles va_list
 * parameters, clang's inline va_arg and va_copy, but no variadic definitions
 * or calls) can run it:
 *  - the caller stores the variadic arguments into a buffer of 8-byte slots
 *    (ExpandVariadics' "vararg_buffer"): on SysV x86-64 every argument by
 *    value in the overflow-area layout; on Win64 aggregates that are not 1, 2,
 *    4 or 8 bytes by pointer to a caller copy;
 *  - it builds a native va_list over that buffer (initializeVaList):
 *    Win64 char* = the buffer; SysV __va_list_tag {gp_offset=48,
 *    fp_offset=176, overflow_arg_area=buffer, reg_save_area=0};
 *  - the variadic definition becomes a function of its fixed parameters plus
 *    the va_list (ExpandVariadics' fixed-arity replacement, in which
 *    va_start(ap) became va_copy(ap, param));
 *  - a native v-variant (vsnprintf) receives the same va_list unchanged.
 * The native build (clang for the native triple) reaches the SAME va_list
 * functions through real variadic wrappers: the reference output.
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

int puts(const char *s);

struct S3 { int a, b, c; };          /* 12 bytes: SysV INTEGER x2 (regs or memory), Win64 indirect */
struct S24 { long long x, y, z; };   /* 24 bytes: SysV MEMORY (by value), Win64 indirect */
struct D2 { double d, e; };          /* 16 bytes: SysV SSE x2, Win64 indirect */
struct C1 { char c; };               /* 1 byte: Win64 by value */

static void put_ll(const char *name, long long v) {
  char buf[64], *p = buf + 63;
  unsigned long long m = v < 0 ? 0ull - (unsigned long long)v : (unsigned long long)v;
  *p = 0;
  do { *--p = (char)('0' + m % 10); m /= 10; } while (m);
  if (v < 0) *--p = '-';
  char line[128];
  size_t n = strlen(name);
  memcpy(line, name, n);
  line[n] = ' '; line[n + 1] = '=';  line[n + 2] = ' ';
  strcpy(line + n + 3, p);
  puts(line);
}

/* ---- The callees, written against va_list (what the definitions become). */
static long long sum_ints_va(int n, va_list ap) {
  long long s = 0;
  for (int i = 0; i < n; i++) s += va_arg(ap, int);
  return s;
}
static long long mixed_va(const char *kinds, va_list ap) {
  long long acc = 0;
  for (const char *k = kinds; *k; k++) {
    switch (*k) {
    case 'i': acc = acc * 3 + va_arg(ap, int); break;
    case 'l': acc = acc * 3 + va_arg(ap, long long); break;
    case 'd': acc = acc * 3 + (long long)(va_arg(ap, double) * 8); break;
    case 'p': acc = acc * 3 + (long long)strlen(va_arg(ap, const char *)); break;
    case 'c': { struct C1 s = va_arg(ap, struct C1); acc = acc * 3 + s.c; break; }
    case 's': { struct S3 s = va_arg(ap, struct S3); acc = acc * 3 + s.a + 10 * s.b + 100 * s.c; break; }
    case 'S': { struct S24 s = va_arg(ap, struct S24); acc = acc * 3 + (s.x + s.y + s.z); break; }
    case 'D': { struct D2 s = va_arg(ap, struct D2); acc = acc * 3 + (long long)((s.d - s.e) * 8); break; }
    }
  }
  return acc;
}
/* va_copy: walk twice. */
static long long twice_va(int n, va_list ap) {
  va_list c;
  va_copy(c, ap);
  long long a = sum_ints_va(n, ap);
  long long b = sum_ints_va(n, c);
  va_end(c);
  return a * 1000 + b;
}
/* A C function taking va_list that forwards it to the native v-variant. */
static int my_vsnprintf(char *buf, size_t n, const char *fmt, va_list ap) {
  return vsnprintf(buf, n, fmt, ap);
}

#ifdef __MSIL__
/* ---- MSIL: what the caller side of the lowering emits, by hand. */
typedef struct { _Alignas(16) unsigned char b[512]; size_t off; } Frame;
static void put(Frame *f, const void *p, size_t size, size_t align) {
  if (align < 8) align = 8;
  f->off = (f->off + align - 1) & ~(align - 1);
  memcpy(f->b + f->off, p, size);
  f->off += (size + 7) & ~(size_t)7;
}
#define PUT(f, T, v) do { T tmp_ = (v); put(&(f), &tmp_, sizeof(T), _Alignof(T)); } while (0)
#ifdef _WIN32
/* Win64: aggregates of 1, 2, 4 or 8 bytes by value, others by pointer to a copy. */
#define PUTAGG(f, T, v, copy) do { copy = (v); T *p_ = &copy; \
    if (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8) put(&(f), p_, sizeof(T), 8); \
    else put(&(f), &p_, 8, 8); } while (0)
#define VA_FROM(name, f) va_list name; name = (char *)(f).b
#else
/* SysV x86-64: every aggregate by value in the overflow area. */
#define PUTAGG(f, T, v, copy) do { copy = (v); put(&(f), &copy, sizeof(T), _Alignof(T)); } while (0)
#define VA_FROM(name, f) va_list name; name[0].gp_offset = 48; name[0].fp_offset = 176; \
    name[0].overflow_arg_area = (f).b; name[0].reg_save_area = 0
#endif
#define START(f) Frame f; f.off = 0
#else
/* ---- Native: real variadic wrappers. */
static long long sum_ints(int n, ...) { va_list ap; va_start(ap, n); long long r = sum_ints_va(n, ap); va_end(ap); return r; }
static long long mixed(const char *k, ...) { va_list ap; va_start(ap, k); long long r = mixed_va(k, ap); va_end(ap); return r; }
static long long twice(int n, ...) { va_list ap; va_start(ap, n); long long r = twice_va(n, ap); va_end(ap); return r; }
static int my_snprintf(char *b, size_t n, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r = my_vsnprintf(b, n, fmt, ap); va_end(ap); return r; }
#endif

int main(void) {
  char out[256];
  struct S3 s3 = {1, 2, 3}, s3c;
  struct S24 s24 = {100, 200, 300}, s24c;
  struct D2 d2 = {7.5, 0.25}, d2c;
  struct C1 c1 = {9}, c1c;
  (void)s3c; (void)s24c; (void)d2c; (void)c1c;
#ifdef __MSIL__
  { START(f); PUT(f, int, 10); PUT(f, int, 20); PUT(f, int, 12); VA_FROM(ap, f);
    put_ll("sum_ints(3, 10, 20, 12)", sum_ints_va(3, ap)); }
  { START(f); PUT(f, int, 7); PUT(f, double, 2.5); PUT(f, long long, 1LL << 40); PUT(f, const char *, "hello");
    PUTAGG(f, struct S3, s3, s3c); PUTAGG(f, struct S24, s24, s24c); PUTAGG(f, struct D2, d2, d2c); PUTAGG(f, struct C1, c1, c1c);
    for (int i = 0; i < 8; i++) PUT(f, double, i + 0.5);
    PUT(f, int, -3);
    VA_FROM(ap, f);
    put_ll("mixed", mixed_va("idlpsSDcddddddddi", ap)); }
  { START(f); PUT(f, int, 1); PUT(f, int, 2); PUT(f, int, 3); PUT(f, int, 4); VA_FROM(ap, f);
    put_ll("twice(4, 1, 2, 3, 4)", twice_va(4, ap)); }
  { START(f); PUT(f, int, 42); PUT(f, double, 2.5); PUT(f, const char *, "hello"); PUT(f, double, -1.25);
    PUT(f, long long, 1LL << 40);
    for (int i = 0; i < 9; i++) PUT(f, double, i * 1.5);
    VA_FROM(ap, f);
    my_vsnprintf(out, sizeof out, "%d|%f|%s|%f|%lld|%g %g %g %g %g %g %g %g %g", ap); puts(out); }
#else
  put_ll("sum_ints(3, 10, 20, 12)", sum_ints(3, 10, 20, 12));
  put_ll("mixed", mixed("idlpsSDcddddddddi", 7, 2.5, 1LL << 40, "hello", s3, s24, d2, c1,
                        0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, -3));
  put_ll("twice(4, 1, 2, 3, 4)", twice(4, 1, 2, 3, 4));
  my_snprintf(out, sizeof out, "%d|%f|%s|%f|%lld|%g %g %g %g %g %g %g %g %g", 42, 2.5, "hello", -1.25, 1LL << 40,
              0 * 1.5, 1 * 1.5, 2 * 1.5, 3 * 1.5, 4 * 1.5, 5 * 1.5, 6 * 1.5, 7 * 1.5, 8 * 1.5);
  puts(out);
#endif
  return 0;
}

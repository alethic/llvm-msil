/* va.c: variadic definitions, calls, va_list, va_copy, function pointers and
 * native printf-family calls; run natively and through the prototype
 * (msil-expand-variadics + the tree's llc and msil-ld). */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct foo { int i, j, k; char *p; float v; };   /* 32 bytes: SysV MEMORY, Win64 by pointer */
struct pt { double x, y; };                       /* 16 bytes: SysV SSE+SSE */
struct small { short a; char b; };                /* 4 bytes: Win64 by value */
typedef struct { long long a, b; } pair;          /* 16 bytes: SysV INTEGER+INTEGER */
typedef struct { char c[3]; } odd3;               /* 3 bytes: Win64 by pointer (not a power of 2) */

/* c-testsuite 00140: a variadic definition that never calls va_start. */
int f1(struct foo f, struct foo *p, int n, ...) {
  if (f.i != p->i)
    return 0;
  return p->j + n;
}

static int sum(int n, ...) {
  va_list ap;
  int s = 0;
  va_start(ap, n);
  for (int i = 0; i < n; i++)
    s += va_arg(ap, int);
  va_end(ap);
  return s;
}

static double avg(int n, ...) {
  va_list ap;
  double s = 0;
  va_start(ap, n);
  for (int i = 0; i < n; i++)
    s += va_arg(ap, double);
  va_end(ap);
  return n ? s / n : 0;
}

static long long mixed(const char *kinds, ...) {
  va_list ap;
  long long acc = 0;
  va_start(ap, kinds);
  for (const char *k = kinds; *k; k++) {
    switch (*k) {
    case 'i': acc = acc * 7 + va_arg(ap, int); break;
    case 'u': acc = acc * 7 + va_arg(ap, unsigned long); break;
    case 'z': acc = acc * 7 + (long long)va_arg(ap, size_t); break;
    case 'l': acc = acc * 7 + va_arg(ap, long long); break;
    case 'd': acc = acc * 7 + (long long)(va_arg(ap, double) * 16); break;
    case 'c': acc = acc * 7 + va_arg(ap, int); break;            /* char promoted */
    case 'f': acc = acc * 7 + (long long)(va_arg(ap, double) * 16); break; /* float promoted */
    case 'p': acc = acc * 7 + (long long)strlen(va_arg(ap, const char *)); break;
    case 's': { struct small s = va_arg(ap, struct small); acc = acc * 7 + s.a * 3 + s.b; break; }
    case 'P': { struct pt s = va_arg(ap, struct pt); acc = acc * 7 + (long long)((s.x - s.y) * 16); break; }
    case 'F': { struct foo s = va_arg(ap, struct foo); acc = acc * 7 + s.i + s.j + s.k + (long long)strlen(s.p) + (long long)(s.v * 4); break; }
    case 'q': { pair s = va_arg(ap, pair); acc = acc * 7 + s.a - s.b; break; }
    case 'o': { odd3 s = va_arg(ap, odd3); acc = acc * 7 + s.c[0] + s.c[1] + s.c[2]; break; }
    }
  }
  va_end(ap);
  return acc;
}

/* A va_list-taking function, a forwarder, va_copy, va_list by pointer. */
static int vsum(int n, va_list ap) {
  int s = 0;
  while (n--)
    s += va_arg(ap, int);
  return s;
}
static int forward(int n, ...) {
  va_list ap;
  va_start(ap, n);
  int r = vsum(n, ap);
  va_end(ap);
  return r;
}
static int twice(int n, ...) {
  va_list ap, cp;
  va_start(ap, n);
  va_copy(cp, ap);
  int a = vsum(n, ap);
  int b = vsum(n, cp);
  va_end(cp);
  va_end(ap);
  return a * 1000 + b;
}
static int eat(va_list *ap) { return va_arg(*ap, int); }
static int by_pointer(int n, ...) {
  va_list ap;
  va_start(ap, n);
  int s = 0;
  for (int i = 0; i < n; i++)
    s = s * 10 + eat(&ap);
  va_end(ap);
  return s;
}
/* Two va_start in one function. */
static int restart(int n, ...) {
  va_list ap;
  int a = 0, b = 0;
  va_start(ap, n);
  for (int i = 0; i < n; i++) a += va_arg(ap, int);
  va_end(ap);
  va_start(ap, n);
  for (int i = 0; i < n; i++) b = b * 2 + va_arg(ap, int);
  va_end(ap);
  return a * 100 + b;
}
static int rec(int depth, ...) {
  va_list ap;
  va_start(ap, depth);
  int v = va_arg(ap, int);
  va_end(ap);
  return depth ? v + rec(depth - 1, v * 2) : v;
}
static int none(int n, ...) { return n; }

/* Our variadic function forwarding to a native v-variant. */
static int my_printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vprintf(fmt, ap);
  va_end(ap);
  return r;
}
static int my_snprintf(char *b, size_t n, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vsnprintf(b, n, fmt, ap);
  va_end(ap);
  return r;
}

int (*vp)(int, ...) = sum;
int (*pp)(const char *, ...) = printf;

int main(void) {
  struct foo f;
  char buf[200];
  f.i = f.j = 1;
  f.k = 5; f.p = "abc"; f.v = 2.5f;
  struct small sm = {300, 7};
  struct pt pt = {9.5, 1.25};
  pair pr = {1LL << 40, 3};
  odd3 o3 = {{1, 2, 3}};

  printf("f1 %d %d\n", f1(f, &f, 2), f1(f, &f, 2, 1, f, &f));
  printf("sum %d %d %d\n", sum(0), sum(3, 10, 20, 12), sum(10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10));
  printf("avg %.4f %.4f\n", avg(3, 1.0, 2.0, 4.5), avg(10, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.5));
  printf("mixed %lld\n", mixed("iuzldcfpsPFqoi", -4, 123456789012UL, (size_t)77, -(1LL << 50), 3.75, 'x',
                               1.5f, "hello", sm, pt, f, pr, o3, 42));
  printf("forward %d twice %d by_pointer %d restart %d\n", forward(4, 1, 2, 3, 4), twice(3, 5, 6, 7),
         by_pointer(4, 1, 2, 3, 4), restart(4, 1, 0, 1, 1));
  printf("rec %d none %d vp %d\n", rec(5, 1), none(9), vp(4, 1, 1, 1, 1));
  my_printf("my_printf %d %s %.3f %c %lld %u\n", -17, "str", 3.14159, 'Q', 1LL << 33, 4000000000u);
  my_snprintf(buf, sizeof buf, "%08.3f|%-5d|%5s|%x", -2.5, 42, "ab", 0xbeef);
  printf("my_snprintf [%s]\n", buf);
  snprintf(buf, sizeof buf, "%g %g %g %g %g %g %g %g %g %g", 0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5, 9.5);
  printf("snprintf [%s]\n", buf);
  pp("via pointer %d %f\n", 7, 0.25);
  fprintf(stdout, "fprintf %s %d\n", "done", 1);
  fflush(stdout);
  return 0;
}

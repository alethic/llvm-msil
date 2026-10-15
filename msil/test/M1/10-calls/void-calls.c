// Calls of void functions and calls whose results are not used (Milestone
// 9): a void method has no result to pop, and a call whose result is
// ignored must pop exactly one value, so the evaluation stack stays
// balanced (MSILStackCheck, jitall). Covered: void functions with no
// parameters, with output parameters, with an early return and with a
// loop; void functions calling void functions; results of int, long long,
// double, float and pointer functions discarded, with and without a (void)
// cast; discarded results inside a loop and in a comma expression; a void
// function called through a pointer and a table; and a function returning
// a struct whose result is discarded. Side effects go to globals and are
// printed. MSIL_EXPECT returns 1 + (line % 255) at the first failed check;
// [exit 0] means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

volatile int zero = 0;
static long long trace;
static int calls;

static void mark(int v) { trace = trace * 10 + v; }

NOINLINE void nothing(void) { calls++; }
NOINLINE void step1(void) {
  calls++;
  mark(1);
}
NOINLINE void step2(void) {
  step1();
  mark(2);
}
NOINLINE void step3(void) {
  step2();
  step1();
  mark(3);
}

NOINLINE void divmod(int a, int b, int *q, int *r) {
  calls++;
  if (b == 0) {
    *q = *r = 0;
    return;
  }
  *q = a / b;
  *r = a % b;
}

NOINLINE void fill(int *v, int n, int start) {
  for (int i = 0; i < n; i++)
    v[i] = start + i * i;
}

NOINLINE int side_int(int v) {
  mark(v);
  return v * 2;
}
NOINLINE long long side_ll(long long v) {
  mark((int)(v & 7));
  return v << 3;
}
NOINLINE double side_double(double v) {
  mark((int)v);
  return v * 3;
}
NOINLINE float side_float(float v) {
  mark((int)v);
  return v / 2;
}
NOINLINE const char *side_ptr(const char *s) {
  mark(s[0] - '0');
  return s + 1;
}

struct triple {
  long long a, b, c;
};
NOINLINE struct triple side_struct(int v) {
  struct triple t;
  mark(v);
  t.a = v;
  t.b = v * 2;
  t.c = v * 3;
  return t;
}

static void (*const steps[])(void) = {step1, step2, step3, nothing};

int main(void) {
  nothing();
  nothing();
  MSIL_EXPECT(calls == 2);

  trace = 0;
  step3();
  put_kv("step3", trace);
  MSIL_EXPECT(trace == 1213);

  int q, r;
  divmod(17 + zero, 5, &q, &r);
  MSIL_EXPECT(q == 3 && r == 2);
  divmod(-17 + zero, 5, &q, &r);
  MSIL_EXPECT(q == -3 && r == -2);
  divmod(1, zero, &q, &r);
  MSIL_EXPECT(q == 0 && r == 0);

  int v[6];
  fill(v, 6, zero - 3);
  long long s = 0;
  for (int i = 0; i < 6; i++)
    s = s * 100 + v[i] + 3;
  put_kv("fill", s);

  trace = 0;
  side_int(1);
  (void)side_ll(2 + zero);
  side_double(3.0);
  (void)side_float(4.0f + zero);
  side_ptr("5x");
  side_struct(6);
  (void)side_struct(7 + zero);
  put_kv("discarded", trace);
  MSIL_EXPECT(trace == 1234567);

  trace = 0;
  for (int i = 1; i <= 5; i++)
    side_int(i + zero);
  int last = (side_int(8), side_int(9));
  MSIL_EXPECT(last == 18);
  put_kv("loop and comma", trace);
  MSIL_EXPECT(trace == 1234589);

  trace = 0;
  calls = 0;
  void (*volatile pv)(void) = step2;
  pv();
  pv = nothing;
  pv();
  for (int i = 0; i < 4; i++)
    steps[i + zero]();
  put_kv("through pointers", trace);
  put_kv("calls", calls);
  MSIL_EXPECT(trace == 121121213 && calls == 1 + 1 + 1 + 1 + 2 + 1);
  return 0;
}

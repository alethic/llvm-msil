// Calls through function pointers (Milestone 9): an address taken in code is a
// load of the function's address field, which <Module>::.cctor sets once with
// ldftn, and every call through a pointer is a managed calli whose signature
// comes from the call site, so it must match the target's method signature
// exactly. Covered: pointers to external and static functions with int32,
// long long (native int), double, float, char, short (int32 in CIL), pointer
// and void parameters and results; a pointer chosen at run time through a
// volatile object, so -O2 keeps the indirect call; callbacks passed as
// arguments, composed, and returned from a function; an unused result of an
// indirect call; conversion to another function-pointer type and back before
// the call (C11 6.3.2.3p8); equality of pointers to the same function taken in
// different places and in data, inequality of pointers to different functions,
// comparison with a null pointer; and recursion through a pointer held in a
// global. MSIL_EXPECT returns 1 + (line % 255) at the first failed check;
// [exit 0] means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

typedef int (*unop)(int);

volatile int zero = 0;
static int counter;

NOINLINE int add1(int x) { return x + 1; }
NOINLINE int twice(int x) { return 2 * x; }
static NOINLINE int negate(int x) { return -x; }
NOINLINE int count_calls(int x) {
  counter += x;
  return x * 3;
}

NOINLINE long long mul_ll(long long a, long long b) { return a * b; }
NOINLINE double half(double x) { return x * 0.5; }
NOINLINE float addf(float a, float b) { return a + b; }
NOINLINE int narrow(signed char c, unsigned short s, unsigned char u, short t) {
  return c * 1000000 + s * 10 + u - t;
}
NOINLINE const char *skip(const char *s, int n) { return s + n; }
NOINLINE void bump(void) { counter++; }
NOINLINE void add_to(int *p, int v) { *p += v; }
NOINLINE long long seven(void) { return 7; }
// Narrow results: extended to int32 by the callee, as the signature says.
NOINLINE signed char low_byte(int x) { return (signed char)(x & 0xff); }
NOINLINE unsigned short low_half(long long x) {
  return (unsigned short)(x & 0xffff);
}

// Every parameter class in one signature.
NOINLINE long long mixed(int i, long long l, double d, float f, const char *p,
                         unsigned char b) {
  return i * 1000003LL + l * 31 + (long long)(d * 4.0) +
         (long long)(f * 8.0f) * 7 + (p[0] - 'a') * 11 + b;
}

// Pointers in data: <Module>::.cctor stores each function's address field.
unop table[] = {add1, twice, negate};

NOINLINE unop choose(int k) {
  if (k == 0)
    return add1;
  if (k == 1)
    return twice;
  return negate;
}

NOINLINE int apply(unop f, int x) { return f(x); }
NOINLINE int compose(unop f, unop g, int x) { return f(g(x)); }
NOINLINE long long fold(long long (*f)(long long, long long),
                        const long long *v, int n, long long init) {
  for (int i = 0; i < n; i++)
    init = f(init, v[i]);
  return init;
}

// Recursion through pointers: one initialized in data, one at run time.
NOINLINE long long fact(long long n);
long long (*fact_ptr)(long long) = fact;
NOINLINE long long fact(long long n) {
  return n <= 1 ? 1 : n * fact_ptr(n - 1);
}

static int (*fib_ptr)(int);
static NOINLINE int fib(int n) {
  return n < 2 ? n : fib_ptr(n - 1) + fib_ptr(n - 2);
}

int main(void) {
  // Volatile pointer objects: every call loads the pointer and uses calli.
  int (*volatile pick)(int) = add1;
  MSIL_EXPECT(pick(41) == 42);
  pick = twice;
  MSIL_EXPECT(pick(21) == 42);
  pick = negate;
  MSIL_EXPECT(pick(42) == -42);
  put_kv("pick", pick(-7));

  long long (*volatile pll)(long long, long long) = mul_ll;
  MSIL_EXPECT(pll(1LL << 20, 1LL << 21) == 1LL << 41);
  put_kv("mul_ll", pll(-123456789LL, 1000LL));

  double (*volatile pd)(double) = half;
  MSIL_EXPECT(pd(5.0) == 2.5);
  float (*volatile pf)(float, float) = addf;
  MSIL_EXPECT(pf(1.25f, 2.5f) == 3.75f);
  put_kv("half*4", (long long)(pd(81.0) * 4.0));

  int (*volatile pn)(signed char, unsigned short, unsigned char, short) =
      narrow;
  MSIL_EXPECT(pn(-100, 65535, 255, -32768) ==
              -100000000 + 655350 + 255 + 32768);
  put_kv("narrow", pn((signed char)(zero - 5), (unsigned short)(zero + 40000),
                      (unsigned char)(zero + 200), (short)(zero - 300)));

  const char *(*volatile ps)(const char *, int) = skip;
  put_str(ps("function pointers", 9));

  void (*volatile pv)(void) = bump;
  counter = 0;
  pv();
  pv();
  pv();
  MSIL_EXPECT(counter == 3);
  void (*volatile pa)(int *, int) = add_to;
  int acc = 5;
  pa(&acc, 37);
  MSIL_EXPECT(acc == 42);
  long long (*volatile p7)(void) = seven;
  MSIL_EXPECT(p7() * p7() == 49);
  signed char (*volatile pb)(int) = low_byte;
  unsigned short (*volatile ph)(long long) = low_half;
  MSIL_EXPECT(pb(zero + 0x17f) == 127 && ph(-1 + zero) == 65535);
  MSIL_EXPECT(ph((1LL << 40) + 5 + zero) == 5);
  put_kv("narrow results", pb(zero + 0x180) * 100000LL + ph(zero - 2));

  long long (*volatile pm)(int, long long, double, float, const char *,
                           unsigned char) = mixed;
  long long m = pm(-3, 1LL << 40, 2.75, -1.5f, "kilo", 200);
  MSIL_EXPECT(m == mixed(-3, 1LL << 40, 2.75, -1.5f, "kilo", 200));
  put_kv("mixed", m);

  // The result of an indirect call that is not used.
  int (*volatile pc)(int) = count_calls;
  counter = 0;
  pc(10);
  (void)pc(20);
  MSIL_EXPECT(counter == 30);

  // Callbacks as arguments and results.
  MSIL_EXPECT(apply(add1, 1) == 2 && apply(twice, 4) == 8);
  MSIL_EXPECT(apply(negate, 9) == -9);
  MSIL_EXPECT(compose(add1, twice, 5) == 11 && compose(twice, add1, 5) == 12);
  MSIL_EXPECT(apply(choose(zero + 1), 50) == 100);
  MSIL_EXPECT(choose(zero + 2)(choose(zero)(6)) == -7);
  int sum = 0;
  for (int k = 0; k < 3; k++)
    sum = sum * 100 + choose(k + zero)(10 + k);
  MSIL_EXPECT(sum == 112188);
  put_kv("choose", sum);
  for (int k = 0; k < 3; k++)
    put_kv("table", table[k + zero](100));

  static const long long vals[] = {3, 5, 7, 11, 13};
  MSIL_EXPECT(fold(mul_ll, vals, 5, 1) == 15015);
  put_kv("fold", fold(pll, vals, 5, -2));

  // Conversion to another function-pointer type and back.
  void (*volatile generic)(void) = (void (*)(void))twice;
  unop back = (unop)generic;
  MSIL_EXPECT(back(21) == 42);
  void (*volatile gd)(void) = (void (*)(void))half;
  MSIL_EXPECT(((double (*)(double))gd)(3.0) == 1.5);

  // Identity: the same function compares equal wherever its address is
  // taken (code, data, another function's result); different ones differ.
  unop a = add1;
  MSIL_EXPECT(a == choose(zero) && a == table[zero] && a == &add1);
  MSIL_EXPECT(table[zero + 2] == negate && choose(2 + zero) == negate);
  MSIL_EXPECT(a != table[zero + 1] && (unop)twice != (unop)negate);
  unop none = 0;
  MSIL_EXPECT(none == 0 && a != 0 && table[zero + 1] != none);
  MSIL_EXPECT((void (*)(void))bump == pv);

  // Recursion through pointers.
  MSIL_EXPECT(fact(20) == 2432902008176640000LL);
  fib_ptr = fib;
  MSIL_EXPECT(fib(20 + zero) == 6765);
  put_kv("fact(15)", fact_ptr(15 + zero));
  put_kv("fib(23)", fib_ptr(23 + zero));
  return 0;
}

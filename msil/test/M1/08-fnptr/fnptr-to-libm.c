// Pointers to C math-library imports (Milestone 9, D14, recon cil-runtime
// e18): the address of sqrt, floor, sqrtf, pow, frexp, ... is the import's
// address field, which <Module>::.cctor sets with an ldftn of its pinvokeimpl
// declaration (ucrtbase on windows-msvc; libm.so.6 on linux-gnu, a library
// other than libc), and a call through it is a managed calli that the runtime
// routes through the P/Invoke stub, here with float64, float32, int32 and
// out-pointer (native int) parameters. Covered: a const table in data mixing
// double imports with a function of the module, whose elements the .cctor
// writes from the address fields, called in a loop; a const table of float
// imports; a const table of binary double imports; a writable volatile
// global pointer to sqrtf, reassigned at run time to floorf; pointers to
// sqrt, pow, powf, ldexp, frexp and modf taken in code and held in volatile
// locals, frexp and modf writing their second result through an out-pointer;
// a pointer to an import passed as a callback; and identity of each import's
// address taken in code and in data (tbl[i] == sqrt, f == sqrt). Only exactly
// specified results are used (sqrt of squares, rounding, pow(2, 10), fmod,
// ldexp, frexp, modf), so UCRT and glibc agree; they are printed scaled by
// 1000 as integers. Indexes and arguments are read through volatile globals,
// so -O2 keeps the indirect calls. MSIL_EXPECT returns 1 + (line % 255) at
// the first failed check; [exit 0] means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))
#define COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))

double sqrt(double), floor(double), ceil(double), trunc(double);
double pow(double, double), fmod(double, double), ldexp(double, int);
double frexp(double, int *), modf(double, double *);
float sqrtf(float), floorf(float), ceilf(float), powf(float, float);

volatile int zero = 0;
volatile double v_one = 1.0;
volatile float v_onef = 1.0f;

// x, but computed at run time.
static double D(double x) { return x * v_one; }
static float F(float x) { return x * v_onef; }

// x scaled by 1000 and truncated, for printing.
static long long milli(double x) { return (long long)(x * 1000.0); }

static double twice(double x) { return x + x; }

double (*const unary_ops[])(double) = {floor, ceil, trunc, sqrt, twice};
static const char *const unary_names[] = {"floor", "ceil", "trunc", "sqrt",
                                          "twice"};
static const double unary_args[] = {-2.5, -2.25, -3.75, 6.25, 1.5};

static float (*const float_ops[])(float) = {floorf, ceilf, sqrtf};
static const char *const float_names[] = {"floorf", "ceilf", "sqrtf"};

double (*const binary_ops[])(double, double) = {pow, fmod};

float (*volatile float_op)(float) = sqrtf;

NOINLINE double apply(double (*f)(double), double x) { return f(x); }

int main(void) {
  // A const table in data: imports and a function of the module.
  for (int i = 0; i < COUNT(unary_ops); i++)
    put_kv(unary_names[i], milli(unary_ops[i + zero](D(unary_args[i]))));
  for (int i = 0; i < COUNT(float_ops); i++)
    put_kv(float_names[i], milli(float_ops[i + zero](F(2.25f))));
  put_kv("pow", milli(binary_ops[zero](D(2.0), D(10.0))));
  put_kv("fmod", milli(binary_ops[zero + 1](D(7.5), D(2.0))));

  // A writable global pointer, initialized in data and replaced in code.
  put_kv("float_op", milli(float_op(F(6.25f))));
  float_op = floorf;
  put_kv("float_op", milli(float_op(F(6.25f))));

  // Pointers taken in code.
  double (*volatile f)(double) = sqrt;
  double (*volatile p2)(double, double) = pow;
  float (*volatile pf2)(float, float) = powf;
  double (*volatile ld)(double, int) = ldexp;
  double (*volatile fx)(double, int *) = frexp;
  double (*volatile mf)(double, double *) = modf;
  put_kv("sqrt", milli(f(D(2.25))));
  put_kv("pow", milli(p2(D(2.0), D(10.0))));
  put_kv("powf", milli(pf2(F(2.0f), F(10.0f))));
  put_kv("ldexp", milli(ld(D(0.75), 4 + zero)));
  int e = 0;
  double m = fx(D(48.0), &e);
  put_kv("frexp", milli(m));
  put_kv("frexp exponent", e);
  double ip = 0.0;
  double fr = mf(D(-3.25), &ip);
  put_kv("modf", milli(fr));
  put_kv("modf integral", milli(ip));
  MSIL_EXPECT(m == 0.75 && e == 6 && fr == -0.25 && ip == -3.0);

  // A pointer to an import as a callback.
  put_kv("apply sqrt", milli(apply(sqrt, D(0.25))));
  put_kv("apply table", milli(apply(unary_ops[zero + 3], D(16.0))));
  put_kv("apply local", milli(apply(f, D(0.5625))));

  // One import, one address, wherever it was taken.
  MSIL_EXPECT(unary_ops[zero + 3] == sqrt && f == sqrt);
  MSIL_EXPECT(f == unary_ops[zero + 3] && unary_ops[zero + 4] == twice);
  MSIL_EXPECT(unary_ops[zero] == floor && unary_ops[zero] != sqrt);
  MSIL_EXPECT(float_ops[zero + 2] == sqrtf && float_ops[zero] == floorf);
  MSIL_EXPECT(float_op == floorf && float_op == float_ops[zero]);
  MSIL_EXPECT(binary_ops[zero] == pow && p2 == binary_ops[zero]);
  MSIL_EXPECT(fx == frexp && mf == modf && ld == ldexp && pf2 == powf);
  MSIL_EXPECT(f != 0 && fx != 0 && float_op != 0);
  return 0;
}

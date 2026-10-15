// K&R function definitions and calls without a prototype in scope
// (Milestone 9). Every function here is declared without a prototype
// ('int add3();') or defined K&R style, so the arguments of each call get
// the default argument promotions (char and short to int, float to
// double) and a K&R parameter declared float, char or short is received in
// its promoted type and converted on entry. clang gives each call the type
// of the promoted arguments; the IR fix-up passes (AddMissingPrototypes,
// FixFunctionBitcasts) and the call lowering must make every call match
// the callee's method signature exactly (no MissingMethodException).
// Covered: calls before and after the definition, a definition with a
// prototype called through an earlier prototype-less declaration, float,
// double, char, short, unsigned, long long and pointer parameters, K&R
// functions returning float, void and a pointer, recursion through a
// prototype-less declaration, the address of a K&R function stored in a
// prototyped function pointer and called through it, and K&R calls through
// function pointers without a prototype ('int (*)()', plain and volatile,
// so the call stays indirect at -O2) with promoted float, char and short
// arguments, a float result, a void result and a prototyped callee. Such a
// call is fixed-arity on every MSIL triple, although native SysV x86-64
// (the linux triple) types it as variadic for %al
// (X86_64TargetCodeGenInfo::isNoProtoCallVariadic), and its calli
// signature must match the callee's method signature. Values are printed;
// MSIL_EXPECT returns 1 + (line % 255) at the first failed check; [exit 0]
// means every check passed.
// RUN: %{msil_c}
#include "msil_test.h"

#pragma clang diagnostic ignored "-Wdeprecated-non-prototype"

#define NOINLINE __attribute__((noinline))

int add3();
double mix();
float halve();
long long widen();
void note();
char *skip();
int later();
int countdown();
unsigned digits();

volatile int zero = 0;
static int notes;

NOINLINE int add3(a, b, c)
int a, b, c;
{
  return a * 100 + b * 10 + c;
}

// Parameters declared float, char and short arrive as double and int.
NOINLINE double mix(f, c, s, d)
float f;
char c;
short s;
double d;
{
  return f * 4 + c + s * 1000 + d;
}

NOINLINE float halve(x)
float x;
{
  return x / 2;
}

NOINLINE long long widen(hi, lo)
long long hi;
unsigned lo;
{
  return hi * 4294967296LL + lo;
}

NOINLINE void note(s, n)
const char *s;
int n;
{
  notes += n;
  put_str(s);
}

NOINLINE char *skip(s, n)
char *s;
int n;
{
  return s + n;
}

int main(void) {
  // Calls before the definitions below and after the ones above.
  MSIL_EXPECT(add3(1, 2, 3) == 123);
  MSIL_EXPECT(later(7, 0.5) == 1407);
  char c = (char)(zero + 'A');
  short s = (short)(zero - 3);
  float f = 2.25f;
  double m = mix(f, c, s, 0.5);
  put_kv("mix*4", (long long)(m * 4));
  MSIL_EXPECT(m == 9.0 + 65 - 3000 + 0.5);
  MSIL_EXPECT(halve(f) == 1.125f && halve(3.0f) == 1.5f);
  put_kv("halve*8", (long long)(halve(f + zero) * 8));
  MSIL_EXPECT(widen(-2LL, 4000000000u) == -8589934592LL + 4000000000LL);
  put_kv("widen", widen((long long)zero + 3, 7u));
  note("a note", 5);
  note("another note", 6 + zero);
  MSIL_EXPECT(notes == 11);
  char text[] = "prototype-less";
  put_str(skip(text, 10));
  MSIL_EXPECT(*skip(text, zero + 3) == 't');
  MSIL_EXPECT(countdown(10 + zero) == 55);
  put_kv("digits", digits(4096u + zero));

  // A K&R function through a function pointer with a prototype.
  int (*p)(int, int, int) = add3;
  int (*volatile vp)(int, int, int) = add3;
  MSIL_EXPECT(p(4, 5, 6) == 456 && vp(9, 8, zero) == 980);
  double (*pm)(double, int, int, double) = mix;
  put_kv("mix via pointer", (long long)pm(1.5, 'x' + zero, 2, -0.5));

  // K&R calls through function pointers without a prototype: each call has
  // the type of its promoted arguments, fixed-arity on every MSIL triple.
  int (*fp)() = add3;
  int (*volatile vfp)() = add3;
  MSIL_EXPECT(fp(1, 2, 3) == 123 && vfp(7, zero, 9) == 709);
  put_kv("add3 via no-prototype pointer", vfp(zero + 3, 1, 4));
  // float to double, char and short to int.
  double (*volatile vmp)() = mix;
  put_kv("mix*4 via no-prototype pointer", (long long)(vmp(f, c, s, 0.25) * 4));
  float g = 0.5f;
  char a = (char)(zero + 'a');
  short two = (short)(zero + 2);
  MSIL_EXPECT(vmp(g, a, two, 1.0) == 2.0 + 97 + 2000 + 1.0);
  double (*mp)() = mix;
  MSIL_EXPECT(mp(g, a, two, 1.0) == 2100.0);
  // A float argument promoted to double and a float result.
  float (*volatile vhp)() = halve;
  MSIL_EXPECT(vhp(f) == 1.125f);
  put_kv("halve*8 via no-prototype pointer", (long long)(vhp(g + 3) * 8));
  // A void result.
  void (*volatile vnp)() = note;
  vnp("a note via a pointer", 4 + zero);
  MSIL_EXPECT(notes == 15);
  // A prototyped definition: the promoted float matches its double.
  int (*volatile vlp)() = later;
  MSIL_EXPECT(vlp(3, g) == 607);
  put_kv("later via no-prototype pointer", vlp(zero + 2, 1.0));
  return 0;
}

// A prototyped definition called through the prototype-less declaration
// above; the promoted argument types match the parameters.
NOINLINE int later(int a, double b) { return a * 200 + (int)(b * 14); }

NOINLINE int countdown(n)
int n;
{
  return n == 0 ? 0 : n + countdown(n - 1);
}

NOINLINE unsigned digits(v)
unsigned v;
{
  return v < 10 ? 1 : 1 + digits(v / 10);
}

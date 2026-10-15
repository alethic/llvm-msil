// Value-returning C constructors in a module without data (Milestone 8, D12):
// this test pins the constructor-only shape of <Module>::.cctor. The module
// has no global variable and no string literal (it does not include
// msil_test.h, and putchar takes characters), so the data segment is empty:
// the .cctor has no '<msil>data' field, no AllocHGlobal, no '<msil>image' and
// no address field, only the calls of the llvm.global_ctors entries in
// priority order and 'ret'. Constructors may return a value, which the .cctor
// must discard: a call of a constructor returning int, long long, double,
// float, a pointer or short is followed by 'pop', a call of a void one is
// not, and a wrong sequence fails ILVerify or the JIT. The constructors are
// defined out of priority order (101 to 500 and one without a priority, which
// runs last), have internal and external linkage, and each writes one letter
// with putchar; their results depend on putchar's result, so they are real
// values on the evaluation stack. The output 'ABCDEFG' is the run-time
// priority order; main ends the line and returns 7, the number of
// constructors.
// RUN: %{msil_c}

int putchar(int c);

__attribute__((constructor)) short c_default(void) {
  return (short)-putchar('G');
}

__attribute__((constructor(500))) static void *c500_pointer(void) {
  return (void *)(__UINTPTR_TYPE__)putchar('F');
}

__attribute__((constructor(300))) double c300_double(void) {
  return putchar('D') * 0.5;
}

__attribute__((constructor(101))) static int c101_int(void) {
  return putchar('A');
}

__attribute__((constructor(250))) static void c250_void(void) { putchar('C'); }

__attribute__((constructor(400))) static float c400_float(void) {
  return putchar('E') * 0.25f;
}

__attribute__((constructor(200))) long long c200_long_long(void) {
  return putchar('B') * 0x100000001ll;
}

int main(void) {
  putchar('\n');
  return 7;
}

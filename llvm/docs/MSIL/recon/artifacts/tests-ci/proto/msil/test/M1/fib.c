// Recursion, a loop, and output through puts. Exit code fib(10) = 55.
// RUN: %{msil_c}

#include "msil_test.h"

static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

int main(void) {
  for (int i = 0; i < 10; ++i)
    put_u64((unsigned)fib(i));
  return fib(10) & 0xff;
}

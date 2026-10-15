// A C math library call through P/Invoke (Milestone 8, D14): main returns
// (int)__builtin_sqrt of 49.0 read from a volatile global, so the call is
// made at run time at every optimization level and the exit status is 7.
// Under the math-errno default of the triple clang emits either a call of
// sqrt or llvm.sqrt (an FSQRT libcall); on both x86_64 MSIL triples it is the
// call. Either way it must end in the sqrt P/Invoke of the triple's math
// library: ucrtbase on windows-msvc, libm.so.6 on linux-gnu (the WSL leg),
// because glibc keeps libm apart from libc.
// RUN: %{msil_c}
volatile double input = 49.0;

int main(void) { return (int)__builtin_sqrt(input); }

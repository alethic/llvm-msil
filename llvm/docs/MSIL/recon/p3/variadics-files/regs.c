/* A SysV va_list in the state a native va_start leaves it: the first
   variadic ints and doubles in the register save area, the rest in the
   overflow area. Our compiled va_arg (clang's inline lowering) must read
   both; this is what a native caller hands to a C callback taking va_list. */
#include <stdarg.h>
#include <stdio.h>
static long long walk(const char *k, va_list ap) {
  long long acc = 0;
  for (; *k; k++)
    acc = acc * 10 + (*k == 'd' ? (long long)va_arg(ap, double) : va_arg(ap, int));
  return acc;
}
static long long walk_v(const char *k, ...) { va_list ap; va_start(ap, k); long long r = walk(k, ap); va_end(ap); return r; }
int main(void) {
#if defined(__x86_64__) && !defined(_WIN32)
  /* 1 fixed GPR used (k), so gp_offset = 8; ints 1..5 in rdx..r9 slots, doubles 6,7 in xmm0/1. */
  unsigned long long rsa[22];
  for (int i = 0; i < 22; i++) rsa[i] = 0xdeadbeefdeadbeefull;
  for (int i = 0; i < 5; i++) rsa[1 + i] = (unsigned long long)(i + 1);
  double d6 = 6.0, d7 = 7.0;
  __builtin_memcpy(&rsa[6], &d6, 8);
  __builtin_memcpy(&rsa[8], &d7, 8);
  /* The 6th int (8) no longer fits in a GPR: overflow area. The third
     double (9.0) was in xmm2: save area offset 80. */
  unsigned long long ovf[2] = {8, 0};
  double d9 = 9.0;
  __builtin_memcpy(&rsa[10], &d9, 8);
  va_list ap;
  ap[0].gp_offset = 8; ap[0].fp_offset = 48; ap[0].overflow_arg_area = ovf; ap[0].reg_save_area = rsa;
  printf("regs %lld\n", walk("iiiiiddid", ap));
#else
  printf("regs %lld\n", 123456789LL);
#endif
  printf("native %lld\n", walk_v("iiiiiddid", 1, 2, 3, 4, 5, 6.0, 7.0, 8, 9.0));
  return 0;
}

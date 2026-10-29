#include <setjmp.h>
int f(int (*g)(void)) {
  jmp_buf b; if (__builtin_setjmp((void**)b)) return 1; return g();
}
void h(void **b) { __builtin_longjmp(b, 1); }

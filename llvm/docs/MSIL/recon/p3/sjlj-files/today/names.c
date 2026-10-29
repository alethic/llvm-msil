#include <setjmp.h>
extern void g(void);
jmp_buf b;
int f1(void) { if (setjmp(b)) return 1; g(); return 0; }
void f2(int v) { longjmp(b, v); }
#ifdef __GLIBC__
sigjmp_buf sb;
int f3(void) { if (sigsetjmp(sb, 1)) return 1; g(); return 0; }
void f4(int v) { siglongjmp(sb, v); }
int f5(void) { if (_setjmp(b)) return 1; g(); return 0; }
void f6(int v) { _longjmp(b, v); }
#endif

#include <setjmp.h>
sigjmp_buf sb;
int f(void) { if (sigsetjmp(sb, 1)) return 1; siglongjmp(sb, 2); }
int g(jmp_buf b) { return _setjmp(b); }

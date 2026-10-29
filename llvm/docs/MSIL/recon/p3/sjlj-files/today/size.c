#include <setjmp.h>
#include <stddef.h>
int jb_size = sizeof(jmp_buf);
int jb_align = _Alignof(jmp_buf);
#ifdef __GLIBC__
int sjb_size = sizeof(sigjmp_buf);
#else
int sjb_size = -1;
#endif

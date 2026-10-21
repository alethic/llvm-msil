/* p6b.c */
int putchar(int);
__attribute__((constructor(120))) static void c120(void) { putchar('B'); }
__attribute__((destructor(120))) static void d120(void) { putchar('b'); }
__attribute__((constructor)) static void cdef(void) { putchar('D'); }

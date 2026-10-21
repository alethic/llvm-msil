/* p6a.c: constructors and destructors with priorities across objects (representation) */
int putchar(int);
__attribute__((constructor(150))) static void c150(void) { putchar('C'); }
__attribute__((destructor(150))) static void d150(void) { putchar('c'); }
int main(void) { putchar('M'); return 0; }

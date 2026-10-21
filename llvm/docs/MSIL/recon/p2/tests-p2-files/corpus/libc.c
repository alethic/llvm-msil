/* Export-table stand-in for glibc libc.so.6 (the prototype msil-ld reads only COFF exports). */
__declspec(dllexport) int puts(const char *s) { (void)s; return 0; }
__declspec(dllexport) int putchar(int c) { return c; }

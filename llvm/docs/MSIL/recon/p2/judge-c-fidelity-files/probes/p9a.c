/* p9a.c: a weak definition ... */
__attribute__((weak)) int cw = 5;
int get(void) { return cw; }

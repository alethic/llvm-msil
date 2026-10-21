int f(int, int);         /* disagrees with the definition in callee.c (C undefined behaviour, but common) */
int main(void) { return f(40, 2) & 0xff; }

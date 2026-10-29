#include <stdio.h>
int (*pf)(const char *, ...) = printf;
int main(void) { return pf("hi %d\n", 1) < 0; }

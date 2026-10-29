#include <stdio.h>
#include <stdlib.h>
static void first(void) { puts("first registered, runs last"); }
static void second(void) { puts("second registered, runs first"); }
__attribute__((destructor)) static void dtor(void) { puts("destructor"); }
int main(void) { atexit(first); atexit(second); printf("main returns %d\n", 42); return 0; }

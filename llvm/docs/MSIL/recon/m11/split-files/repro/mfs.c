__attribute__((noinline)) static int bar(void) { return 1; }
__attribute__((noinline)) static int baz(void) { return 2; }
int foo1(int c) { return c ? bar() : baz(); }

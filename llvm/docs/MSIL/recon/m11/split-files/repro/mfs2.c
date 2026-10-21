__attribute__((noinline)) int bar(void) { return 1; }
__attribute__((noinline)) int baz(void) { return 2; }
int foo1(int c) {
  if (c)
    return bar();
  return baz();
}

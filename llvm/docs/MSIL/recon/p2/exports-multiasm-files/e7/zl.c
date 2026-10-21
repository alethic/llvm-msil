/* E7 library: what 'clang -shared -o zl.dll -Wl,--export-class=Contoso.Zl.Native' would build. */
int counter = 5;                       /* exported data */
static int calls;                      /* internal data */
int *pcounter = &counter;              /* intra-assembly data relocation */
__attribute__((visibility("hidden"))) int helper(int x) { return x * 10; }
int twice(int x) { calls++; return 2 * x; }
int bump(void) { return ++counter; }
int (*get_twice(void))(int) { return twice; }
int apply(int (*f)(int), int x) { return f(x) + helper(0); }
__attribute__((constructor)) static void init(void) { counter = 100; }

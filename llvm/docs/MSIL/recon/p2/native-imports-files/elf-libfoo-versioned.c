int foo(int x) { return x + 1; }
int bar = 42;
_Thread_local int tlsvar;
static int impl(void) { return 7; }
static int (*resolve_ifn(void))(void) { return impl; }
int ifn(void) __attribute__((ifunc("resolve_ifn")));
int new_v1(void) { return 1; }
int new_v2(void) { return 2; }
__asm__(".symver new_v1, newf@V1");
__asm__(".symver new_v2, newf@@V2");
int old_v1(void) { return 3; }
__asm__(".symver old_v1, oldf@V1");

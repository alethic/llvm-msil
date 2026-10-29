void f(void);
__attribute__((destructor)) static void d(void) { f(); }
__attribute__((destructor(150))) static void d150(void) { f(); }
int r(void (*)(void));
int g(void) { return r(f); }

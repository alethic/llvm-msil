/* b.c: callee side of the binding experiment */
int puts(const char *);
int counter = 5;
int common_c;                       /* tentative definition again */
__attribute__((noinline)) static int helper(int x) { return x * 3; }
static int local_g = 11;
const char *msg_b = "hello";        /* same literal: one copy on msvc (comdat) */
int add3(int x) { puts("add3"); counter += 1; local_g++; return helper(x) + counter + local_g; }
int twice(int x) { return 2 * x; }
int weakfn(void) { return 2; }      /* strong: wins over a.c's weak */
int alias_twice(int) __attribute__((alias("twice")));
static volatile int vb = 2;
__attribute__((constructor(100))) static void ctor_b(void) { counter *= vb; }
static volatile int idx = 4;
/* references, from code, a common symbol and a COMDAT string that a.c's copies win */
int b_extra(void) { return common_c * 1000 + "hello"[idx]; }

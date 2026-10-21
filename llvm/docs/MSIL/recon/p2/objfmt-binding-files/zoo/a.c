/* a.c: caller side of the binding experiment */
int puts(const char *);
extern int counter;                 /* strong global defined in b.c */
int add3(int x);                    /* strong function in b.c */
int twice(int x);
int alias_twice(int x);             /* alias defined in b.c */
int b_extra(void);
extern int maybe(void) __attribute__((weak));   /* weak undefined, defined nowhere */
int common_c;                       /* tentative definition (common with -fcommon) */
__attribute__((noinline)) static int helper(int x) { return x + 100; } /* local, same name as b.c's */
static int local_g = 7;             /* local data, same name as b.c's */
typedef int (*fn_t)(int);
fn_t table[3] = { add3, twice, alias_twice };  /* function pointers to another TU in data */
int *pcounter = &counter;           /* data pointer to another TU's global */
const char *msg = "hello";          /* string literal (linkonce_odr comdat on msvc) */
__attribute__((weak)) int weakfn(void) { return 1; }   /* overridden by b.c */
static volatile int va = 10;
__attribute__((constructor(200))) static void ctor_a(void) { counter += va; }
int main(void) {
  puts("main");                          /* native import from two objects */
  local_g++;
  int r = 0;
  r += table[0](1);                      /* add3(1) = 36 */
  r += table[1](2);                      /* 4 */
  r += table[2](3);                      /* 6 */
  r += *pcounter;                        /* 21 */
  r += helper(local_g);                  /* 108 */
  r += weakfn() * 1000;                  /* strong b.c wins: 2000 */
  if (maybe) r += 100000;                /* weak undefined: null */
  common_c += 1; r += common_c;          /* one common_c: 1 */
  r += (table[1] == table[2]) ? 0 : 7;   /* alias identity at run time: 0 */
  r += msg[0];                           /* 104 */
  r += b_extra();                        /* 1 * 1000 + 'o' = 1111 */
  return r & 0xff;                       /* 3391 & 255 = 63 */
}

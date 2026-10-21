/* E7 program: what 'clang main.c zl.dll -o main.exe' would build. */
extern int counter;
extern int *pcounter;
int twice(int);
int bump(void);
int (*get_twice(void))(int);
int apply(int (*)(int), int);
int *pc = &counter;                    /* cross-assembly data relocation */
int (*ft)(int) = twice;                /* cross-assembly function relocation */
static int add1(int x) { return x + 1; }
int main(void) {
  int fails = 0;
  fails |= (counter != 100) << 0;                    /* the library's constructor ran first */
  fails |= (pc != &counter || pcounter != &counter) << 1;
  fails |= (ft != get_twice() || ft != &twice) << 2; /* one address per function */
  fails |= (ft(21) != 42) << 3;
  fails |= (apply(add1, 41) != 42) << 4;             /* program pointer called by the library */
  *pc = 7;
  fails |= (bump() != 8) << 5;
  return fails;
}

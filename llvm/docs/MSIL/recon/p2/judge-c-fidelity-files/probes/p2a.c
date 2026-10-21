/* p2a.c: calls through K&R (unprototyped) declarations; definitions in p2b.c (-std=gnu17) */
int f();                  /* defined as int f(int): one argument too many below */
int g();                  /* defined as int g(int, int) */
double h();               /* defined K&R style with a float parameter (promoted to double) */
int main(void) {
  int r = f(1, 2);        /* native cdecl ignores the extra argument: 11 */
  r += g(3, 4);           /* 7 */
  r += (int)h(2.5f);      /* 5 */
  return r;               /* 23 */
}

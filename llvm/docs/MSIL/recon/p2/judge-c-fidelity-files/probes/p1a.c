/* p1a.c: musl-style weak alias overridden by a strong definition in p1b.c,
   plus a data alias whose identity is checked. */
static int dummy_hook(void) { return 1; }
extern int hook(void) __attribute__((weak, alias("dummy_hook")));
int base_arr[4] = { 1, 2, 3, 4 };
extern int arr_alias[4] __attribute__((alias("base_arr")));
int other_hook(void);                       /* p1b.c or p1c.c */
int *volatile pa = arr_alias, *volatile pb = base_arr;   /* address identity at run time */
int main(void) {
  int r = hook() * 10;                      /* strong hook in p1b.c: 40; weak alias alone: 10 */
  r += (pa == pb) ? 1 : 0;                  /* data alias identity: 1 */
  r += arr_alias[2];                        /* 3 */
  r += other_hook();                        /* 100 */
  return r;                                 /* with p1b.c: 144; with p1c.c: 114 */
}

/* p1m.c: main; links with p1w.c and either p1b.c (strong hook: 144) or p1c.c (114) */
int hook(void);
extern int base_arr[4], arr_alias[4];
int other_hook(void);
int *volatile pa = arr_alias, *volatile pb = base_arr;
int main(void) {
  int r = hook() * 10;
  r += (pa == pb) ? 1 : 0;
  r += arr_alias[2];
  r += other_hook();
  return r;
}

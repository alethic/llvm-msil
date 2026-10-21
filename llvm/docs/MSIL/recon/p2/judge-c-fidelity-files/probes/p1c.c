/* p1c.c: no strong hook, so p1a.c's weak alias stays */
extern int base_arr[4];
int other_hook(void) { return base_arr[0] * 100; }

/* p1b.c: the strong definition that must beat p1a.c's weak alias */
int hook(void) { return 4; }
extern int base_arr[4];
int other_hook(void) { return base_arr[0] * 100; }

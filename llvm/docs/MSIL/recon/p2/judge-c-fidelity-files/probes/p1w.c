/* p1w.c: the weak function alias and the data alias, not used in this TU
   (the option-2 emulation cannot keep in-TU uses of an alias) */
static int dummy_hook(void) { return 1; }
extern int hook(void) __attribute__((weak, alias("dummy_hook")));
int base_arr[4] = { 1, 2, 3, 4 };
extern int arr_alias[4] __attribute__((alias("base_arr")));

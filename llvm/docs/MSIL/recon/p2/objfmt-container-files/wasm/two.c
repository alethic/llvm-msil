extern int counter;
int ext(int);
static int helper(int y) { return y + 1; }
__attribute__((weak)) int twice(int x) { return helper(x) * 2 + counter + ext(x); }
int *table[2] = { &counter, 0 };

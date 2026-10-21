int counter = 1;
int table[4] = {10, 20, 30, 40};
__attribute__((noinline)) static int helper(int y) { return y - 1; }
__attribute__((noinline)) int twice(int x) { return helper(x) * 2 + table[counter]; }

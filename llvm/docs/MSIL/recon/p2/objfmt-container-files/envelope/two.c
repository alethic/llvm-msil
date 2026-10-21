int counter = 1;
__attribute__((noinline)) static int helper(int y) { return y + 1; }
__attribute__((noinline)) int twice(int x) { return helper(x) * 2 + counter; }
int main(void) { return twice(20); }

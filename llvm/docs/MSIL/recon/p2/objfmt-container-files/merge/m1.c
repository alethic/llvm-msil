int seven = 7;
int counter = 1;   /* removed from the IL below: becomes an unresolved reference */
int twice(int x);
__attribute__((noinline)) static int helper(int y) { return y + 1; }
int main(void) { return twice(helper(19)) + counter + seven; }

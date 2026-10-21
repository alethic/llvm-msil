extern int counter;
int twice(int x);
static int helper(int y) { return y + 1; }
int main(void) { return twice(helper(19)) + counter; }

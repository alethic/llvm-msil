extern "C" int twice(int x);
extern "C" int counter;
static int helper(int y) { return y + 1; }
inline int inl(int z) { return z * 3; }
extern "C" int add1(int x) { return helper(x) + inl(x) + counter; }
extern "C" int main(void) { return add1(twice(20)); }

extern "C" int counter = 1;
static int helper(int y) { return y - 1; }
inline int inl(int z) { return z * 3; }
extern "C" int twice(int x) { return helper(x) * 2 + inl(0); }

int puts(const char *);
typedef unsigned _BitInt(200) u200;
__attribute__((noinline)) u200 d(u200 a, u200 b) { return a / b; }
__attribute__((noinline)) double cv(u200 a) { return (double)a; }
int main(void) {
  u200 a = ((u200)0x123456789abcdefull << 130) | 12345;
  u200 q = d(a, 1000000007);
  double f = cv(a);
  return (int)((q ^ (q >> 64) ^ (q >> 128)) & 0x7f) ^ (f > 1e40 ? 1 : 0);
}

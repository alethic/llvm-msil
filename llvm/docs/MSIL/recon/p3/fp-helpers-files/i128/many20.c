typedef unsigned __int128 u128;
int puts(const char *);
__attribute__((noinline)) u128 f(u128 a, u128 b, u128 c) {
  a = a / (b + 0) + a % (c + 0);
  a = a / (b + 1) + a % (c + 1);
  a = a / (b + 2) + a % (c + 2);
  a = a / (b + 3) + a % (c + 3);
  a = a / (b + 4) + a % (c + 4);
  a = a / (b + 5) + a % (c + 5);
  a = a / (b + 6) + a % (c + 6);
  a = a / (b + 7) + a % (c + 7);
  a = a / (b + 8) + a % (c + 8);
  a = a / (b + 9) + a % (c + 9);
  return a;
}
int main(int argc, char **argv) { u128 r = 0; for (int i = 0; i < 200000; ++i) r += f(~(u128)0 / (argc + i), 3 + i, 5); char b[2] = {(char)(48 + (int)(r & 7)), 0}; puts(b); return 0; }

// Compiled with the MSIL clang (x86_64-pc-windows-msvc-msil).
int puts(const char *);
int counter = 3;
const char *names[] = {"alpha", "beta", "gamma", 0};
int sum_squares(int n) {
  int s = 0;
  for (int i = 1; i <= n; i++) s += i * i;
  counter++;
  return s;
}

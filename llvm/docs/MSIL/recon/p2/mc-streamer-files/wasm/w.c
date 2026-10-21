extern int ext(int);
extern int ext_data;
int counter = 1;
int *pcounter = &counter;
int (*fp)(int) = ext;
static int helper(int x) { return x * 3; }
int entry(int a, int (*f)(int)) {
  counter += ext(a) + ext_data;
  return f(a) + helper(a) + fp(a) + *pcounter;
}

#include <math.h>
#include <stdlib.h>
#include <float.h>
int puts(const char *);
static void hx(unsigned long long v){char b[20];for(int i=0;i<16;i++)b[i]="0123456789abcdef"[(v>>(60-4*i))&15];b[16]=0;puts(b);}
int main(int argc, char **argv) {
  long double x = argc + 1.0L;
  long double r = sqrtl(x) + expl(x) + strtold("2.5", 0);
  double d = (double)r;
  unsigned long long u; __builtin_memcpy(&u, &d, 8); hx(u);
  hx(sizeof(long double)); hx(LDBL_MANT_DIG);
  return 0;
}

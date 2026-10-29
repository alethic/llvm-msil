int puts(const char *);
double cabs(double _Complex);
double _Complex cexp(double _Complex);
static void hx(unsigned long long v){char b[20];for(int i=0;i<16;i++)b[i]="0123456789abcdef"[(v>>(60-4*i))&15];b[16]=0;puts(b);}
int main(int argc, char **argv) {
  double _Complex z = argc * 3.0 + 4.0 * 1.0i;
  double a = cabs(z);
  double _Complex e = cexp(z * 0.25);
  unsigned long long u; __builtin_memcpy(&u, &a, 8); hx(u);
  double r = __real__ e; __builtin_memcpy(&u, &r, 8); hx(u);
  return 0;
}

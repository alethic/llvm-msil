// 20M complex multiply+divide steps on ordinary values; prints a checksum.
int puts(const char *);
static void hx(unsigned long long v){char b[20];for(int i=0;i<16;i++)b[i]="0123456789abcdef"[(v>>(60-4*i))&15];b[16]=0;puts(b);}
int main(int argc, char **argv) {
  double _Complex z = 0.5 + 0.25 * 1.0i, w = 1.0001 - 0.0002 * 1.0i * argc, acc = 0;
  for (int i = 0; i < 20000000; ++i) {
    z = z * w;
    acc += z / (w + 1.0);
    if (i % 1024 == 0) z = 0.5 + 0.25 * 1.0i;
  }
  double r = __real__ acc, m = __imag__ acc; unsigned long long u, v;
  __builtin_memcpy(&u, &r, 8); __builtin_memcpy(&v, &m, 8); hx(u); hx(v);
  return 0;
}

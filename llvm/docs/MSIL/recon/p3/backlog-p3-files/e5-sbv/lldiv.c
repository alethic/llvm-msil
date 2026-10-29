typedef struct { long long quot, rem; } lldiv_t;
typedef struct { int quot, rem; } div_t;
lldiv_t lldiv(long long, long long);
div_t div(int, int);
int puts(const char *);
int main(void) {
  lldiv_t r = lldiv(1000000000007LL, 10);
  div_t d = div(47, 5);
  return (int)((r.rem == 7 && r.quot == 100000000000LL) ? 0 : 1) + (d.quot == 9 && d.rem == 2 ? 0 : 2);
}

struct S { long a[8]; };
__attribute__((noinline)) long take(struct S s) { return s.a[0] + s.a[7]; }
long many(struct S *p) {
  long r = 0;
  r += take(p[0]); r += take(p[1]); r += take(p[2]); r += take(p[3]); r += take(p[4]);
  r += take(p[5]); r += take(p[6]); r += take(p[7]); r += take(p[8]); r += take(p[9]);
  return r;
}
long loop(struct S *p, int n) { long r = 0; for (int i = 0; i < n; i++) r += take(p[i]); return r; }

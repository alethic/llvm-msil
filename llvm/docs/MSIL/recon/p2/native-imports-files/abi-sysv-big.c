struct big { long a, b, c; };
struct big mk(long x) { struct big r = { x, x * 2, x * 3 }; return r; }
long sum(struct big v) { return v.a + v.b + v.c; }

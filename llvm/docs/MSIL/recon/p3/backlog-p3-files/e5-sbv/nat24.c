struct s24 { long long a, b, c; };
struct s24 make24(long long x) { struct s24 r = { x, x * 2, x * 3 }; return r; }

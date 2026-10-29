typedef struct { int quot, rem; } div_t;
div_t div(int, int);
int main(void) { div_t d = div(47, 5); return d.quot * 10 + d.rem; }

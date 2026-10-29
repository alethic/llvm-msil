typedef struct { int quot, rem; } div_t;
div_t div(int, int);
int puts(const char *);
int main(void) { div_t d = div(47, 5); char b[4] = {(char)('0' + d.quot), ' ', (char)('0' + d.rem), 0}; puts(b); return d.quot * 10 + d.rem; }

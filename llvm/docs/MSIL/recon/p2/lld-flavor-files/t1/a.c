// Compiled with STOCK clang for the native triple (x86_64-pc-windows-msvc).
int puts(const char *);
unsigned long long strlen(const char *);   // from ucrtbase.dll via -lucrtbase
int atoi(const char *);                    // from ucrtbase.dll via -lucrtbase
int toupper(int);                          // from ucrtbase.dll via -lucrtbase
static const char *volatile text = "twelve chars";
static const char *volatile digits = "-42";
static volatile int lower = 'q';
int sum_squares(int n);                    // b.c
int helper_from_lib(int);                  // libhelp.a(h1.o)
extern const char *names[];                // b.c data, relocations across TUs
extern int counter;                        // b.c
static void put_num(int v) {
  char buf[16]; char *p = buf + 15; *p = 0;
  unsigned u = v < 0 ? -(unsigned)v : (unsigned)v;
  do { *--p = (char)('0' + u % 10); u /= 10; } while (u);
  if (v < 0) *--p = '-';
  puts(p);
}
int main(void) {
  puts("hello from a.c (stock clang bitcode)");
  for (int i = 0; names[i]; i++) puts(names[i]);
  put_num(sum_squares(10));
  put_num(helper_from_lib(5));
  put_num((int)strlen(text));
  put_num(atoi(digits));
  put_num(toupper(lower));
  put_num(counter);
  return 7;
}

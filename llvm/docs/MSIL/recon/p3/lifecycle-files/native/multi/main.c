/* main.c - program linked against libb and libc (in that order): order of constructors, exit handlers and
 * destructors across modules. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define IMPORT __declspec(dllimport)
#else
#define IMPORT
#endif
IMPORT void b_reg(void);
IMPORT void c_reg(void);
static void tr(const char *s) { fputs(s, stderr); fputc('\n', stderr); }
static void h1(void) { tr("prog: atexit h1 (registered before b_reg)"); }
static void h2(void) { tr("prog: atexit h2 (registered after c_reg)"); }
__attribute__((constructor)) static void c(void) { tr("prog: ctor"); }
__attribute__((destructor)) static void d(void) { tr("prog: dtor"); }
int main(int argc, char **argv) {
  tr("prog: main");
  atexit(h1);
  b_reg();
  c_reg();
  atexit(h2);
  if (argc > 1 && !strcmp(argv[1], "exit"))
    exit(3);
  return 0;
}

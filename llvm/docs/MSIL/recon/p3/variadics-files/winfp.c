#include <stdio.h>
#ifdef __MSIL__
/* msvcrt.dll's sprintf is a real native variadic export; called fixed-arity
   (the prototype lists msvcrt_sprintf), with doubles as their i64 bits. */
int msvcrt_sprintf(char *, const char *, ...) __attribute__((import_module("msvcrt.dll"), import_name("sprintf")));
#else
#define msvcrt_sprintf sprintf
#endif
int main(void) {
  char b[128];
  msvcrt_sprintf(b, "%f|%f|%d|%f|%f", 2.5, -1.25, 7, 0.125, 1e10);
  puts(b);
  return 0;
}

/* libc.c - a shared library with a constructor, a destructor and an exit handler (multi-module probe). */
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
static void tr(const char *s) { fputs(s, stderr); fputc('\n', stderr); }
static void h(void) { tr("libC: atexit handler (registered by c_reg)"); }
static void hc(void) { tr("libC: atexit handler (registered in libC ctor)"); }
__attribute__((constructor)) static void c(void) { tr("libC: ctor"); atexit(hc); }
__attribute__((destructor)) static void d(void) { tr("libC: dtor"); }
EXPORT void c_reg(void) { tr("libC: c_reg registers its handler"); atexit(h); }

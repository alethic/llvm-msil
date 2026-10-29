/* libb.c - a shared library with a constructor, a destructor and an exit handler (multi-module probe). */
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
static void tr(const char *s) { fputs(s, stderr); fputc('\n', stderr); }
static void h(void) { tr("libB: atexit handler (registered by b_reg)"); }
static void hc(void) { tr("libB: atexit handler (registered in libB ctor)"); }
__attribute__((constructor)) static void c(void) { tr("libB: ctor"); atexit(hc); }
__attribute__((destructor)) static void d(void) { tr("libB: dtor"); }
EXPORT void b_reg(void) { tr("libB: b_reg registers its handler"); atexit(h); }

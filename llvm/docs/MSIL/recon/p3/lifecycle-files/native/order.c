/* order.c - native C program-lifecycle probe (P3 lifecycle recon).
 * Traces to stderr (unbuffered); stdout gets buffered lines whose flush is
 * observed when stdout is a pipe or file. argv[1] picks how the program ends. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

static const char *mode = "ret";
static int is(const char *m) { return strcmp(mode, m) == 0; }
static void tr(const char *s) { fputs(s, stderr); fputc('\n', stderr); }

static void h_late(void) { tr("atexit h_late (registered by h2 during exit)"); }
static void h_ctor101(void) { tr("atexit h_ctor101 (registered in ctor 101)"); }
static void h_ctor(void) { tr("atexit h_ctor (registered in default ctor)"); }
static void h1(void) {
  tr("atexit h1 (registered 1st in main)");
  printf("stdout: from h1\n");
}
static void h2(void) {
  tr("atexit h2 (registered 2nd in main)");
  if (is("nested")) {
    int r = atexit(h_late);
    fprintf(stderr, "  h2: atexit(h_late) during exit returned %d\n", r);
  }
}
static void h3(void) {
  tr("atexit h3 calls exit(9)");
  exit(9);
}
static void h_last(void) { tr("atexit h_last (registered after exit-calling h3)"); }

__attribute__((constructor(101))) static void c101(void) {
  tr("ctor 101");
  atexit(h_ctor101);
}
__attribute__((constructor)) static void cdef(void) {
  tr("ctor default");
  atexit(h_ctor);
  if (getenv("ORDER_EXIT_IN_CTOR")) {
    tr("  ctor default calls exit(13)");
    exit(13);
  }
}
__attribute__((destructor(101))) static void d101(void) { tr("dtor 101"); }
__attribute__((destructor(200))) static void d200(void) { tr("dtor 200"); }
__attribute__((destructor)) static void ddef(void) {
  tr("dtor default");
  printf("stdout: from dtor default\n");
  if (is("exitindtor")) {
    tr("  dtor default calls exit(11)");
    exit(11);
  }
}
static void d_late(void) { tr("atexit d_late (registered by a destructor)"); }
__attribute__((destructor(300))) static void d300(void) {
  tr("dtor 300");
  if (is("atexitindtor")) {
    int r = atexit(d_late);
    fprintf(stderr, "  dtor 300: atexit(d_late) returned %d\n", r);
  }
}

#ifdef _WIN32
static void xt_slot(void) { tr(".CRT$XTU slot"); }
#pragma section(".CRT$XTU", read)
__declspec(allocate(".CRT$XTU")) __attribute__((used)) static void (*const p_xt)(void) = xt_slot;
static void xp_slot(void) { tr(".CRT$XPU slot"); }
#pragma section(".CRT$XPU", read)
__declspec(allocate(".CRT$XPU")) __attribute__((used)) static void (*const p_xp)(void) = xp_slot;
#else
static void fa_slot(void) { tr(".fini_array slot (plain)"); }
__attribute__((used, section(".fini_array"))) static void (*const p_fa)(void) = fa_slot;
static void fa150_slot(void) { tr(".fini_array.150 slot"); }
__attribute__((used, section(".fini_array.150"))) static void (*const p_fa150)(void) = fa150_slot;
extern int __cxa_atexit(void (*)(void *), void *, void *);
extern void *__dso_handle;
static void cxa(void *p) { tr(p == &mode ? "__cxa_atexit handler (arg ok)" : "__cxa_atexit handler (arg BAD)"); }
#endif

static void q1(void) { tr("at_quick_exit q1"); }
static void q2(void) {
  tr("at_quick_exit q2");
  printf("stdout: from q2\n");
}

int main(int argc, char **argv) {
  if (argc > 1)
    mode = argv[1];
  printf("stdout: buffered line from main\n");
  tr("main start");
  atexit(h1);
  atexit(h2);
#ifndef _WIN32
  __cxa_atexit(cxa, &mode, __dso_handle);
#endif
  at_quick_exit(q1);
  at_quick_exit(q2);
  if (is("exitinhandler")) {
    atexit(h3);
    atexit(h_last);
  }
  tr("main end");
  if (is("ret") || is("nested") || is("exitindtor") || is("exitinhandler") || is("atexitindtor"))
    return 0;
  if (is("ret300"))
    return 300;
  if (is("exit"))
    exit(3);
  if (is("exit300"))
    exit(300);
  if (is("exitneg"))
    exit(-1);
  if (is("_Exit"))
    _Exit(5);
  if (is("_exit"))
    _exit(5);
  if (is("quick"))
    quick_exit(6);
  if (is("abort"))
    abort();
  tr("unknown mode");
  return 1;
}

/* hosted-libc recon: stand-ins for Phase 3 features the simulation lacks.
   Forced in with -include; each macro renames one native call that today's
   msil-ld refuses (callback list: signal, sigaction, atexit, Win32 handler
   registration) to a do-nothing helper in sim.c. Nothing here is a proposal. */
#ifndef MSIL_SIM_H
#define MSIL_SIM_H
#if defined(__MSIL__)
#define signal(s, h) msil_sim_signal(s, h)
#define sigaction(s, a, o) msil_sim_sigaction(s, a, o)
#define atexit(f) msil_sim_atexit(f)
#define SetConsoleCtrlHandler(h, a) msil_sim_SetConsoleCtrlHandler(h, a)
#if defined(_WIN32) && defined(MSIL_SIM_LUA_ABORT)
/* Lua without setjmp: an error or a yield ends the program (exit 98). */
#define LUAI_THROW(L, c) msil_sim_throw(L, (c)->status)
#define LUAI_TRY(L, c, a) { a }
#define luai_jmpbuf int
void msil_sim_throw(void *L, int status);
#endif
#if defined(_WIN32) && defined(MSIL_SIM_LUA_NATIVE_SJLJ)
/* Lua with vcruntime's native setjmp/longjmp, frame 0 (no unwinding), the
   Windows twin of what the Linux triple does today with glibc's _setjmp. */
__attribute__((returns_twice, import_module("vcruntime140.dll"), import_name("__intrinsic_setjmp")))
int msil_sim_setjmp(void *buf, void *frame);
__attribute__((noreturn, import_module("vcruntime140.dll"), import_name("longjmp")))
void msil_sim_longjmp(void *buf, int v);
#define LUAI_THROW(L, c) msil_sim_longjmp((c)->b, 1)
#define LUAI_TRY(L, c, a) if (msil_sim_setjmp((c)->b, 0) == 0) { a }
#define luai_jmpbuf jmp_buf
#endif
#endif
#endif
#if defined(__MSIL__) && defined(MSIL_SIM_SQLITE)
#define pthread_create(t, a, f, x) msil_sim_pthread_create(t, a, f, x)
#endif
#if defined(__MSIL__) && defined(MSIL_SIM_SQLITE) && defined(_WIN32)
#define _beginthreadex(s, st, f, a, fl, id) msil_sim_beginthreadex(s, st, f, a, fl, id)
#endif

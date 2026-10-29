/* hosted-libc recon: do-nothing helpers behind sim.h; compiled WITHOUT -include sim.h. */
#include <stdio.h>
#include <stdlib.h>
typedef void (*msil_sim_handler)(int);
msil_sim_handler msil_sim_signal(int s, msil_sim_handler h) { (void)s; (void)h; return 0; }
struct sigaction;
int msil_sim_sigaction(int s, const struct sigaction *a, struct sigaction *o) { (void)s; (void)a; (void)o; return 0; }
int msil_sim_atexit(void (*f)(void)) { (void)f; return 0; }
int msil_sim_SetConsoleCtrlHandler(void *h, int a) { (void)h; (void)a; return 1; }
/* Lua's LUAI_THROW stand-in for the Windows triple (no setjmp/longjmp yet). */
void msil_sim_throw(void *L, int status) {
  (void)L;
  fprintf(stderr, "msil-sim: LUAI_THROW(%d) reached: setjmp/longjmp not simulated on this triple\n", status);
  fflush(stderr);
  exit(98);
}
/* SQLite's sorter threads (never started at SQLITE_DEFAULT_WORKER_THREADS=0). */
int msil_sim_pthread_create(void *t, const void *a, void *(*f)(void *), void *x) { (void)t; (void)a; (void)f; (void)x; return 11; }
#ifdef _WIN32
unsigned long long msil_sim_beginthreadex(void *s, unsigned st, unsigned (*f)(void *), void *a, unsigned fl, unsigned *id) { (void)s; (void)st; (void)f; (void)a; (void)fl; (void)id; return 0; }
#endif

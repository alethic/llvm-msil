/* E6c: a pre-C11 spin loop on a plain int with a compiler barrier (D36's MEMBARRIER), and the same
   loop on a volatile int and with a relaxed atomic load. Each spin gives up after SPINS iterations. */
#include <stdatomic.h>
#ifdef PROTO
#include "../e8-threads/proto-atomic.h"
#define IMPORT(m) __attribute__((import_module(m)))
#else
#define IMPORT(m)
#endif
typedef __SIZE_TYPE__ size_t;
IMPORT(LIBC) int puts(const char *);
int plain_flag;
volatile int vol_flag;
_Atomic int at_flag;
#ifdef _WIN32
typedef unsigned long DWORD; typedef void *HANDLE;
IMPORT("kernel32.dll") HANDLE CreateThread(void *, size_t, DWORD (*)(void *), void *, DWORD, DWORD *);
IMPORT("kernel32.dll") void Sleep(DWORD);
static void sleep_ms(int ms) { Sleep(ms); }
static DWORD start(void *arg) { (void)arg; sleep_ms(300); plain_flag = 1; vol_flag = 1; atomic_store_explicit(&at_flag, 1, memory_order_relaxed); return 0; }
#else
typedef unsigned long pthread_t;
IMPORT("libc.so.6") int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
IMPORT("libc.so.6") int usleep(unsigned);
static void sleep_ms(int ms) { usleep(ms * 1000); }
static void *start(void *arg) { (void)arg; sleep_ms(300); plain_flag = 1; vol_flag = 1; atomic_store_explicit(&at_flag, 1, memory_order_relaxed); return 0; }
#endif
__attribute__((noinline)) static int spin_barrier(void) {
  for (long long i = 0; i < 20000000000LL; i++) {   /* ~10+ s, then give up */
    if (plain_flag) return 1;
    __asm__ volatile("" ::: "memory");
  }
  return 0;
}
__attribute__((noinline)) static int spin_volatile(void) {
  for (long long i = 0; i < 20000000000LL; i++) if (vol_flag) return 1;
  return 0;
}
__attribute__((noinline)) static int spin_relaxed(void) {
  for (long long i = 0; i < 20000000000LL; i++) if (atomic_load_explicit(&at_flag, memory_order_relaxed)) return 1;
  return 0;
}
int main(void) {
#ifdef _WIN32
  CreateThread(0, 0, start, 0, 0, 0);
#else
  pthread_t h; pthread_create(&h, 0, start, 0);
#endif
#if WHICH == 0
  puts(spin_barrier() ? "plain + asm memory barrier: exited" : "plain + asm memory barrier: GAVE UP (load hoisted)");
#elif WHICH == 1
  puts(spin_volatile() ? "volatile: exited" : "volatile: GAVE UP");
#else
  puts(spin_relaxed() ? "relaxed atomic: exited" : "relaxed atomic: GAVE UP");
#endif
  return 0;
}

/* E8b: a thread function that ends with pthread_exit / ExitThread instead of returning, then a GC-heavy
   phase in the main thread (Marshal-free: the runtime's own allocations come from the P/Invokes). */
#include <stdatomic.h>
#ifdef PROTO
#include "proto-atomic.h"
#define IMPORT(m) __attribute__((import_module(m)))
#else
#define IMPORT(m)
#endif
typedef __SIZE_TYPE__ size_t;
IMPORT(LIBC) int puts(const char *);
_Atomic int hits;
#ifdef _WIN32
typedef unsigned long DWORD; typedef void *HANDLE;
IMPORT("kernel32.dll") HANDLE CreateThread(void *, size_t, DWORD (*)(void *), void *, DWORD, DWORD *);
IMPORT("kernel32.dll") DWORD WaitForSingleObject(HANDLE, DWORD);
IMPORT("kernel32.dll") int GetExitCodeThread(HANDLE, DWORD *);
IMPORT("kernel32.dll") void ExitThread(DWORD);
static DWORD start(void *arg) { atomic_fetch_add(&hits, 1); if (arg) ExitThread(42); return 7; }
#else
typedef unsigned long pthread_t;
IMPORT("libc.so.6") int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
IMPORT("libc.so.6") int pthread_join(pthread_t, void **);
IMPORT("libc.so.6") void pthread_exit(void *);
static void *start(void *arg) { atomic_fetch_add(&hits, 1); if (arg) pthread_exit((void *)42); return (void *)7; }
#endif
static void put_num(long long v) { char b[32]; char *p = b + 31; *p = 0; do { *--p = (char)('0' + v % 10); v /= 10; } while (v); puts(p); }
int main(void) {
  for (int round = 0; round < 2; round++) {
    void *arg = (void *)(long)round;   /* round 0: return; round 1: exit call */
#ifdef _WIN32
    HANDLE h = CreateThread(0, 0, start, arg, 0, 0);
    WaitForSingleObject(h, 0xFFFFFFFF);
    DWORD code = 0; GetExitCodeThread(h, &code);
    puts(round ? "ExitThread: exit code" : "return: exit code"); put_num(code);
#else
    pthread_t t; pthread_create(&t, 0, start, arg);
    void *res = 0; pthread_join(t, &res);
    puts(round ? "pthread_exit: value" : "return: value"); put_num((long)res);
#endif
  }
  puts("main done");
  return atomic_load(&hits);
}

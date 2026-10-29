/* E8d: cost of creating and joining a thread whose start routine is a managed method behind a delegate
   thunk (first reverse P/Invoke on a new OS thread attaches it to the runtime), and process exit while
   a created thread still runs (EXITRUN=1). */
#include <stdatomic.h>
#ifdef PROTO
#include "proto-atomic.h"
#define IMPORT(m) __attribute__((import_module(m)))
#else
#define IMPORT(m)
#endif
typedef __SIZE_TYPE__ size_t;
IMPORT(LIBC) int puts(const char *);
_Atomic int ran;
#ifndef NT
#define NT 2000
#endif
#ifdef _WIN32
typedef unsigned long DWORD; typedef void *HANDLE;
IMPORT("kernel32.dll") HANDLE CreateThread(void *, size_t, DWORD (*)(void *), void *, DWORD, DWORD *);
IMPORT("kernel32.dll") DWORD WaitForSingleObject(HANDLE, DWORD);
IMPORT("kernel32.dll") int CloseHandle(HANDLE);
IMPORT("kernel32.dll") int QueryPerformanceCounter(long long *);
IMPORT("kernel32.dll") int QueryPerformanceFrequency(long long *);
static long long now_ns(void) { long long c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return (long long)((double)c * 1e9 / (double)f); }
static DWORD start(void *arg) { atomic_fetch_add(&ran, 1); if (arg) for (;;) { } return 0; }
static void run_one(void *arg, int join) { HANDLE h = CreateThread(0, 0, start, arg, 0, 0); if (join) WaitForSingleObject(h, 0xFFFFFFFF); CloseHandle(h); }
#else
typedef unsigned long pthread_t;
struct timespec_ { long tv_sec, tv_nsec; };
IMPORT("libc.so.6") int clock_gettime(int, struct timespec_ *);
IMPORT("libc.so.6") int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
IMPORT("libc.so.6") int pthread_join(pthread_t, void **);
static long long now_ns(void) { struct timespec_ t; clock_gettime(1, &t); return t.tv_sec * 1000000000LL + t.tv_nsec; }
static void *start(void *arg) { atomic_fetch_add(&ran, 1); if (arg) for (;;) { } return 0; }
static void run_one(void *arg, int join) { pthread_t t; pthread_create(&t, 0, start, arg); if (join) pthread_join(t, 0); }
#endif
static void put_num(long long v) { char b[32]; char *p = b + 31; *p = 0; do { *--p = (char)('0' + v % 10); v /= 10; } while (v); puts(p); }
int main(void) {
#if EXITRUN
  run_one((void *)1, 0);                 /* a thread that never ends */
  while (atomic_load(&ran) == 0) { }
  puts("main returns while a thread spins");
  return 3;
#else
  run_one(0, 1);                         /* warm up */
  long long t0 = now_ns();
  for (int i = 0; i < NT; i++) run_one(0, 1);
  long long t1 = now_ns();
  puts("microseconds per create+join:");
  put_num((t1 - t0) / NT / 1000);
  return atomic_load(&ran) == NT + 1 ? 0 : 1;
#endif
}

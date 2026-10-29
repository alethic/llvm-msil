/* E6: store-buffering litmus test (Dekker). T0: x = 1; r0 = y.  T1: y = 1; r1 = x.
   r0 == r1 == 0 is forbidden when all four accesses are seq_cst, allowed (and seen on x86)
   with release stores and acquire loads. */
#include <stdatomic.h>
#ifdef PROTO
#include "../e8-threads/proto-atomic.h"
#define IMPORT(m) __attribute__((import_module(m)))
#else
#define IMPORT(m)
#endif
#ifndef ROUNDS
#define ROUNDS 2000000
#endif
#ifndef ORD_ST
#define ORD_ST memory_order_seq_cst
#define ORD_LD memory_order_seq_cst
#endif
typedef __SIZE_TYPE__ size_t;
IMPORT(LIBC) int puts(const char *);
_Atomic int x, y, r0, r1;
_Atomic int go0, go1, done0, done1;
static void t0(void) {
  for (int i = 1; i <= ROUNDS; i++) {
    while (atomic_load(&go0) != i) { }
    atomic_store_explicit(&x, 1, ORD_ST);
    atomic_store_explicit(&r0, atomic_load_explicit(&y, ORD_LD), memory_order_relaxed);
    atomic_store(&done0, i);
  }
}
#ifdef _WIN32
typedef unsigned long DWORD; typedef void *HANDLE;
IMPORT("kernel32.dll") HANDLE CreateThread(void *, size_t, DWORD (*)(void *), void *, DWORD, DWORD *);
IMPORT("kernel32.dll") DWORD WaitForSingleObject(HANDLE, DWORD);
static DWORD start(void *arg) { (void)arg; t0(); return 0; }
#else
typedef unsigned long pthread_t;
IMPORT("libc.so.6") int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
IMPORT("libc.so.6") int pthread_join(pthread_t, void **);
static void *start(void *arg) { (void)arg; t0(); return 0; }
#endif
static void put_num(long long v) {
  char buf[32]; char *p = buf + sizeof buf - 1; *p = 0;
  do { *--p = (char)('0' + v % 10); v /= 10; } while (v);
  puts(p);
}
int main(void) {
#ifdef _WIN32
  HANDLE h = CreateThread(0, 0, start, 0, 0, 0);
#else
  pthread_t h; pthread_create(&h, 0, start, 0);
#endif
  long long both0 = 0;
  for (int i = 1; i <= ROUNDS; i++) {
    atomic_store(&x, 0); atomic_store(&y, 0);
    atomic_store(&go0, i);                      /* T0 starts */
    atomic_store_explicit(&y, 1, ORD_ST);       /* this thread is T1 */
    int v1 = atomic_load_explicit(&x, ORD_LD);
    while (atomic_load(&done0) != i) { }
    if (atomic_load(&r0) == 0 && v1 == 0) both0++;
  }
#ifdef _WIN32
  WaitForSingleObject(h, 0xFFFFFFFF);
#else
  pthread_join(h, 0);
#endif
  puts("r0 == r1 == 0 seen:");
  put_num(both0);
  return 0;
}

/* cbnative: the native side of the callback experiments (Windows DLL and Linux .so). */
#include <stdlib.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#define EXPORT __declspec(dllexport)
#else
#include <pthread.h>
#include <dlfcn.h>
#define EXPORT __attribute__((visibility("default")))
#endif

typedef int (*cb_t)(int);

/* One call through a pointer that native code received. */
EXPORT int call_cb(cb_t f, int x) { return f(x); }

/* n calls; the pointer is read through a volatile so the call is never folded. */
EXPORT long long loop_cb(cb_t f, int n) {
  cb_t volatile vf = f;
  long long s = 0;
  for (int i = 0; i < n; i++) s += vf(i);
  return s;
}

static int native_add1(int x) { return x + 1; }
/* A native function pointer handed to managed code (the dlsym/GetProcAddress case). */
EXPORT void *get_native_fn(void) { return (void *)native_add1; }
EXPORT long long loop_native(int n) { return loop_cb(native_add1, n); }

/* qsort with a comparator that native qsort calls. */
EXPORT void do_qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *)) {
  qsort(base, n, sz, cmp);
}

/* A function pointer read from memory that managed code wrote (struct field case). */
struct ops { cb_t f; int bias; };
EXPORT int call_ops(struct ops *o, int x) { return o->f(x) + o->bias; }

/* Start a native thread on a start routine and join it. */
typedef void *(*start_t)(void *);
#ifdef _WIN32
struct tramp { start_t s; void *a; void *r; };
static DWORD WINAPI tramp_fn(LPVOID p) { struct tramp *t = p; t->r = t->s(t->a); return 0; }
EXPORT int run_thread(start_t s, void *arg, void **res) {
  struct tramp t = {s, arg, 0};
  HANDLE h = CreateThread(0, 0, tramp_fn, &t, 0, 0);
  if (!h) return -1;
  WaitForSingleObject(h, INFINITE);
  CloseHandle(h);
  if (res) *res = t.r;
  return 0;
}
EXPORT uintptr_t native_thread_id(void) { return GetCurrentThreadId(); }
#else
EXPORT int run_thread(start_t s, void *arg, void **res) {
  pthread_t t;
  int r = pthread_create(&t, 0, s, arg);
  if (r) return r;
  return pthread_join(t, res);
}
EXPORT uintptr_t native_thread_id(void) { return (uintptr_t)pthread_self(); }
#endif

/* Calls a callback that may longjmp/throw: native frames between managed frames. */
EXPORT int call_cb_twice(cb_t f, int x) { int a = f(x); int b = f(x + 1); return a + b; }

static int native_cmp(const void *a, const void *b) {
  int x = *(const int *)a, y = *(const int *)b;
  return (x > y) - (x < y);
}
EXPORT void *get_native_cmp(void) { return (void *)native_cmp; }
/* A long-running native function whose address managed code calls (GC-suspension hazard). */
EXPORT void *get_loop_native(void) { return (void *)loop_native; }

#ifndef _WIN32
#include <signal.h>
#include <time.h>
/* Asynchronous signal delivery to a thread that runs managed code. */
struct killer { pthread_t target; int sig; int delay_ms; };
static void *killer_fn(void *p) {
  struct killer k = *(struct killer *)p;
  free(p);
  struct timespec ts = {k.delay_ms / 1000, (k.delay_ms % 1000) * 1000000L};
  nanosleep(&ts, 0);
  pthread_kill(k.target, k.sig);
  return 0;
}
EXPORT int start_killer(int sig, int delay_ms) {
  struct killer *k = malloc(sizeof *k);
  k->target = pthread_self(); k->sig = sig; k->delay_ms = delay_ms;
  pthread_t t;
  return pthread_create(&t, 0, killer_fn, k);
}
#endif

#ifndef _WIN32
/* pthread_key_create destructors that are managed thunks, run at native thread exit. */
static pthread_key_t key;
static void *set_and_exit(void *arg) { pthread_setspecific(key, arg); return 0; }
EXPORT int key_create(void (*dtor)(void *)) { return pthread_key_create(&key, dtor); }
EXPORT int key_set(void *v) { return pthread_setspecific(key, v); }
/* A native thread that never ran managed code sets the key and exits. */
EXPORT int key_native_thread(void *v) { pthread_t t; pthread_create(&t, 0, set_and_exit, v); return pthread_join(t, 0); }
#endif

/* Callbacks that take or return structs by value (the thunk must follow the native ABI). */
struct s16 { long long a, b; };
struct s24 { long long a, b, c; };
EXPORT long long call_s16(long long (*f)(struct s16), long long x) { struct s16 s = {x, 2 * x}; return f(s); }
EXPORT long long call_s24(long long (*f)(struct s24), long long x) { struct s24 s = {x, 2 * x, 3 * x}; return f(s); }
EXPORT long long call_r16(struct s16 (*g)(long long), long long x) { struct s16 r = g(x); return r.a * 1000 + r.b; }
EXPORT long long call_r24(struct s24 (*g)(long long), long long x) { struct s24 r = g(x); return r.a * 1000000 + r.b * 1000 + r.c; }

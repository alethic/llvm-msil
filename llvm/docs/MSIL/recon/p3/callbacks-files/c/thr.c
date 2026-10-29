/* A C start routine on a native thread (pthread_create / CreateThread), compiled by the MSIL toolchain. */
int puts(const char *);
static void put_int(long v) {
  char buf[24], *p = buf + 23; int neg = v < 0; unsigned long u = neg ? -(unsigned long)v : (unsigned long)v;
  *p = 0; do { *--p = (char)('0' + u % 10); u /= 10; } while (u); if (neg) *--p = '-'; puts(p);
}
static int counter;
#ifdef _WIN32
typedef unsigned long DWORD;
void *CreateThread(void *, unsigned long long, DWORD (*)(void *), void *, DWORD, DWORD *)
    __attribute__((import_module("kernel32.dll"), import_name("CreateThread")));
DWORD WaitForSingleObject(void *, DWORD) __attribute__((import_module("kernel32.dll"), import_name("WaitForSingleObject")));
int GetExitCodeThread(void *, DWORD *) __attribute__((import_module("kernel32.dll"), import_name("GetExitCodeThread")));
static DWORD worker(void *arg) { int *p = arg; *p *= 2; counter++; return (DWORD)(*p + 1); }
int main(void) {
  int v = 21; DWORD code = 0;
  void *h = CreateThread(0, 0, worker, &v, 0, 0);
  WaitForSingleObject(h, 0xFFFFFFFFu);
  GetExitCodeThread(h, &code);
  put_int(v); put_int((long)code); put_int(counter);
  return 0;
}
#else
typedef unsigned long pthread_t;
int pthread_create(pthread_t *, const void *, void *(*)(void *), void *)
    __attribute__((import_module("libc.so.6"), import_name("pthread_create")));
int pthread_join(pthread_t, void **) __attribute__((import_module("libc.so.6"), import_name("pthread_join")));
static void *worker(void *arg) { int *p = arg; *p *= 2; counter++; return (void *)(long)(*p + 1); }
int main(void) {
  int v = 21; pthread_t t; void *r = 0;
  pthread_create(&t, 0, worker, &v);
  pthread_join(t, &r);
  put_int(v); put_int((long)r); put_int(counter);
  return 0;
}
#endif

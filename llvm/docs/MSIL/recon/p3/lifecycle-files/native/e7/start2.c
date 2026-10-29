/* start2.c - the E7 Linux oracle's _start (msil/utils/linux-oracle/start.c) extended for P3's lifecycle rows:
 * it registers a .fini_array runner with __cxa_atexit before it runs .init_array, as glibc's __libc_start_main
 * registers _dl_fini first, so the destructors run after every exit handler, in reverse array order; and it
 * defines atexit and at_quick_exit as libc_nonshared.a does (glibc's libc.so.6 exports only __cxa_atexit and
 * __cxa_at_quick_exit). */
int main(int, char **);
void exit(int) __attribute__((noreturn));
int __cxa_atexit(void (*)(void *), void *, void *);
int __cxa_at_quick_exit(void (*)(void *), void *);
extern void *__dso_handle;
typedef void (*msil_init_fn)(int, char **, char **);
typedef void (*msil_fini_fn)(void);
extern msil_init_fn __init_array_start[] __attribute__((weak));
extern msil_init_fn __init_array_end[] __attribute__((weak));
extern msil_fini_fn __fini_array_start[] __attribute__((weak));
extern msil_fini_fn __fini_array_end[] __attribute__((weak));
int atexit(void (*f)(void)) { return __cxa_atexit((void (*)(void *))f, 0, __dso_handle); }
int at_quick_exit(void (*f)(void)) { return __cxa_at_quick_exit((void (*)(void *))f, __dso_handle); }
static void run_fini_array(void *unused) {
  (void)unused;
  for (msil_fini_fn *f = __fini_array_end; f > __fini_array_start;)
    (*--f)();
}
__attribute__((used)) static void msil_start_c(long *sp) {
  int argc = (int)sp[0];
  char **argv = (char **)(sp + 1);
  char **envp = argv + argc + 1;
  __cxa_atexit(run_fini_array, 0, __dso_handle);
  for (msil_init_fn *f = __init_array_start; f < __init_array_end; ++f)
    (*f)(argc, argv, envp);
  exit(main(argc, argv));
}
__attribute__((naked, noreturn)) void _start(void) {
  __asm__ volatile("xor %ebp, %ebp\n\t"
                   "mov %rsp, %rdi\n\t"
                   "and $-16, %rsp\n\t"
                   "call msil_start_c\n\t"
                   "hlt");
}

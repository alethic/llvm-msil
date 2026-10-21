/* A minimal crt for a native Linux oracle linked on Windows (tests-p2 recon E7):
   no glibc crt1.o, crti.o or headers are needed. ld.so runs libc.so.6's own
   initializers before _start; this runs the program's .init_array (C
   constructors), then main, then exit (which flushes stdio and runs atexit). */
int main(int, char **);
void exit(int) __attribute__((noreturn));
typedef void (*msil_init_fn)(int, char **, char **);
extern msil_init_fn __init_array_start[] __attribute__((weak));
extern msil_init_fn __init_array_end[] __attribute__((weak));
__attribute__((used)) static void msil_start_c(long *sp) {
  int argc = (int)sp[0];
  char **argv = (char **)(sp + 1);
  char **envp = argv + argc + 1;
  for (msil_init_fn *f = __init_array_start; f < __init_array_end; ++f)
    (*f)(argc, argv, envp);
  exit(main(argc, argv));
}
__attribute__((naked, noreturn)) void _start(void) {
  __asm__ volatile("xor %ebp, %ebp\n\tmov %rsp, %rdi\n\tand $-16, %rsp\n\tcall msil_start_c\n\thlt");
}

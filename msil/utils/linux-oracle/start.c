/*===-- start.c - Program entry of the E7 Linux native oracle ----*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* A minimal crt for the native Linux oracle that is linked on Windows (plan  *|
|* 5.12.8, recon tests-p2 E7): no glibc crt1.o, crti.o or headers are needed. *|
|* Built with                                                                 *|
|*   clang --target=x86_64-unknown-linux-gnu -ffreestanding -fno-pic -c       *|
|* and linked first by                                                        *|
|*   ld.lld -dynamic-linker /lib64/ld-linux-x86-64.so.2 --as-needed start.o   *|
|*     <objects> libm.so.6 libc.so.6 libgcc_s.so.1                            *|
|* ld.so runs libc.so.6's own initializers before _start; this runs the       *|
|* program's .init_array (C constructors), then main, then exit (which        *|
|* flushes stdio and runs atexit).                                            *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

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
  __asm__ volatile("xor %ebp, %ebp\n\t"
                   "mov %rsp, %rdi\n\t"
                   "and $-16, %rsp\n\t"
                   "call msil_start_c\n\t"
                   "hlt");
}

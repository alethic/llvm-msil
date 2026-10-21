/* Minimal _start for a no-sudo glibc oracle: no crt1.o (libc6-dev is not installed). */
int main(int, char **);
void exit(int) __attribute__((noreturn));
__attribute__((naked, noreturn)) void _start(void) {
  __asm__ volatile(
      "xor %ebp, %ebp\n\t"
      "mov (%rsp), %edi\n\t"      /* argc */
      "lea 8(%rsp), %rsi\n\t"     /* argv */
      "and $-16, %rsp\n\t"
      "call main\n\t"
      "mov %eax, %edi\n\t"
      "call exit\n\t");
}

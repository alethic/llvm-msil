/* hosted-libc recon: glibc _FORTIFY_SOURCE with clang (bits/stdio2.h's clang branch
   defines printf & co. as static overloadable variadic functions over __v*_chk). */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
struct S { char name[8]; int n; };
__attribute__((noinline)) static void fill(struct S *s, const char *src, size_t n) { memcpy(s->name, src, n); }
int main(int argc, char **argv) {
  char buf[32], small[16];
  struct S s;
  printf("argc=%d %s %5.2f %ld\n", argc, "fortified", 3.14159, 1234567890123L);
  fprintf(stderr, "to stderr %d\n", 7);
  snprintf(buf, sizeof buf, "%s-%d-%.3e", "x", 42, 6.02e23);
  puts(buf);
  sprintf(small, "%d:%d", 1234, 5678); puts(small);
  strcpy(small, "abc"); strcat(small, "def"); puts(small);
  fill(&s, "1234567", 8); s.n = 3; printf("%s %d\n", s.name, s.n);
  int fd = open("/proc/self/stat", O_RDONLY); ssize_t r = read(fd, buf, sizeof buf - 1); close(fd);
  printf("read>0=%d\n", r > 0);
  char *p = realpath(".", NULL); printf("realpath=%d\n", p != NULL); free(p);
  return 0;
}

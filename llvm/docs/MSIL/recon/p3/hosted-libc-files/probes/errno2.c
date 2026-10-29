/* hosted-libc recon: glibc errno (through __errno_location) after a failing P/Invoke. */
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
int main(void) {
  int fd0 = open("/no-such-dir-msil/f", O_RDONLY); int e0 = errno;
  printf("first: open=%d errno=%d (want %d) %s\n", fd0, e0, ENOENT, strerror(e0));
  int bad = 0, bad2 = 0;
  for (int i = 0; i < 300000; i++) {
    errno = 0;
    int fd = open(i & 1 ? "/no-such-dir-msil/f" : "/proc/self/fd/999999", O_RDONLY);
    if (fd >= 0 || errno != ENOENT) { if (bad < 5) printf("i=%d errno=%d\n", i, errno); bad++; }
    errno = 0;
    if (close(-1) != -1 || errno != EBADF) bad2++;
  }
  printf("mismatches of 300000: open/ENOENT %d close/EBADF %d\n", bad, bad2);
  return 0;
}

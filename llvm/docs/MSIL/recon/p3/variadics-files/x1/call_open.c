#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#define open _open
#else
#include <unistd.h>
#include <sys/ioctl.h>
#include <syslog.h>
#endif
int main(void) {
  int fd = open("x.txt", O_CREAT | O_WRONLY, 0644);
#ifndef _WIN32
  int fl = fcntl(fd, F_GETFL);
  int n; ioctl(0, FIONREAD, &n);
  syslog(LOG_INFO, "x %d", fl);
#endif
  return fd < 0;
}

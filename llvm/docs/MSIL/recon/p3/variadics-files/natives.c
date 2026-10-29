#include <fcntl.h>
#include <stdio.h>
#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#define OPEN _open
#define MODE (_S_IREAD | _S_IWRITE)
#define CLOSE _close
#else
#include <unistd.h>
#include <sys/ioctl.h>
#include <syslog.h>
#define OPEN open
#define MODE 0644
#define CLOSE close
#endif
int main(void) {
  int fd = OPEN("va-natives.tmp", O_CREAT | O_WRONLY | O_TRUNC, MODE);
  printf("open %s\n", fd >= 0 ? "ok" : "failed");
#ifndef _WIN32
  int fl = fcntl(fd, F_GETFL);
  printf("fcntl F_GETFL & O_ACCMODE = %d\n", fl & O_ACCMODE);
  int fd2 = fcntl(fd, F_DUPFD, 10);
  printf("fcntl F_DUPFD >= 10: %d\n", fd2 >= 10);
  int n = -1, r = ioctl(fd, FIONREAD, &n);
  printf("ioctl FIONREAD on a file: r=%d\n", r);
  openlog("va-natives", LOG_PID, LOG_USER);
  syslog(LOG_DEBUG, "va-natives %d %s %f", 42, "x", 2.5);
  closelog();
  puts("syslog ok");
  CLOSE(fd2);
#endif
  CLOSE(fd);
  return 0;
}

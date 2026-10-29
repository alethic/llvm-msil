/* hosted-libc recon: UCRT state that native startup (vcstartup) sets up. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <locale.h>
#include <time.h>
#include <io.h>
#include <fcntl.h>
int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  printf("printf-options 0x%llx scanf-options 0x%llx\n", *__local_stdio_printf_options(), *__local_stdio_scanf_options());
  printf("%%.0f(0.5)=%.0f %%.0f(2.5)=%.0f %%.2f(1.005)=%.2f %%.1f(0.25)=%.1f\n", 0.5, 2.5, 1.005, 0.25);
  printf("__argc=%d __argv=%s\n", __argc, __argv ? "set" : "NULL");
  printf("_environ=%s getenv(PATH)=%s\n", _environ ? "set" : "NULL", getenv("PATH") ? "set" : "NULL");
  /* _get_pgmptr: fast-fails (0xC0000409) in MSIL, see report */
  printf("fmode=%d\n", (int)_fmode);
  printf("setlocale=%s\n", setlocale(LC_ALL, NULL));
  FILE *f = fopen("no/such/file", "r"); printf("fopen=%p errno=%d %s\n", (void *)f, errno, strerror(errno));
  printf("isatty(0)=%d fileno(stdout)=%d\n", _isatty(0) != 0, _fileno(stdout));
  printf("RAND_MAX=%d rand=%d\n", RAND_MAX, (srand(7), rand()));
  printf("timezone-available=%d\n", 1);
  int w = 0; int n = sscanf("12 ab 3.5", "%d %2s %n", &w, (char[3]){0}, &w); printf("sscanf=%d\n", n);
  printf("%ls|%S|%hs\n", L"wide", L"wid2", "narrow");
  return 0;
}

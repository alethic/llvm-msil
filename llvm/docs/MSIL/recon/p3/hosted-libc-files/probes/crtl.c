/* hosted-libc recon: glibc state that native startup (__libc_start_main) sets up. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <locale.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
extern char **environ;
int main(int argc, char **argv) {
  printf("argc=%d argv0-basename=%s\n", argc, strrchr(argv[0], '/') ? strrchr(argv[0], '/') + 1 : argv[0]);
  printf("program_invocation_short_name=%s\n", program_invocation_short_name);
  printf("environ=%s getenv(PATH)=%s\n", environ ? "set" : "NULL", getenv("PATH") ? "set" : "NULL");
  printf("setlocale=%s\n", setlocale(LC_ALL, NULL));
  FILE *f = fopen("no/such/file", "r"); printf("fopen=%p errno=%d %s\n", (void *)f, errno, strerror(errno));
  printf("isatty(0)=%d fileno(stdout)=%d\n", isatty(0) != 0, fileno(stdout));
  printf("RAND_MAX=%d rand=%d\n", RAND_MAX, (srand(7), rand()));
  tzset(); printf("timezone=%ld daylight=%d tzname0=%s\n", timezone, daylight, tzname[0]);
  int c, a = 0; char *o = 0; optind = 1;
  char *av[] = {"p", "-a", "-o", "val", "rest", 0};
  while ((c = getopt(5, av, "ao:")) != -1) { if (c == 'a') a = 1; if (c == 'o') o = optarg; }
  printf("getopt a=%d o=%s optind=%d\n", a, o, optind);
  printf("%%.0f(0.5)=%.0f %%.0f(2.5)=%.0f %%.2f(1.005)=%.2f\n", 0.5, 2.5, 1.005);
  printf("pagesize=%ld pid>0=%d\n", sysconf(_SC_PAGESIZE), getpid() > 0);
  return 0;
}

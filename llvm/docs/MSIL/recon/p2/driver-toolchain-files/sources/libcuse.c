#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
int main(int argc, char **argv) {
  char *p = malloc(32);
  strcpy(p, "abc");
  if (isalpha(p[0])) p[0] = (char)toupper(p[0]);
  printf("%s %d\n", p, argc);
  fputs(p, stdout);
  errno = 0;
  free(p);
  return getchar() == EOF ? 0 : 1;
}

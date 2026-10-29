#include <stdio.h>
int main(int argc, char **argv) {
  double d = 24.317588 * argc;
  printf("%%Lf given a double (the MSIL argument): %Lf\n", d);    /* -Wformat warns natively */
  printf("%%Lf given a long double: %Lf\n", (long double)d);
  printf("%%Lf given a double again: %Lf\n", d * 2);
  return 0;
}

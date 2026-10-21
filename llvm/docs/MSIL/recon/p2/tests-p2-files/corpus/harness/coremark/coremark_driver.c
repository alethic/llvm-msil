/* Runs CoreMark (core_main.c compiled with -Dmain=coremark_main) and reports
   its error count. */
#undef main /* core_main.c is compiled with -Dmain=coremark_main */
#include "sup.h"
int coremark_main(int argc, char *argv[]);
int main(void) {
  static char arg0[] = "coremark";
  char *argv[] = {arg0, 0};
  int r = coremark_main(1, argv);
  sup_put_i("coremark errors", r);
  return r & 0xff;
}

#include <stdarg.h>
#include <stdio.h>
int my_vlog(const char *fmt, va_list ap) { return vprintf(fmt, ap); }
int main(void) { return 0; }

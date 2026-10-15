// Many lines through puts (Milestone 8, D14): 2000 numbered lines of 0 to 23
// pattern characters, so the C library's stdout buffer fills and is written
// out many times, plus a line of 5000 characters (longer than the usual
// 4096-byte stdio buffer) in the middle and one at the end. Nothing flushes
// stdout explicitly: the output that is still buffered when main returns is
// written when the process exits, the behavior this test pins (ucrtbase on
// Windows, glibc on Linux, stdout redirected to a file or a pipe). The lines
// are built at run time in a static buffer (the data segment's BSS). The exit
// status is the number of characters written, including the newlines, modulo
// 256.
// RUN: %{msil_c}
#include "msil_test.h"

#define LINES 2000
#define LONG 5000

static char line[LONG + 1];
static unsigned total;

// Writes "line NNNN: " (4 digits) and then len pattern characters.
static void make_line(int n, int len) {
  int i = 0;
  const char *head = "line ";
  while (head[i] != '\0') {
    line[i] = head[i];
    i++;
  }
  line[i++] = (char)('0' + n / 1000 % 10);
  line[i++] = (char)('0' + n / 100 % 10);
  line[i++] = (char)('0' + n / 10 % 10);
  line[i++] = (char)('0' + n % 10);
  line[i++] = ':';
  line[i++] = ' ';
  for (int k = 0; k < len; k++)
    line[i++] = (char)('!' + (n + k) % 94);
  line[i] = '\0';
}

static void put_line(void) {
  unsigned n = 0;
  while (line[n] != '\0')
    n++;
  total += n + 1;
  puts(line);
}

static void long_line(char c) {
  for (int i = 0; i < LONG; i++)
    line[i] = (char)(c + i % 10);
  line[LONG] = '\0';
  put_line();
}

int main(void) {
  for (int n = 0; n < LINES; n++) {
    make_line(n, n * 7 % 24);
    put_line();
    if (n == LINES / 2)
      long_line('0');
  }
  long_line('A');
  return (int)(total & 0xff);
}

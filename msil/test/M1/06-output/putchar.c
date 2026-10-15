// putchar, the other C library import of Phase 1 (Milestone 8, D14): a
// P/Invoke into ucrtbase on windows-msvc and libc on linux-gnu, like puts.
// Covered: words and digits written one character at a time, newlines
// written with putchar, output of putchar and puts interleaved on one line
// and across lines (both write to the same C stdout stream, so the order
// holds), and putchar's result, the character written as an unsigned char
// converted to int (an argument above 255 writes and returns its low byte).
// The characters come from a string literal and from arithmetic on a value
// read from a volatile global, so they are not folded. MSIL_EXPECT returns
// 1 + (line % 255) at the first failed check; [exit 0] means every check
// passed.
// RUN: %{msil_c}
#include "msil_test.h"

int putchar(int c);

volatile int base = 'a';

static int put_all(const char *s) {
  int n = 0;
  for (; *s != '\0'; s++, n++)
    if (putchar(*s) != (unsigned char)*s)
      return -1;
  return n;
}

int main(void) {
  MSIL_EXPECT(put_all("putchar") == 7);
  MSIL_EXPECT(putchar('\n') == '\n');

  // The alphabet from a volatile base, then the digits.
  for (int i = 0; i < 26; i++)
    MSIL_EXPECT(putchar(base + i) == 'a' + i);
  MSIL_EXPECT(putchar('\n') == '\n');
  for (int i = 0; i < 10; i++)
    MSIL_EXPECT(putchar('0' + i) == '0' + i);
  MSIL_EXPECT(putchar('\n') == '\n');

  // puts and putchar on the same stream: "<<" then puts's line, then ">>".
  putchar('<');
  putchar('<');
  puts("between");
  putchar('>');
  putchar('>');
  putchar('\n');

  // Only the low byte is written and returned: 256 + 'b' writes 'b'.
  MSIL_EXPECT(putchar(256 + base + 1) == 'b');
  MSIL_EXPECT(putchar('\n') == '\n');

  // An empty line, then a last line without putchar's help.
  putchar('\n');
  puts("end");
  return 0;
}

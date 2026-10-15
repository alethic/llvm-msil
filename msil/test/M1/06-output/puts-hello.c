// Output through puts, end to end (Milestone 8, D14): puts is a Phase-1
// import, a P/Invoke into the C library of the triple (ucrtbase on
// windows-msvc, libc on linux-gnu), and its argument is a string literal in
// the data segment, an address the code loads from the literal's initonly
// field. Covered: "Hello, world!", an empty string (puts writes only the
// newline), a line of spaces, tabs and punctuation, the characters that
// ILAsm and C escape in strings (quote, apostrophe, backslash; they are plain
// bytes in the data image), the same literal printed twice, a pointer into
// the middle of a literal, a string built at run time in a local array, and
// puts's result (nonnegative on success). MSIL_EXPECT returns
// 1 + (line % 255) at the first failed check; [exit 0] means every check
// passed.
// RUN: %{msil_c}
#include "msil_test.h"

#define NOINLINE __attribute__((noinline))

// Fills buf with "built: " and the letters a..z, at run time.
static NOINLINE char *build(char *buf, int n) {
  const char *prefix = "built: ";
  int i = 0;
  for (; prefix[i] != '\0'; i++)
    buf[i] = prefix[i];
  for (int k = 0; k < n; k++)
    buf[i++] = (char)('a' + k);
  buf[i] = '\0';
  return buf;
}

int main(void) {
  MSIL_EXPECT(puts("Hello, world!") >= 0);
  MSIL_EXPECT(puts("") >= 0);
  MSIL_EXPECT(puts("  two leading spaces, a\ttab, and: ;,.!?()[]{}<>=+-*/%&|^~#@$") >= 0);
  MSIL_EXPECT(puts("quote \" apostrophe ' backslash \\ end") >= 0);
  MSIL_EXPECT(puts("Hello, world!") >= 0);
  const char *word = "skip this:visible";
  MSIL_EXPECT(puts(word + 10) >= 0);
  char buf[64];
  MSIL_EXPECT(puts(build(buf, 26)) >= 0);
  MSIL_EXPECT(puts("done") >= 0);
  return 0;
}

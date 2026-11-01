// Hosted puts: a program that includes the triple's real <stdio.h> and calls only puts, and whose main ends without a return statement.
// <stdio.h> is the UCRT's (through INCLUDE) on Windows triples and glibc's
// (the sysroot of %{msil_glibc_sysroot}) on Linux triples, compiled without
// -ffreestanding (hosted mode, plan 5.17.3), so puts is the header's own
// declaration, with the header's attributes, and msil_test.h's declaration
// of it only repeats it. puts is the one native function the program calls,
// so it stays inside D14's standalone import table and is a %{msil_c} test
// (plan 5.17.13): under msil_equiv=1 its O3 and O6 standalone -S references
// build too, which no other hosted function would allow. Checked: a short
// line, an empty line, a line holding a tab, one of 600 characters, the
// strings put_kv composes on the stack, puts's result (a nonnegative value
// on success: glibc returns a count, the UCRT 0, so only its sign is
// printed), EOF from the header, and main's implicit 'return 0' at its
// closing brace, which C99 gives a hosted main (-ffreestanding turned it off
// on Linux triples, where falling off main was an error under
// -Werror=return-type).
// RUN: %{msil_c}
#include <stdio.h>

#include "msil_test.h"

static char long_line[601];

int main(void) {
  puts("hello from the real stdio.h");
  puts("");
  puts("a\ttab");
  for (int i = 0; i < 600; i++)
    long_line[i] = (char)('a' + i % 26);
  puts(long_line);
  put_kv("puts result is nonnegative", puts("puts returns") >= 0);
  put_kv("EOF", EOF);
}

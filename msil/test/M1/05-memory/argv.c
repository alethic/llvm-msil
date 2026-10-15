// argv contents from C (D27): '<msil>Main' builds argv from
// Environment.GetCommandLineArgs() with Marshal::StringToHGlobalAnsi, so
// argv[0] names the program (the .dll path; natively the executable) and is a
// non-empty NUL-terminated string, every argv[i] below argc is non-NULL, and
// argv[argc] is the NULL terminator. Run without arguments argc is 1, but the
// checks hold for any argc >= 1. argv[0] is also walked to its NUL in a
// noinline helper (byte loads through a pointer argument). MSIL_EXPECT returns
// 1 + (line % 255) at the first failed check; [exit 0] means every check
// passed.
// RUN: %{msil_c}
#include "msil_test.h"
#include <stddef.h>

#define NOINLINE __attribute__((noinline))

// The number of bytes before the NUL of s.
static NOINLINE int length(const char *s) {
  int n = 0;
  while (s[n] != '\0')
    n++;
  return n;
}

int main(int argc, char **argv) {
  MSIL_EXPECT(argc >= 1);
  MSIL_EXPECT(argv[0] != NULL);
  MSIL_EXPECT(argv[0][0] != '\0');
  MSIL_EXPECT(length(argv[0]) >= 1);
  for (int i = 1; i < argc; i++)
    MSIL_EXPECT(argv[i] != NULL);
  MSIL_EXPECT(argv[argc] == NULL);
  return 0;
}

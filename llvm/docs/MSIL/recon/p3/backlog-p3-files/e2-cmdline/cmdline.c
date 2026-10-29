/* What a C program on x86_64-pc-windows-msvc-msil sees as its command line. */
typedef unsigned short wchar;
__declspec(dllimport) wchar *__stdcall GetCommandLineW(void);
__declspec(dllimport) char *__stdcall GetCommandLineA(void);
int puts(const char *);
int *__p___argc(void);
char ***__p___argv(void);
static void putw(const wchar *w) {
  char buf[1024]; int i = 0;
  for (; w[i] && i < 1023; ++i) buf[i] = w[i] < 128 ? (char)w[i] : '?';
  buf[i] = 0; puts(buf);
}
int main(int argc, char **argv) {
  puts("-- main argv:"); for (int i = 0; i < argc; ++i) puts(argv[i]);
  puts("-- GetCommandLineW:"); putw(GetCommandLineW());
  puts("-- GetCommandLineA:"); puts(GetCommandLineA());
  int *pc = __p___argc(); char ***pv = __p___argv();
  puts("-- ucrt __argc/__argv:");
  if (!pc || !pv || !*pv) puts("(null)"); else { char b[2] = {(char)('0' + (*pc % 10)), 0}; puts(b); for (int i = 0; i < *pc; ++i) puts((*pv)[i] ? (*pv)[i] : "(null)"); }
  return 0;
}

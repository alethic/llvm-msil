// The native dependency of prog.il (../../dep-local.test, dep-wsl.test):
// built as native.dll with %lld_link -dll -noentry -nodefaultlib, or as
// libnative.so with %ld_lld -shared -soname libnative.so (plan 5.12.5).
#ifdef _WIN32
__declspec(dllexport)
#endif
int add3(int a, int b, int c) {
  return a + b + c;
}

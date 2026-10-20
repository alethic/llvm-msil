/* PROTOTYPE ONLY: clang.exe -> python fake-clang.py <args>. */
#include <process.h>
#include <stdlib.h>
int main(int argc, char **argv) {
  char **a = calloc(argc + 4, sizeof(char *));
  a[0] = "python";
  a[1] = "<scratch>/recon/tests-ci/proto/fake/fake-clang.py"; a[2] = "--as-llc";
  for (int i = 1; i < argc; ++i) a[i + 2] = argv[i];
  return (int)_spawnv(_P_WAIT, "%LOCALAPPDATA%/Python/pythoncore-3.14-64/python.exe", (const char *const *)a);
}

/* PROTOTYPE ONLY: clang.exe -> python fake-clang.py <args>. */
#include <process.h>
#include <stdlib.h>
int main(int argc, char **argv) {
  char **a = calloc(argc + 3, sizeof(char *));
  a[0] = "python";
  a[1] = "C:/Users/JEROME~1/AppData/Local/Temp/claude/D--llvm-msil/fa776849-ce33-4cee-bcc7-05382d063d3e/scratchpad/recon/tests-ci/proto/fake/fake-clang.py";
  for (int i = 1; i < argc; ++i) a[i + 1] = argv[i];
  return (int)_spawnv(_P_WAIT, "C:/Users/JeromeHaltom/AppData/Local/Python/pythoncore-3.14-64/python.exe", (const char *const *)a);
}

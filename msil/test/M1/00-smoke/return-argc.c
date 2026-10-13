// -O0 needs frames (M7): at -O0 windows-msvc gives main a return-value alloca (D32).
// main(int, char **) from C: '<msil>Main' builds argc and argv from the command
// line; run without arguments, argc is 1 (argv[0] names the program).
// RUN: %{msil_c_O2}

int main(int argc, char **argv) {
  (void)argv;
  return argc;
}

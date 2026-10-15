// main(int, char **) from C: '<msil>Main' builds argc and argv from the command
// line; run without arguments, argc is 1 (argv[0] names the program).
// RUN: %{msil_c}

int main(int argc, char **argv) {
  (void)argv;
  return argc;
}

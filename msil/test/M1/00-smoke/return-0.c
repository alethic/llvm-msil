// -O0 needs frames (M7): at -O0 windows-msvc gives main a return-value alloca (D32).
// Smoke test: the smallest C program, int main(void) returning 0, exits with
// status 0 through the generated entry point '<msil>Main'.
// RUN: %{msil_c_O2}

int main(void) { return 0; }

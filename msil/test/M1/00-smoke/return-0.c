// Smoke test: the smallest C program, int main(void) returning 0, exits with
// status 0 through the generated entry point '<msil>Main'.
// RUN: %{msil_c}

int main(void) { return 0; }

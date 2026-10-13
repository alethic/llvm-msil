// The negative control of wrong-expected.test, run only by a nested lit:
// %{msil_c} with an expected file that says '[exit 8]', while the program
// exits with 7. The run must fail with the transcript diff.
// RUN: %{msil_c}

static int add3(int x) { return x + 3; }

int main(void) { return add3(4); }

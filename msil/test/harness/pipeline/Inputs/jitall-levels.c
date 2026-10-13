// The positive case of jitall-runs.test, run only by a nested lit with -a: a
// program that verifies, force-JITs and runs at every level of %{msil_c}, so
// that the nested output shows the msil-verify command and the jitall
// summary of each level.
// RUN: %{msil_c}

static int add2(int x) { return x + 2; }

int main(void) { return add2(7); }

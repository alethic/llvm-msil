// Smoke test (Milestone-3 goal): int main(void){return 42;} through clang
// exits with status 42: '<msil>Main' calls main and returns its result.
// RUN: %{msil_c}

int main(void) { return 42; }

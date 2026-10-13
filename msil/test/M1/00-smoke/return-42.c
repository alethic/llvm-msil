// -O0 needs frames (M7): at -O0 windows-msvc gives main a return-value alloca (D32).
// Smoke test (Milestone-3 goal): int main(void){return 42;} through clang -O2
// exits with status 42: '<msil>Main' calls main and returns its result.
// RUN: %{msil_c_O2}

int main(void) { return 42; }

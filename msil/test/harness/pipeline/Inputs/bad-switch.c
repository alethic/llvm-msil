// The negative case of verify-runs.test, run only by a nested lit: %{msil_c}
// on a program that runs correctly but breaks plan D35 (its 'switch' takes a
// native int, bad-switch.il.in). msil-verify rejects it (ILVerify
// StackUnexpected) while the JIT and the runtime accept it, so the run fails
// only if the composite really runs %msil_verify.
// RUN: %{msil_c}

int main(void) { return 7; }

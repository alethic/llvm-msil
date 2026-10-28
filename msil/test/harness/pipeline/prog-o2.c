// The per-level form %{msil_c_O2} (plan 5.5, D32): -O2 only, whatever
// msil_opt_levels says, with the same legs as %{msil_c}.
// The asm expansion, the asm oracle from M23 (harness/pipeline-obj pins obj).
// REQUIRES: msil-pipeline=asm
// RUN: rm -f %t.*.out
// RUN: %{msil_c_O2}
// RUN: %{harness_check} equals %t.O2.out "[exit 11]"
// RUN: %{harness_check} missing %t.O0.out
// RUN: %if msil-native-oracle %{ %{harness_check} equals %t.O2.native.out "[exit 11]" %}
// RUN: %if msil-run-netfx %{ %{harness_check} equals %t.O2.netfx.out "[exit 11]" %}

static int add5(int x) { return x + 5; }

int main(void) { return add5(6); }

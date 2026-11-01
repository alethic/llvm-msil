// Run in a nested lit by ../../../expected-rule.test only (Windows triple): a
// rule that names only the other operating system leaves the native oracle
// on, so its transcript ([exit 42], prog-native) must fail the comparison.
// REDEFINE: %{msil_expected_rule} = linux: the reason of rule-other.c
// RUN: %{msil_c_O0}
// prog-value: 41
// prog-native: 1
#include "msil_test.h"
int main(void) { return 41; }

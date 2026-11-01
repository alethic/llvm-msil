// Run in a nested lit by ../../../expected-rule.test only: rule.c without
// the rule, so the native oracle's transcript ([exit 22], prog-native) must
// fail the comparison with norule.expected.
// RUN: %{msil_c_O0}
// prog-value: 21
// prog-native: 1
#include "msil_test.h"
int main(void) { return 21; }

// Run in a nested lit by ../../../expected-rule.test only: the rule skips
// only the native oracle, so an MSIL run whose transcript ([exit 31]) differs
// from the hand-written rule-msil-wrong.expected must still fail.
// REDEFINE: %{msil_expected_rule} = windows,linux: the reason of rule-msil-wrong.c
// RUN: %{msil_c_O0}
// prog-value: 31
// prog-native: 1
#include "msil_test.h"
int main(void) { return 31; }

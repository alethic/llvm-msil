// Run in a nested lit by ../../../expected-rule.test only: a hand-written
// expected file (plan 5.17.4) for both operating systems. The stand-in's
// native program exits with one more than the MSIL program
// (prog-native), so only the rule lets the composite pass: its native build
// and native run print the skip line, and every MSIL run still compares with
// rule.expected.
// REDEFINE: %{msil_expected_rule} = windows,linux: the reason of rule.c, with: a colon
// RUN: %{msil_c_O0}
// prog-value: 9
// prog-native: 1
#include "msil_test.h"
int main(void) { return 9; }

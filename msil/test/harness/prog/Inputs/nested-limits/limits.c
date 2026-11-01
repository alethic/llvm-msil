// Run in a nested lit by ../../../limits-redefine.test only: a composite
// whose %{msil_limits} is redefined for all of its commands (plan 5.17.9),
// so the build takes the new limits with --timeout-factor 10 and every step
// after it takes them as they are.
// REDEFINE: %{msil_limits} = --timeout 37 --mem-limit-mb 3777
// RUN: %{msil_c_O0}
// prog-value: 6
#include "msil_test.h"
int main(void) { return 6; }

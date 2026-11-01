// Run in a nested lit by ../../../netfx-skip.test only: skip.c without the
// skip, so the Framework run's transcript ([exit 16], prog-netfx) must fail
// the comparison with noskip.expected.
// RUN: %{msil_c_O0}
// prog-value: 15
// prog-netfx: 1
#include "msil_test.h"
int main(void) { return 15; }

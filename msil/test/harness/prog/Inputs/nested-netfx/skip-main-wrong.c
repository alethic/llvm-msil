// Run in a nested lit by ../../../netfx-skip.test only: the skip covers only
// the Framework run, so a main run whose transcript ([exit 25]) differs from
// skip-main-wrong.expected must still fail.
// REDEFINE: %{msil_netfx_skip} = --skip-netfx 'the attribute that the Framework lacks'
// RUN: %{msil_c_O0}
// prog-value: 25
// prog-netfx: 1
#include "msil_test.h"
int main(void) { return 25; }

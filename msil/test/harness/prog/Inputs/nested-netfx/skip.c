// Run in a nested lit by ../../../netfx-skip.test only: the stand-in's MSIL
// program exits with one more on the .NET Framework (prog-netfx), as a test
// of a construct that Framework lacks would fail there, so only
// %{msil_netfx_skip} lets the composite pass: its Framework run prints the
// skip line, and the main run still compares with skip.expected.
// REDEFINE: %{msil_netfx_skip} = --skip-netfx 'the attribute that the Framework lacks'
// RUN: %{msil_c_O0}
// prog-value: 5
// prog-netfx: 1
#include "msil_test.h"
int main(void) { return 5; }

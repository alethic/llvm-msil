// Run in a nested lit by ../../../../copts-expect.test only, for its case
// (3): %{msil_c_O2} under the default obj pipeline (msil-prog.py with the
// stand-ins of ../../prog-cc.py, which reads the value below), with neither
// %{msil_copts} nor %{msil_expect} redefined, so the run compares with
// no-redefine.expected. It is this test's own file (../lit.local.cfg).
// RUN: %{msil_c_O2}
// prog-value: 9
#include "msil_test.h"
int main(void) { return 9; }

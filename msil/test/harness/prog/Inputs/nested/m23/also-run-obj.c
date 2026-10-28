// Run in a nested lit by ../../../../also-run.test only, for its case (3):
// %{msil_c_O2} under the default obj pipeline (msil-prog.py with the
// stand-ins of ../../prog-cc.py, which reads the value below), with
// --param msil_also_run. It is this test's own file (../lit.local.cfg).
// RUN: %{msil_c_O2}
// prog-value: 11
#include "msil_test.h"
int main(void) { return 11; }

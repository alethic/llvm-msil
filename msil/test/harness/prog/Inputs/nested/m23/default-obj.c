// Run in a nested lit by ../../../../default-pipeline.test only:
// %{msil_c_O2} under the default obj pipeline (no --param msil_pipeline;
// msil-prog.py with the stand-ins of ../../prog-cc.py, which reads the value
// below). It is this test's own file (../lit.local.cfg).
// RUN: %{msil_c_O2}
// prog-value: 13
#include "msil_test.h"
int main(void) { return 13; }

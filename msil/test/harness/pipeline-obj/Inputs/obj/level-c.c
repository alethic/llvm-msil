// Run in a nested lit by ../../levels.test with --param msil_pipeline=obj:
// the per-level composites %{msil_c_O1} and %{msil_c_Os}, whatever
// msil_opt_levels says.
// RUN: %{msil_c_O1}
// RUN: %{msil_c_Os}
// prog-value: 5
#include "msil_test.h"
int main(void) { return 5; }

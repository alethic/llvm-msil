// Run in a nested lit by ../../c.test with --param
// msil_pipeline=obj: %{msil_c} at every level of msil_opt_levels, a
// one-source program that msil-prog.py builds with the stand-ins of
// ../../../prog/Inputs/prog-cc.py (which read the value below).
// RUN: %{msil_c}
// prog-value: 7
#include "msil_test.h"
int main(void) { return 7; }

// Run in a nested lit by ../../c-equiv.test with --param msil_pipeline=obj
// and msil_equiv=1: %{msil_c} at every level of msil_opt_levels, a
// one-source program that msil-prog.py builds with the stand-ins of
// ../../../prog/Inputs/prog-cc.py (which read the value below). It is a copy
// of prog-c.c of its own, so that the nested runs of ../../c.test, which may
// run at the same time, never share Output/ files with these.
// RUN: %{msil_c}
// prog-value: 7
#include "msil_test.h"
int main(void) { return 7; }

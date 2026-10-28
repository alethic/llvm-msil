// Run in a nested lit by ../../../copts-expect.test only, for its case (4):
// %{msil_c_O2} with %{msil_copts} redefined, under msil_pipeline=obj with
// msil_equiv=1 and under msil_pipeline=asm. The stand-ins of
// ../../../prog/Inputs/prog-cc.py read the value below.
// REDEFINE: %{msil_copts} = -DMSIL_COPTS_CHECK=4
// RUN: %{msil_c_O2}
// prog-value: 4
#include "msil_test.h"
int main(void) { return 4; }

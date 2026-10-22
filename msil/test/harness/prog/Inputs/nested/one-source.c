// Run in a nested lit by ../../pipelines.test: %{msil_c_O2} under
// msil_pipeline=asm (../../../Inputs/pipeline-cc.py compiles this file into
// one-source.il.in, then ilasm -dll) and under lto (msil-prog.py with the
// stand-ins of ../prog-cc.py, which reads the value below).
// RUN: %{msil_c_O2}
// prog-value: 7
#include "msil_test.h"
int main(void) { return 7; }

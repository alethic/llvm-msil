// Run in a nested lit by ../../../loud-composite.test only: loud-throw.c
// with %{msil_netfx_skip}, so the Framework run is skipped and the check
// after it says which --stderr-out file it did not check.
// REDEFINE: %{msil_expected_rule} = windows,linux: a loud failure (plan 5.17.4)
// REDEFINE: %{msil_netfx_skip} = --skip-netfx 'the attribute that the Framework lacks'
// RUN: %{msil_prog_loud_O0}
// prog-value: 3
// prog-stderr: msil: error: the construct needs M99 (loud-netfx-skip)
// prog-throw: System.InvalidOperationException the thrown text
// LOUD: msil: error: the construct needs M99 (loud-netfx-skip)
#include "msil_test.h"
int main(void) { return 3; }

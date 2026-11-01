// Run in a nested lit by ../../../loud-composite.test only: loud-throw.c
// with a check line (prefix LOUD) that the program's stderr does not hold,
// so the check after the main run must fail.
// REDEFINE: %{msil_expected_rule} = windows,linux: a loud failure (plan 5.17.4)
// RUN: %{msil_prog_loud_O0}
// prog-value: 3
// prog-stderr: msil: error: the construct needs M99 (loud-wrong-check)
// prog-throw: System.InvalidOperationException the thrown text
// LOUD: msil: error: a message that no run prints
#include "msil_test.h"
int main(void) { return 3; }

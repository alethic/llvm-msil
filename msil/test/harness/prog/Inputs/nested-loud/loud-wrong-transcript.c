// Run in a nested lit by ../../../loud-composite.test only: loud-throw.c
// whose hand-written expected file names another exception, so the main
// run's comparison must fail before its check runs.
// REDEFINE: %{msil_expected_rule} = windows,linux: a loud failure (plan 5.17.4)
// RUN: %{msil_prog_loud_O0}
// prog-value: 3
// prog-stderr: msil: error: the construct needs M99 (loud-wrong-transcript)
// prog-throw: System.InvalidOperationException the thrown text
// LOUD: msil: error: the construct needs M99 (loud-wrong-transcript)
#include "msil_test.h"
int main(void) { return 3; }

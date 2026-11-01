// Run in a nested lit by ../../../loud-composite.test only: a loud failure
// (plan 5.17.4). The stand-in's MSIL program writes the toolchain's message
// to stderr (prog-stderr) and then throws (prog-throw); every run's
// transcript is the hand-written loud-throw.expected, and after each run the
// check line below (prefix LOUD) must match the run's --stderr-out file.
// The thrown message is another text, so the check matches the toolchain's
// message, not the runtime's report of the exception.
// REDEFINE: %{msil_expected_rule} = windows,linux: a loud failure (plan 5.17.4)
// RUN: %{msil_prog_loud_O0}
// prog-value: 3
// prog-stderr: msil: error: the construct needs M99 (loud-throw)
// prog-throw: System.InvalidOperationException the thrown text
// LOUD: msil: error: the construct needs M99 (loud-throw)
#include "msil_test.h"
int main(void) { return 3; }

#!/usr/bin/env python3
# ===-- fake-steps.py - A msil-steps.py stand-in with planted faults -------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""A stand-in for msil-steps.py with one planted fault, which
steps-replay.py must report (the failure path of ../steps-equivalence.test).

    fake-steps.py MODE --step=NAME CMD [ARG...] [--step=NAME CMD...]...

Each step prints 'msil-steps: NAME: <command line>' (shlex quoting), runs the
command as a child process and passes its stdout, stderr and exit status on;
the first failing step prints 'msil-steps: NAME failed (exit status N)' on
stderr and ends the run with status N. MODE plants one fault:

    ok            none (the control)
    extra-out     one more stdout line after each step's output
    other-words   the header shows the step's words plus '--fake'
    lost-err      each step's stderr is dropped
    no-stop       the steps after a failing step run too
    wrong-status  the run exits 0 after a failing step
"""

import shlex
import subprocess
import sys

MODES = ("ok", "extra-out", "other-words", "lost-err", "no-stop", "wrong-status")


def main(argv):
    if not argv or argv[0] not in MODES:
        sys.stderr.write("fake-steps: error: MODE is one of %s\n" % ", ".join(MODES))
        return 2
    mode, steps = argv[0], []
    for word in argv[1:]:
        if word.startswith("--step="):
            steps.append((word[len("--step=") :], []))
        elif steps:
            steps[-1][1].append(word)
    first = 0
    for name, words in steps:
        shown = words + ["--fake"] if mode == "other-words" else words
        sys.stdout.write("msil-steps: %s: %s\n" % (name, shlex.join(shown)))
        sys.stdout.flush()
        res = subprocess.run(
            words,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        sys.stdout.buffer.write(res.stdout)
        if mode == "extra-out":
            sys.stdout.buffer.write(b"fake-steps: extra\n")
        sys.stdout.flush()
        if mode != "lost-err":
            sys.stderr.buffer.write(res.stderr)
        if res.returncode:
            sys.stderr.buffer.write(
                b"msil-steps: %s failed (exit status %d)\n"
                % (name.encode(), res.returncode)
            )
            first = first or res.returncode
            if mode != "no-stop":
                break
        sys.stderr.flush()
    return 0 if mode == "wrong-status" else first


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

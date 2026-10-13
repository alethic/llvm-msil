#!/usr/bin/env python3
# ===-- harness-check.py - Checks for the MSIL harness self-tests ---------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Checks for the harness self-tests (msil/test/harness).

M0 builds no LLVM tools, so these tests cannot use FileCheck. Subcommands:

    equals FILE [LINE...]        FILE is exactly the LINEs, each ending in LF,
                                 with no CR anywhere (no LINE: FILE is empty)
    contains FILE REGEX...       every REGEX matches somewhere in FILE
    lacks FILE REGEX...          no REGEX matches anywhere in FILE
    missing FILE                 FILE does not exist
    write FILE [LINE...]         create FILE from the LINEs, LF line endings
    within SECONDS CMD [ARG...]  run CMD and exit with its status, but fail if
                                 it has not finished after SECONDS (it is then
                                 killed)
    exits STATUS CMD [ARG...]    run CMD and succeed only if it exits with
                                 exactly STATUS ('not' accepts any failure)

REGEXes use Python syntax in MULTILINE mode ('^' and '$' match at line
boundaries); 'contains' and 'lacks' fold CRLF to LF first. A failing check
exits 1 and prints what it expected and the file contents.
"""

import os
import re
import subprocess
import sys
import time


def read_bytes(path):
    with open(path, "rb") as f:
        return f.read()


def read_text(path):
    return (
        read_bytes(path)
        .decode("utf-8", errors="backslashreplace")
        .replace("\r\n", "\n")
    )


def fail(msg, path=None):
    sys.stderr.write("harness-check: %s\n" % msg)
    if path and os.path.isfile(path):
        sys.stderr.write("---- %s ----\n%s---- end ----\n" % (path, read_text(path)))
    return 1


def lines_bytes(lines):
    return "".join(line + "\n" for line in lines).encode("utf-8")


def within(argv):
    if len(argv) < 2:
        return fail("'within' needs SECONDS and a command")
    limit = float(argv[0])
    start = time.monotonic()
    proc = subprocess.Popen(argv[1:])
    try:
        status = proc.wait(timeout=limit)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        return fail("%s did not finish within %g s" % (argv[1], limit))
    sys.stderr.write(
        "harness-check: finished in %.1f s with status %d\n"
        % (time.monotonic() - start, status)
    )
    return status


def exits(argv):
    if len(argv) < 2:
        return fail("'exits' needs STATUS and a command")
    want = int(argv[0])
    status = subprocess.call(argv[1:])
    if status != want:
        return fail("%s exited with status %d, expected %d" % (argv[1], status, want))
    sys.stderr.write(
        "harness-check: %s exited with status %d as expected\n" % (argv[1], status)
    )
    return 0


def main(argv):
    commands = ("equals", "contains", "lacks", "missing", "write", "within", "exits")
    if len(argv) < 2 or argv[0] not in commands:
        sys.stderr.write(__doc__)
        return 2
    cmd, rest = argv[0], argv[1:]
    if cmd == "within":
        return within(rest)
    if cmd == "exits":
        return exits(rest)
    path, rest = rest[0], rest[1:]
    if cmd == "missing":
        return (
            fail("%s exists but should not" % path, path) if os.path.exists(path) else 0
        )
    if cmd == "write":
        with open(path, "wb") as f:
            f.write(lines_bytes(rest))
        return 0
    if not os.path.isfile(path):
        return fail("%s does not exist" % path)
    if cmd == "equals":
        want, got = lines_bytes(rest), read_bytes(path)
        if got != want:
            return fail("%s differs:\n  expected %r\n  actual   %r" % (path, want, got))
        return 0
    if not rest:
        return fail("'%s' needs at least one REGEX" % cmd)
    text = read_text(path)
    for pattern in rest:
        found = re.search(pattern, text, re.MULTILINE) is not None
        if cmd == "contains" and not found:
            return fail("no match for %r in %s" % (pattern, path), path)
        if cmd == "lacks" and found:
            return fail("unexpected match for %r in %s" % (pattern, path), path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

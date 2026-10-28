#!/usr/bin/env python3
# ===-- check-recipe.py - Same program in a det test and its program test -===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Check that a determinism test builds the same program as its program test.

    python check-recipe.py DET_TEST PROGRAM_TEST

msil/test/Programs/det/<name>.test repeats the REDEFINE: lines (and the
REQUIRES: line) of msil/test/Programs/<name>/<name>.test, so that %{msil_det}
builds exactly the program that %{msil_prog} runs (plan 5.12.9, M23). Both
files write their paths as %S/../..., which names the same files from either
directory. This script reads the two files' REDEFINE: and REQUIRES:
directives, joining continuation lines the way lit does (a trailing '\\' and
the blanks around it become one blank), and fails, printing both versions,
when they differ or when a file has no REDEFINE: line.

Exit status: 0 when the directives are equal, 1 when they differ, 2 on a
usage error or an unreadable file.
"""

import re
import sys

DIRECTIVE_RE = re.compile(r"(?<![\w-])(REDEFINE|REQUIRES):(.*)$")


def directives(path):
    """The REDEFINE: and REQUIRES: directives of path, in order, as
    (keyword, text) with continuation lines joined."""
    out = []
    pending = None
    with open(path, encoding="utf-8") as f:
        for number, line in enumerate(f, 1):
            m = DIRECTIVE_RE.search(line.rstrip("\r\n"))
            if not m:
                if pending:
                    raise ValueError(
                        "%s:%d: %s: directive continued with '\\' but not "
                        "followed by one" % (path, number, pending[0])
                    )
                continue
            keyword, text = m.group(1), m.group(2)
            if pending:
                if keyword != pending[0]:
                    raise ValueError(
                        "%s:%d: %s: continuation of a %s: directive"
                        % (path, number, keyword, pending[0])
                    )
                text = pending[1] + " " + text.strip()
            else:
                text = text.strip()
            if text.endswith("\\"):
                pending = (keyword, text[:-1].rstrip())
                continue
            pending = None
            out.append((keyword, " ".join(text.split())))
    if pending:
        raise ValueError("%s: unterminated %s: directive" % (path, pending[0]))
    return out


def main(argv):
    if len(argv) != 3:
        sys.stderr.write("usage: check-recipe.py DET_TEST PROGRAM_TEST\n")
        return 2
    try:
        det, prog = directives(argv[1]), directives(argv[2])
    except (OSError, ValueError) as e:
        sys.stderr.write("check-recipe.py: %s\n" % e)
        return 2
    bad = False
    for path, found in ((argv[1], det), (argv[2], prog)):
        if not any(k == "REDEFINE" for k, _ in found):
            print("check-recipe.py: %s has no REDEFINE: line" % path)
            bad = True
    if det != prog:
        print("check-recipe.py: the directives of %s differ from %s's:" % (argv[1], argv[2]))
        for path, found in ((argv[1], det), (argv[2], prog)):
            print("  %s:" % path)
            for keyword, text in found:
                print("    %s: %s" % (keyword, text))
        bad = True
    if bad:
        return 1
    print("check-recipe.py: %d directives agree" % len(det))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

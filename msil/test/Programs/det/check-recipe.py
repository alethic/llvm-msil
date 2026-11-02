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

A program test in a hosted directory (plan 5.17.3; M26's zlib-full) gets
hosted mode from the marker line config.msil_enable_hosted(config) of its
directory's lit.local.cfg, or of a directory above it up to the suite's root
(the directory of lit.cfg.py, whose own lit.local.cfg is not read; lit
inherits a lit.local.cfg). det/ is not hosted, so such a det test sets the
part of hosted mode that a build needs itself, and the two lines that do it
are the only directives it may have beyond its program test's:

    REQUIRES: msil-hosted
    REDEFINE: %{msil_cflags} = <hosted mode's value>

each on a line of its own, anywhere among the others. The value is read
from HOSTED_CFLAGS in lit.cfg.py, with its '-I%s' (the inputs directory)
written '-I%msil_inputs' and '%%' written '%', so a det test follows a change
of hosted mode's flags or fails. When the program test itself redefines
%{msil_cflags}, the det test repeats that line instead and adds only the
REQUIRES: line.

Exit status: 0 when the directives are equal, 1 when they differ, 2 on a
usage error or an unreadable file (also lit.cfg.py without one
HOSTED_CFLAGS line, when a hosted program test needs it).
"""

import os
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


HOSTED_MARKER = "config.msil_enable_hosted(config)"
HOSTED_CFLAGS_RE = re.compile(r'^HOSTED_CFLAGS = "([^"]*)"', re.M)


def suite_root(path):
    """The directory of lit.cfg.py above path (its own directory included),
    or None."""
    d = os.path.dirname(os.path.abspath(path))
    while True:
        if os.path.isfile(os.path.join(d, "lit.cfg.py")):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            return None
        d = parent


def is_hosted(path):
    """Whether the test path lies in a hosted directory: a lit.local.cfg of
    its directory or of one above it, below the suite's root, holds the
    marker line (plan 5.17.3)."""
    root = suite_root(path)
    if root is None:
        return False
    d = os.path.dirname(os.path.abspath(path))
    while os.path.normcase(d) != os.path.normcase(root):
        cfg = os.path.join(d, "lit.local.cfg")
        if os.path.isfile(cfg):
            with open(cfg, encoding="utf-8") as f:
                if any(line.strip() == HOSTED_MARKER for line in f):
                    return True
        d = os.path.dirname(d)
    return False


def hosted_cflags(path):
    """Hosted mode's %{msil_cflags} as a REDEFINE: line writes it, from
    HOSTED_CFLAGS in the lit.cfg.py of path's suite."""
    cfg = os.path.join(suite_root(path), "lit.cfg.py")
    with open(cfg, encoding="utf-8") as f:
        found = HOSTED_CFLAGS_RE.findall(f.read())
    if len(found) != 1 or found[0].count("%s") != 1:
        raise ValueError(
            "%s: no single line 'HOSTED_CFLAGS = \"... -I%%s ...\"' (hosted "
            "mode's %%{msil_cflags}, plan 5.17.3)" % cfg
        )
    value = found[0].replace("%s", "%msil_inputs").replace("%%", "%")
    return " ".join(value.split())


def redefined(text):
    """The placeholder that a REDEFINE: directive's text sets."""
    return text.partition("=")[0].strip()


def hosted_extras(det_path, prog_path, prog):
    """The directives the det test has beyond the program test's: hosted
    mode's, when the program test is hosted and the det test is not."""
    if not is_hosted(prog_path) or is_hosted(det_path):
        return []
    extras = [("REQUIRES", "msil-hosted")]
    if not any(k == "REDEFINE" and redefined(t) == "%{msil_cflags}" for k, t in prog):
        extras.append(("REDEFINE", "%{msil_cflags} = " + hosted_cflags(prog_path)))
    return extras


def main(argv):
    if len(argv) != 3:
        sys.stderr.write("usage: check-recipe.py DET_TEST PROGRAM_TEST\n")
        return 2
    try:
        det, prog = directives(argv[1]), directives(argv[2])
        extras = hosted_extras(argv[1], argv[2], prog)
    except (OSError, ValueError) as e:
        sys.stderr.write("check-recipe.py: %s\n" % e)
        return 2
    bad = False
    for path, found in ((argv[1], det), (argv[2], prog)):
        if not any(k == "REDEFINE" for k, _ in found):
            print("check-recipe.py: %s has no REDEFINE: line" % path)
            bad = True
    # The det test without hosted mode's lines, each taken out once.
    rest, missing = list(det), []
    for extra in extras:
        if extra in rest:
            rest.remove(extra)
        else:
            missing.append(extra)
    if rest != prog or missing:
        print("check-recipe.py: the directives of %s differ from %s's:" % (argv[1], argv[2]))
        for path, found in ((argv[1], det), (argv[2], prog)):
            print("  %s:" % path)
            for keyword, text in found:
                print("    %s: %s" % (keyword, text))
        if extras:
            print(
                "  %s is in a hosted directory (plan 5.17.3), so %s also has "
                "these lines, each on its own:" % (argv[2], argv[1])
            )
            for keyword, text in extras:
                gone = " (missing)" if (keyword, text) in missing else ""
                print("    %s: %s%s" % (keyword, text, gone))
        bad = True
    if bad:
        return 1
    if extras:
        print(
            "check-recipe.py: %d directives agree, and %s adds hosted mode's %d "
            "(plan 5.17.3)" % (len(prog), argv[1], len(extras))
        )
    else:
        print("check-recipe.py: %d directives agree" % len(det))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

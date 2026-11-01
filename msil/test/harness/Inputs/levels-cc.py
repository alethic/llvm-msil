#!/usr/bin/env python3
# ===-- levels-cc.py - A stand-in compiler that acts per level ------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""A stand-in for the native oracle's clang whose result depends on the
optimization level, for update-expected.py --levels
(harness/update-expected-levels.test).

    levels-cc.py [--at L=ACTION]... CLANG-ARGS...

update-expected.py compiles and links a %{msil_c} or %{msil_ll} test in one
step, as 'clang --target=<triple> -<L> <flags> SRC <ldflags> -o EXE'. The
stand-in checks that CLANG-ARGS hold exactly one -O<level>, one '-o EXE', a
native --target=<triple> (not an -msil one) and one source file that exists
('-I DIR' takes its directory as a value), and then does the ACTION of the
--at whose L is that level (O0, O1, O2, O3, Os or Oz):

    copy:FILE      copy FILE to EXE: a .NET Framework executable that stands
                   in for the native program (it runs directly on Windows,
                   as a native program would), or a file that is no program
    fail:MESSAGE   fail as a compile error: 'levels-cc: error: MESSAGE' on
                   stderr and exit status 1

A level without an --at fails the same way ('levels-cc: error: no --at for
-<L>'), and so does any other mismatch, so a test sees which levels ran.
Without any --at it fails whenever it runs: harness/update-expected-walk.test
gives it so to update-expected.py runs that must build nothing.
"""

import os
import re
import shutil
import sys

LEVEL_RE = re.compile(r"^O[0-3sz]$")


def fail(msg):
    sys.stderr.write("levels-cc: error: %s\n" % msg)
    return 1


def main(argv):
    actions, levels, sources, out, target = {}, [], [], None, None
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--at", "-o", "-I"):
            if i + 1 >= len(argv):
                return fail("%s needs a value" % a)
            value = argv[i + 1]
            i += 2
            if a == "-o":
                out = value
            elif a == "--at":
                level, eq, action = value.partition("=")
                if not eq or not LEVEL_RE.match(level):
                    return fail("--at %r is not L=ACTION for a level L" % value)
                if level in actions:
                    return fail("--at names %s twice" % level)
                actions[level] = action
            continue
        if a.startswith("--target="):
            target = a[len("--target=") :]
        elif re.match(r"^-O.$", a):
            levels.append(a[1:])
        elif not a.startswith("-"):
            sources.append(a)
        i += 1
    if len(levels) != 1:
        return fail("expected exactly one -O<level>, got %r" % levels)
    if not out:
        return fail("no '-o EXE'")
    if not target or target.endswith("-msil"):
        return fail("no native --target=<triple> (got %r)" % target)
    if len(sources) != 1 or not os.path.isfile(sources[0]):
        return fail("expected one existing source file, got %r" % sources)
    action = actions.get(levels[0])
    if action is None:
        return fail("no --at for -%s" % levels[0])
    kind, _, arg = action.partition(":")
    if kind == "fail":
        return fail(arg)
    if kind != "copy":
        return fail("--at %s=%s: neither copy: nor fail:" % (levels[0], action))
    try:
        shutil.copyfile(arg, out)
    except OSError as e:
        return fail("cannot copy %s to %s: %s" % (arg, out, e))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

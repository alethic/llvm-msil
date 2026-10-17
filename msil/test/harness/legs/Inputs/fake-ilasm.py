#!/usr/bin/env python3
# ===-- fake-ilasm.py - Stand-in ilasm for the round-trip self-tests ------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""A stand-in ilasm for the msil-verify.py --roundtrip self-tests.

    fake-ilasm.py make WRAPPER [--replace OLD NEW]... -- ILASM [ARG...]

writes WRAPPER, a program that msil-verify.py can run as its --ilasm: a .cmd
file on Windows (so its name must end in .cmd there), a /bin/sh script (mode
755) elsewhere, plus WRAPPER.json, which holds the replacements and the real
assembler command. WRAPPER runs

    fake-ilasm.py run WRAPPER.json ILASM-ARGS...

which replaces every OLD with NEW in the one .il source among ILASM-ARGS (in
place, as bytes), then runs ILASM [ARG...] ILASM-ARGS and exits with its
status. An OLD that does not occur in the source is an error (status 3,
nothing runs), so a test never passes because its edit missed the listing.
"""

import json
import os
import shlex
import subprocess
import sys


def make(argv):
    if "--" not in argv:
        return usage("make needs '-- ILASM [ARG...]'")
    sep = argv.index("--")
    opts, real = argv[:sep], argv[sep + 1 :]
    if not opts or not real:
        return usage("make needs WRAPPER and ILASM")
    wrapper, opts = os.path.abspath(opts[0]), opts[1:]
    replace = []
    while opts:
        if opts[0] != "--replace" or len(opts) < 3:
            return usage("unexpected %r" % opts[0])
        replace.append([opts[1], opts[2]])
        opts = opts[3:]
    spec = wrapper + ".json"
    with open(spec, "w", encoding="utf-8") as f:
        json.dump({"replace": replace, "ilasm": real}, f, indent=2)
    cmd = [sys.executable, os.path.abspath(__file__), "run", spec]
    if os.name == "nt":
        # %* passes the arguments on as they were written (-output=X keeps
        # its '='); %ERRORLEVEL% is the status of the assembler.
        text = "@%s %%*\r\n@exit /b %%ERRORLEVEL%%\r\n" % " ".join(
            '"%s"' % a for a in cmd
        )
    else:
        text = '#!/bin/sh\nexec %s "$@"\n' % " ".join(shlex.quote(a) for a in cmd)
    with open(wrapper, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    os.chmod(wrapper, 0o755)
    return 0


def run(argv):
    if not argv:
        return usage("run needs SPEC")
    with open(argv[0], encoding="utf-8") as f:
        spec = json.load(f)
    args = argv[1:]
    sources = [a for a in args if not a.startswith("-") and a.lower().endswith(".il")]
    if len(sources) != 1:
        sys.stderr.write("fake-ilasm: expected one .il source in %r\n" % args)
        return 3
    source = sources[0]
    with open(source, "rb") as f:
        data = f.read()
    for old, new in spec["replace"]:
        old_b, new_b = old.encode("utf-8"), new.encode("utf-8")
        if old_b not in data:
            sys.stderr.write("fake-ilasm: %r does not occur in %s\n" % (old, source))
            return 3
        data = data.replace(old_b, new_b)
    with open(source, "wb") as f:
        f.write(data)
    return subprocess.call(spec["ilasm"] + args)


def usage(msg):
    sys.stderr.write("fake-ilasm: %s\n%s" % (msg, __doc__))
    return 2


def main(argv):
    if argv and argv[0] == "make":
        return make(argv[1:])
    if argv and argv[0] == "run":
        return run(argv[1:])
    return usage("use 'make' or 'run'")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

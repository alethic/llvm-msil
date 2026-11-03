#!/usr/bin/env python3
# ===-- corpus-envelope.py - cli-corpus-check.py's envelope check ---------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Run the envelope check of cli-corpus-check.py --mode object on one object.

    python corpus-envelope.py CLI_CORPUS_CHECK_PY DUMP LISTING ARCH SOURCE

Run by ../corpus-o1.test. It loads CLI_CORPUS_CHECK_PY as a module
(sys.dont_write_bytecode keeps __pycache__ out of the source tree) and calls
check_envelope() as the object mode does: DUMP is the object's 'llvm-cil-dump
--headers --tables --link-info' output, LISTING its default 'llvm-cil-dump
--canonical' listing, ARCH the triple's architecture (msil64, x86_64, ...)
and SOURCE the source file whose name the module row carries (O3). It prints
'envelope: ok' and exits 0 when the check finds nothing, else one
'envelope: <problem>' line per problem and exits 1; 2 on a usage error.
Nothing else runs.
"""

import importlib.util
import sys


def main(argv):
    if len(argv) != 5:
        sys.stderr.write(
            "usage: corpus-envelope.py CLI_CORPUS_CHECK_PY DUMP LISTING ARCH SOURCE\n"
        )
        return 2
    script, dump_path, listing_path, arch, source = argv
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location("cli_corpus_check", script)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    with open(dump_path, "rb") as f:
        dump = f.read()
    with open(listing_path, "rb") as f:
        listing = f.read()
    headers, li_text = mod.split_dump(dump)
    problems = mod.check_envelope(
        headers, mod.LinkInfo(li_text), arch, listing, source
    )
    # LF line ends on every host, for harness-check.py's 'equals'.
    lines = ["envelope: " + p for p in problems] or ["envelope: ok"]
    sys.stdout.buffer.write("".join(l + "\n" for l in lines).encode("utf-8"))
    sys.stdout.flush()
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

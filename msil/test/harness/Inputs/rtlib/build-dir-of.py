#!/usr/bin/env python3
# ===-- build-dir-of.py - Run a command with a tool's build directory -----===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""build-dir-of.py TOOL CMD [ARG...]

Runs CMD ARG... --build-dir DIR and exits with its status, where DIR is the
directory above TOOL's bin/: for a build's tool such as %clang, that build's
directory. lit's shell has no command substitution, so build-builtins.test
cannot write --build-dir $(dirname $(dirname %clang)). A TOOL that is not a
file in a directory named bin, or a missing CMD, is a usage error (exit 2).
"""

import os
import subprocess
import sys


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    tool = os.path.abspath(argv[0])
    bin_dir = os.path.dirname(tool)
    if not os.path.isfile(tool) or os.path.basename(bin_dir).lower() != "bin":
        sys.stderr.write(
            "build-dir-of.py: error: %s is not a file in a bin/ directory\n" % argv[0]
        )
        return 2
    sys.stdout.flush()
    return subprocess.call(argv[1:] + ["--build-dir", os.path.dirname(bin_dir)])


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

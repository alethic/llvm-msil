#!/usr/bin/env python3
# ===-- fake-program.py - Stand-in compiler for expect-error.test ---------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""A stand-in for a compiler in expect-error.test.

    fake-program.py STATUS FILE

Prints FILE to stdout unchanged and exits with STATUS. It lives outside the
build directory, so msil-expect-error.ps1 runs it without the build lock.
"""

import sys

with open(sys.argv[2], "rb") as f:
    sys.stdout.buffer.write(f.read())
sys.stdout.flush()
sys.exit(int(sys.argv[1]))

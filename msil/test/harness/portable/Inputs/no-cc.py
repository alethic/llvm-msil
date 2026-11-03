#!/usr/bin/env python3
# ===-- no-cc.py - A compiler stand-in that compiles nothing --------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""The --clang of ../skip-lines.test: prints 'no-cc: <arguments>' and exits
3, so msil-prog.py stops at its first compile, after it has decided whether a
skip line covers the test."""

import sys

print("no-cc: " + " ".join(sys.argv[1:]))
sys.exit(3)

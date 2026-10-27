#!/usr/bin/env python3
# ===-- snan-roundtrip.py - Helpers of roundtrip-snan.test ----------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Helpers of ../roundtrip-snan.test (M21) and ../roundtrip-moved-class.test
(M22).

    snan-roundtrip.py compare FIRST SECOND
        compare two ildasm listings as msil-verify.py --roundtrip compares
        the listing of an image with the listing of its reassembly
        (compare_listings): print 'equal up to N quieted float32 signaling
        NaN(s)', followed by ' and the moved class block(s) NAME, ...' when
        the second listing moves a class block that the .assembly block's
        .custom names, and exit 0, or print 'differ' and the unified diff
        that --roundtrip would print and exit 1
    snan-roundtrip.py patch FILE FROM TO
        replace the one occurrence of the bytes FROM (hex) in FILE by the
        bytes TO (hex, the same length), for example an ldc.r4 operand that
        ilasm would have quieted; any other count of occurrences fails
"""

import difflib
import importlib.util
import os
import sys

UTILS = os.path.join(
    os.path.dirname(
        os.path.dirname(
            os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        )
    ),
    "utils",
)


def load_msil_verify():
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location(
        "msil_verify", os.path.join(UTILS, "msil-verify.py")
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def compare(first, second):
    with open(first, encoding="utf-8") as f:
        a = f.read().splitlines(True)
    with open(second, encoding="utf-8") as f:
        b = f.read().splitlines(True)
    quieted, moved, a, b = load_msil_verify().compare_listings(a, b)
    if a == b:
        print(
            "equal up to %d quieted float32 signaling NaN%s%s"
            % (
                quieted,
                "" if quieted == 1 else "s",
                " and the moved class block%s %s"
                % ("" if len(moved) == 1 else "s", ", ".join(moved))
                if moved
                else "",
            )
        )
        return 0
    print("differ")
    sys.stdout.writelines(difflib.unified_diff(a, b, first, second))
    return 1


def patch(path, old_hex, new_hex):
    old, new = bytes.fromhex(old_hex), bytes.fromhex(new_hex)
    if len(old) != len(new):
        print("patch: %s and %s differ in length" % (old_hex, new_hex))
        return 1
    with open(path, "rb") as f:
        data = f.read()
    count = data.count(old)
    if count != 1:
        print("patch: %s occurs %d times in %s, not once" % (old_hex, count, path))
        return 1
    with open(path, "wb") as f:
        f.write(data.replace(old, new))
    return 0


def main(argv):
    if len(argv) == 3 and argv[0] == "compare":
        return compare(argv[1], argv[2])
    if len(argv) == 4 and argv[0] == "patch":
        return patch(argv[1], argv[2], argv[3])
    sys.stderr.write(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

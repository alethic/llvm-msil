#!/usr/bin/env python3
# ===-- private-toolchain.py - A clang in a private toolchain directory ---===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""private-toolchain.py CLANG DIR [--helper-archive TRIPLE]

Makes DIR, which must not exist, a private toolchain for linux-rtlib.test:
DIR/bin/clang (clang.exe when CLANG ends in .exe) is a hard link of CLANG, or
a copy where the link fails (DIR on another volume), so its resource
directory, which clang derives from its own path, is DIR/lib/clang/<version>
and holds nothing: no helper archive, whatever the build's resource directory
holds. With --helper-archive TRIPLE a stand-in file (an empty archive) is
also written where plan 5.14.3 A1 puts TRIPLE's helper archive in that
resource directory: lib/TRIPLE/clang_rt.builtins.lib for a *-windows-msvc*
triple, lib/TRIPLE/libclang_rt.builtins.a otherwise. Prints the private
clang (and how it was made), the resource directory and the stand-in. Exit 0
OK, 1 when the private clang does not name a resource directory under DIR,
2 usage.
"""

import os
import shutil
import subprocess
import sys


def usage(msg):
    sys.stderr.write("private-toolchain.py: error: %s\n%s" % (msg, __doc__))
    return 2


def main(argv):
    triple = None
    if len(argv) == 4 and argv[2] == "--helper-archive" and argv[3]:
        triple = argv[3]
    elif len(argv) != 2:
        return usage("expected CLANG DIR [--helper-archive TRIPLE]")
    clang, top = os.path.abspath(argv[0]), os.path.abspath(argv[1])
    if not os.path.isfile(clang):
        return usage("%s is not a file" % argv[0])
    if os.path.exists(top):
        return usage("%s exists" % argv[1])

    os.makedirs(os.path.join(top, "bin"))
    exe = ".exe" if clang.lower().endswith(".exe") else ""
    private = os.path.join(top, "bin", "clang" + exe)
    try:
        os.link(clang, private)
        how = "hard link"
    except OSError as e:
        shutil.copy2(clang, private)
        how = "copy, no hard link: %s" % e.strerror
    print("private clang: %s (%s of %s)" % (private, how, clang))

    proc = subprocess.run(
        [private, "-print-resource-dir"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        universal_newlines=True,
    )
    resource_dir = os.path.normpath(proc.stdout.strip()) if proc.stdout else ""
    inside = os.path.normcase(os.path.join(top, "lib", "clang")) + os.sep
    if proc.returncode or not os.path.normcase(resource_dir).startswith(inside):
        sys.stderr.write(
            "private-toolchain.py: error: %s -print-resource-dir gave %r (exit "
            "status %d), not a directory under %s\n"
            % (private, proc.stdout, proc.returncode, inside)
        )
        return 1
    print("resource directory: %s" % resource_dir)

    if triple:
        name = (
            "clang_rt.builtins.lib"
            if "-windows-msvc" in triple
            else "libclang_rt.builtins.a"
        )
        archive = os.path.join(resource_dir, "lib", triple, name)
        os.makedirs(os.path.dirname(archive))
        with open(archive, "wb") as f:
            f.write(b"!<arch>\n")
        print("stand-in helper archive: %s" % archive)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

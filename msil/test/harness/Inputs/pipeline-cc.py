#!/usr/bin/env python3
# ===-- pipeline-cc.py - Stand-in compiler for the pipeline self-tests ----===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""A stand-in for clang and llc in the pipeline self-tests (harness/pipeline).

    pipeline-cc.py --as clang-msil --target=<triple>-msil [args] -S SRC -o OUT
    pipeline-cc.py --as llc-msil -mtriple=<triple>-msil [args] SRC -o OUT
    pipeline-cc.py --as clang-native --ilasm ILASM --target=<triple> [args] SRC -o OUT

M0 builds no compiler, so harness/pipeline/lit.local.cfg replaces %clang_msil,
%llc_msil and %clang_native with this script and leaves every other
substitution of lit.cfg.py alone. It checks the arguments that the composite
substitutions (%{msil_c}, %{msil_ll}, %{msil_c_<L>}, ...) pass: exactly one
-O<level>, which also matches the '.O<level>.' part of the output name when
there is one; the MSIL triple and -S for clang; %{msil_cflags} (-std=gnu17
ahead of -ffreestanding, -Werror=return-type and an -I directory that holds
msil_test.h) for C sources. Then it "compiles" SRC by copying the prepared
<SRC without extension>.il.in to OUT, or for clang-native assembles that file
with 'ILASM -exe' into a .NET Framework executable, which runs directly on
Windows like a native program. Exit status 1 and a message on any mismatch.
"""

import os
import shutil
import subprocess
import sys

C_LEVELS = ("0", "1", "2", "3", "s", "z")
LLC_LEVELS = ("0", "1", "2", "3")


def fail(msg):
    sys.stderr.write("pipeline-cc: error: %s\n" % msg)
    return 1


def main(argv):
    mode, ilasm, out, triple = None, None, None, None
    levels, sources, flags = [], [], []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--as", "--ilasm", "-o"):
            if i + 1 >= len(argv):
                return fail("%s needs a value" % a)
            value = argv[i + 1]
            if a == "--as":
                mode = value
            elif a == "--ilasm":
                ilasm = value
            else:
                out = value
            i += 2
            continue
        if a.startswith("--target="):
            triple = a[len("--target=") :]
        elif a.startswith("-mtriple="):
            triple = a[len("-mtriple=") :]
        elif a.startswith("-O"):
            levels.append(a[2:])
        elif a.startswith("-"):
            flags.append(a)
        else:
            sources.append(a)
        i += 1

    if mode not in ("clang-msil", "llc-msil", "clang-native"):
        return fail("unknown --as %r" % mode)
    if len(sources) != 1 or not os.path.isfile(sources[0]):
        return fail("expected one existing source file, got %r" % sources)
    src = sources[0]
    stem, ext = os.path.splitext(src)
    if ext not in (".c", ".ll"):
        return fail("%s: expected a .c or .ll source" % src)
    if not out:
        return fail("no -o")
    allowed = LLC_LEVELS if mode == "llc-msil" else C_LEVELS
    if len(levels) != 1 or levels[0] not in allowed:
        return fail(
            "expected exactly one of %s, got %r"
            % (", ".join("-O" + x for x in allowed), ["-O" + x for x in levels])
        )
    tagged = [p[1:] for p in os.path.basename(out).split(".") if p.startswith("O")]
    if tagged and tagged[-1] in C_LEVELS and tagged[-1] != levels[0]:
        return fail("%s is compiled with -O%s" % (out, levels[0]))
    if not triple:
        return fail("no target triple")
    if (mode == "clang-native") == triple.endswith("-msil"):
        return fail("%s with the triple %s" % (mode, triple))

    if mode == "clang-msil" and "-S" not in flags:
        return fail("%clang_msil without -S")
    if mode != "clang-msil" and "-S" in flags:
        return fail("-S outside %clang_msil")
    if mode == "llc-msil" and ext != ".ll":
        return fail("%%llc_msil on %s" % src)
    if mode == "llc-msil" and flags:
        return fail("unexpected llc options %r" % flags)
    if ext == ".c":
        for need in ("-std=gnu17", "-ffreestanding", "-Werror=return-type"):
            if need not in flags:
                return fail("%s without %s (%%{msil_cflags})" % (mode, need))
        if flags.index("-std=gnu17") > flags.index("-ffreestanding"):
            return fail(
                "%s with -std=gnu17 after -ffreestanding (%%{msil_cflags} starts "
                "with it)" % mode
            )
        includes = [f[2:] for f in flags if f.startswith("-I")]
        if not any(os.path.isfile(os.path.join(d, "msil_test.h")) for d in includes):
            return fail(
                "no -I directory with msil_test.h in %r (%%{msil_cflags})" % includes
            )
    elif mode == "clang-native" and "-Wno-override-module" not in flags:
        return fail("native IR build without -Wno-override-module")

    il = stem + ".il.in"
    if not os.path.isfile(il):
        return fail("%s does not exist" % il)
    if mode != "clang-native":
        shutil.copyfile(il, out)
        return 0
    if not ilasm:
        return fail("clang-native needs --ilasm")
    arch = {"x86_64": ["-pe64", "-x64"], "aarch64": ["-pe64", "-arm64"]}.get(
        triple.split("-")[0], []
    )
    res = subprocess.run(
        [ilasm, "-nologo", "-quiet", "-exe", *arch, "-output=" + out, il],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=120,
    )
    if res.returncode != 0:
        sys.stderr.write(res.stdout.decode("utf-8", "replace"))
        return fail("ilasm -exe failed with status %d" % res.returncode)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

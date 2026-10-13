#!/usr/bin/env python3
# ===-- update-expected.py - Generate .expected files from native builds --===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Create or check <stem>.expected files for MSIL execution tests.

    update-expected.py [--clang build/bin/clang] msil/test/M1/02-fp/*.c
    update-expected.py --check msil/test/M1            (compare, do not write)

The oracle is a native build of the same source with the just-built clang for
the host triple (default <build>/bin/clang, where <build> is $MSIL_BUILD_DIR
or <repo>/build), at --opt (default -O0), with the flags of %{msil_cflags}
(-ffreestanding -Werror=return-type -I msil/test/Inputs; -ffreestanding turns
off main's implicit 'return 0' except on MSVCRT triples, so falling off main
is a compile error on Linux triples). It
runs through 'msil-run.py --native', so the transcript format, the private
directory, the timeout and the memory cap are those of the lit tests. .ll
tests are compiled with -Wno-override-module instead of the C flags. The
transcript newline rule of msil-run.py (every run of CRs before an LF becomes
one LF) applies to what is written and, with --check, to the .expected file
read, so a file that passes --check also passes the lit test.

Exit statuses outside 0..255 are refused (plan D37): Linux truncates them, so
such a test would pass on one OS by accident. Nothing is written for them, and
--check fails on them whatever the .expected file says. Any other status that
is not '[exit N]' (a crash, a signal, a timeout) is refused as well: a native
run that does not finish normally is not an oracle.

Directories are searched for .c and .ll files outside Inputs/ and Output/
whose RUN lines use %{msil_c...} or %{msil_ll...} (the pipelines with a
native-oracle leg); tests with other RUN lines are skipped. Files named on the
command line are always processed. Only <stem>.expected is written; a
hand-maintained <stem>.<os>.expected override for the host OS is reported and
left alone.

Windows: run from a VS x64 developer prompt (the native link needs LIB).
Exit status: 0 if everything was written or matched, 1 otherwise.
"""

import argparse
import difflib
import importlib.util
import os
import platform
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
TEST_ROOT = os.path.join(os.path.dirname(HERE), "test")
ORACLE_RUN_RE = re.compile(r"RUN:.*%\{msil_(c|ll)(_O[0-3sz])?\}")
EXIT_RE = re.compile(r"^\[exit (\d+)\]$")


def load_msil_run():
    # No __pycache__ next to the scripts in the source tree.
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location(
        "msil_run", os.path.join(HERE, "msil-run.py")
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


msil_run = load_msil_run()


def default_target():
    arch = {
        "amd64": "x86_64",
        "x86_64": "x86_64",
        "arm64": "aarch64",
        "aarch64": "aarch64",
    }.get(platform.machine().lower(), "x86_64")
    return {"Windows": arch + "-pc-windows-msvc", "Darwin": arch + "-apple-darwin"}.get(
        platform.system(), arch + "-unknown-linux-gnu"
    )


def default_clang():
    build = os.environ.get("MSIL_BUILD_DIR") or os.path.join(REPO, "build")
    clang = os.path.join(build, "bin", "clang" + (".exe" if os.name == "nt" else ""))
    return clang if os.path.isfile(clang) else None


def has_oracle_run_line(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return any(ORACLE_RUN_RE.search(line) for line in f)
    except OSError:
        return False


def collect(paths):
    """(path, explicit) for every test source to process."""
    for p in paths:
        if os.path.isdir(p):
            for root, dirs, files in os.walk(p):
                dirs[:] = sorted(d for d in dirs if d not in ("Inputs", "Output"))
                for f in sorted(files):
                    if f.endswith((".c", ".ll")):
                        yield os.path.join(root, f), False
        else:
            yield p, True


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument(
        "--clang",
        default=default_clang(),
        help="clang for the native build (default: <build>/bin/clang)",
    )
    ap.add_argument(
        "--target",
        default=default_target(),
        help="native triple (default: the host, %(default)s)",
    )
    ap.add_argument(
        "--cflags",
        default="-ffreestanding -Werror=return-type",
        help="must match %%{msil_cflags} of the tests' directory "
        "(without -I; default: %(default)s)",
    )
    ap.add_argument(
        "--opt",
        default="-O0",
        help="optimization level: O2 or --opt=-O2 (default: -O0)",
    )
    ap.add_argument("--check", action="store_true", help="compare, do not write")
    ap.add_argument(
        "--timeout",
        type=float,
        default=msil_run.DEFAULT_TIMEOUT,
        help="limit for the native program in seconds (default %g); the "
        "compile gets ten times as long" % msil_run.DEFAULT_TIMEOUT,
    )
    ap.add_argument(
        "--mem-limit-mb",
        type=int,
        default=msil_run.DEFAULT_MEM_LIMIT_MB,
        help="memory cap in MiB (default %d)" % msil_run.DEFAULT_MEM_LIMIT_MB,
    )
    ap.add_argument("tests", nargs="+", help=".c/.ll files or directories")
    args = ap.parse_args()

    if not args.clang or not os.path.isfile(args.clang):
        ap.error("no clang found; build clang or pass --clang PATH")
    if not args.opt.startswith("-"):
        args.opt = "-" + args.opt
    if os.name == "nt" and not os.environ.get("LIB"):
        print(
            "update-expected: warning: LIB is not set; the native link needs a "
            "VS x64 developer prompt",
            file=sys.stderr,
        )
    osname = msil_run.host_os()
    env = dict(os.environ)
    failures = 0
    with tempfile.TemporaryDirectory(prefix="msil-update-expected-") as tmp:
        for src, explicit in collect(args.tests):
            if not explicit and not has_oracle_run_line(src):
                print("skip (no %%{msil_c}/%%{msil_ll} RUN line): %s" % src)
                continue
            stem = os.path.splitext(src)[0]
            if os.path.exists(stem + "." + osname + ".expected"):
                print("skip (has a %s override): %s" % (osname, src))
                continue
            exe = os.path.join(tmp, os.path.basename(stem) + ".exe")
            # The same command lines as the native-oracle legs of %{msil_c} and
            # %{msil_ll} in msil/test/lit.cfg.py.
            cc = [args.clang, "--target=" + args.target, args.opt]
            if src.endswith(".ll"):
                cc.append("-Wno-override-module")
            else:
                cc += args.cflags.split() + ["-I", os.path.join(TEST_ROOT, "Inputs")]
            cc += [src, "-o", exe]
            try:
                res = msil_run.run_limited(
                    cc, None, env, args.timeout * 10, args.mem_limit_mb
                )
            except OSError as e:
                res = None
                sys.stderr.write(
                    "update-expected: cannot run %s: %s\n" % (args.clang, e)
                )
            if res is not None:
                msil_run.emit(sys.stdout, res.stdout)
                msil_run.emit(sys.stderr, res.stderr)
            if res is None or res.returncode != 0 or res.timed_out or res.memory_limit:
                print("FAIL (native compile): %s" % src)
                failures += 1
                continue

            run = [
                sys.executable,
                os.path.join(HERE, "msil-run.py"),
                "--native",
                "--timeout",
                repr(args.timeout),
                "--mem-limit-mb",
                str(args.mem_limit_mb),
                "--",
                exe,
            ]
            proc = subprocess.run(run, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            out = msil_run.text_of(proc.stdout)
            status = out.rstrip("\n").split("\n")[-1]
            # msil-run.py exits with 1 (after printing the transcript) only for
            # an out-of-range status, which is refused below.
            if proc.returncode not in (0, 1) or not msil_run.STATUS_RE.match(status):
                sys.stderr.write(msil_run.text_of(proc.stderr))
                print("FAIL (native run): %s" % src)
                failures += 1
                continue
            m = EXIT_RE.match(status)
            if not m or int(m.group(1)) > 255:
                why = (
                    "its exit status is outside 0..255; Linux would truncate it (plan D37)"
                    if msil_run.OUTSIDE_RE.match(status)
                    else "a native run that does not exit normally is no oracle"
                )
                print(
                    "REFUSED: %s: the native run ended with %s: %s" % (src, status, why)
                )
                failures += 1
                continue

            if args.check:
                path = stem + ".expected"
                if not os.path.isfile(path):
                    print(
                        "MISSING: %s (native transcript below)\n%s" % (path, out),
                        end="",
                    )
                    failures += 1
                    continue
                with open(path, encoding="utf-8", newline="") as f:
                    expected = msil_run.fold_newlines(f.read())
                if expected == out:
                    print("ok: " + src)
                else:
                    print("MISMATCH: " + src)
                    sys.stdout.writelines(
                        difflib.unified_diff(
                            expected.splitlines(True),
                            out.splitlines(True),
                            path,
                            "native",
                        )
                    )
                    failures += 1
            else:
                with open(stem + ".expected", "w", encoding="utf-8", newline="\n") as f:
                    f.write(out)
                print("wrote %s.expected" % stem)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

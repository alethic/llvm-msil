#!/usr/bin/env python3
"""Create or check <stem>.expected files for MSIL execution tests.

The oracle is a native build of the same source with the just-built clang for
the host triple, run through msil-run.py --native, so the transcript format is
identical to what the lit tests compare against.

    update-expected.py --clang build/bin/clang msil/test/M1/03-memory/*.c
    update-expected.py --clang build/bin/clang --check msil/test/M1      (CI)

Windows: run from a VS x64 developer prompt (link.exe and LIB are needed).
Only <stem>.expected is written; a hand-maintained <stem>.<os>.expected
override is reported and left alone.
"""

import argparse
import os
import platform
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TEST_ROOT = os.path.join(os.path.dirname(HERE), "test")


def default_target():
    arch = {"amd64": "x86_64", "x86_64": "x86_64", "arm64": "aarch64",
            "aarch64": "aarch64"}.get(platform.machine().lower(), "x86_64")
    return {"Windows": arch + "-pc-windows-msvc", "Darwin": arch + "-apple-darwin"}.get(
        platform.system(), arch + "-unknown-linux-gnu")


def collect(paths):
    for p in paths:
        if os.path.isdir(p):
            for root, dirs, files in os.walk(p):
                dirs[:] = [d for d in dirs if d not in ("Inputs", "Output")]
                for f in sorted(files):
                    if f.endswith(".c"):
                        yield os.path.join(root, f)
        else:
            yield p


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--clang", required=True)
    ap.add_argument("--target", default=default_target())
    ap.add_argument("--cflags", default="-ffreestanding",
                    help="must match %%{msil_cflags} of the tests' directory")
    ap.add_argument("--opt", default="-O0")
    ap.add_argument("--check", action="store_true", help="compare, do not write")
    ap.add_argument("tests", nargs="+")
    args = ap.parse_args()

    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        for src in collect(args.tests):
            stem = os.path.splitext(src)[0]
            osname = platform.system().lower()
            if os.path.exists(stem + "." + osname + ".expected"):
                print("skip (has %s override): %s" % (osname, src))
                continue
            exe = os.path.join(tmp, os.path.basename(stem) + ".exe")
            cc = [args.clang, "--target=" + args.target, args.opt] + args.cflags.split() + [
                "-I", os.path.join(TEST_ROOT, "Inputs"), src, "-o", exe]
            if subprocess.call(cc) != 0:
                print("FAIL (native compile): %s" % src)
                failures += 1
                continue
            run = [sys.executable, os.path.join(HERE, "msil-run.py"), "--native", exe]
            if args.check:
                rc = subprocess.call(run[:-1] + ["--expect", stem, exe])
                print(("ok: " if rc == 0 else "MISMATCH: ") + src)
                failures += rc != 0
            else:
                out = subprocess.run(run, stdout=subprocess.PIPE, check=True).stdout
                with open(stem + ".expected", "wb") as f:
                    f.write(out.replace(b"\r\n", b"\n"))
                print("wrote %s.expected" % stem)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

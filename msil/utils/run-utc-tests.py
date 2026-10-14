#!/usr/bin/env python3
# ===-- run-utc-tests.py - Run simple update_llc_test_checks tests --------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Run simple update_llc_test_checks.py tests without lit.

    run-utc-tests.py --llc build/bin/llc.exe msil-basic x86-basic

llvm/test/tools/UpdateTestChecks/lit.local.cfg sets the 'llc-binary' feature
only when a file named exactly 'llc' exists in the tools directory, so on
Windows (llc.exe) lit reports the whole directory UNSUPPORTED. This script
runs the tests of llvm/test/tools/UpdateTestChecks/update_llc_test_checks
that have exactly these two RUN lines (anything else is an error):

    # RUN: cp -f %S/Inputs/<f> %t.ll && %update_llc_test_checks %t.ll
    # RUN: diff -u %S/Inputs/<f>.expected %t.ll

(%t may have another extension than .ll if it is the same throughout, and a
RUN line may continue on the next one after a backslash.) For each test it
copies Inputs/<f> into a scratch directory, runs

    python llvm/utils/update_llc_test_checks.py --llc-binary <llc> --version=1 <copy>

(the arguments lit.local.cfg adds) and compares the result with
Inputs/<f>.expected, ignoring a CR at the end of a line (a Windows checkout
has CRLF files, the script writes LF). Differences are printed as a unified
diff. 'REQUIRES: <target>-registered-target' lines are checked against
'llvm-config --targets-built' when llvm-config is next to llc; a test whose
target is missing fails rather than being skipped.

A test name is the .test file name with or without '.test', or a path to a
.test file. --llc defaults to <build>/bin/llc, where <build> is
$MSIL_BUILD_DIR or <repo>/build. The scratch directory is deleted when every
test passes, unless --keep or --work-dir is given.

Exit status: 0 if every test passed, 1 otherwise.
"""

import argparse
import difflib
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
TEST_DIR = os.path.join(
    REPO, "llvm", "test", "tools", "UpdateTestChecks", "update_llc_test_checks"
)
UPDATE_SCRIPT = os.path.join(REPO, "llvm", "utils", "update_llc_test_checks.py")

# lit directives: RUN lines (with '\' continuations) and REQUIRES are
# understood; any other directive makes the test an error.
DIRECTIVE_RE = re.compile(
    r"\b(RUN|REQUIRES|UNSUPPORTED|XFAIL|DEFINE|REDEFINE|ALLOW_RETRIES):(.*)$"
)
RUN_UPDATE_RE = re.compile(
    r"^cp -f %S/Inputs/(?P<input>[^\s/\\]+) %t(?P<ext>\.\w+)"
    r" && %update_llc_test_checks %t(?P=ext)$"
)
RUN_DIFF_RE = re.compile(
    r"^diff -u %S/Inputs/(?P<expected>[^\s/\\]+) %t(?P<ext>\.\w+)$"
)
REGISTERED_TARGET_RE = re.compile(r"^([\w-]+)-registered-target$")


class TestError(Exception):
    pass


def default_llc():
    build = os.environ.get("MSIL_BUILD_DIR") or os.path.join(REPO, "build")
    exe = ".exe" if sys.platform == "win32" else ""
    return os.path.join(build, "bin", "llc" + exe)


def find_test(name):
    if name.endswith(".test") and os.path.isfile(name):
        return os.path.abspath(name)
    if not name.endswith(".test"):
        name += ".test"
    path = os.path.join(TEST_DIR, name)
    if not os.path.isfile(path):
        raise TestError("no such test: %s" % path)
    return path


def parse_test(path):
    """Returns (input file, expected file, scratch extension, required features)."""
    runs, requires = [], []
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = DIRECTIVE_RE.search(line)
            if not m:
                continue
            keyword, value = m.group(1), m.group(2).strip()
            if keyword == "REQUIRES":
                requires += [x.strip() for x in value.split(",") if x.strip()]
            elif keyword != "RUN":
                raise TestError("unsupported lit directive '%s:'" % keyword)
            elif runs and runs[-1].endswith("\\"):
                runs[-1] = runs[-1][:-1].rstrip() + " " + value
            else:
                runs.append(value)
    if len(runs) != 2:
        raise TestError("expected 2 RUN lines, found %d" % len(runs))
    update, diff = RUN_UPDATE_RE.match(runs[0]), RUN_DIFF_RE.match(runs[1])
    if not update:
        raise TestError(
            "first RUN line is not 'cp -f %%S/Inputs/<f> %%t.ll && "
            "%%update_llc_test_checks %%t.ll': %s" % runs[0]
        )
    if not diff:
        raise TestError(
            "second RUN line is not 'diff -u %%S/Inputs/<f>.expected %%t.ll': %s"
            % runs[1]
        )
    expected = update.group("input") + ".expected"
    if diff.group("expected") != expected or diff.group("ext") != update.group("ext"):
        raise TestError(
            "the RUN lines do not compare %%t%s with Inputs/%s.expected"
            % (update.group("ext"), update.group("input"))
        )
    inputs = os.path.join(os.path.dirname(path), "Inputs")
    return (
        os.path.join(inputs, update.group("input")),
        os.path.join(inputs, expected),
        update.group("ext"),
        requires,
    )


def get_targets_built(llc):
    """Returns the lower-case names of the targets built, or None if unknown."""
    llvm_config = os.path.join(
        os.path.dirname(llc), "llvm-config" + os.path.splitext(llc)[1]
    )
    if not os.path.isfile(llvm_config):
        return None
    out = subprocess.run(
        [llvm_config, "--targets-built"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        encoding="utf-8",
        errors="replace",
    )
    if out.returncode != 0:
        raise TestError("%s --targets-built failed:\n%s" % (llvm_config, out.stdout))
    return set(t.lower() for t in out.stdout.split())


def check_requires(requires, targets):
    for feature in requires:
        m = REGISTERED_TARGET_RE.match(feature)
        if not m:
            raise TestError("unsupported REQUIRES feature '%s'" % feature)
        if targets is not None and m.group(1) not in targets:
            raise TestError(
                "requires %s, but llc was built for: %s"
                % (feature, " ".join(sorted(targets)))
            )


def read_lines(path):
    """Lines of a file without their line terminator and any CR before it."""
    with open(path, "rb") as f:
        text = f.read().decode("utf-8")
    return [line.rstrip("\r") for line in text.split("\n")]


def run_test(path, llc, targets, work_dir):
    """Runs one test; returns (messages to print, whether it passed)."""
    name = os.path.basename(path)
    input_file, expected_file, ext, requires = parse_test(path)
    check_requires(requires, targets)
    for f in (input_file, expected_file):
        if not os.path.isfile(f):
            raise TestError("missing file %s" % f)
    # lit's %t is <output dir>/<test file name>.tmp.
    result_file = os.path.join(work_dir, name + ".tmp" + ext)
    shutil.copyfile(input_file, result_file)
    cmd = [
        sys.executable,
        UPDATE_SCRIPT,
        "--llc-binary",
        llc,
        "--version=1",
        result_file,
    ]
    proc = subprocess.run(
        cmd,
        cwd=work_dir,
        # Keep the source tree free of UpdateTestChecks/__pycache__.
        env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1"),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        encoding="utf-8",
        errors="replace",
    )
    messages = []
    if proc.stdout.strip():
        # Warnings are shown but, as under lit, only the result is compared.
        messages.append("update_llc_test_checks.py output:\n" + proc.stdout.rstrip())
    if proc.returncode != 0:
        messages.insert(
            0,
            "update_llc_test_checks.py exited with %d: %s"
            % (proc.returncode, subprocess.list2cmdline(cmd)),
        )
        return messages, False
    diff = list(
        difflib.unified_diff(
            read_lines(expected_file),
            read_lines(result_file),
            fromfile=expected_file,
            tofile=result_file,
            lineterm="",
        )
    )
    if diff:
        messages.append("\n".join(diff))
        return messages, False
    return messages, True


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--llc", default=default_llc(), help="llc to run (default: %(default)s)"
    )
    parser.add_argument(
        "--work-dir", help="scratch directory to use and keep (default: a new one)"
    )
    parser.add_argument(
        "--keep", action="store_true", help="keep the scratch directory"
    )
    parser.add_argument("tests", nargs="+", metavar="test-name")
    args = parser.parse_args()

    llc = os.path.abspath(args.llc)
    if not os.path.isfile(llc):
        parser.error("llc not found: %s" % llc)
    if re.search(r"\s", llc):
        # update_llc_test_checks.py runs '<llc> <args>' through the shell.
        parser.error("the llc path must not contain whitespace: %s" % llc)

    try:
        targets = get_targets_built(llc)
    except (TestError, OSError) as e:
        print("error: %s" % e)
        return 1
    if targets is None:
        print("note: no llvm-config next to %s; REQUIRES targets not checked" % llc)

    if args.work_dir:
        work_root = os.path.abspath(args.work_dir)
        os.makedirs(work_root, exist_ok=True)
    else:
        work_root = tempfile.mkdtemp(prefix="run-utc-tests-")

    failures = 0
    for test in args.tests:
        label = test
        try:
            path = find_test(test)
            label = os.path.basename(path)[: -len(".test")]
            work_dir = os.path.join(work_root, label)
            os.makedirs(work_dir, exist_ok=True)
            messages, passed = run_test(path, llc, targets, work_dir)
        except (TestError, OSError, UnicodeDecodeError) as e:
            messages, passed = ["error: %s" % e], False
        print("%s: %s" % ("PASS" if passed else "FAIL", label))
        for message in messages:
            print(message)
        if not passed:
            failures += 1

    total = len(args.tests)
    print("%d of %d tests passed" % (total - failures, total))
    if failures or args.keep or args.work_dir:
        print("scratch directory: %s" % work_root)
    else:
        shutil.rmtree(work_root, ignore_errors=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

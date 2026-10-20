#!/usr/bin/env python3
# ===-- run-utc-tests.py - Run simple update_llc_test_checks tests --------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Run simple update_llc_test_checks.py tests without lit.

    run-utc-tests.py --llc build/bin/llc.exe msil-basic x86-basic x86-mir-only

llvm/test/tools/UpdateTestChecks/lit.local.cfg sets the 'llc-binary' feature
only when a file named exactly 'llc' exists in the tools directory, so on
Windows (llc.exe) lit reports the whole directory UNSUPPORTED. This script
runs the tests of llvm/test/tools/UpdateTestChecks/update_llc_test_checks
whose RUN lines use only the commands below (anything else is an error).
A RUN line may continue on the next one after a backslash and may chain
commands with '&&'; the commands run in order in a scratch directory, with
%t = <scratch dir>/<test file name>.tmp, and the first one that fails fails
the test. For example:

    # RUN: cp -f %S/Inputs/<f> %t.ll && %update_llc_test_checks %t.ll
    # RUN: diff -u %S/Inputs/<f>.expected %t.ll

The accepted commands are:

  cp [-f] %S/Inputs/<f> %t<ext>
      Copies Inputs/<f>. The first command must be a cp.
  %update_llc_test_checks %t<ext>
      Runs 'python llvm/utils/update_llc_test_checks.py --llc-binary <llc>
      --version=1 <file>' (the arguments lit.local.cfg adds). Its output is
      shown, but only a non-zero exit status fails the test.
  diff -u %S/Inputs/<f> %t<ext>
      Compares Inputs/<f> with the file, ignoring a CR at the end of a line
      (a Windows checkout has CRLF files, the script writes LF), and prints a
      unified diff if they differ.
  llc <args> < %t<ext> | FileCheck %t<ext>
      Runs <llc> with the file as stdin and pipes its output into the
      FileCheck next to <llc>, without a shell; either exiting non-zero fails
      the test. <args> may contain no substitutions and no shell
      metacharacters (each is made of letters, digits and '_.,=+:/-').

%t keeps one extension (such as .ll) throughout, and a test must have at least
one %update_llc_test_checks command and at least one diff or FileCheck.
'REQUIRES: <target>-registered-target' lines are checked against
'llvm-config --targets-built' when llvm-config is next to llc; a test whose
target is missing fails rather than being skipped.

A test name is the .test file name with or without '.test', or a path to a
.test file (its Inputs directory is next to it). --llc defaults to
<build>/bin/llc, where <build> is $MSIL_BUILD_DIR or <repo>/build. The
scratch directory is deleted when every test passes, unless --keep or
--work-dir is given.

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
# The commands a RUN line may chain with '&&' (see the module docstring).
# Whitespace in a command is normalized to single spaces before matching.
INPUT = r"%S/Inputs/(?P<input>[^\s/\\]+)"
TMP = r"%t(?P<ext>\.\w+)"
CP_RE = re.compile(r"^cp (?:-f )?%s %s$" % (INPUT, TMP))
UPDATE_RE = re.compile(r"^%%update_llc_test_checks %s$" % TMP)
DIFF_RE = re.compile(r"^diff -u %s %s$" % (INPUT, TMP))
LLC_FILECHECK_RE = re.compile(
    r"^llc(?P<args>(?: \S+)*) < %s \| FileCheck %%t(?P=ext)$" % TMP
)
PLAIN_ARG_RE = re.compile(r"^[\w.,=+:/-]+$")
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


def parse_command(command):
    """Returns (kind, operand, ext) for one command of a RUN line.

    kind is 'cp' or 'diff' (operand: the Inputs file name), 'update' (operand:
    None) or 'llc' (operand: the llc arguments)."""
    for kind, regex in (("cp", CP_RE), ("update", UPDATE_RE), ("diff", DIFF_RE)):
        m = regex.match(command)
        if m:
            operand = m.group("input") if kind != "update" else None
            return kind, operand, m.group("ext")
    m = LLC_FILECHECK_RE.match(command)
    if m:
        args = m.group("args").split()
        for arg in args:
            if not PLAIN_ARG_RE.match(arg):
                raise TestError(
                    "unsupported llc argument '%s' in RUN command: %s" % (arg, command)
                )
        return "llc", args, m.group("ext")
    raise TestError("unsupported RUN command: %s" % command)


def parse_test(path):
    """Returns (steps, scratch extension, required features).

    Each step is (command text, kind, operand) where kind and operand are as
    parse_command returns them, with Inputs file names made into paths."""
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
    if runs and runs[-1].endswith("\\"):
        raise TestError("the last RUN line ends with a backslash")

    inputs = os.path.join(os.path.dirname(path), "Inputs")
    steps, exts = [], set()
    for run in runs:
        for command in re.split(r"\s+&&\s+", run):
            command = " ".join(command.split())
            kind, operand, ext = parse_command(command)
            if kind in ("cp", "diff"):
                operand = os.path.join(inputs, operand)
            steps.append((command, kind, operand))
            exts.add(ext)
    if not steps:
        raise TestError("no RUN lines")
    if steps[0][1] != "cp":
        raise TestError(
            "the first RUN command is not 'cp [-f] %%S/Inputs/<f> %%t<ext>': %s"
            % steps[0][0]
        )
    if len(exts) != 1:
        raise TestError(
            "%%t is used with more than one extension: %s" % " ".join(sorted(exts))
        )
    kinds = set(kind for _, kind, _ in steps)
    if "update" not in kinds:
        raise TestError("no '%update_llc_test_checks %t<ext>' RUN command")
    if not kinds & {"diff", "llc"}:
        raise TestError("no 'diff -u' or 'llc ... | FileCheck' RUN command")
    return steps, exts.pop(), requires


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


def filecheck_for(llc):
    """The FileCheck in the same directory as llc."""
    return os.path.join(os.path.dirname(llc), "FileCheck" + os.path.splitext(llc)[1])


def run_update(result_file, llc, work_dir):
    """Runs update_llc_test_checks.py; returns (messages, whether it passed)."""
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
    return messages, True


def run_diff(expected_file, result_file):
    """Compares the files as 'diff -u'; returns (messages, whether they match)."""
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
        return ["\n".join(diff)], False
    return [], True


def run_llc_filecheck(args, result_file, llc, work_dir):
    """Runs 'llc <args> < file | FileCheck file' without a shell; returns
    (messages, whether both commands exited with 0)."""
    llc_cmd = [llc] + args
    filecheck_cmd = [filecheck_for(llc), result_file]
    with open(result_file, "rb") as stdin, tempfile.TemporaryFile() as llc_stderr:
        llc_proc = subprocess.Popen(
            llc_cmd,
            cwd=work_dir,
            stdin=stdin,
            stdout=subprocess.PIPE,
            # A file, not a pipe: nothing reads llc's stderr until it exits.
            stderr=llc_stderr,
        )
        try:
            filecheck_proc = subprocess.Popen(
                filecheck_cmd,
                cwd=work_dir,
                stdin=llc_proc.stdout,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
        except OSError:
            llc_proc.kill()
            llc_proc.wait()
            raise
        finally:
            # Only FileCheck reads llc's output now.
            llc_proc.stdout.close()
        filecheck_out = filecheck_proc.communicate()[0]
        llc_rc = llc_proc.wait()
        llc_stderr.seek(0)
        llc_err = llc_stderr.read()
    messages = []
    # As under lit (pipefail), a failure of either command fails the test.
    for name, cmd, rc, out in (
        ("llc", llc_cmd, llc_rc, llc_err),
        ("FileCheck", filecheck_cmd, filecheck_proc.returncode, filecheck_out),
    ):
        if rc != 0:
            messages.append(
                "%s exited with %d: %s" % (name, rc, subprocess.list2cmdline(cmd))
            )
            out = out.decode("utf-8", errors="replace").rstrip()
            if out:
                messages.append(out)
    return messages, not messages


def run_test(path, llc, targets, work_dir):
    """Runs one test; returns (messages to print, whether it passed)."""
    name = os.path.basename(path)
    steps, ext, requires = parse_test(path)
    check_requires(requires, targets)
    needed = set(operand for _, kind, operand in steps if kind in ("cp", "diff"))
    if any(kind == "llc" for _, kind, _ in steps):
        needed.add(filecheck_for(llc))
    for f in sorted(needed):
        if not os.path.isfile(f):
            raise TestError("missing file %s" % f)
    # lit's %t is <output dir>/<test file name>.tmp.
    result_file = os.path.join(work_dir, name + ".tmp" + ext)
    messages = []
    for index, (command, kind, operand) in enumerate(steps, 1):
        if kind == "cp":
            shutil.copyfile(operand, result_file)
            step_messages, passed = [], True
        elif kind == "update":
            step_messages, passed = run_update(result_file, llc, work_dir)
        elif kind == "diff":
            step_messages, passed = run_diff(operand, result_file)
        else:
            step_messages, passed = run_llc_filecheck(
                operand, result_file, llc, work_dir
            )
        if not passed:
            messages.append(
                "RUN command %d of %d failed: %s" % (index, len(steps), command)
            )
            return messages + step_messages, False
        messages += step_messages
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

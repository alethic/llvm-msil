#!/usr/bin/env python3
# ===-- lit-retry.py - The tool timeouts of a failed lit run ----------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Select the failed tests of a lit run that only a tool timeout failed (plan
5.17.9), for the automated retry of msil/utils/ci/lit-legs.ps1 and
lit-legs.sh.

    python msil/utils/ci/lit-retry.py select <xunit> [--max N]
    python msil/utils/ci/lit-retry.py --self-test

select reads the xunit file that lit wrote (--xunit-xml-output) and prints
the full lit name of every timeout-only failed test, one per line, in the
file's order, for example 'MSIL :: M1/09-structs/struct-byval.c'. A failed
test is a testcase with a <failure> (lit's form) or an <error> element. It is
timeout-only when the part of its failure text after the last line that
starts '# executed command:' (the command at which lit's internal shell
stopped) contains a timeout marker:

  - a line containing 'did not finish within <T> s' (msil-run.py's
    'msil-run: <tool> did not finish within <T> s', msil-verify.py's
    '<tool> did not finish within <T> s', msil-jitall.py's 'did not finish
    within <T> s' and '... (wsl.exe)', msil-steps.py's);
  - a transcript status line '[timeout]' (msil-run.py, msil-jitall.py), also
    as the added line '+[timeout]' of msil-run.py's diff against the
    expected file;
  - msil-prog.py's '<step> failed (timeout after <T> s)'.

lit's own output prefix '# | ' is ignored. A failure text without a
'# executed command:' line (lit did not run a command: an unresolved test, a
substitution error) is never timeout-only. The full name is lit's
'<suite> :: <path in suite>', rebuilt from the testcase's classname
('<suite>.<directory>', or '<suite>.<suite>' for a test at the suite's root)
and name; lit writes a '.' of a directory name as '_' there, so a test below
such a directory would be printed with '_' (no test directory of this
repository has a '.' in its name) and its re-run would match nothing and fail.

Exit status of select:
  0  every failed test is timeout-only and at most N (default 3) are, which
     includes a file without a failed test (nothing is printed then);
  3  every failed test is timeout-only, but more than N are (the names are
     printed; stderr says how many);
  4  a failed test is not timeout-only (the timeout-only names are printed;
     stderr names the others): nothing may be re-run;
  2  usage error, or the xunit file is missing or cannot be parsed.

--self-test checks the selection on synthetic xunit files in a temporary
directory outside the repository and exits 0, or 1 naming the failed case.
Python 3 standard library only.
"""

import argparse
import contextlib
import io
import os
import re
import shutil
import sys
import tempfile
import xml.etree.ElementTree as ET

DEFAULT_MAX = 3

EXECUTED_RE = re.compile(r"^# executed command:.*$", re.M)
# lit's prefix of a command's output lines (TestRunner.formatOutput).
OUTPUT_PREFIX_RE = re.compile(r"^# \| ?")
NUMBER = r"[0-9]+(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?"
FINISH_RE = re.compile(r"did not finish within " + NUMBER + r" s\b")
PROG_STEP_RE = re.compile(r"failed \(timeout after " + NUMBER + r" s\)")
STATUS_RE = re.compile(r"^\+?\[timeout\]\s*$")


class XunitError(Exception):
    pass


def full_name(classname, name):
    """The full lit name of an xunit testcase (lit/reports.py writes
    classname '<suite>.<directory with . as _>', or '<suite>.<suite>' for a
    test at the suite's root)."""
    suite, sep, directory = classname.partition(".")
    if not sep or not suite:
        raise XunitError(
            "testcase classname %r is not '<suite>.<directory>'" % classname
        )
    if directory == suite or not directory:
        return "%s :: %s" % (suite, name)
    return "%s :: %s/%s" % (suite, directory, name)


def stopped_part(text):
    """The failure text after the last '# executed command:' line, or None
    when it has none."""
    last = None
    for m in EXECUTED_RE.finditer(text):
        last = m
    if last is None:
        return None
    return text[last.end() :]


def is_timeout_only(text):
    part = stopped_part(text)
    if part is None:
        return False
    for line in part.splitlines():
        line = OUTPUT_PREFIX_RE.sub("", line, count=1)
        if FINISH_RE.search(line) or PROG_STEP_RE.search(line) or STATUS_RE.match(line):
            return True
    return False


def failed_tests(path):
    """[(full name, failure text)] of the failed testcases of an xunit
    file, in its order."""
    try:
        tree = ET.parse(path)
    except FileNotFoundError:
        raise XunitError("%s: no such file" % path)
    except (OSError, ET.ParseError) as e:
        raise XunitError("cannot read %s: %s" % (path, e))
    out = []
    for case in tree.getroot().iter("testcase"):
        bad = [c for c in case if c.tag in ("failure", "error")]
        if not bad:
            continue
        classname, name = case.get("classname"), case.get("name")
        if classname is None or name is None:
            raise XunitError("%s: a testcase without classname or name" % path)
        text = "\n".join(
            "\n".join(filter(None, [c.get("message"), "".join(c.itertext())]))
            for c in bad
        )
        out.append((full_name(classname, name), text))
    return out


def select(path, max_count, out, err):
    try:
        failed = failed_tests(path)
    except XunitError as e:
        err.write("lit-retry: %s\n" % e)
        return 2
    timeouts = [n for n, text in failed if is_timeout_only(text)]
    others = [n for n, text in failed if not is_timeout_only(text)]
    for n in timeouts:
        out.write(n + "\n")
    if others:
        shown = others[:10]
        more = len(others) - len(shown)
        err.write(
            "lit-retry: %d failed test(s) not timeout-only, so nothing is re-run: %s%s\n"
            % (len(others), ", ".join(shown), " and %d more" % more if more else "")
        )
        return 4
    if len(timeouts) > max_count:
        err.write(
            "lit-retry: %d tests timed out, more than --max %d\n"
            % (len(timeouts), max_count)
        )
        return 3
    return 0


# ---------------------------------------------------------------- self-test

RUN_LINE = "# RUN: at line 3\n"


def failure_output(*commands):
    """lit's output of a failed test whose internal shell ran the given
    (command, stdout, stderr) triples, the last of them failing."""
    text = "Exit Code: 1\n\nCommand Output (stdout):\n--\n" + RUN_LINE
    for i, (cmd, stdout, stderr) in enumerate(commands):
        text += "# executed command: %s\n" % cmd
        for title, data in (("command stdout", stdout), ("command stderr", stderr)):
            if data:
                text += "# .---%s%s\n" % (title, "-" * (26 - len(title)))
                text += "".join("# | %s\n" % line for line in data.splitlines())
                text += "# `---%s\n" % ("-" * 26)
        if i == len(commands) - 1:
            text += "# error: command failed with exit status: 1\n"
    return text + "\n--\n\n"


def xunit(cases):
    """An xunit file of lit's form: cases are (classname, name, failure text
    or None, element tag)."""
    out = ['<?xml version="1.0" encoding="UTF-8"?>', '<testsuites time="1.00">']
    out.append(
        '<testsuite name="MSIL" tests="%d" failures="0" skipped="0" time="1.00">'
        % len(cases)
    )
    for case in cases:
        classname, name, text = case[:3]
        tag = case[3] if len(case) > 3 else "failure"
        if text is None:
            out.append(
                '<testcase classname="%s" name="%s" time="0.50"/>' % (classname, name)
            )
            continue
        text = text.replace("]]>", "]]]]><![CDATA[>")
        out.append(
            '<testcase classname="%s" name="%s" time="0.50">\n  <%s><![CDATA[%s]]></%s>\n</testcase>'
            % (classname, name, tag, text, tag)
        )
    out += ["</testsuite>", "</testsuites>", ""]
    return "\n".join(out)


VERIFY = "python msil-verify.py --timeout 60 prog.exe"
RUN = "python msil-run.py --timeout 60 prog.exe --expect prog.expected"
FILECHECK = "FileCheck prog.c"

TIMEOUT_VERIFY = failure_output(
    ("clang -c prog.c -o prog.o", "", ""),
    (VERIFY, "", "O6: peverify /md prog.exe: PEVerify.exe did not finish within 60 s"),
)
TIMEOUT_RUN_DIFF = failure_output(
    (
        RUN,
        "",
        "msil-run: output of prog.exe does not match prog.expected\n"
        "--- prog.expected\n+++ actual\n@@ -1,2 +1 @@\n-hello\n-[exit 0]\n+[timeout]",
    ),
)
TIMEOUT_STATUS = failure_output(("python msil-jitall.py prog.exe", "[timeout]", ""))
TIMEOUT_WSL = failure_output(
    (
        "python msil-jitall.py --runner wsl prog.exe",
        "",
        "did not finish within 70.5 s (wsl.exe)",
    ),
)
TIMEOUT_PROG = failure_output(
    (
        "python msil-prog.py --mode native",
        "",
        "native compile failed (timeout after 600 s)",
    ),
)
TIMEOUT_STEPS = failure_output(
    (
        "python msil-steps.py level.json",
        "msil-steps: verify: msil-verify.py prog.exe",
        "msil-steps: dotnet did not finish within 1e+03 s",
    ),
)
NOT_TIMEOUT = failure_output(
    (RUN, "", "msil-run: output of prog.exe does not match prog.expected")
)
# An earlier command timed out, but lit stopped at a later one that failed
# on its own (the marker is before the last '# executed command:' line).
TIMEOUT_EARLIER = failure_output(
    (
        "python msil-run.py --tool dotnet",
        "",
        "msil-run: dotnet did not finish within 60 s",
    ),
    (FILECHECK, "", "prog.c:12:11: error: CHECK: expected string not found in input"),
)
# The marker text appears only in the failed command line itself.
MARKER_IN_COMMAND = failure_output(("echo did not finish within 60 s && false", "", ""))
NO_COMMAND = "Exit Code: 1\n\nmsil-run: dotnet did not finish within 60 s\n"


def self_test():
    work = tempfile.mkdtemp(prefix="lit-retry-self-test-")
    problems = []

    def case(
        what, cases, want_rc, want_names, max_count=DEFAULT_MAX, err_has=None, raw=None
    ):
        path = os.path.join(work, "%d.xml" % len(problems_seen))
        problems_seen.append(what)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(raw if raw is not None else xunit(cases))
        out, err = io.StringIO(), io.StringIO()
        rc = select(path, max_count, out, err)
        names = out.getvalue().splitlines()
        if rc != want_rc or names != want_names:
            problems.append(
                "%s: exit %d (want %d), printed %r (want %r); stderr %r"
                % (what, rc, want_rc, names, want_names, err.getvalue())
            )
        elif err_has and err_has not in err.getvalue():
            problems.append("%s: stderr %r lacks %r" % (what, err.getvalue(), err_has))

    problems_seen = []
    passed = ("MSIL.M1/03-control", "loops.c", None)
    struct = ("MSIL.M1/09-structs", "struct-byval.c", TIMEOUT_VERIFY)
    case("no failure", [passed], 0, [])
    case(
        "one msil-verify.py timeout",
        [passed, struct],
        0,
        ["MSIL :: M1/09-structs/struct-byval.c"],
    )
    case(
        "the [timeout] status line in msil-run.py's diff",
        [("MSIL.M2/08-semantics", "p6-dtors.test", TIMEOUT_RUN_DIFF)],
        0,
        ["MSIL :: M2/08-semantics/p6-dtors.test"],
    )
    case(
        "a [timeout] transcript line of msil-jitall.py",
        [("MSIL.M3/08-atomics", "barrier-spin.test", TIMEOUT_STATUS)],
        0,
        ["MSIL :: M3/08-atomics/barrier-spin.test"],
    )
    case(
        "msil-jitall.py's wsl.exe timeout",
        [("MSIL.M1/11-programs", "fib.c", TIMEOUT_WSL)],
        0,
        ["MSIL :: M1/11-programs/fib.c"],
    )
    case(
        "msil-prog.py's step timeout",
        [("MSIL.Programs/zlib", "zlib.test", TIMEOUT_PROG)],
        0,
        ["MSIL :: Programs/zlib/zlib.test"],
    )
    case(
        "msil-steps.py's timeout",
        [("MSIL.M1/02-fp", "strict-i128.c", TIMEOUT_STEPS)],
        0,
        ["MSIL :: M1/02-fp/strict-i128.c"],
    )
    case(
        "a virtual c-testsuite test and a test at the suite's root",
        [
            ("MSIL.Programs/c-testsuite", "00016", TIMEOUT_VERIFY),
            ("MSIL.MSIL", "top.test", TIMEOUT_RUN_DIFF),
        ],
        0,
        ["MSIL :: Programs/c-testsuite/00016", "MSIL :: top.test"],
    )
    case(
        "an <error> element",
        [("LLVM.CodeGen/MSIL", "fence.ll", TIMEOUT_VERIFY, "error")],
        0,
        ["LLVM :: CodeGen/MSIL/fence.ll"],
    )
    case(
        "a failure that is not a timeout",
        [struct, ("MSIL.M1/05-memory", "memcpy.c", NOT_TIMEOUT)],
        4,
        ["MSIL :: M1/09-structs/struct-byval.c"],
        err_has="MSIL :: M1/05-memory/memcpy.c",
    )
    case(
        "a timeout before the command at which lit stopped",
        [("MSIL.M1/05-memory", "memcpy.c", TIMEOUT_EARLIER)],
        4,
        [],
        err_has="not timeout-only",
    )
    case(
        "the marker only in the failed command line",
        [("MSIL.M1/05-memory", "memcpy.c", MARKER_IN_COMMAND)],
        4,
        [],
    )
    case(
        "a failure without an executed command",
        [("MSIL.M1/05-memory", "memcpy.c", NO_COMMAND)],
        4,
        [],
    )
    four = [("MSIL.M1/09-structs", "s%d.c" % i, TIMEOUT_VERIFY) for i in range(4)]
    names4 = ["MSIL :: M1/09-structs/s%d.c" % i for i in range(4)]
    case("four timeouts", four, 3, names4, err_has="4 tests timed out")
    case("four timeouts with --max 4", four, 0, names4, max_count=4)
    case("three timeouts", four[:3], 0, names4[:3])
    case(
        "a CDATA terminator in the output",
        [("MSIL.M1/09-structs", "cdata.c", TIMEOUT_VERIFY + "]]> end\n")],
        0,
        ["MSIL :: M1/09-structs/cdata.c"],
    )
    case(
        "an unparsable file",
        [],
        2,
        [],
        raw="<testsuites><testsuite>",
        err_has="cannot read",
    )
    case(
        "a classname without a suite",
        [],
        2,
        [],
        raw=xunit([("nosuite", "x.c", TIMEOUT_VERIFY)]),
        err_has="not '<suite>.<directory>'",
    )
    out, err = io.StringIO(), io.StringIO()
    rc = select(os.path.join(work, "missing.xml"), DEFAULT_MAX, out, err)
    if rc != 2 or "no such file" not in err.getvalue():
        problems.append("a missing file: exit %d, stderr %r" % (rc, err.getvalue()))
    # The command line: usage errors exit 2 and print nothing on stdout.
    for argv in ([], ["select"], ["select", "a.xml", "--max", "-1"], ["frob"]):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = main(argv)
        if rc != 2 or out.getvalue():
            problems.append(
                "command line %r: exit %d, stdout %r" % (argv, rc, out.getvalue())
            )
    shutil.rmtree(work, ignore_errors=True)
    for p in problems:
        print("lit-retry: self-test: FAIL: %s" % p)
    print(
        "lit-retry: self-test: %d of %d cases passed"
        % (len(problems_seen) + 5 - len(problems), len(problems_seen) + 5)
    )
    return 1 if problems else 0


class Parser(argparse.ArgumentParser):
    def error(self, message):
        self.print_usage(sys.stderr)
        sys.stderr.write("lit-retry: %s\n" % message)
        raise SystemExit(2)


def non_negative(text):
    try:
        value = int(text)
    except ValueError:
        raise argparse.ArgumentTypeError("not a number: %r" % text)
    if value < 0:
        raise argparse.ArgumentTypeError("--max must not be negative")
    return value


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    ap = Parser(prog="lit-retry.py", description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="command")
    sp = sub.add_parser(
        "select", help="print the timeout-only failed tests of an xunit file"
    )
    sp.add_argument("xunit")
    sp.add_argument("--max", type=non_negative, default=DEFAULT_MAX, metavar="N")
    try:
        args = ap.parse_args(argv)
    except SystemExit as e:
        return e.code if isinstance(e.code, int) else 2
    if args.command != "select":
        ap.print_usage(sys.stderr)
        sys.stderr.write("lit-retry: missing command 'select' (or --self-test)\n")
        return 2
    return select(args.xunit, args.max, sys.stdout, sys.stderr)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

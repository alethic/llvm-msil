#!/usr/bin/env python3
# ===-- il-snapshot-selftest.py - Self-test of il-snapshot.py --compare ---===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Self-test of msil/utils/il-snapshot.py's comparison and usage errors.

    python il-snapshot-selftest.py IL_SNAPSHOT_PY WORKDIR

Run by msil/test/harness/il-snapshot.test. It loads IL_SNAPSHOT_PY as a module
(the script loads msil-run.py from its own directory; sys.dont_write_bytecode
keeps __pycache__ out of the source tree) and checks:

  * compare(new, old) on copies of the fixture snapshot baseline/ (next to
    this script) made under WORKDIR, which is emptied first. Equal trees, and
    trees that differ only in il-snapshot.json, give 0 and list nothing. Each
    other case gives a nonzero result, exactly the expected 'differs: <name>',
    'missing: <name> (only in <old>)' and 'extra: <name> (only in <new>)'
    lines and the matching counts in the summary line: a changed .il (one
    byte near its end, the size unchanged; with a unified diff from
    old/<name> to new/<name>), a changed .err (only its exit status), an .il
    whose line endings changed, a file only in the old tree, files only in
    the new tree, and all three kinds at once.
  * main(argv) returns or exits with 2 on usage errors, names the problem on
    stderr and creates no --out: --compare naming a missing directory, --out
    inside --compare, --out inside the repository or holding it, no PATH,
    --jobs 0. Each run names a --build that does not exist, so even a check
    that stopped working could neither compile nor empty anything.
  * tracked_inputs(paths, excludes), the input list (plan 5.12.4: the files
    HEAD tracks, minus each --exclude; a directory excludes its subtree),
    with the module's git() replaced by a fake that answers
    'ls-tree -r -z --name-only HEAD -- <path>' from the listing FAKE_HEAD
    and rejects every other command. Each case checks the inputs (only .c
    and .ll files, each once, sorted), the number of inputs each exclude
    removed, and that git was asked for HEAD once per path, in order ('.'
    for the repository root): a file exclude, a directory exclude,
    msil/test/M1/00 against msil/test/M1/00-smoke (the '/' boundary),
    excludes that match no input, tracked files that are not inputs,
    paths that overlap, paths out of order. find_inputs() prints
    'note: --exclude <path> matches no input' for exactly the excludes
    that removed nothing (its --build holds empty stand-ins for clang, llc
    and split-file, which are never run).

It prints 'ok: <case>' on stdout for each case that passes and, for one that
fails, 'FAIL: <case>: <why>' with the output it saw on stderr. Exit status 0
when every case passes, 1 otherwise, 2 on a usage error of this script.
Nothing runs git, clang or llc.
"""

import contextlib
import importlib.util
import io
import os
import re
import shutil
import sys
import types

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
BASELINE = os.path.join(HERE, "baseline")
STAMP = "il-snapshot.json"

# The files of the fixture snapshot, by their names in a snapshot tree.
WIN_O0_IL = "x86_64-pc-windows-msvc-msil/O0/msil/test/M1/00-smoke/return-0.c.il"
WIN_O0_ERR = "x86_64-pc-windows-msvc-msil/O0/msil/test/M1/00-smoke/return-0.c.err"
WIN_O2_IL = "x86_64-pc-windows-msvc-msil/O2/msil/test/M1/00-smoke/return-0.c.il"
WIN_O2_ERR = "x86_64-pc-windows-msvc-msil/O2/msil/test/M1/00-smoke/return-0.c.err"
LNX_IL = "x86_64-unknown-linux-gnu-msil/O2/llvm/test/CodeGen/MSIL/dtors.ll.il"
LNX_ERR = "x86_64-unknown-linux-gnu-msil/O2/llvm/test/CodeGen/MSIL/dtors.ll.err"
FIXTURE = (WIN_O0_IL, WIN_O0_ERR, WIN_O2_IL, WIN_O2_ERR, LNX_IL, LNX_ERR)
# Files that only a new tree has (a configuration the old one lacks).
MINGW_IL = "x86_64-w64-windows-gnu-msil/O2/msil/test/M1/00-smoke/return-0.c.il"
MINGW_ERR = "x86_64-w64-windows-gnu-msil/O2/msil/test/M1/00-smoke/return-0.c.err"

LISTING_RE = re.compile(r"^(?:differs|missing|extra): ")
SUMMARY_RE = re.compile(
    r"^il-snapshot: compared (\d+) files with (.*): "
    r"(\d+) differ, (\d+) missing, (\d+) extra$"
)


class SetupError(Exception):
    pass


def slash(path):
    return path.replace("\\", "/")


def fixture_path(root, name):
    return os.path.join(root, *name.split("/"))


def read(root, name):
    with open(fixture_path(root, name), "rb") as f:
        return f.read()


def write(root, name, data):
    path = fixture_path(root, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def replace_last(root, name, old, new):
    """Replace the last occurrence of old in the file; the size stays."""
    if len(old) != len(new):
        raise SetupError("replace_last(%s) would change the size" % name)
    data = read(root, name)
    i = data.rfind(old)
    if i < 0:
        raise SetupError("%r is not in the fixture %s" % (old, name))
    write(root, name, data[:i] + new + data[i + len(old) :])


def remove(root, name):
    os.remove(fixture_path(root, name))


# ------------------------------------------------------------ compare cases
# Each case changes a fresh copy of the baseline (the new tree) and gives
# the names that compare(new, old) must list: (differs, missing, extra), and
# the lines that must appear besides the listing.


def case_equal(new):
    return [], [], [], []


def case_stamp_only(new):
    # il-snapshot.json is left out of the comparison (only HEAD differs, so
    # there is no note either).
    replace_last(new, STAMP, b'"head": "0', b'"head": "1')
    return [], [], [], []


def case_changed_il(new):
    # The last '.maxstack 1' is '<msil>Main''s, three lines before the end.
    replace_last(new, WIN_O2_IL, b"  .maxstack 1\n", b"  .maxstack 2\n")
    diff = [
        "--- old/" + WIN_O2_IL,
        "+++ new/" + WIN_O2_IL,
        "-  .maxstack 1",
        "+  .maxstack 2",
    ]
    return [WIN_O2_IL], [], [], diff


def case_changed_err(new):
    # The .il beside it is unchanged; only the exit status differs.
    replace_last(new, WIN_O0_ERR, b"[exit 0]\n", b"[exit 1]\n")
    diff = ["--- old/" + WIN_O0_ERR, "+++ new/" + WIN_O0_ERR, "-[exit 0]", "+[exit 1]"]
    return [WIN_O0_ERR], [], [], diff


def case_changed_eol(new):
    # The same lines with CRLF: the snapshot is compared byte for byte.
    data = read(new, WIN_O0_IL)
    if b"\r" in data:
        raise SetupError("the fixture %s already has CR bytes" % WIN_O0_IL)
    write(new, WIN_O0_IL, data.replace(b"\n", b"\r\n"))
    return [WIN_O0_IL], [], [], []


def case_missing(new):
    # The compiler printed nothing this time: the .il is gone, the .err stays.
    remove(new, LNX_IL)
    return [], [LNX_IL], [], []


def case_extra(new):
    # A configuration that the old snapshot did not have.
    write(new, MINGW_IL, read(new, WIN_O2_IL))
    write(new, MINGW_ERR, read(new, WIN_O2_ERR))
    return [], [], [MINGW_ERR, MINGW_IL], []


def case_mixed(new):
    replace_last(new, LNX_ERR, b"@msil_dtor_fn", b"@msil_dtor_fx")
    remove(new, WIN_O0_ERR)
    write(new, MINGW_IL, read(new, WIN_O2_IL))
    return [LNX_ERR], [WIN_O0_ERR], [MINGW_IL], []


COMPARE_CASES = (
    ("equal", case_equal),
    ("stamp-only", case_stamp_only),
    ("changed-il", case_changed_il),
    ("changed-err", case_changed_err),
    ("changed-eol", case_changed_eol),
    ("missing", case_missing),
    ("extra", case_extra),
    ("mixed", case_mixed),
)


def run_compare(module, new, old):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        result = module.compare(new, old)
    return result, out.getvalue()


def check_compare(module, work, old, label, mutate):
    new = os.path.join(work, label)
    shutil.copytree(BASELINE, new)
    differs, missing, extra, lines = mutate(new)
    result, output = run_compare(module, new, old)
    got = output.splitlines()
    want = (
        ["differs: %s" % n for n in sorted(differs)]
        + ["missing: %s (only in %s)" % (n, slash(old)) for n in sorted(missing)]
        + ["extra: %s (only in %s)" % (n, slash(new)) for n in sorted(extra)]
    )
    errors = []
    expect_diff = bool(differs or missing or extra)
    # main() exits 1 when the result is nonzero.
    if expect_diff and not result:
        errors.append("compare() returned %r for trees that differ" % (result,))
    elif not expect_diff and result:
        errors.append("compare() returned %r for equal trees" % (result,))
    listing = [l for l in got if LISTING_RE.match(l)]
    if sorted(listing) != sorted(want):
        errors.append(
            "listing differs\n  expected: %r\n  got:      %r" % (sorted(want), listing)
        )
    summary = [m for m in map(SUMMARY_RE.match, got) if m]
    counts = (str(len(differs)), str(len(missing)), str(len(extra)))
    if len(summary) != 1:
        errors.append("expected one summary line, got %d" % len(summary))
    elif summary[0].group(3, 4, 5) != counts or summary[0].group(2) != slash(old):
        errors.append(
            "summary line %r, expected '... with %s: %s differ, %s missing, "
            "%s extra'" % ((summary[0].group(0), slash(old)) + counts)
        )
    for line in lines:
        if line not in got:
            errors.append("no line %r in the output" % line)
    return errors, output


# -------------------------------------------------------------- usage cases


def run_main(module, argv):
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        try:
            status = module.main(argv)
        except SystemExit as e:  # argparse's usage errors
            status = e.code
    return status, out.getvalue() + err.getvalue(), err.getvalue()


def usage_cases(work, repo):
    """(label, argv, stderr regex, --out that must not appear)."""
    build = os.path.join(work, "no-build")
    out = os.path.join(work, "usage-out")
    baseline = os.path.join(work, "usage-baseline")
    os.makedirs(baseline)
    in_repo = os.path.join(repo, "msil", "test", "il-snapshot-selftest-out")
    return (
        (
            "compare-not-a-directory",
            [
                "--build",
                build,
                "--out",
                out,
                "--compare",
                os.path.join(work, "no-such-baseline"),
                "msil/test/M1",
            ],
            r"^il-snapshot: error: --compare .*no-such-baseline is not a directory$",
            out,
        ),
        (
            "out-inside-compare",
            [
                "--build",
                build,
                "--out",
                os.path.join(baseline, "out"),
                "--compare",
                baseline,
                "msil/test/M1",
            ],
            r"^il-snapshot: error: --out and --compare overlap$",
            os.path.join(baseline, "out"),
        ),
        (
            "out-inside-repository",
            ["--build", build, "--out", in_repo, "msil/test/M1"],
            r"^il-snapshot: error: --out .*il-snapshot-selftest-out overlaps "
            r"the repository ",
            in_repo,
        ),
        (
            "out-holds-repository",
            ["--build", build, "--out", os.path.dirname(repo), "msil/test/M1"],
            r"^il-snapshot: error: --out .* overlaps the repository ",
            None,
        ),
        (
            "no-path",
            ["--build", build, "--out", out],
            r"error: the following arguments are required: PATH",
            out,
        ),
        (
            "jobs-zero",
            ["--build", build, "--out", out, "--jobs", "0", "msil/test/M1"],
            r"error: --jobs must be at least 1$",
            out,
        ),
    )


def check_usage(module, argv, pattern, must_not_exist):
    if must_not_exist and os.path.lexists(must_not_exist):
        raise SetupError("%s exists before the run" % must_not_exist)
    status, output, err = run_main(module, argv)
    errors = []
    if status != 2:
        errors.append("main() gave %r, expected 2" % (status,))
    if not re.search(pattern, err, re.M):
        errors.append("no stderr line matches %r" % pattern)
    if must_not_exist and os.path.lexists(must_not_exist):
        errors.append("main() created %s" % must_not_exist)
    return errors, output


# ------------------------------------------------------------ exclude cases
# What 'git ls-tree -r --name-only HEAD' lists in the fake repository. The
# working tree plays no part: tracked_inputs() takes its list from git alone.
# msil/test/M1/00 is a directory whose name is a prefix of 00-smoke's.
FAKE_HEAD = (
    "llvm/test/CodeGen/MSIL/add.ll",
    "llvm/test/CodeGen/MSIL/lit.local.cfg",
    "llvm/test/CodeGen/MSIL/stack-check-errors.mir",
    "msil/test/M1/00-smoke/lit.local.cfg",
    "msil/test/M1/00-smoke/return-0.c",
    "msil/test/M1/00-smoke/return-0.expected",
    "msil/test/M1/00-smoke/return-42.c",
    "msil/test/M1/00-smoke/sub/nested.ll",
    "msil/test/M1/00/zero.c",
    "msil/test/M1/01-int/add.c",
    "msil/test/M1/01-int/add.test",
    "msil/test/M1/01-int/helper.h",
    "msil/test/M1/01-int/mul.ll",
    "msil/test/M1/01-int/notes.cpp",
)
LL_ADD = "llvm/test/CodeGen/MSIL/add.ll"
SMOKE_0 = "msil/test/M1/00-smoke/return-0.c"
SMOKE_42 = "msil/test/M1/00-smoke/return-42.c"
SMOKE_SUB = "msil/test/M1/00-smoke/sub/nested.ll"
ZERO = "msil/test/M1/00/zero.c"
INT_ADD = "msil/test/M1/01-int/add.c"
INT_MUL = "msil/test/M1/01-int/mul.ll"
# The inputs under msil/test/M1, sorted.
M1_INPUTS = [SMOKE_0, SMOKE_42, SMOKE_SUB, ZERO, INT_ADD, INT_MUL]

# (label, paths, excludes, the inputs, {exclude: inputs it removed}).
EXCLUDE_CASES = (
    (
        "file",
        ["msil/test/M1"],
        [SMOKE_0],
        [SMOKE_42, SMOKE_SUB, ZERO, INT_ADD, INT_MUL],
        {SMOKE_0: 1},
    ),
    (
        # The subtree, sub/ included; lit.local.cfg and the .expected file
        # under it are no inputs and do not count.
        "directory",
        ["msil/test/M1"],
        ["msil/test/M1/00-smoke"],
        [ZERO, INT_ADD, INT_MUL],
        {"msil/test/M1/00-smoke": 3},
    ),
    (
        # A path component is matched whole: 00 is not a prefix of 00-smoke.
        "boundary",
        ["msil/test/M1"],
        ["msil/test/M1/00"],
        [SMOKE_0, SMOKE_42, SMOKE_SUB, INT_ADD, INT_MUL],
        {"msil/test/M1/00": 1},
    ),
    (
        # A file HEAD lacks (gate step 19's exclude before the commit), a
        # tracked file outside the paths, a directory HEAD lacks.
        "no-match",
        ["msil/test/M1"],
        ["llvm/test/CodeGen/MSIL/imports-linked.ll", LL_ADD, "msil/test/M1/02-fp"],
        M1_INPUTS,
        {
            "llvm/test/CodeGen/MSIL/imports-linked.ll": 0,
            LL_ADD: 0,
            "msil/test/M1/02-fp": 0,
        },
    ),
    (
        # Only .c and .ll files are inputs; an exclude that names a tracked
        # file of another kind removes none.
        "non-input",
        ["msil/test/M1", "llvm/test/CodeGen/MSIL"],
        ["msil/test/M1/00-smoke/return-0.expected", "msil/test/M1/01-int/helper.h"],
        [LL_ADD] + M1_INPUTS,
        {
            "msil/test/M1/00-smoke/return-0.expected": 0,
            "msil/test/M1/01-int/helper.h": 0,
        },
    ),
    (
        # Overlapping paths ('' is the repository root): each input once,
        # and an exclude counts each input it removed once.
        "duplicates",
        ["msil/test/M1/00-smoke", "", "msil/test/M1", INT_ADD],
        [SMOKE_42, "msil/test/M1/01-int"],
        [LL_ADD, SMOKE_0, SMOKE_SUB, ZERO],
        {SMOKE_42: 1, "msil/test/M1/01-int": 2},
    ),
    (
        # git lists each path in order; the inputs come back sorted.
        "sorted",
        ["msil/test/M1/01-int", "llvm/test/CodeGen/MSIL", "msil/test/M1/00-smoke"],
        [],
        [LL_ADD, SMOKE_0, SMOKE_42, SMOKE_SUB, INT_ADD, INT_MUL],
        {},
    ),
)


class FakeGitError(Exception):
    pass


class FakeGit:
    """git(args) of il-snapshot.py over FAKE_HEAD. It answers only
    'ls-tree -r -z --name-only HEAD -- <path>' (names ending in NUL, the
    subtree of a directory, every name for '.') and records each path."""

    OPTIONS = ["--name-only", "-r", "-z"]

    def __init__(self):
        self.paths = []

    def __call__(self, args):
        args = list(args)
        if (
            len(args) < 4
            or args[0] != "ls-tree"
            or args[-3:-1] != ["HEAD", "--"]
            or sorted(args[1:-3]) != self.OPTIONS
        ):
            raise FakeGitError(
                "git %r: expected 'ls-tree -r -z --name-only HEAD -- <path>'" % args
            )
        path = args[-1]
        self.paths.append(path)
        if not path:
            raise FakeGitError("git %r: an empty path is not a pathspec" % args)
        names = [
            n
            for n in sorted(FAKE_HEAD)
            if path == "." or n == path or n.startswith(path + "/")
        ]
        return b"".join(n.encode("utf-8") + b"\0" for n in names)


@contextlib.contextmanager
def fake_git(module):
    fake, real = FakeGit(), module.git
    module.git = fake
    try:
        yield fake
    finally:
        module.git = real


def differ(what, want, got):
    if got != want:
        return ["%s differ\n  expected: %r\n  got:      %r" % (what, want, got)]
    return []


def check_git_paths(fake, paths):
    want = [p or "." for p in paths]
    if fake.paths != want:
        return ["git ls-tree ran for %r, expected %r" % (fake.paths, want)]
    return []


def check_exclude(module, paths, excludes, inputs, used):
    errors = []
    with fake_git(module) as fake:
        try:
            got_inputs, got_used = module.tracked_inputs(list(paths), list(excludes))
        except (FakeGitError, module.UsageError) as e:
            return ["tracked_inputs() raised %s: %s" % (type(e).__name__, e)], ""
    output = "paths %r excludes %r\ninputs %r\nused %r\ngit ls-tree paths %r\n" % (
        paths,
        excludes,
        got_inputs,
        got_used,
        fake.paths,
    )
    errors += differ("inputs", inputs, list(got_inputs))
    errors += differ("used counts", used, dict(got_used))
    errors += check_git_paths(fake, paths)
    return errors, output


def check_note(module, work):
    """find_inputs() names each exclude that removed no input, and only
    those, before anything is compiled."""
    build = os.path.join(work, "exclude-build")
    for tool in ("clang", "llc", "split-file"):
        write(build, "bin/" + tool + module.EXE, b"")
    unused = "llvm/test/CodeGen/MSIL/imports-linked.ll"
    args = types.SimpleNamespace(
        build=build,
        paths=["msil/test/M1"],
        exclude=["msil/test/M1/00", unused, SMOKE_42],
    )
    out = io.StringIO()
    with fake_git(module) as fake, contextlib.redirect_stdout(out):
        try:
            inputs = module.find_inputs(args)
        except (FakeGitError, module.UsageError) as e:
            error = "find_inputs() raised %s: %s" % (type(e).__name__, e)
            return [error], out.getvalue()
    output = out.getvalue()
    notes = [l for l in output.splitlines() if "note:" in l]
    errors = differ("inputs", [SMOKE_0, SMOKE_SUB, INT_ADD, INT_MUL], list(inputs))
    errors += differ(
        "notes", ["il-snapshot: note: --exclude %s matches no input" % unused], notes
    )
    errors += check_git_paths(fake, args.paths)
    return errors, output


# --------------------------------------------------------------------- main


def load(path):
    spec = importlib.util.spec_from_file_location("il_snapshot", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main(argv):
    if len(argv) != 2:
        sys.stderr.write("usage: il-snapshot-selftest.py IL_SNAPSHOT_PY WORKDIR\n")
        return 2
    script = os.path.abspath(argv[0])
    work = os.path.abspath(argv[1])
    # <repo>/msil/utils/il-snapshot.py
    repo = os.path.dirname(os.path.dirname(os.path.dirname(script)))
    module = load(script)
    for name in FIXTURE + (STAMP,):
        if not os.path.isfile(fixture_path(BASELINE, name)):
            sys.stderr.write("FAIL: setup: the fixture lacks %s\n" % name)
            return 1
    if os.path.lexists(work):
        shutil.rmtree(work)
    os.makedirs(work)
    old = os.path.join(work, "old")
    shutil.copytree(BASELINE, old)

    failed = 0

    def report(label, errors, output):
        nonlocal failed
        if errors:
            failed += 1
            sys.stdout.write("FAIL: %s\n" % label)
            sys.stderr.write(
                "FAIL: %s: %s\n---- output ----\n%s---- end ----\n"
                % (label, "\n  ".join(errors), output)
            )
        else:
            sys.stdout.write("ok: %s\n" % label)

    for label, mutate in COMPARE_CASES:
        label = "compare " + label
        try:
            errors, output = check_compare(module, work, old, label.split()[1], mutate)
        except SetupError as e:
            errors, output = ["setup: %s" % e], ""
        report(label, errors, output)
    for label, args, pattern, must_not_exist in usage_cases(work, repo):
        label = "usage " + label
        try:
            errors, output = check_usage(module, args, pattern, must_not_exist)
        except SetupError as e:
            errors, output = ["setup: %s" % e], ""
        report(label, errors, output)
    for label, paths, excludes, inputs, used in EXCLUDE_CASES:
        label = "exclude " + label
        errors, output = check_exclude(module, paths, excludes, inputs, used)
        report(label, errors, output)
    errors, output = check_note(module, work)
    report("exclude note", errors, output)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

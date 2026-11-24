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
  * compare(new, old, ['forms']), --normalize forms (M18, plan 5.12.4: on
    both sides short branches become long ones, 'ldc.i4.m1', 'ldc.i4.<n>'
    and 'ldc.i4.s <n>' become 'ldc.i4 <n>' and ' nomangle cdecl' becomes
    ' cdecl'; only the .il files are normalized), on an old and a new copy
    of the baseline made for each case: an .il whose copies differ only in
    those forms, each in both directions (in the fixture's 'main' and in a
    method with branches, ldc.i4 constants and P/Invokes appended to each
    copy), gives 0, lists nothing and prints '--normalize forms: ... 1 of
    them differ only in those forms', and compare(new, old) lists it and
    gives nonzero; with a branch label changed as well it is listed
    (nonzero) and diffed between the normalized texts, exactly that line
    removed and added, while another .il that differs only in a form is
    counted, not listed; the same kinds of change in an .err file are listed
    (nonzero) with their diff as they are.
  * main(argv) returns or exits with 2 on usage errors, names the problem on
    stderr and creates no --out: --compare naming a missing directory, --out
    inside --compare, --out inside the repository or holding it, no PATH,
    --jobs 0, --normalize without --compare. Each run names a --build that
    does not exist, so even a check that stopped working could neither
    compile nor empty anything.
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
  * M20's options (plan 5.12.4). --skip-changed: compare(new, old, (),
    skipped) on a new tree that lacks the four files of a skipped input and
    has a changed .err of another one leaves the skipped files out, prints
    'il-snapshot: --skip-changed: 4 files of 1 inputs changed against HEAD
    were not compared' and still lists the .err (compare(new, old) lists the
    skipped input's files as missing); changed_inputs(paths), with git()
    replaced by a fake, asks 'git status --porcelain -z
    --untracked-files=all -- <paths>' ('.' for the repository root) once and
    returns a modified file, an untracked one and both names of a rename.
    --llc-arg: Snapshot.compile(), with run() replaced so nothing runs,
    passes each argument to llc after the optimization level and to clang
    as '-mllvm <argument>' before the input.
  * FX-2's --expect-change REGEX (plan 5.17.5, FX-2.b's interface; FX-2's
    gate step 22 passes 'initblk'): compare(new, old, normalize, skipped,
    re.compile(REGEX)), the call main() makes, and once
    compare(..., expect_change=REGEX), on an old and a new copy of the
    baseline made for each case. A differing .il whose baseline (the old
    tree's file as written) matches REGEX through re.search is accepted:
    listed as 'il-snapshot: expected change: <name>' (sorted, with no
    diff), counted in the one line 'il-snapshot: --expect-change: <N>
    differing .il files were accepted because their baseline text matches
    <REGEX>' (printed with N = 0 too) and left out of the result and of the
    summary's differ count, so trees whose only differences are accepted
    give 0; without the option the same trees list it, with its diff, and
    print neither line. A differing .il whose baseline does not match is
    listed, also when only its new text matches, and case matters; a
    changed .err (of the accepted .il's own input and configuration), a
    missing .il and an extra .il are listed although REGEX matches each of
    them. With --normalize forms a file that differs only in the forms is
    not accepted, and REGEX is matched against the baseline as written,
    not as normalized; with --skip-changed the skipped input's files are
    neither accepted nor counted; a baseline that is not valid UTF-8 is
    searched all the same. main() exits 2 as above for --expect-change
    without --compare, given twice, empty, not a valid regular expression,
    and without a value. In the cases expect-change-accepted and
    expect-change-not-matched main(argv) also runs end to end on the
    case's trees, with --compare <old tree> --expect-change REGEX, a
    --build that does not exist and two PATHs under the module's REPO,
    which is moved to a folder inside WORKDIR with --out beside it (under
    lit WORKDIR lies inside the checkout, and main() refuses an --out
    inside the repository). Its input listing (find_inputs()) and its
    snapshot (snapshot()) are replaced, so --out receives a copy of the new
    tree and nothing is compiled, and git() by a fake that answers only
    'rev-parse HEAD' (the stamp's head). Trees whose only difference is
    accepted give exit status 0, the 'expected change:' line, the count
    line and no 'differs:' line; with 'cpblk', which no baseline holds,
    main() gives 1 and lists each differing file as 'differs:'; and
    il-snapshot.json in --out records REGEX as "expect-change".

It prints 'ok: <case>' on stdout for each case that passes and, for one that
fails, 'FAIL: <case>: <why>' with the output it saw on stderr. Exit status 0
when every case passes, 1 otherwise, 2 on a usage error of this script.
Nothing runs git, clang or llc.
"""

import contextlib
import importlib.util
import io
import json
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


def substitute(root, name, old, new):
    """Replace the one occurrence of old in the file; the size may change."""
    data = read(root, name)
    if data.count(old) != 1:
        raise SetupError(
            "%r occurs %d times in %s, expected once" % (old, data.count(old), name)
        )
    write(root, name, data.replace(old, new))


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


def run_compare(module, new, old, normalize=None):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        if normalize is None:
            result = module.compare(new, old)
        else:
            result = module.compare(new, old, normalize)
    return result, out.getvalue()


def check_compare(module, work, old, label, mutate):
    new = os.path.join(work, label)
    shutil.copytree(BASELINE, new)
    differs, missing, extra, lines = mutate(new)
    result, output = run_compare(module, new, old)
    errors = verify_compare(result, output, new, old, differs, missing, extra, lines)
    return errors, output


def verify_compare(result, output, new, old, differs, missing, extra, lines):
    """The errors of compare(new, old)'s result and output against the
    names it must list and the lines that must appear besides the listing."""
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
    return errors


# ---------------------------------------------------------- normalize cases
# --normalize forms (M18, plan 5.12.4) rewrites the .il files of both trees:
# short branches become long ones, 'ldc.i4.m1', 'ldc.i4.<n>' and
# 'ldc.i4.s <n>' become 'ldc.i4 <n>', ' nomangle cdecl' becomes ' cdecl'.
# Each case changes fresh copies of the baseline, a new and an old tree, and
# gives the runs of compare(new, old[, normalize]) to check: (normalize, or
# None for compare(new, old); the names it must list as differing; the N of
# its one line '--normalize forms: ... N of them differ only in those forms',
# or None to skip that check; the exact '-' and '+' lines of its diffs, or
# None to skip that check).

NORMALIZE_RE = re.compile(
    r"^il-snapshot: --normalize forms: .* (\d+) of them differ only in those forms$"
)


def pinvoke(flags, result, name):
    return (
        '.method public static pinvokeimpl("libc.so.6" %s) %s %s(native int) '
        "cil managed preservesig" % (flags, result, name)
    )


# A method that the old and the new tree print in different forms: (old
# line, new line), or one string for a line that both print. Every pair
# differs only in the forms, some in each direction.
FORMS_METHOD = (
    ".method public static int32 'pick'(int32 'x') cil managed",
    "{",
    "  .maxstack 2",
    "  ldarg.0",
    ("  brfalse.s @BB1_3", "  brfalse @BB1_3"),
    "  ldarg.0",
    ("  ldc.i4.m1", "  ldc.i4 -1"),
    ("  beq @BB1_2", "  beq.s @BB1_2"),
    "  ldarg.0",
    ("  ldc.i4.s 100", "  ldc.i4 100"),
    ("  bge.un.s @BB1_2", "  bge.un @BB1_2"),
    ("  br @BB1_1", "  br.s @BB1_1"),
    "@BB1_1:",
    ("  ldc.i4 8", "  ldc.i4.8"),
    "  ret",
    "@BB1_2:",
    ("  ldc.i4 -5", "  ldc.i4.s -5"),
    "  ret",
    "@BB1_3:",
    ("  ldc.i4.3", "  ldc.i4 3"),
    "  ret",
    "} // end of method 'pick'",
    "",
    (pinvoke("cdecl", "int32", "puts"), pinvoke("nomangle cdecl", "int32", "puts")),
    "{",
    "}",
    "",
    (
        pinvoke("nomangle cdecl", "native int", "strlen"),
        pinvoke("cdecl", "native int", "strlen"),
    ),
    "{",
    "}",
    "",
)
# Lines of the fixture's 'main' in WIN_O0_IL (the old tree's) and the forms
# the new tree prints instead.
FORMS_IN_MAIN = (
    (b"  ldc.i4.s 16\n", b"  ldc.i4 16\n"),
    (b"  ldc.i4.0\n", b"  ldc.i4 0\n"),
    (b"  ldc.i4 12\n", b"  ldc.i4.s 12\n"),
)


def side(table, which):
    """The text of one tree (0 old, 1 new) of a table like FORMS_METHOD."""
    lines = [l if isinstance(l, str) else l[which] for l in table]
    return ("\n".join(lines) + "\n").encode("utf-8")


def forms_only(new, old):
    """WIN_O0_IL of the two trees differs only in the forms."""
    for a, b in FORMS_IN_MAIN:
        substitute(new, WIN_O0_IL, a, b)
    write(old, WIN_O0_IL, read(old, WIN_O0_IL) + side(FORMS_METHOD, 0))
    write(new, WIN_O0_IL, read(new, WIN_O0_IL) + side(FORMS_METHOD, 1))


def case_normalize_forms(new, old):
    forms_only(new, old)
    return [
        (["forms"], [], 1, []),
        # Without --normalize the same trees differ.
        (None, [WIN_O0_IL], None, None),
    ]


def case_normalize_real_difference(new, old):
    forms_only(new, old)
    # A branch label is no form: after normalization one line remains, and
    # the diff is taken between the normalized texts.
    substitute(new, WIN_O0_IL, b"  br.s @BB1_1\n", b"  br.s @BB1_2\n")
    # Another .il that differs only in a form is counted, not listed.
    substitute(new, WIN_O2_IL, b"  ldc.i4.0\n", b"  ldc.i4 0\n")
    return [(["forms"], [WIN_O0_IL], 1, ["-  br @BB1_1", "+  br @BB1_2"])]


def case_normalize_err_exact(new, old):
    # The .err files are compared as they are, with --normalize too.
    lines = (
        ("  br @BB0_1", "  br.s @BB0_1"),
        ("  ldc.i4 0", "  ldc.i4.0"),
        ("  ldc.i4 -1", "  ldc.i4.m1"),
        ('  pinvokeimpl("x" cdecl)', '  pinvokeimpl("x" nomangle cdecl)'),
    )
    write(old, WIN_O0_ERR, side(lines, 0) + read(old, WIN_O0_ERR))
    write(new, WIN_O0_ERR, side(lines, 1) + read(new, WIN_O0_ERR))
    substitute(new, WIN_O2_IL, b"  ldc.i4.0\n", b"  ldc.i4 0\n")
    diff = ["-" + a for a, b in lines] + ["+" + b for a, b in lines]
    return [(["forms"], [WIN_O0_ERR], 1, diff)]


NORMALIZE_CASES = (
    ("normalize-forms", case_normalize_forms),
    ("normalize-real-difference", case_normalize_real_difference),
    ("normalize-err-exact", case_normalize_err_exact),
)


def check_normalize(module, work, label, mutate):
    new = os.path.join(work, label, "new")
    old = os.path.join(work, label, "old")
    shutil.copytree(BASELINE, new)
    shutil.copytree(BASELINE, old)
    errors, outputs = [], []
    for normalize, differs, forms, diff in mutate(new, old):
        result, output = run_compare(module, new, old, normalize)
        what = "compare(new, old%s)" % ("" if normalize is None else ", %r" % normalize)
        outputs.append("%s gave %r:\n%s" % (what, result, output))
        got = output.splitlines()
        errs = verify_compare(result, output, new, old, differs, [], [], [])
        counts = [m.group(1) for m in map(NORMALIZE_RE.match, got) if m]
        if forms is not None and counts != [str(forms)]:
            errs.append(
                "expected one line '--normalize forms: ... %d of them differ "
                "only in those forms', got the counts %r" % (forms, counts)
            )
        if diff is not None:
            changed = [
                l
                for l in got
                if l.startswith(("-", "+"))
                and not l.startswith(("--- old/", "+++ new/"))
            ]
            if sorted(changed) != sorted(diff):
                errs.append(
                    "diff lines differ\n  expected: %r\n  got:      %r"
                    % (sorted(diff), sorted(changed))
                )
        errors += ["%s: %s" % (what, e) for e in errs]
    return errors, "".join(outputs)


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
    # FX-2's --expect-change runs name a --compare that exists, so that only
    # the REGEX is wrong.
    compare = ["--build", build, "--out", out, "--compare", baseline]
    bad_regex = "(initblk"
    bad_message = re.escape(regex_error(bad_regex))
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
        (
            "normalize-without-compare",
            ["--build", build, "--out", out, "--normalize", "forms", "msil/test/M1"],
            r"^il-snapshot: error: --normalize applies to --compare, which is missing$",
            out,
        ),
        (
            "expect-change-without-compare",
            ["--build", build, "--out", out, "--expect-change", "x", "msil/test/M1"],
            r"^il-snapshot: error: --expect-change applies to --compare, "
            r"which is missing$",
            out,
        ),
        (
            "expect-change-repeated",
            compare + ["--expect-change", "a", "--expect-change", "b", "msil/test/M1"],
            r"^il-snapshot: error: --expect-change is given 2 times; give one "
            r"REGEX \(alternatives with '\|'\)$",
            out,
        ),
        (
            "expect-change-empty",
            compare + ["--expect-change", "", "msil/test/M1"],
            r"^il-snapshot: error: --expect-change is empty: it would accept any "
            r"\.il file$",
            out,
        ),
        (
            # The message ends with re's own message for the REGEX.
            "expect-change-invalid",
            compare + ["--expect-change", bad_regex, "msil/test/M1"],
            r"^il-snapshot: error: --expect-change %s is not a valid regular "
            r"expression: %s$" % (re.escape(bad_regex), bad_message),
            out,
        ),
        (
            "expect-change-no-value",
            compare + ["msil/test/M1", "--expect-change"],
            r"error: argument --expect-change: expected one argument$",
            out,
        ),
    )


def regex_error(pattern):
    try:
        re.compile(pattern)
    except re.error as e:
        return str(e)
    raise SetupError("%r is a valid regular expression" % pattern)


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


# --------------------------------------------------------- M20 option cases

# --skip-changed: compare(new, old, (), skipped) leaves out every file of a
# skipped input on both sides (a baseline taken before the change still holds
# them), counts them, and still lists every other difference;
# changed_inputs() asks 'git status --porcelain -z --untracked-files=all --
# <paths>' and takes both names of a rename; --llc-arg: the llc command gets
# each argument, the clang command '-mllvm <argument>'.

SKIP_LINE_RE = re.compile(
    r"^il-snapshot: --skip-changed: (\d+) files of (\d+) inputs changed against "
    r"HEAD were not compared$"
)


def check_skip_changed_compare(module, work, old):
    # return-0.c is skipped: the new tree lacks its four files and the old
    # tree's copies stay; dtors.ll is not skipped, and its changed .err is
    # listed.
    new = os.path.join(work, "skip-changed")
    shutil.copytree(BASELINE, new)
    for name in (WIN_O0_IL, WIN_O0_ERR, WIN_O2_IL, WIN_O2_ERR):
        remove(new, name)
    replace_last(new, LNX_ERR, b"@msil_dtor_fn", b"@msil_dtor_fx")
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        result = module.compare(new, old, (), ["msil/test/M1/00-smoke/return-0.c"])
    output = out.getvalue()
    errors = verify_compare(result, output, new, old, [LNX_ERR], [], [], [])
    counts = [m.groups() for m in map(SKIP_LINE_RE.match, output.splitlines()) if m]
    errors += differ("--skip-changed counts", [("4", "1")], counts)
    # Without skipped inputs the same trees list return-0.c's files as missing.
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        module.compare(new, old)
    if "missing: %s" % WIN_O0_IL not in out.getvalue():
        errors.append("compare(new, old) did not list %s as missing" % WIN_O0_IL)
    return errors, output


class FakeStatus:
    """git(args) answering 'status --porcelain -z --untracked-files=all --
    <paths>' with a modification, an untracked file and a rename (whose
    original name follows as its own NUL-terminated field)."""

    def __init__(self):
        self.calls = []

    def __call__(self, args):
        self.calls.append(list(args))
        return (
            b" M msil/test/M1/00-smoke/return-0.c\0"
            b"?? msil/test/M1/01-int/new.c\0"
            b"R  msil/test/M1/01-int/renamed.ll\0msil/test/M1/01-int/mul.ll\0"
        )


def check_skip_changed_git(module):
    fake, real = FakeStatus(), module.git
    module.git = fake
    try:
        got = module.changed_inputs(["msil/test/M1", ""])
    finally:
        module.git = real
    errors = differ(
        "changed inputs",
        sorted(
            [
                "msil/test/M1/00-smoke/return-0.c",
                "msil/test/M1/01-int/new.c",
                "msil/test/M1/01-int/renamed.ll",
                "msil/test/M1/01-int/mul.ll",
            ]
        ),
        sorted(got),
    )
    status = ["status", "--porcelain", "-z", "--untracked-files=all", "--"]
    errors += differ("git command", [status + ["msil/test/M1", "."]], fake.calls)
    return errors, "changed %r\ncalls %r\n" % (sorted(got), fake.calls)


def check_llc_arg(module, work):
    # Snapshot.compile() builds the command; run() is replaced so that
    # nothing runs.
    args = types.SimpleNamespace(
        build=os.path.join(work, "no-build"),
        out=os.path.join(work, "llc-arg-out"),
        timeout=1,
        llc_args=["-msil-reloc-loop-threshold=1000000000", "-x"],
    )
    s = module.Snapshot(args)
    seen = []
    s.run = lambda cmd, cwd: (seen.append(cmd), (b"", "[exit 0]\n"))[1]
    s.compile(("ll", work, "a.ll", "a.ll", "x86_64-pc-windows-msvc-msil", "O2"))
    s.compile(("c", work, "a.c", "a.c", "x86_64-pc-windows-msvc-msil", "O0"))
    errors = []
    llc, clang = seen
    if llc[3:5] != args.llc_args or llc[-3:] != ["a.ll", "-o", "-"]:
        errors.append("llc command %r" % llc)
    want = ["-mllvm", args.llc_args[0], "-mllvm", args.llc_args[1]]
    if clang[-7:-3] != want or clang[-3:] != ["a.c", "-o", "-"] or "-S" not in clang:
        errors.append("clang command %r" % clang)
    return errors, "llc %r\nclang %r\n" % (llc, clang)


def run_m20_cases(module, work, old, report):
    errors, output = check_skip_changed_compare(module, work, old)
    report("compare skip-changed", errors, output)
    errors, output = check_skip_changed_git(module)
    report("skip-changed git-status", errors, output)
    errors, output = check_llc_arg(module, work)
    report("llc-arg commands", errors, output)


# -------------------------------------------------- FX-2's --expect-change
# --expect-change REGEX (FX-2, plan 5.17.5; FX-2.b's interface): once
# --skip-changed has left out the skipped inputs' files and --normalize has
# decided which files are equal, each .il file that both trees hold and that
# still differs is accepted when re.search(REGEX, <its baseline, the old
# tree's file as written, decoded as UTF-8 with surrogateescape>) matches.
# An accepted file is listed as 'il-snapshot: expected change: <name>'
# (sorted, with no diff) instead of 'differs: <name>', and counts neither in
# compare()'s result nor in the summary's differ count, but in the line
# 'il-snapshot: --expect-change: <N> differing .il files were accepted
# because their baseline text matches <REGEX>', which is printed whenever
# the option is given. .err, missing and extra files are never accepted.
# Each case changes fresh copies of the baseline, a new and an old tree, and
# gives the runs of compare() to check.

EXPECTED_PREFIX = "il-snapshot: expected change: "
EXPECT_COUNT_RE = re.compile(
    r"^il-snapshot: --expect-change: (\d+) differing \.il files were accepted "
    r"because their baseline text matches (.*)$"
)

# 'clear', a memset with a run-time length, as D15 printed it before FX-2
# (one initblk of the length cut by conv.i4) and in FX-2's guarded loop (a
# guard on bits 32 and up, initblk in 2 GiB blocks, initblk of the rest).
CLEAR_BEFORE = (
    ".method public static void 'clear'(native int 'p', native int 'n') cil managed",
    "{",
    "  .maxstack 3",
    "  ldarg.0",
    "  ldc.i4.0",
    "  ldarg.1",
    "  conv.i4",
    "  initblk",
    "  ret",
    "} // end of method 'clear'",
    "",
)
CLEAR_AFTER = (
    ".method public static void 'clear'(native int 'p', native int 'n') cil managed",
    "{",
    "  .maxstack 3",
    "  .locals (native int, native int)",
    "  ldarg.0",
    "  stloc.0",
    "  ldarg.1",
    "  stloc.1",
    "  ldarg.1",
    "  ldc.i8 4294967295",
    "  conv.i",
    "  cgt.un",
    "  brfalse.s @BB1_2",
    "@BB1_1:",
    "  ldloc.0",
    "  ldc.i4.0",
    "  ldc.i4 -2147483648",
    "  initblk",
    "  ldloc.0",
    "  ldc.i8 2147483648",
    "  conv.i",
    "  add",
    "  stloc.0",
    "  ldloc.1",
    "  ldc.i8 2147483648",
    "  conv.i",
    "  sub",
    "  dup",
    "  stloc.1",
    "  ldc.i8 4294967295",
    "  conv.i",
    "  cgt.un",
    "  brtrue.s @BB1_1",
    "@BB1_2:",
    "  ldloc.0",
    "  ldc.i4.0",
    "  ldloc.1",
    "  conv.i4",
    "  initblk",
    "  ret",
    "} // end of method 'clear'",
    "",
)
# 'zero' as a loop of byte stores and as the one initblk that the optimizer
# can make of it.
ZERO_LOOP = (
    ".method public static void 'zero'(native int 'p') cil managed",
    "{",
    "  .maxstack 3",
    "  .locals (native int)",
    "  ldc.i4.0",
    "  conv.i",
    "  stloc.0",
    "@BB2_1:",
    "  ldarg.0",
    "  ldloc.0",
    "  add",
    "  ldc.i4.0",
    "  stind.i1",
    "  ldloc.0",
    "  ldc.i4.1",
    "  conv.i",
    "  add",
    "  dup",
    "  stloc.0",
    "  ldc.i4.s 64",
    "  conv.i",
    "  blt.un.s @BB2_1",
    "  ret",
    "} // end of method 'zero'",
    "",
)
ZERO_INITBLK = (
    ".method public static void 'zero'(native int 'p') cil managed",
    "{",
    "  .maxstack 3",
    "  ldarg.0",
    "  ldc.i4.0",
    "  ldc.i4.s 64",
    "  initblk",
    "  ret",
    "} // end of method 'zero'",
    "",
)


def append_method(root, name, lines):
    write(root, name, read(root, name) + ("\n".join(lines) + "\n").encode("utf-8"))


def expect(regex, accepted=(), differs=(), missing=(), extra=(), **options):
    """One run of compare(): REGEX (None runs it without --expect-change),
    the names it must accept and list, and the options normalize and
    skipped (compare()'s third and fourth arguments), as_str (pass REGEX as
    the keyword expect_change=, not compiled as main() passes it), present
    (lines the output must hold) and main (run main() end to end with
    --compare and --expect-change REGEX instead: run_main_expect())."""
    run = types.SimpleNamespace(
        regex=regex,
        accepted=list(accepted),
        differs=list(differs),
        missing=list(missing),
        extra=list(extra),
        normalize=(),
        skipped=(),
        as_str=False,
        present=(),
        main=False,
    )
    for key, value in options.items():
        if not hasattr(run, key):
            raise SetupError("expect() has no option %r" % key)
        setattr(run, key, value)
    return run


def case_expect_accepted(new, old):
    # FX-2's gate (step 22 passes 'initblk'): 'clear' had one initblk in
    # the baseline and has the guarded loop now.
    append_method(old, WIN_O0_IL, CLEAR_BEFORE)
    append_method(new, WIN_O0_IL, CLEAR_AFTER)
    return [
        expect("initblk", accepted=[WIN_O0_IL]),
        expect("initblk", accepted=[WIN_O0_IL], as_str=True),
        # main() end to end passes the option on to the comparison: exit
        # status 0, and the stamp records the pattern.
        expect("initblk", accepted=[WIN_O0_IL], main=True),
        # Without the option the same trees differ, with the diff.
        expect(
            None,
            differs=[WIN_O0_IL],
            present=["--- old/" + WIN_O0_IL, "+++ new/" + WIN_O0_IL],
        ),
    ]


def case_expect_not_matched(new, old):
    # FX-2.b's sample: of the two changed .il files only WIN_O0_IL's
    # baseline holds localloc; the changed .err is listed too.
    replace_last(new, WIN_O0_IL, b"  .maxstack 2\n", b"  .maxstack 3\n")
    replace_last(new, WIN_O2_IL, b"  .maxstack 1\n", b"  .maxstack 2\n")
    replace_last(new, LNX_ERR, b"@msil_dtor_fn", b"@msil_dtor_fx")
    diff = [
        "--- old/" + WIN_O2_IL,
        "+++ new/" + WIN_O2_IL,
        "-  .maxstack 1",
        "+  .maxstack 2",
    ]
    return [
        expect(
            "localloc", accepted=[WIN_O0_IL], differs=[WIN_O2_IL, LNX_ERR], present=diff
        ),
        # Case matters (re.search with no flags), and the count line comes
        # with 0 accepted files too.
        expect("LOCALLOC", differs=[WIN_O0_IL, WIN_O2_IL, LNX_ERR]),
        # main() end to end with 'cpblk', which no baseline holds (WIN_O0_IL's
        # holds 'localloc'): exit status 1, all three files listed, WIN_O0_IL
        # with its diff, and the stamp records the pattern.
        expect(
            "cpblk",
            differs=[WIN_O0_IL, WIN_O2_IL, LNX_ERR],
            present=["--- old/" + WIN_O0_IL, "+++ new/" + WIN_O0_IL],
            main=True,
        ),
    ]


def case_expect_baseline_only(new, old):
    # The baseline alone decides, shown with the same two texts in both
    # directions: WIN_O0_IL's baseline holds initblk and its new text does
    # not (accepted); WIN_O2_IL's new text holds initblk and its baseline
    # does not (listed: at FX-2's gate, an input whose baseline IL has no
    # initblk must compile to the same IL).
    append_method(old, WIN_O0_IL, ZERO_INITBLK)
    append_method(new, WIN_O0_IL, ZERO_LOOP)
    append_method(old, WIN_O2_IL, ZERO_LOOP)
    append_method(new, WIN_O2_IL, ZERO_INITBLK)
    return [expect("initblk", accepted=[WIN_O0_IL], differs=[WIN_O2_IL])]


def case_expect_err_missing_extra(new, old):
    # 'MSIL|exit' matches every file of both trees (each .il names MSIL in
    # its first line, each .err ends with its exit status), yet only the
    # changed .il is accepted: the changed .err of the same input and
    # configuration, a missing .il and an extra .il are listed.
    replace_last(new, WIN_O0_IL, b"  .maxstack 2\n", b"  .maxstack 3\n")
    replace_last(new, WIN_O0_ERR, b"[exit 0]\n", b"[exit 1]\n")
    remove(new, LNX_IL)
    write(new, MINGW_IL, read(new, WIN_O2_IL))
    return [
        expect(
            "MSIL|exit",
            accepted=[WIN_O0_IL],
            differs=[WIN_O0_ERR],
            missing=[LNX_IL],
            extra=[MINGW_IL],
        )
    ]


def case_expect_normalize(new, old):
    # --normalize decides first: WIN_O0_IL differs only in the forms, so it
    # is not accepted although its baseline holds 'ldc.i4.0'. WIN_O2_IL
    # still differs, and REGEX is matched against its baseline as written
    # ('ldc.i4.0'), not as normalized ('ldc.i4 0').
    forms_only(new, old)
    replace_last(new, WIN_O2_IL, b"  .maxstack 1\n", b"  .maxstack 2\n")
    return [
        expect(r"ldc\.i4\.0", accepted=[WIN_O2_IL], normalize=["forms"]),
        expect(r"ldc\.i4 0", differs=[WIN_O2_IL], normalize=["forms"]),
    ]


def case_expect_skip_changed(new, old):
    # --skip-changed leaves a skipped input's files out first: return-0.c's
    # two .il files differ and their baselines match 'MSIL', yet with that
    # input skipped only dtors.ll's .il is accepted and counted.
    replace_last(new, WIN_O0_IL, b"  .maxstack 2\n", b"  .maxstack 3\n")
    replace_last(new, WIN_O2_IL, b"  .maxstack 1\n", b"  .maxstack 2\n")
    replace_last(new, LNX_IL, b"'dtors'", b"'dtorz'")
    return [
        expect("MSIL", accepted=[LNX_IL], skipped=["msil/test/M1/00-smoke/return-0.c"]),
        # Without the skipped input all three are accepted, listed sorted.
        expect("MSIL", accepted=[WIN_O0_IL, WIN_O2_IL, LNX_IL]),
    ]


def case_expect_raw_bytes(new, old):
    # A baseline that is not valid UTF-8 is searched all the same (an IR
    # name need not be UTF-8, and D11 prints a non-ASCII name's bytes as
    # they are).
    for root in (new, old):
        write(root, LNX_IL, read(root, LNX_IL) + b"// '\xff'\n")
    replace_last(new, LNX_IL, b"'dtors'", b"'dtorz'")
    return [expect("dtors", accepted=[LNX_IL])]


EXPECT_CASES = (
    ("expect-change-accepted", case_expect_accepted),
    ("expect-change-not-matched", case_expect_not_matched),
    ("expect-change-baseline-only", case_expect_baseline_only),
    ("expect-change-err-missing-extra", case_expect_err_missing_extra),
    ("expect-change-normalize", case_expect_normalize),
    ("expect-change-skip-changed", case_expect_skip_changed),
    ("expect-change-raw-bytes", case_expect_raw_bytes),
)


def run_expect(module, new, old, run):
    """compare(new, old, ...) as the run says: (what was called, its result,
    its output)."""
    args = [new, old, run.normalize, run.skipped]
    kwargs = {}
    what = "compare(new, old, %r, %r" % (run.normalize, run.skipped)
    if run.regex is None:
        what += ")"
    elif run.as_str:
        kwargs["expect_change"] = run.regex
        what += ", expect_change=%r)" % run.regex
    else:
        args.append(re.compile(run.regex))
        what += ", re.compile(%r))" % run.regex
    out = io.StringIO()
    try:
        with contextlib.redirect_stdout(out):
            result = module.compare(*args, **kwargs)
    except Exception as e:  # a crash fails the case, not the self-test
        result, returned = "%s: %s" % (type(e).__name__, e), False
    else:
        returned = True
    # A diff of a file that is not UTF-8 may hold surrogates, which the
    # report on stderr could not encode.
    output = out.getvalue().encode("ascii", "backslashreplace").decode("ascii")
    return what, result, output, returned


# main() end to end, the runs with main=True: main(argv) with --compare
# <old tree> --expect-change REGEX, a --build that does not exist and the
# fixture stamp's two paths. The input listing (find_inputs()) and the
# snapshot (snapshot()) are replaced, so that --out receives a copy of the
# new tree (its il-snapshot.json aside) and nothing is compiled, and git() by
# a fake that answers only 'rev-parse HEAD', the stamp's head. Under lit
# WORKDIR lies inside the checkout, and main() refuses an --out inside the
# repository: the module's REPO becomes a folder inside WORKDIR that holds
# the paths, with --out beside it.

MAIN_HEAD = "0123456789abcdef0123456789abcdef01234567"
# The paths and inputs of the fixture's stamp (so compare() prints no note
# that the snapshots differ in their paths), and its counts.
MAIN_PATHS = ("msil/test/M1", "llvm/test/CodeGen/MSIL")
MAIN_INPUTS = ("llvm/test/CodeGen/MSIL/dtors.ll", "msil/test/M1/00-smoke/return-0.c")
MAIN_COUNTS = {"inputs": 2, "files": 2, "split-parts": 0, "compiles": 12}


def fake_rev_parse(args):
    """git(args) of il-snapshot.py, answering only 'rev-parse HEAD'."""
    if list(args) != ["rev-parse", "HEAD"]:
        raise FakeGitError("git %r: expected 'rev-parse HEAD'" % list(args))
    return (MAIN_HEAD + "\n").encode("ascii")


@contextlib.contextmanager
def replaced(module, **values):
    """The module's globals set to values, and restored afterwards."""
    for name in values:
        if not hasattr(module, name):
            raise SetupError("il-snapshot.py has no %s" % name)
    saved = {name: getattr(module, name) for name in values}
    for name, value in values.items():
        setattr(module, name, value)
    try:
        yield
    finally:
        for name, value in saved.items():
            setattr(module, name, value)


def copy_snapshot(src, dst):
    """Copy the files of the snapshot src, its il-snapshot.json aside, to dst."""
    for d, dirs, names in os.walk(src):
        for n in names:
            name = slash(os.path.relpath(os.path.join(d, n), src))
            if name != STAMP:
                write(dst, name, read(src, name))


def run_main_expect(module, base, new, old, run):
    """main(argv) end to end as the run says, with its folders in base: (what
    was called, its exit status, its output, whether it returned, its --out,
    the errors of the stamp it wrote)."""
    if run.regex is None or run.normalize or run.skipped or run.as_str:
        raise SetupError("a main() run takes a REGEX and no other option")
    repo = os.path.join(base, "repo")
    out = os.path.join(base, "out")
    paths = [fixture_path(repo, p) for p in MAIN_PATHS]
    for path in paths:
        os.makedirs(path)
    argv = ["--build", os.path.join(base, "no-build"), "--out", out]
    argv += ["--compare", old, "--expect-change", run.regex] + paths

    def find_inputs(args):
        # The real one checks --build's tools and asks git for HEAD's files.
        args.skipped = []
        return list(MAIN_INPUTS)

    def snapshot(args, inputs):
        copy_snapshot(new, args.out)
        return dict(MAIN_COUNTS)

    what = "main(--compare <old> --expect-change %r)" % run.regex
    fakes = dict(
        REPO=repo, find_inputs=find_inputs, snapshot=snapshot, git=fake_rev_parse
    )
    stdout, stderr = io.StringIO(), io.StringIO()
    with replaced(module, **fakes):
        try:
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                try:
                    status = module.main(argv)
                except SystemExit as e:  # argparse's usage errors
                    status = e.code
        except Exception as e:  # a crash fails the case, not the self-test
            status, returned = "%s: %s" % (type(e).__name__, e), False
        else:
            returned = True
    output = stdout.getvalue() + stderr.getvalue()
    output = output.encode("ascii", "backslashreplace").decode("ascii")
    errors = []
    if returned:
        try:
            with open(os.path.join(out, STAMP), encoding="utf-8") as f:
                stamp = json.load(f)
        except (OSError, ValueError) as e:
            errors.append("no readable %s in --out: %s" % (STAMP, e))
        else:
            if not isinstance(stamp, dict):
                stamp = {}
            errors += differ(
                "the stamp's 'expect-change'", run.regex, stamp.get("expect-change")
            )
            errors += differ("the stamp's 'head'", MAIN_HEAD, stamp.get("head"))
    return what, status, output, returned, out, errors


def check_expect(module, work, label, mutate):
    new = os.path.join(work, label, "new")
    old = os.path.join(work, label, "old")
    shutil.copytree(BASELINE, new)
    shutil.copytree(BASELINE, old)
    errors, outputs = [], []
    for i, run in enumerate(mutate(new, old)):
        # tree: the new tree that compare() saw, a main() run's --out.
        if run.main:
            base = os.path.join(work, label, "main%d" % i)
            what, result, output, returned, tree, errs = run_main_expect(
                module, base, new, old, run
            )
        else:
            what, result, output, returned = run_expect(module, new, old, run)
            tree, errs = new, []
        if not returned:
            errors.append("%s raised %s" % (what, result))
            outputs.append("%s raised %s:\n%s" % (what, result, output))
            continue
        outputs.append("%s gave %r:\n%s" % (what, result, output))
        got = output.splitlines()
        errs += verify_compare(
            result, output, tree, old, run.differs, run.missing, run.extra, run.present
        )
        if run.main:
            # main() exits 1 for a difference that is not accepted, else 0.
            status = 1 if run.differs or run.missing or run.extra else 0
            if result != status:
                errs.append("main() gave %r, expected %d" % (result, status))
        accepted = [
            l[len(EXPECTED_PREFIX) :] for l in got if l.startswith(EXPECTED_PREFIX)
        ]
        errs += differ("accepted files (output order)", sorted(run.accepted), accepted)
        counts = [m.groups() for m in map(EXPECT_COUNT_RE.match, got) if m]
        want = [] if run.regex is None else [(str(len(run.accepted)), run.regex)]
        errs += differ("'--expect-change: <N> ... matches <REGEX>' lines", want, counts)
        for name in run.accepted:
            for line in ("--- old/" + name, "+++ new/" + name):
                if line in got:
                    errs.append("the accepted file is diffed: %r" % line)
        errors += ["%s: %s" % (what, e) for e in errs]
    return errors, "".join(outputs)


def run_fx2_cases(module, work, report):
    for label, mutate in EXPECT_CASES:
        try:
            errors, output = check_expect(module, work, label, mutate)
        except SetupError as e:
            errors, output = ["setup: %s" % e], ""
        report("compare " + label, errors, output)


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
    for label, mutate in NORMALIZE_CASES:
        label = "compare " + label
        try:
            errors, output = check_normalize(module, work, label.split()[1], mutate)
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
    run_m20_cases(module, work, old, report)
    run_fx2_cases(module, work, report)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

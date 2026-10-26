#!/usr/bin/env python3
# ===-- msil-equiv.py - Compare the canonical listings of two images ------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Compare the canonical listings of two images (the oracles O3 and O4).

    msil-equiv.py --canon "<cmd>" --allowed FILE [--allow NAME]...
        [--oracle O3|O4] [--test PATH [--triple T] [--level L]]
        [--ilasm "<cmd>"] A B

Plan 5.12.4 and 5.12.6 (M20). The lit substitution %msil_equiv is this
script with --canon '%msil_canon' (llvm-cil-dump --canonical, or mdcanon)
and --allowed msil/utils/equiv-allowed.txt; the composites of
msil/test/lit.cfg.py run it for O3 (%{msil_c} and %{msil_ll} under
msil_pipeline=obj, msil_equiv=1: A the --no-gc-sections image of msil-ld, B
the ilasm image of the -S text) and O4 (%{msil_prog} under msil_pipeline=lto,
msil_link_output=pe, msil_equiv=1: A msil-ld's --no-gc-sections LTO image, B
the --lto-emit-asm text of the same link).

It runs the canonical lister <cmd> on A and on B, each time with the dumper
options of every allowed entry appended (in the order of FILE), then the
file, and compares the two listings line by line. Equal listings exit 0 with
one line that says so; otherwise it prints the first 40 differing lines (a
unified diff without context, '-' for A and '+' for B, after its @@ lines)
and exits 1. A lister that fails, or an image that ilasm cannot build, exits
1 too, with that tool's output.

  --allowed FILE  the entries and skip lines (equiv-allowed.txt): a line is
                  a comment ('#' first), an entry '<name> <option>[ <option>]...
                  # <justification>' (every option starts with '--'; the
                  justification is required) or a skip line
                  'skip <oracle>[@<triple>][:<level>] <test path> <reason>'
                  (plan section 12, 'Plan 5.12.6 amended (skip lines)'), whose
                  oracle is O3, O4 or O6 (O6 lines are msil-verify.py's: this
                  script compares O3 and O4 only and never matches them),
                  whose optional triple is an MSIL triple (it ends in '-msil')
                  and whose optional level is one of O0, O1, O2, O3, Os and
                  Oz, as in
                  'skip O3@x86_64-pc-windows-msvc-msil:O2 <path> <reason>'.
                  A malformed line, or a name given twice, is a usage error.
  --allow NAME    allow the entry NAME (repeatable); a name that is not an
                  entry of FILE is a usage error (exit 2) that lists the
                  entries.
  --oracle O3|O4  the comparison (default O3): the label of the output and the
                  oracle whose skip lines count.
  --test PATH     the test that runs the comparison (lit's %s). When a skip
                  line of FILE covers the test, it prints
                  '<oracle> skipped: <reason>' and exits 0 without looking at
                  A or B, which then need not exist. A skip line covers the
                  test when its oracle is --oracle, its path is the test's
                  path relative to the repository root (an absolute PATH
                  inside the repository is made relative; '/' separates) or
                  a directory above it (a skip path that ends in '/'), its
                  triple, if it names one, is --triple, and its level, if it
                  names one, is --level. An unqualified line covers every
                  triple and level; a qualified one covers nothing when the
                  option it needs is not given.
  --triple T      the MSIL triple of the build (the composites pass the
                  test's triple), for qualified skip lines.
  --level L       the optimization level of the build (O0, O1, O2, O3, Os
                  or Oz; the composites pass the composite's level), for
                  qualified skip lines.
  --ilasm "<cmd>" an operand that ends in '.il' is ILAsm text: it is
                  assembled first, '<cmd> -exe -output=<private dir>/<stem>.exe
                  <file>' (the module is named <stem>.exe, as msil-ld names
                  the module of <stem>.exe), and that image is listed. O4's
                  text side is msil-ld's --lto-emit-asm output.

<cmd> (and the --ilasm command) is one string split into words at blanks,
where "..." and '...' quote without escape characters, as msil-prog.py
splits its command options; the lit substitution passes '%msil_canon' in
single quotes because it holds double-quoted paths.

Exit status: 0 when the listings are equal or the test is skipped; 1 when
they differ or a tool fails; 2 on a usage error (an unknown --allow name, a
malformed or unreadable FILE, a missing A or B).

Python standard library only.
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
ORACLES = ("O3", "O4")
# Skip lines may name O3, O4 and O6: msil-verify.py --md-baseline --equiv-skip
# reads the O6 lines (plan section 12, 'M21.c extended'), while --oracle stays
# O3 or O4.
SKIP_ORACLES = ORACLES + ("O6",)
LEVELS = ("O0", "O1", "O2", "O3", "Os", "Oz")
NAME_RE = re.compile(r"^[a-z0-9][a-z0-9-]*$")
# The word after 'skip': <oracle>[@<triple>][:<level>] (plan section 12,
# 'Plan 5.12.6 amended (skip lines)').
SKIP_KEY_RE = re.compile(
    r"^(?P<oracle>[^@:]+)(?:@(?P<triple>[^@:]+))?(?::(?P<level>[^@:]+))?$"
)
MAX_LINES = 40
TOOL_TIMEOUT = 600


class UsageError(Exception):
    pass


class Failure(Exception):
    pass


class Allowed:
    """The parsed allow-list: entries (name -> [option], in file order) and
    skip lines [(oracle, triple or None, level or None, path, reason, line
    number)]."""

    def __init__(self, path):
        self.path = path
        self.entries = {}
        self.justifications = {}
        self.skips = []
        try:
            with open(path, encoding="utf-8") as f:
                lines = f.read().splitlines()
        except OSError as e:
            raise UsageError("cannot read %s: %s" % (path, e))
        for n, line in enumerate(lines, 1):
            text = line.strip()
            if not text or text.startswith("#"):
                continue
            where = "%s:%d" % (path, n)
            words = text.split()
            if words[0] == "skip":
                key = SKIP_KEY_RE.match(words[1]) if len(words) >= 4 else None
                if not key or key.group("oracle") not in SKIP_ORACLES:
                    raise UsageError(
                        "%s: expected 'skip <oracle>[@<triple>][:<level>] "
                        "<test path> <reason>' with the oracle O3, O4 or O6"
                        % where
                    )
                triple, level = key.group("triple"), key.group("level")
                if triple is not None and not triple.endswith("-msil"):
                    raise UsageError(
                        "%s: the triple %s of the skip line is not an MSIL "
                        "triple (it does not end in -msil)" % (where, triple)
                    )
                if level is not None and level not in LEVELS:
                    raise UsageError(
                        "%s: the level %s of the skip line is not one of %s"
                        % (where, level, ", ".join(LEVELS))
                    )
                reason = text.split(None, 3)[3].strip()
                self.skips.append(
                    (key.group("oracle"), triple, level, words[2], reason, n)
                )
                continue
            body, hash_, why = text.partition("#")
            words = body.split()
            name, options = words[0], words[1:]
            if not NAME_RE.match(name):
                raise UsageError("%s: bad entry name %r" % (where, name))
            if not options or not all(o.startswith("--") for o in options):
                raise UsageError(
                    "%s: entry %s needs one or more dumper options ('--...')"
                    % (where, name)
                )
            if not hash_ or not why.strip():
                raise UsageError(
                    "%s: entry %s has no '# <justification>'" % (where, name)
                )
            if name in self.entries:
                raise UsageError("%s: entry %s is given twice" % (where, name))
            self.entries[name] = options
            self.justifications[name] = why.strip()

    def options(self, names):
        """The dumper options of the entries names, in file order."""
        unknown = [n for n in names if n not in self.entries]
        if unknown:
            raise UsageError(
                "unknown --allow name %s (the entries of %s: %s)"
                % (
                    ", ".join(repr(n) for n in unknown),
                    self.path,
                    ", ".join(self.entries) or "none",
                )
            )
        return [o for n, opts in self.entries.items() if n in names for o in opts]

    def skip_reason(self, oracle, test, triple=None, level=None):
        """The reason of the first skip line for oracle that covers the test
        path test (repository-relative, '/') built for triple at level, or
        None. A line that names a triple or a level covers only that triple
        or level, and nothing when triple or level is None; a line that names
        neither covers every triple and level."""
        key = os.path.normcase(test)
        for o, t, lv, path, reason, _ in self.skips:
            if o != oracle:
                continue
            if t is not None and t != triple:
                continue
            if lv is not None and lv != level:
                continue
            p = os.path.normcase(path)
            if path.endswith("/"):
                if key.startswith(p):
                    return reason
            elif key == p:
                return reason
        return None


def test_path(path):
    """A test path as the skip lines write it: relative to the repository
    root with '/', when it lies inside the repository."""
    full = os.path.abspath(path)
    try:
        inside = os.path.commonpath(
            [os.path.normcase(full), os.path.normcase(REPO)]
        ) == os.path.normcase(REPO)
    except ValueError:
        inside = False
    rel = os.path.relpath(full, REPO) if inside else path
    rel = rel.replace("\\", "/")
    while rel.startswith("./"):
        rel = rel[2:]
    return rel


def split_words(text):
    """Split a command string into words at blanks; "..." and '...' quote
    without escape characters, and a quoted part joins the text next to it
    (msil-prog.py's rule)."""
    words, cur, quote, started = [], [], None, False
    for c in text:
        if quote:
            if c == quote:
                quote = None
            else:
                cur.append(c)
        elif c in "\"'":
            quote, started = c, True
        elif c.isspace():
            if started:
                words.append("".join(cur))
                cur, started = [], False
        else:
            cur.append(c)
            started = True
    if quote:
        raise UsageError("unterminated %s quote in %r" % (quote, text))
    if started:
        words.append("".join(cur))
    return words


def run(cmd, what):
    """Run cmd; its stdout as text. A failure raises Failure with its
    output."""
    try:
        res = subprocess.run(
            cmd,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=TOOL_TIMEOUT,
        )
    except subprocess.TimeoutExpired:
        raise Failure("%s did not finish within %d s" % (what, TOOL_TIMEOUT))
    except OSError as e:
        raise Failure("%s: cannot run %s: %s" % (what, cmd[0], e))
    out = res.stdout.decode("utf-8", errors="replace").replace("\r\n", "\n")
    err = res.stderr.decode("utf-8", errors="replace").replace("\r\n", "\n")
    if res.returncode != 0:
        raise Failure(
            "%s failed (exit status %d):\n%s%s" % (what, res.returncode, err, out)
        )
    return out


def assemble(ilasm, il, private):
    """ilasm -exe of the text il into private/<stem>.exe; its path."""
    out = os.path.join(private, os.path.splitext(os.path.basename(il))[0] + ".exe")
    run(ilasm + ["-exe", "-output=" + out, il], "ilasm of %s" % il)
    if not os.path.isfile(out):
        raise Failure("ilasm of %s wrote no %s" % (il, out))
    return out


def listing(canon, options, image):
    return run(canon + options + [image], "the lister on %s" % image).splitlines()


def compare(a_name, a, b_name, b, label, allowed_names):
    """Print the result; returns the exit status."""
    allowed = ", ".join(allowed_names) or "none"
    if a == b:
        print(
            "msil-equiv: %s: %s and %s are equal (%d lines; allowed: %s)"
            % (label, a_name, b_name, len(a), allowed)
        )
        return 0
    diff = list(difflib.unified_diff(a, b, a_name, b_name, n=0, lineterm=""))
    total = sum(
        1
        for d in diff[2:]
        if d.startswith(("-", "+")) and not d.startswith(("---", "+++"))
    )
    print(
        "msil-equiv: %s: %s and %s differ (%d differing line%s; allowed: %s); "
        "the first %d:"
        % (
            label,
            a_name,
            b_name,
            total,
            "" if total == 1 else "s",
            allowed,
            min(total, MAX_LINES),
        )
    )
    shown = 0
    for d in diff:
        if shown >= MAX_LINES:
            break
        print(d)
        if d.startswith(("-", "+")) and not d.startswith(("---", "+++")):
            shown += 1
    if total > shown:
        print("msil-equiv: ... %d more differing lines" % (total - shown))
    return 1


def parse_args(argv):
    p = argparse.ArgumentParser(
        prog="msil-equiv.py",
        allow_abbrev=False,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=__doc__,
    )
    p.add_argument("--canon", required=True, help="the canonical lister command")
    p.add_argument("--allowed", required=True, help="equiv-allowed.txt")
    p.add_argument(
        "--allow", action="append", default=[], metavar="NAME", help="allow an entry"
    )
    p.add_argument("--oracle", choices=ORACLES, default="O3")
    p.add_argument("--test", help="the test's path (lit's %%s)")
    p.add_argument("--triple", help="the MSIL triple, for qualified skip lines")
    p.add_argument(
        "--level", choices=LEVELS, help="the level, for qualified skip lines"
    )
    p.add_argument("--ilasm", help="assemble '.il' operands with this command")
    p.add_argument("a", metavar="A")
    p.add_argument("b", metavar="B")
    return p.parse_args(argv)


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        allowed = Allowed(args.allowed)
        options = allowed.options(args.allow)
        names = [n for n in allowed.entries if n in args.allow]
        if args.test:
            reason = allowed.skip_reason(
                args.oracle, test_path(args.test), args.triple, args.level
            )
            if reason is not None:
                print("%s skipped: %s" % (args.oracle, reason))
                return 0
        canon = split_words(args.canon)
        if not canon:
            raise UsageError("--canon is empty")
        ilasm = split_words(args.ilasm) if args.ilasm else None
        if args.ilasm is not None and not ilasm:
            raise UsageError("--ilasm is empty")
        for f in (args.a, args.b):
            if not os.path.isfile(f):
                raise UsageError("%s: no such file" % f)
    except UsageError as e:
        sys.stderr.write("msil-equiv: error: %s\n" % e)
        return 2
    private = tempfile.mkdtemp(prefix="msil-equiv-")
    try:
        images = []
        for f in (args.a, args.b):
            if ilasm and f.lower().endswith(".il"):
                images.append(assemble(ilasm, os.path.abspath(f), private))
            else:
                images.append(f)
        a = listing(canon, options, images[0])
        b = listing(canon, options, images[1])
    except Failure as e:
        sys.stderr.write("msil-equiv: error: %s\n" % e)
        return 1
    finally:
        shutil.rmtree(private, ignore_errors=True)
    return compare(args.a, a, args.b, b, args.oracle, names)


if __name__ == "__main__":
    sys.exit(main())

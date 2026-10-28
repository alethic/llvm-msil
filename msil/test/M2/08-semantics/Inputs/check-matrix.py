#!/usr/bin/env python3
# ===-- check-matrix.py - Check the link-semantics matrix list -------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Check msil/test/M2/08-semantics/matrix.txt against the tests it names.

    check-matrix.py REPO         check the matrix of the repository at REPO
    check-matrix.py --self-test  check this script on trees it builds

matrix.txt is the coverage list of the link-semantics matrix
(ImplementationPlan.md 5.9.16 and 5.12.7, condition 5): one line per row of
5.9.16 and per variant of a row, with seven columns separated by '|':

    <case> | <tests> | <native> | <pipelines> | <triples> | <Phase 2> | <milestones>

  case        the 5.9.16 row ('zoo'), or row/variant ('p1-weak-alias/reversed');
              rows that are not 5.9.16's (08-semantics tests of later
              section 12 rows) use their own names
  tests       comma-separated test files, relative to msil/test/M2, or to the
              repository when they start with llvm/, lld/, clang/ or msil/;
              '-' only for an 'untested' line
  native      'transcript', 'link error', 'UB' or 'n/a', optionally followed by
              ': <what>' (the transcript, or why there is none)
  pipelines   comma-separated: asm, obj, lto
  triples     comma-separated: linux-gnu (x86_64-unknown-linux-gnu-msil),
              windows-msvc (x86_64-pc-windows-msvc-msil)
  Phase 2     'transcript' (the native transcript, optionally ': <note>'),
              'diag:<text>' (a named diagnostic; '<...>' in <text> stands for
              a part the test may spell as a pattern), 'untested: <reason>' or
              'object: <what>' (an object-content FileCheck test over
              llvm-cil-dump --link-info, row p7-flags)
  milestones  free text; a line whose milestones name a P3 milestone ('P3',
              'P3-A', 'P3-B') is a P3 row, whose transcript comes later

Lines starting with '#' and blank lines are comments.

The rules (M23.c):
- every listed file exists, and every .test of 08-semantics is listed
  (except matrix-coverage.test, which runs this script);
- a 'UB' or 'link error' native column has a 'diag:' Phase 2 column;
- a 'transcript' line runs the composite %{msil_prog} (every level of the
  leg) on both x86_64 triples, unless the line names one triple and the test
  REQUIRES it or guards the composite with '%if msil-triple-<os>'; the
  composite runs under exactly the line's obj and lto pipelines; asm needs a
  -S build run at -O0 and -O2 (or %{msil_c}); <stem>.expected exists (or
  <stem>.linux.expected for a linux-gnu-only line) unless the test
  redefines %{msil_expect};
- a 'diag:' line, for each listed pipeline, has failing builds ('not' +
  build, %{msil_prog_fail_<L>}) at -O0 and -O2 and a FileCheck of their
  output whose check lines (of its prefixes, -NOT lines aside) contain
  <text>, with --implicit-check-not for 'LLVM ERROR' and 'PLEASE submit'.
  The JIT-time exception names of 5.9.16 (InvalidProgram, MissingMethod,
  TypeLoad, EntryPointNotFound, BadImageFormat) are required only of a
  FileCheck whose input comes from a run: a build that fails never runs the
  program, so they cannot occur in its output;
- every row 5.9.16 assigns to M20 lists obj and lto (over its variants);
- a P3 row is 'diag:' (its current Phase 2 diagnostic, pinned by a test) or
  'untested: <reason>'; no other row is 'untested';
- every row of 5.9.16 has a line;
- pipeline-specific checks of %{msil_prog} tests are guarded by
  '%if msil-prog-pipeline=<p>', never by 'msil-pipeline=<p>' (5.12.1), which
  decides only %{msil_c} and %{msil_ll};
- no test of 08-semantics and no listed test has an XFAIL line.

Exit status: 0 when every rule holds, 1 otherwise (each problem on its own
line), 2 on a usage error.
"""

import os
import re
import shutil
import sys
import tempfile

MATRIX = os.path.join("msil", "test", "M2", "08-semantics", "matrix.txt")
SEMANTICS = os.path.join("msil", "test", "M2", "08-semantics")
M2 = os.path.join("msil", "test", "M2")
COVERAGE_TEST = "matrix-coverage.test"
REPO_DIRS = ("llvm/", "lld/", "clang/", "msil/")
TRIPLES = {"linux-gnu": "linux", "windows-msvc": "windows"}
FULL_TRIPLES = {
    "linux-gnu": "x86_64-unknown-linux-gnu-msil",
    "windows-msvc": "x86_64-pc-windows-msvc-msil",
}
PIPELINES = ("asm", "obj", "lto")
PROG_PIPELINES = ("obj", "lto")
NATIVE_KINDS = ("transcript", "link error", "UB", "n/a")
JIT_NAMES = (
    "InvalidProgram",
    "MissingMethod",
    "TypeLoad",
    "EntryPointNotFound",
    "BadImageFormat",
)
LEVELS = ("O0", "O2")

# The rows of plan 5.9.16, and those whose 'From' column names M20, which
# must list both obj and lto.
ROWS_5916 = (
    "zoo zoo-archive zoo-defined p1-weak-alias p2-knr knr-two-arities "
    "sig-mismatch p3-start-stop p4-selectany p5-tls p6-ctors p6-dtors "
    "p7-flags p8-section-ctor ctor-slot-order weak-def-sig-mismatch "
    "import-attrs p9-weak-common common-largest static-same-name "
    "inline-comdat byval-sret-xtu knr-def-xtu main-not-first data-only-tu "
    "unaligned-reloc int-ptr-adapter missing-arg-adapter type-mismatch "
    "duplicate-strong undefined float-int-mismatch weak-undef-guarded "
    "gc-used-retain pragma-comment-lib native-callback "
    # P3 rows: p5-tls and p6-dtors (above) as transcripts, .fini_array and
    # .CRT$XT, atexit order, a variadic definition across objects.
    "fini-array crt-xt atexit-order variadic-def-xtu "
    # P2-C rows (M22).
    "p2c-bsymbolic p2c-cycle p2c-weak-def"
).split()
M20_ROWS = frozenset(
    (
        "zoo zoo-archive zoo-defined p1-weak-alias p2-knr knr-two-arities "
        "sig-mismatch p3-start-stop p4-selectany p5-tls p6-ctors p6-dtors "
        "p8-section-ctor ctor-slot-order weak-def-sig-mismatch import-attrs "
        "p9-weak-common common-largest static-same-name inline-comdat "
        "byval-sret-xtu knr-def-xtu main-not-first data-only-tu "
        "unaligned-reloc int-ptr-adapter missing-arg-adapter type-mismatch "
        "duplicate-strong undefined float-int-mismatch weak-undef-guarded "
        "gc-used-retain pragma-comment-lib native-callback"
    ).split()
)

CASE_RE = re.compile(r"^[a-z0-9][a-z0-9-]*(/[a-z0-9][a-z0-9-]*)?$")
P3_RE = re.compile(r"\bP3(-[A-Z])?\b")
LEADER = r"^\s*(?:#|//|;)\s*"
DIRECTIVE_RE = re.compile(
    LEADER + r"(RUN|REQUIRES|UNSUPPORTED|XFAIL|DEFINE|REDEFINE):(.*)$"
)
XFAIL_RE = re.compile(LEADER + r"XFAIL:", re.M)
FAIL_BUILD_RE = re.compile(
    r"^not\s+(?:--crash\s+)?(?:%msil_prog_build|%clang\w*|%msil_ld|%llc\w*|llc|clang|msil-ld)\b"
)
LEVEL_RES = (
    re.compile(r"%\{msil_prog_fail_(O\w)\}"),
    re.compile(r"--level\s+(O\w)\b"),
    re.compile(r"(?:^|\s)-(O[0-3sz])\b"),
)
PROG_RE = re.compile(r"%\{msil_prog\}")


class Problems:
    def __init__(self):
        self.items = []

    def add(self, where, text):
        self.items.append("%s: %s" % (where, text))


SUBST_RE = re.compile(r"%\{[A-Za-z_][\w.:-]*\}")


def split_top(text, sep):
    """Split text at sep outside lit %if groups ('%{ ... %}') and quotes; a
    substitution such as %{msil_prog} is no group."""
    parts, depth, quote, cur, i = [], 0, None, [], 0
    while i < len(text):
        if quote:
            cur.append(text[i])
            if text[i] == quote:
                quote = None
            i += 1
            continue
        m = SUBST_RE.match(text, i)
        if m:
            cur.append(m.group(0))
            i = m.end()
            continue
        if text.startswith("%{", i):
            depth += 1
            cur.append("%{")
            i += 2
            continue
        if text.startswith("%}", i):
            depth = max(depth - 1, 0)
            cur.append("%}")
            i += 2
            continue
        if text[i] in "\"'":
            quote = text[i]
            cur.append(text[i])
            i += 1
            continue
        if depth == 0 and text.startswith(sep, i):
            parts.append("".join(cur))
            cur = []
            i += len(sep)
            continue
        cur.append(text[i])
        i += 1
    parts.append("".join(cur))
    return parts


def parse_condition(cond):
    """(pipelines, triples, asm) allowed by a lit %if condition, each None
    when the condition does not restrict it."""
    pipes, triples, asm = None, None, None
    for atom in re.split(r"&&|\|\|", cond):
        atom = atom.strip().strip("()").strip()
        neg = atom.startswith("!")
        atom = atom.lstrip("!").strip()
        m = re.fullmatch(r"msil-prog-pipeline=(\w+)", atom)
        if m:
            want = {m.group(1)}
            got = set(PROG_PIPELINES) - want if neg else want
            pipes = got if pipes is None else pipes & got
            continue
        m = re.fullmatch(r"msil-triple-(linux|windows)", atom)
        if m:
            want = {m.group(1)}
            got = {"linux", "windows"} - want if neg else want
            triples = got if triples is None else triples & got
            continue
        if re.fullmatch(r"msil-pipeline=asm", atom):
            asm = not neg
    return pipes, triples, asm


def commands_of(text):
    """The commands of one RUN line (continuation lines joined, DEFINEs
    expanded): its '&&' parts, each with the guard of a leading
    '%if <condition> %{ ... %}' that surrounds the whole line."""
    text = text.strip()
    guard = (None, None, None)
    m = re.match(r"^%if\s+(.*?)\s*%\{(.*)%\}\s*$", text, re.S)
    if m and "%else" not in m.group(2).split("%}")[-1]:
        guard = parse_condition(m.group(1))
        text = m.group(2).strip()
    return [Command(part, guard) for part in split_top(text, "&&") if part.strip()]


class Command:
    """One command of a RUN line, with the guard of its line."""

    def __init__(self, text, guard):
        self.pipes, self.triples, self.asm = guard
        body = text.strip()
        self.body = body
        stages = split_top(body, "|")
        first = stages[0]
        first_cmd = re.split(r"\s+\d?>>?\s*|\s+2>&1", first)[0].strip()
        self.is_fail = "%{msil_prog_fail_" in first or bool(
            FAIL_BUILD_RE.match(first_cmd)
        )
        self.levels = set()
        if self.is_fail:
            for r in LEVEL_RES:
                self.levels.update(r.findall(first))
        m = re.search(r"\s>\s*(\S+)", first)
        self.writes = m.group(1) if m else None
        self.filecheck = None
        for stage in stages:
            if re.search(r"(?:^|\s)FileCheck(?:\s|$)", stage.strip()):
                self.filecheck = stage.strip()
                break
        self.piped_from_fail = (
            self.filecheck is not None and len(stages) > 1 and self.is_fail
        )
        self.prefixes, self.implicit, self.input_file = [], [], None
        if self.filecheck:
            self.parse_filecheck(self.filecheck)

    def parse_filecheck(self, fc):
        for m in re.finditer(r"--check-prefix(?:es)?=((?:[^ ]|\s(?!--))+)", fc):
            value = re.sub(r"%if\s+[^%]*%\{|%\}|%else\s*%\{|%else", " ", m.group(1))
            for name in re.split(r"[,\s]+", value):
                if re.fullmatch(r"[A-Za-z][A-Za-z0-9_-]*", name):
                    self.prefixes.append(name)
        if not self.prefixes:
            self.prefixes = ["CHECK"]
        for m in re.finditer(
            r"--implicit-check-not(?:=|\s+)(\"[^\"]*\"|'[^']*'|\S+)", fc
        ):
            self.implicit.append(m.group(1).strip("\"'"))
        m = re.search(r"--input-file(?:=|\s+)(\S+)", fc)
        if m:
            self.input_file = m.group(1)

    def runs_under(self, pipeline):
        return self.pipes is None or pipeline in self.pipes


class TestFile:
    def __init__(self, path):
        self.path = path
        self.text = open(path, encoding="utf-8", errors="replace").read()
        self.lines = self.text.splitlines()
        self.requires, self.unsupported, self.xfail = [], [], []
        self.commands = []
        self.redefined = set()
        defines = {}
        pending, kind = None, None
        for line in self.lines:
            m = DIRECTIVE_RE.match(line)
            if not m:
                continue
            k, rest = m.group(1), m.group(2).rstrip()
            if pending is not None:
                if k != kind:
                    pending = None
                else:
                    cont = rest.endswith("\\")
                    pending += " " + rest.rstrip("\\").strip()
                    if not cont:
                        self.finish(kind, pending, defines)
                        pending = None
                    continue
            if k in ("RUN", "DEFINE", "REDEFINE") and rest.endswith("\\"):
                pending, kind = rest.rstrip("\\").strip(), k
                continue
            self.finish(k, rest.strip(), defines)
        if pending is not None:
            self.finish(kind, pending, defines)

    def finish(self, kind, text, defines):
        if kind in ("REQUIRES", "UNSUPPORTED", "XFAIL"):
            feats = [f.strip() for f in text.split(",") if f.strip()]
            {
                "REQUIRES": self.requires,
                "UNSUPPORTED": self.unsupported,
                "XFAIL": self.xfail,
            }[kind].extend(feats)
            return
        if kind in ("DEFINE", "REDEFINE"):
            m = re.match(r"(%\{[\w.:-]+\})\s*=\s*(.*)$", text)
            if m:
                if kind == "DEFINE" or m.group(1) in defines:
                    defines[m.group(1)] = m.group(2)
                self.redefined.add(m.group(1))
            return
        for _ in range(6):
            new = text
            for name, value in defines.items():
                new = new.replace(name, value)
            if new == text:
                break
            text = new
        self.commands.extend(commands_of(text))

    def allowed_pipelines(self):
        allowed = set(PROG_PIPELINES)
        for f in self.requires:
            m = re.fullmatch(r"msil-prog-pipeline=(\w+)", f)
            if m:
                allowed &= {m.group(1)}
        for f in self.unsupported:
            m = re.fullmatch(r"msil-prog-pipeline=(\w+)", f)
            if m:
                allowed -= {m.group(1)}
        return allowed

    def required_triples(self):
        """The OSes the test runs on by REQUIRES/UNSUPPORTED."""
        allowed = {"linux", "windows"}
        for f in self.requires:
            m = re.fullmatch(r"msil-triple-(linux|windows)", f)
            if m:
                allowed &= {m.group(1)}
        for f in self.unsupported:
            m = re.fullmatch(r"msil-triple-(linux|windows)", f)
            if m:
                allowed -= {m.group(1)}
        return allowed

    def check_lines(self, prefix):
        pat = re.compile(
            r"(?:^|[^A-Za-z0-9_-])"
            + re.escape(prefix)
            + r"(-NEXT|-DAG|-SAME|-LABEL|-NOT|-EMPTY)?:\s?(.*)$"
        )
        out = []
        for line in self.lines:
            m = pat.search(line)
            if (
                m
                and m.group(1) not in ("-NOT", "-EMPTY")
                and not DIRECTIVE_RE.match(line)
            ):
                out.append(m.group(2))
        return out


def diag_matches(text, pattern):
    """True when the literal parts of a diag text (split at '<...>') appear
    in order in a FileCheck pattern whose {{...}} and [[...]] parts are
    replaced by a separator."""
    flat = re.sub(r"\{\{.*?\}\}|\[\[.*?\]\]", "\0", pattern)
    pos = 0
    for piece in re.split(r"<[^<>]*>", text):
        if not piece:
            continue
        i = flat.find(piece, pos)
        if i < 0:
            return False
        pos = i + len(piece)
    return True


def resolve(repo, name):
    rel = name if name.startswith(REPO_DIRS) else os.path.join(M2, name)
    return os.path.join(repo, rel.replace("/", os.sep))


def parse_matrix(path, problems):
    rows = []
    for no, raw in enumerate(open(path, encoding="utf-8").read().splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        where = "matrix.txt:%d" % no
        cols = [c.strip() for c in line.split("|")]
        if len(cols) != 7:
            problems.add(where, "%d columns, not 7" % len(cols))
            continue
        case, tests, native, pipes, triples, phase2, ms = cols
        row = {
            "where": where + ": " + case,
            "case": case,
            "base": case.split("/")[0],
            "tests": [t.strip() for t in tests.split(",") if t.strip()],
            "native": native,
            "native_kind": native.split(":")[0].strip(),
            "pipelines": [p.strip() for p in pipes.split(",") if p.strip()],
            "triples": [t.strip() for t in triples.split(",") if t.strip()],
            "phase2": phase2,
            "milestones": ms,
            "p3": bool(P3_RE.search(ms)),
        }
        bad = False
        if not CASE_RE.match(case):
            problems.add(
                row["where"], "case name is not <row> or <row>/<variant> in lower case"
            )
            bad = True
        if row["native_kind"] not in NATIVE_KINDS:
            problems.add(
                row["where"],
                "native column '%s' is none of %s" % (native, ", ".join(NATIVE_KINDS)),
            )
            bad = True
        for p in row["pipelines"]:
            if p not in PIPELINES:
                problems.add(row["where"], "unknown pipeline '%s'" % p)
                bad = True
        for t in row["triples"]:
            if t not in TRIPLES:
                problems.add(row["where"], "unknown triple '%s'" % t)
                bad = True
        if not row["pipelines"] or not row["triples"]:
            problems.add(row["where"], "no pipeline or no triple")
            bad = True
        kind = phase2_kind(phase2)
        if kind is None:
            problems.add(
                row["where"],
                "Phase 2 column '%s' is not transcript, diag:<text>, untested: <reason> or object: <what>"
                % phase2,
            )
            bad = True
        row["kind"] = kind
        if kind == "diag":
            row["diag"] = phase2[len("diag:") :].strip()
            if not row["diag"]:
                problems.add(row["where"], "empty diag text")
                bad = True
        if kind == "untested" and not phase2.split(":", 1)[1].strip():
            problems.add(row["where"], "untested without a reason")
            bad = True
        if row["tests"] == ["-"]:
            row["tests"] = []
        if not row["tests"] and kind != "untested":
            problems.add(row["where"], "no test")
            bad = True
        if not bad:
            rows.append(row)
    return rows


def phase2_kind(text):
    if re.fullmatch(r"transcript(\s*:.*)?", text):
        return "transcript"
    if text.startswith("diag:"):
        return "diag"
    if re.match(r"untested\s*:", text):
        return "untested"
    if re.match(r"object\s*:", text):
        return "object"
    return None


def check_transcript(row, test, tpath, problems):
    where = "%s: %s" % (row["where"], os.path.basename(tpath))
    oses = {TRIPLES[t] for t in row["triples"]}
    comps = [c for c in test.commands if PROG_RE.search(c.body)]
    if not comps:
        problems.add(
            where,
            "a transcript line whose test never runs %{msil_prog} (every level of the leg)",
        )
        return
    req = test.required_triples()
    for c in comps:
        runs_on = req & (c.triples if c.triples is not None else {"linux", "windows"})
        if runs_on != oses:
            problems.add(
                where,
                "%%{msil_prog} runs on %s, the line names %s (a one-triple line needs REQUIRES: "
                "msil-triple-<os> or '%%if msil-triple-<os>')"
                % ("/".join(sorted(runs_on)) or "no triple", "/".join(sorted(oses))),
            )
        runs_in = test.allowed_pipelines() & (
            c.pipes if c.pipes is not None else set(PROG_PIPELINES)
        )
        want = set(row["pipelines"]) & set(PROG_PIPELINES)
        if runs_in != want:
            problems.add(
                where,
                "%%{msil_prog} runs under %s, the line lists %s"
                % (
                    "/".join(sorted(runs_in)) or "no pipeline",
                    "/".join(sorted(want)) or "none",
                ),
            )
    if "asm" in row["pipelines"]:
        asm_ok = any("%{msil_c}" in c.body for c in test.commands)
        if not asm_ok:
            s_levels = set()
            for c in test.commands:
                if "%clang_msil" in c.body and re.search(r"\s-S(\s|$)", c.body):
                    s_levels.update(re.findall(r"\s-(O[0-3sz])\b", c.body))
            runs = any("%msil_exec" in c.body for c in test.commands)
            asm_ok = runs and set(LEVELS) <= s_levels
        if not asm_ok:
            problems.add(
                where,
                "asm is listed, but no -S build runs at -O0 and -O2 (or %{msil_c})",
            )
    if "%{msil_expect}" not in test.redefined:
        stem = os.path.splitext(tpath)[0]
        names = [stem + ".expected"]
        if oses == {"linux"}:
            names.append(stem + ".linux.expected")
        if not any(os.path.isfile(n) for n in names):
            problems.add(
                where,
                "no %s (update-expected.py writes it)" % os.path.basename(names[0]),
            )


def input_from_fail(cmd, test):
    if cmd.piped_from_fail:
        return True
    if cmd.input_file:
        return any(c.is_fail and c.writes == cmd.input_file for c in test.commands)
    return False


def check_diag(row, test, tpath, problems):
    where = "%s: %s" % (row["where"], os.path.basename(tpath))
    oses = {TRIPLES[t] for t in row["triples"]}
    req = test.required_triples()
    if not oses <= req:
        problems.add(
            where, "the test does not run on %s" % "/".join(sorted(oses - req))
        )
    for p in row["pipelines"]:
        if p == "asm":
            if not any(c.asm and c.filecheck for c in test.commands):
                problems.add(
                    where,
                    "asm is listed, but no FileCheck runs under '%if msil-pipeline=asm'",
                )
            continue
        if p not in test.allowed_pipelines():
            problems.add(where, "the test does not run under %s" % p)
            continue

        def on_rows_triples(c):
            got = req & (c.triples if c.triples is not None else {"linux", "windows"})
            return oses <= got

        fails = [
            c
            for c in test.commands
            if c.is_fail and c.runs_under(p) and on_rows_triples(c)
        ]
        levels = set()
        for c in fails:
            levels |= c.levels
        missing = [lv for lv in LEVELS if lv not in levels]
        if missing:
            problems.add(
                where, "%s: no failing build at -%s" % (p, " and -".join(missing))
            )
        found = False
        hints = []
        for c in test.commands:
            if not (c.filecheck and c.runs_under(p) and on_rows_triples(c)):
                continue
            if not input_from_fail(c, test):
                continue
            if not any(
                diag_matches(row["diag"], pat)
                for pref in c.prefixes
                for pat in test.check_lines(pref)
            ):
                continue
            need = ["LLVM ERROR", "PLEASE submit"]
            lacking = [n for n in need if not any(n in i for i in c.implicit)]
            if lacking:
                hints.append(
                    "its FileCheck lacks --implicit-check-not for %s"
                    % " and ".join("'%s'" % n for n in lacking)
                )
                continue
            found = True
            break
        if not found:
            problems.add(
                where,
                "%s: no FileCheck of a failing build checks '%s'%s"
                % (
                    p,
                    row["diag"],
                    ("; " + "; ".join(sorted(set(hints)))) if hints else "",
                ),
            )
    # A FileCheck whose input is a run must exclude the JIT-time exceptions.
    for c in test.commands:
        if (
            c.filecheck
            and not input_from_fail(c, test)
            and re.search(r"%msil_exec|%dotnet", c.body)
        ):
            lacking = [n for n in JIT_NAMES if not any(n in i for i in c.implicit)]
            if lacking:
                problems.add(
                    where,
                    "a FileCheck of a run lacks --implicit-check-not for %s"
                    % ", ".join(lacking),
                )


def check_object(row, test, tpath, problems):
    where = "%s: %s" % (row["where"], os.path.basename(tpath))
    if row["native_kind"] != "n/a":
        problems.add(where, "an object line has native 'n/a'")
    if "--link-info" not in test.text or "FileCheck" not in test.text:
        problems.add(
            where,
            "an object line needs a FileCheck test over llvm-cil-dump --link-info",
        )
    for t in row["triples"]:
        if FULL_TRIPLES[t] not in test.text:
            problems.add(where, "the test never uses %s" % FULL_TRIPLES[t])


def check_guards(test, tpath, problems):
    if not re.search(r"%\{msil_prog|%msil_prog_build", test.text):
        return
    for line in test.lines:
        m = DIRECTIVE_RE.match(line)
        if not m or m.group(1) not in ("RUN", "REQUIRES", "UNSUPPORTED"):
            continue
        if "msil-pipeline=" in m.group(2) and not re.search(
            r"%\{msil_(c|ll)(_O\w)?\}", m.group(2)
        ):
            problems.add(
                os.path.basename(tpath),
                "a %{msil_prog} check guarded by msil-pipeline=<p>; use msil-prog-pipeline=<p> (5.12.1)",
            )


def check(repo, rows_5916=ROWS_5916, m20_rows=M20_ROWS):
    """Every problem of the matrix of repo, as a list of strings."""
    problems = Problems()
    mpath = os.path.join(repo, MATRIX)
    if not os.path.isfile(mpath):
        problems.add("matrix.txt", "missing: %s" % mpath)
        return problems.items, 0, 0
    rows = parse_matrix(mpath, problems)
    tests = {}
    listed = set()
    for row in rows:
        if row["kind"] == "untested" and not row["p3"]:
            problems.add(row["where"], "only a P3 row may be untested")
        if row["native_kind"] in ("UB", "link error") and row["kind"] != "diag":
            problems.add(
                row["where"],
                "native '%s' needs a diag: Phase 2 column" % row["native_kind"],
            )
        if row["p3"] and row["kind"] not in ("diag", "untested"):
            problems.add(
                row["where"],
                "a P3 row pins its Phase 2 diagnostic (diag:) or is untested: <reason>",
            )
        for name in row["tests"]:
            path = resolve(repo, name)
            if not os.path.isfile(path):
                problems.add(row["where"], "%s does not exist" % name)
                continue
            listed.add(os.path.normcase(os.path.abspath(path)))
            if path not in tests:
                tests[path] = TestFile(path)
            test = tests[path]
            if row["kind"] == "transcript":
                check_transcript(row, test, path, problems)
            elif row["kind"] == "diag":
                check_diag(row, test, path, problems)
            elif row["kind"] == "object":
                check_object(row, test, path, problems)
    seen = {}
    for row in rows:
        for p in row["pipelines"]:
            for t in row["triples"]:
                key = (row["case"], p, t)
                if key in seen:
                    problems.add(
                        row["where"], "%s on %s is also on %s" % (p, t, seen[key])
                    )
                seen[key] = row["where"]
    bases = {}
    for row in rows:
        bases.setdefault(row["base"], set()).update(row["pipelines"])
    for name in rows_5916:
        if name not in bases:
            problems.add("matrix.txt", "no line for the 5.9.16 row %s" % name)
        elif name in m20_rows and not set(PROG_PIPELINES) <= bases[name]:
            problems.add(
                "matrix.txt", "the M20 row %s does not list both obj and lto" % name
            )
    sem = os.path.join(repo, SEMANTICS)
    for name in sorted(os.listdir(sem)) if os.path.isdir(sem) else []:
        path = os.path.join(sem, name)
        if not name.endswith(".test") or not os.path.isfile(path):
            continue
        if XFAIL_RE.search(open(path, encoding="utf-8", errors="replace").read()):
            problems.add(name, "has an XFAIL line")
        if name == COVERAGE_TEST:
            continue
        if os.path.normcase(os.path.abspath(path)) not in listed:
            problems.add(name, "is not listed in matrix.txt")
    sem_key = os.path.normcase(os.path.abspath(sem)) + os.sep
    for path, test in sorted(tests.items()):
        if test.xfail and not os.path.normcase(os.path.abspath(path)).startswith(
            sem_key
        ):
            problems.add(os.path.basename(path), "has an XFAIL line")
        check_guards(test, path, problems)
    return problems.items, len(rows), len(tests)


# --- self-test -------------------------------------------------------------

FC = '--implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"'
GOOD_TESTS = {
    "a.test": "# RUN: rm -rf %t && split-file %s %t\n# REDEFINE: %{msil_srcs} = %t/a.c\n# RUN: %{msil_prog}\n",
    "a.expected": "[exit 0]\n",
    "d.test": "".join(
        "# RUN: %%if msil-prog-pipeline=%s %%{ %%{msil_prog_fail_%s} 2>&1 | FileCheck %%s --check-prefix=%s %s %%}\n"
        % (p, lv, p.upper(), FC)
        for p in PROG_PIPELINES
        for lv in LEVELS
    )
    + "# OBJ: {{^}}msil-ld: error: undefined symbol: f{{$}}\n# LTO: {{^}}msil-ld: error: {{.*}}undefined symbol: f{{$}}\n",
    "l.test": "# REQUIRES: msil-triple-linux\n# RUN: %{msil_prog}\n",
    "l.linux.expected": "[exit 0]\n",
}
GOOD_MATRIX = """# a test matrix
ra | 08-semantics/a.test | transcript: 0 | obj,lto | linux-gnu,windows-msvc | transcript | M20
rd | 08-semantics/d.test | link error | obj,lto | linux-gnu,windows-msvc | diag:undefined symbol: <s> | M20
rl | 08-semantics/l.test | transcript | obj,lto | linux-gnu | transcript | M20
rp | 08-semantics/d.test | transcript: later | obj,lto | linux-gnu,windows-msvc | diag:undefined symbol: f | P3-B
ro | llvm/test/o.ll | n/a | obj | linux-gnu | object: flags | M19
ru | - | transcript | obj | linux-gnu | untested: needs a tool | P3
"""
OBJECT_TEST = "; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj %s -o %t.o\n; RUN: llvm-cil-dump --link-info %t.o | FileCheck %s\n"
SELF_ROWS = ("ra", "rd", "rl", "rp", "ro", "ru")
SELF_M20 = frozenset(("ra", "rd", "rl"))

# (what, files to change {name: text or None to delete}, matrix edit
# (old, new) or None, the text one problem must contain)
SELF_CASES = [
    (
        "a missing file",
        {},
        ("08-semantics/l.test |", "08-semantics/nope.test |"),
        "nope.test does not exist",
    ),
    (
        "an unlisted test",
        {"x.test": "# RUN: %{msil_prog}\n"},
        None,
        "x.test: is not listed",
    ),
    (
        "UB with a transcript",
        {},
        ("ra | 08-semantics/a.test | transcript: 0", "ra | 08-semantics/a.test | UB"),
        "needs a diag: Phase 2 column",
    ),
    (
        "a composite under one pipeline",
        {"a.test": "# RUN: %if msil-prog-pipeline=obj %{ %{msil_prog} %}\n"},
        None,
        "%{msil_prog} runs under obj, the line lists lto/obj",
    ),
    (
        "no PLEASE submit",
        {
            "d.test": GOOD_TESTS["d.test"].replace(
                ' --implicit-check-not="PLEASE submit"', ""
            )
        },
        None,
        "lacks --implicit-check-not for 'PLEASE submit'",
    ),
    (
        "a diag text the checks lack",
        {},
        ("diag:undefined symbol: <s>", "diag:duplicate symbol: <s>"),
        "checks 'duplicate symbol: <s>'",
    ),
    (
        "a diag at -O0 only",
        {"d.test": GOOD_TESTS["d.test"].replace("fail_O2", "fail_O0")},
        None,
        "no failing build at -O2",
    ),
    (
        "an M20 row on obj only",
        {},
        (
            "ra | 08-semantics/a.test | transcript: 0 | obj,lto",
            "ra | 08-semantics/a.test | transcript: 0 | obj",
        ),
        "does not list both obj and lto",
    ),
    (
        "a P3 transcript",
        {},
        ("| linux-gnu | transcript | M20", "| linux-gnu | transcript | P3-A"),
        "a P3 row pins its Phase 2 diagnostic",
    ),
    (
        "untested outside P3",
        {},
        ("untested: needs a tool | P3", "untested: needs a tool | M20"),
        "only a P3 row may be untested",
    ),
    (
        "an XFAIL line",
        {"a.test": GOOD_TESTS["a.test"] + "# XFAIL: *\n"},
        None,
        "a.test: has an XFAIL line",
    ),
    (
        "msil-pipeline guarding a composite check",
        {
            "a.test": GOOD_TESTS["a.test"]
            + "# RUN: %if msil-pipeline=obj %{ %msil_prog_build --level O0 --src %t/a.c 2>&1 | FileCheck %s %}\n"
        },
        None,
        "guarded by msil-pipeline=<p>",
    ),
    (
        "a missing 5.9.16 row",
        {},
        ("ru | - |", "rv | - |"),
        "no line for the 5.9.16 row ru",
    ),
    (
        "a one-triple line without REQUIRES",
        {"l.test": "# RUN: %{msil_prog}\n"},
        None,
        "%{msil_prog} runs on linux/windows, the line names linux",
    ),
    ("a missing .expected", {"a.expected": None}, None, "no a.expected"),
    (
        "a FileCheck of a run without the JIT names",
        {"a.test": GOOD_TESTS["a.test"] + "# RUN: %msil_exec %t.exe | FileCheck %s\n"},
        (
            "rd | 08-semantics/d.test | link error",
            "rd | 08-semantics/a.test,08-semantics/d.test | link error",
        ),
        "a FileCheck of a run lacks --implicit-check-not for InvalidProgram",
    ),
]


def make_tree(root, files, matrix):
    sem = os.path.join(root, SEMANTICS)
    os.makedirs(sem)
    os.makedirs(os.path.join(root, "llvm", "test"))
    with open(os.path.join(root, "llvm", "test", "o.ll"), "w", encoding="utf-8") as f:
        f.write(OBJECT_TEST)
    for name, text in files.items():
        if text is not None:
            with open(os.path.join(sem, name), "w", encoding="utf-8") as f:
                f.write(text)
    with open(os.path.join(sem, "matrix.txt"), "w", encoding="utf-8") as f:
        f.write(matrix)


def self_test():
    failures = 0
    work = tempfile.mkdtemp(prefix="check-matrix-")
    try:
        root = os.path.join(work, "good")
        make_tree(root, dict(GOOD_TESTS), GOOD_MATRIX)
        got, _, _ = check(root, SELF_ROWS, SELF_M20)
        if got:
            print(
                "self-test: the good tree: unexpected problems:\n  " + "\n  ".join(got)
            )
            failures += 1
        else:
            print("self-test: the good tree: OK")
        for i, (what, change, edit, want) in enumerate(SELF_CASES):
            files = dict(GOOD_TESTS)
            files.update(change)
            matrix = GOOD_MATRIX
            if edit:
                if edit[0] not in matrix:
                    print("self-test: %s: the matrix edit does not apply" % what)
                    failures += 1
                    continue
                matrix = matrix.replace(edit[0], edit[1], 1)
            root = os.path.join(work, "case%d" % i)
            make_tree(root, files, matrix)
            got, _, _ = check(root, SELF_ROWS, SELF_M20)
            if any(want in p for p in got):
                print("self-test: %s: reported" % what)
            else:
                print(
                    "self-test: %s: not reported (want '%s'); got:\n  %s"
                    % (what, want, "\n  ".join(got) or "nothing")
                )
                failures += 1
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print(
        "self-test: %d of %d checks passed"
        % (len(SELF_CASES) + 1 - failures, len(SELF_CASES) + 1)
    )
    return 1 if failures else 0


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    if len(argv) != 1 or argv[0].startswith("-"):
        sys.stderr.write("usage: check-matrix.py REPO | --self-test\n")
        return 2
    repo = argv[0]
    if not os.path.isdir(repo):
        sys.stderr.write("check-matrix.py: %s is not a directory\n" % repo)
        return 2
    problems, nrows, ntests = check(repo)
    for p in problems:
        print("check-matrix: error: " + p)
    if problems:
        print("check-matrix: %d problems" % len(problems))
        return 1
    print("check-matrix: %d lines, %d tests: OK" % (nrows, ntests))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

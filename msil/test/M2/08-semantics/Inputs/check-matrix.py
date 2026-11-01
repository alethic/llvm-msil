#!/usr/bin/env python3
# ===-- check-matrix.py - Check the link-semantics matrix list -------------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Check msil/test/M2/08-semantics/matrix.txt against the tests it names.

    check-matrix.py [--legs FILE]... REPO  check the matrix of the repository
                                           at REPO (and its legs)
    check-matrix.py --p3-tests REPO        print the Phase 3 feature tests
                                           outside msil/test/M3
    check-matrix.py --self-test            check this script on trees it builds

matrix.txt is the coverage list of the link-semantics matrix
(ImplementationPlan.md 5.9.16, 5.12.7 condition 5 and 5.17.12): one line per
row of 5.9.16 and per variant of a row, with seven columns separated by '|':

    <case> | <tests> | <native> | <pipelines> | <triples> | <Phase 2> | <milestones>

  case        the 5.9.16 row ('zoo'), or row/variant ('p1-weak-alias/reversed');
              rows that are not 5.9.16's (08-semantics tests of later
              section 12 rows) use their own names
  tests       comma-separated test files, relative to msil/test/M2, or to the
              repository when they start with llvm/, lld/, clang/ or msil/;
              '-' only for an 'untested' line
  native      'transcript', 'link error', 'UB' or 'n/a', optionally followed by
              ': <what>' (the transcript, or why there is none), then any
              number of clauses '; rule <triple>: <why>': on that triple the
              test's expected file is hand-written (plan 5.17.4)
  pipelines   comma-separated: asm, obj, lto
  triples     comma-separated: linux-gnu (x86_64-unknown-linux-gnu-msil),
              windows-msvc (x86_64-pc-windows-msvc-msil)
  Phase 2     'transcript' (the native transcript, optionally ': <note>'),
              'diag:<text>' (a named diagnostic; '<...>' in <text> stands for
              a part the test may spell as a pattern), 'untested: <reason>' or
              'object: <what>' (an object-content FileCheck test over
              llvm-cil-dump --link-info, row p7-flags)
  milestones  free text. A line whose milestones name 'P3', 'P3-A' or 'P3-B'
              is a P3 row: its feature comes later and its test pins today's
              diagnostic. The milestone that builds the feature writes its own
              id there in place of the label (M26, M28, M29 or M31) and makes
              the Phase 2 column 'transcript'; a line that a Phase 3 milestone
              adds names that milestone. A line that names a Phase 3 milestone
              (a token M24 to M35) is covered by the gate rule and --p3-tests.

Header lines, each repeatable (plan 5.17.12 item 3; they are not comments):

    #! rows: <name> ...       rows that need a line: 5.9.16's rows and the
                              rows Phase 3 adds
    #! obj-lto: <name> ...    rows whose lines must list obj and lto
    #! p3-tests: <path> ...   Phase 3 feature tests outside msil/test/M3 that
                              no line names, as repository-relative paths

Other lines starting with '#', and blank lines, are comments.

The rules:
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
- a 'diag:' line, for each listed pipeline and for each of -O0 and -O2, has
  a failing build ('not' + build, %{msil_prog_fail_<L>}) at that level and a
  FileCheck of that build's output (piped from it, or reading the file it
  writes through --input-file or '<') whose check lines (of its prefixes,
  -NOT lines aside) contain <text>, with --implicit-check-not for
  'LLVM ERROR' and 'PLEASE submit'. The JIT-time exception names of 5.9.16
  (InvalidProgram, MissingMethod, TypeLoad, EntryPointNotFound,
  BadImageFormat) are required only of a FileCheck whose input comes from a
  run: a build that fails never runs the program, so they cannot occur in
  its output;
- a '; rule <triple>: <why>' clause needs native kind 'transcript' and a
  'transcript' Phase 2 column and names a triple of the line. For each
  triple t of a line, each of its tests redefines %{msil_expected_rule} as
  '<os>[,<os>]: <reason>' (<os>: windows or linux; no single quote in the
  reason) naming t's OS exactly when the line has a clause for t, and for
  each clause the hand-written <stem>.<os>.expected or <stem>.expected
  exists. A test of 08-semantics that redefines %{msil_expected_rule} is on
  a line with a clause;
- every '#! rows:' name has a line (its case, or the <row> of
  <row>/<variant>); every '#! obj-lto:' name is a '#! rows:' name whose lines
  list obj and lto (over its variants); matrix.txt has both headers;
- each '#! p3-tests:' path exists, lies under msil/test but not under
  msil/test/M3 or msil/test/harness, is no line's test and is listed once;
- a P3 row is 'diag:' (its current Phase 2 diagnostic, pinned by a test) or
  'untested: <reason>'; no other row is 'untested'; no line names both a P3
  label and a Phase 3 milestone;
- no two lines have the same case on the same pipeline and triple;
- pipeline-specific checks of %{msil_prog} tests are guarded by
  '%if msil-prog-pipeline=<p>', never by 'msil-pipeline=<p>' (5.12.1), which
  decides only %{msil_c} and %{msil_ll};
- no test of 08-semantics and no listed test has an XFAIL line.

With --legs FILE (repeatable: the JSON of lit-legs.ps1 -Describe or
lit-legs.sh --describe, plan 5.17.6; the legs of all files count together)
two more rules hold (5.17.12 item 1). A run 'runs' a test when (a) one of its
paths is the test or a directory above it, and (b) no --filter-out of its
lit arguments matches the test's full lit name, and a --filter, if any, does
(lit searches case-insensitively; the last of each option counts). The full
lit name is 'MSIL :: <path below msil/test>', 'LLVM :: <path below
llvm/test>', 'lld :: <path below lld/test>' or 'Clang :: <path below
clang/test>'.
- The legs rule: for every line, test, pipeline p and triple t of the line,
  some leg tagged '', 'Wsl' or 'Exit' has a run that runs the test, (c)
  resolves to p (its prog_pipeline for obj and lto, its pipeline for asm)
  and (d) uses t (its triple). A test outside msil/test reads neither
  msil_pipeline nor msil_triple, which only msil/test's lit configuration
  reads, and names its triples itself, so (c) and (d) hold for every run of
  it. A test that builds its asm pipeline itself, with unguarded
  '%clang_msil ... -S' commands at -O0 and -O2 and a %msil_exec run
  (p8-section-ctor), does so in every run, so (c) holds for asm in every run
  of it; a test that uses %{msil_c} or guards with '%if msil-pipeline=asm'
  needs a run whose pipeline is asm. The problem is '<case>: no phase-gate
  leg runs <test> under <p> for <t>'.
- The gate rule, when a file's script is lit-legs.ps1: for every test under
  msil/test of a line that names a Phase 3 milestone and each triple t of
  the line, and for every '#! p3-tests:' path and both triples, some leg
  tagged 'Quick' of such a file has a run that runs the test, uses t, whose
  also_run holds 10+tc0 and whose params set msil_native_oracle=required
  and, for windows-msvc, msil_run_netfx=1. The problem is '<case>: no gate
  leg runs <test> with 10+tc0 for <t>' (the path stands for <case> for a
  '#! p3-tests:' entry).
<test> is the repository-relative path and <t> linux-gnu or windows-msvc.
A run lacking a derived field takes 5.17.6's default (pipeline: msil_pipeline,
else obj; prog_pipeline: lto under lto, else obj; triple: msil_triple, else
the script's default; also_run: msil_also_run); a field that contradicts its
parameter is a problem of the legs file.

--p3-tests REPO prints, one per line, the repository-relative path of each
test under msil/test but outside msil/test/M3 of a line that names a Phase 3
milestone, in matrix order, then the '#! p3-tests:' paths, without
duplicates: lit-legs.ps1's $p3Tests. It runs none of the other checks; it
exits 0, or 1 with its messages on stderr when matrix.txt cannot be parsed or
a path it would print does not exist.

Exit status: 0 when every rule holds, 1 otherwise (each problem on its own
line), 2 on a usage error.
"""

import contextlib
import copy
import io
import json
import os
import posixpath
import re
import shutil
import sys
import tempfile

MATRIX = os.path.join("msil", "test", "M2", "08-semantics", "matrix.txt")
SEMANTICS = os.path.join("msil", "test", "M2", "08-semantics")
M2 = os.path.join("msil", "test", "M2")
M2_REL = "msil/test/M2"
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

# The test suites of a repository-relative path: (directory, lit name).
SUITES = (
    ("msil/test", "MSIL"),
    ("llvm/test", "LLVM"),
    ("lld/test", "lld"),
    ("clang/test", "Clang"),
)
# The legs (plan 5.17.6): the tags of the phase-gate legs, the gate's tag and
# the only script that has it, and each script's default triple.
PHASE_TAGS = ("", "Wsl", "Exit")
GATE_TAG = "Quick"
GATE_SCRIPT = "lit-legs.ps1"
GATE_ALSO_RUN = "10+tc0"
DEFAULT_TRIPLES = {
    "lit-legs.ps1": FULL_TRIPLES["windows-msvc"],
    "lit-legs.sh": FULL_TRIPLES["linux-gnu"],
}

CASE_RE = re.compile(r"^[a-z0-9][a-z0-9-]*(/[a-z0-9][a-z0-9-]*)?$")
ROW_NAME_RE = re.compile(r"^[a-z0-9][a-z0-9-]*$")
P3_RE = re.compile(r"\bP3(-[A-Z])?\b")
PHASE3_RE = re.compile(r"\bM(?:2[4-9]|3[0-5])\b")
HEADER_RE = re.compile(r"^#!\s*([A-Za-z0-9-]+)\s*:(.*)$")
HEADER_KEYS = ("rows", "obj-lto", "p3-tests")
RULE_CLAUSE_SPLIT = re.compile(r";\s*rule\s+")
RULE_CLAUSE_RE = re.compile(r"^(\S+?)\s*:\s*(\S.*?)\s*$")
EXPECTED_RULE = "%{msil_expected_rule}"
EXPECTED_RULE_RE = re.compile(
    r"^\s*((?:windows|linux)(?:\s*,\s*(?:windows|linux))*)\s*:\s*(\S.*?)\s*$"
)
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
        m = re.search(r"--input-file(?:=|\s+)(\S+)", fc) or re.search(
            r"(?:^|\s)<\s*(\S+)", fc
        )
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
        # The last value each DEFINE or REDEFINE gives a substitution.
        self.values = {}
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
                self.values[m.group(1)] = m.group(2)
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

    def expected_rule(self):
        """(OSes, problem) of the test's %{msil_expected_rule}: the OSes it
        names (empty when the test does not redefine it, or redefines it
        empty) and the problem of a malformed value, or None."""
        value = self.values.get(EXPECTED_RULE)
        if value is None or not value.strip():
            return set(), None
        m = EXPECTED_RULE_RE.match(value)
        if not m:
            return set(), (
                "REDEFINE: %s = %s is not '<os>[,<os>]: <reason>' (<os>: windows "
                "or linux)" % (EXPECTED_RULE, value.strip())
            )
        oses = {o.strip() for o in m.group(1).split(",")}
        if "'" in m.group(2):
            return oses, "the reason of %s holds a single quote" % EXPECTED_RULE
        return oses, None


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


def repo_rel(name):
    """The repository-relative path of a test of the tests column."""
    rel = name if name.startswith(REPO_DIRS) else M2_REL + "/" + name
    return posixpath.normpath(rel)


def resolve(repo, name):
    return os.path.join(repo, repo_rel(name).replace("/", os.sep))


def under(rel, directory):
    return rel.startswith(directory + "/")


def lit_name(rel):
    """The full lit name of a repository-relative test path, or None."""
    for directory, suite in SUITES:
        if under(rel, directory):
            return "%s :: %s" % (suite, rel[len(directory) + 1 :])
    return None


def good_rel_path(path):
    if "\\" in path or path.startswith("/") or re.match(r"^[A-Za-z]:", path):
        return False
    return all(part not in ("", ".", "..") for part in path.split("/"))


def parse_matrix(path, problems):
    """(rows, headers) of matrix.txt; headers maps each header key to its
    (value, where) pairs in file order."""
    rows = []
    headers = {key: [] for key in HEADER_KEYS}
    seen = {key: set() for key in HEADER_KEYS}
    for no, raw in enumerate(open(path, encoding="utf-8").read().splitlines(), 1):
        line = raw.strip()
        where = "matrix.txt:%d" % no
        if line.startswith("#!"):
            m = HEADER_RE.match(line)
            if not m or m.group(1) not in HEADER_KEYS:
                problems.add(
                    where,
                    "'%s' is not a header line ('#! rows:', '#! obj-lto:' or "
                    "'#! p3-tests:', then names)" % line,
                )
                continue
            key, values = m.group(1), m.group(2).split()
            if not values:
                problems.add(where, "'#! %s:' names nothing" % key)
            for value in values:
                if key == "p3-tests":
                    if not good_rel_path(value):
                        problems.add(
                            where,
                            "#! p3-tests: '%s' is not a repository-relative path "
                            "(forward slashes, no '.' or '..' parts)" % value,
                        )
                        continue
                elif not ROW_NAME_RE.match(value):
                    problems.add(where, "#! %s: '%s' is not a row name" % (key, value))
                    continue
                if value in seen[key]:
                    problems.add(where, "#! %s: %s is listed twice" % (key, value))
                    continue
                seen[key].add(value)
                headers[key].append((value, where))
            continue
        if not line or line.startswith("#"):
            continue
        cols = [c.strip() for c in line.split("|")]
        if len(cols) != 7:
            problems.add(where, "%d columns, not 7" % len(cols))
            continue
        case, tests, native, pipes, triples, phase2, ms = cols
        clauses = RULE_CLAUSE_SPLIT.split(native)
        row = {
            "where": where + ": " + case,
            "case": case,
            "base": case.split("/")[0],
            "tests": [t.strip() for t in tests.split(",") if t.strip()],
            "native": native,
            "native_kind": clauses[0].split(":")[0].strip(),
            "rules": {},
            "pipelines": [p.strip() for p in pipes.split(",") if p.strip()],
            "triples": [t.strip() for t in triples.split(",") if t.strip()],
            "phase2": phase2,
            "milestones": ms,
            "p3": bool(P3_RE.search(ms)),
            "phase3": bool(PHASE3_RE.search(ms)),
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
        for clause in clauses[1:]:
            m = RULE_CLAUSE_RE.match(clause)
            if not m or m.group(1) not in TRIPLES:
                problems.add(
                    row["where"],
                    "'; rule %s' is not '; rule <triple>: <why>' (<triple>: %s)"
                    % (clause.strip(), " or ".join(TRIPLES)),
                )
                bad = True
            elif m.group(1) in row["rules"]:
                problems.add(row["where"], "two '; rule %s:' clauses" % m.group(1))
                bad = True
            else:
                row["rules"][m.group(1)] = m.group(2)
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
    return rows, headers


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


def fail_input_levels(cmd, test, pipeline, on_triples):
    """The levels of the failing builds whose output the FileCheck cmd reads,
    counting only builds that run under pipeline on the line's triples."""
    if cmd.piped_from_fail:
        return set(cmd.levels)
    levels = set()
    if cmd.input_file:
        for c in test.commands:
            if (
                c.is_fail
                and c.writes == cmd.input_file
                and c.runs_under(pipeline)
                and on_triples(c)
            ):
                levels |= c.levels
    return levels


def check_diag(row, test, tpath, problems):
    where = "%s: %s" % (row["where"], os.path.basename(tpath))
    oses = {TRIPLES[t] for t in row["triples"]}
    req = test.required_triples()
    if not oses <= req:
        problems.add(
            where, "the test does not run on %s" % "/".join(sorted(oses - req))
        )

    def on_rows_triples(c):
        got = req & (c.triples if c.triples is not None else {"linux", "windows"})
        return oses <= got

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
        built = set()
        for c in test.commands:
            if c.is_fail and c.runs_under(p) and on_rows_triples(c):
                built |= c.levels
        missing = [lv for lv in LEVELS if lv not in built]
        if missing:
            problems.add(
                where, "%s: no failing build at -%s" % (p, " and -".join(missing))
            )
        checked = set()
        hints = []
        for c in test.commands:
            if not (c.filecheck and c.runs_under(p) and on_rows_triples(c)):
                continue
            levels = fail_input_levels(c, test, p, on_rows_triples) & set(LEVELS)
            if not levels:
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
            checked |= levels
        unchecked = [lv for lv in LEVELS if lv not in missing and lv not in checked]
        if unchecked:
            problems.add(
                where,
                "%s: no FileCheck of a failing build at -%s checks '%s'%s"
                % (
                    p,
                    " and -".join(unchecked),
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


def expected_stem(test, tpath):
    """The --expect stem of a test (%{msil_expect}), or None when its
    redefinition is not '%S/<name>'."""
    if "%{msil_expect}" not in test.redefined:
        return os.path.splitext(tpath)[0]
    value = test.values.get("%{msil_expect}", "").strip()
    if value.startswith("%S/") and "%" not in value[3:]:
        return os.path.join(os.path.dirname(tpath), value[3:].replace("/", os.sep))
    return None


def check_rules(row, test, tpath, problems):
    """A line's '; rule <triple>:' clauses against one of its tests."""
    where = "%s: %s" % (row["where"], os.path.basename(tpath))
    oses, bad = test.expected_rule()
    if bad:
        problems.add(where, bad)
    for t in row["triples"]:
        os_name = TRIPLES[t]
        if t not in row["rules"]:
            if os_name in oses:
                problems.add(
                    where,
                    "%s names %s, but the line has no '; rule %s:' clause"
                    % (EXPECTED_RULE, os_name, t),
                )
            continue
        if os_name not in oses:
            problems.add(
                where,
                "the line's '; rule %s:' clause needs REDEFINE: %s = <os>[,<os>]: "
                "<reason> naming %s" % (t, EXPECTED_RULE, os_name),
            )
        stem = expected_stem(test, tpath)
        if stem is None:
            continue
        names = [stem + "." + os_name + ".expected", stem + ".expected"]
        if not any(os.path.isfile(n) for n in names):
            problems.add(
                where,
                "no hand-written %s or %s for the line's '; rule %s:' clause"
                % (os.path.basename(names[0]), os.path.basename(names[1]), t),
            )


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


def check_row_lists(rows, headers, problems):
    """The '#! rows:' and '#! obj-lto:' headers."""
    bases = {}
    for row in rows:
        bases.setdefault(row["base"], set()).update(row["pipelines"])
    if not headers["rows"]:
        problems.add("matrix.txt", "no '#! rows:' header line")
    if not headers["obj-lto"]:
        problems.add("matrix.txt", "no '#! obj-lto:' header line")
    names = {name for name, _ in headers["rows"]}
    for name, where in headers["rows"]:
        if name not in bases:
            problems.add(where, "#! rows: no line for the row %s" % name)
    for name, where in headers["obj-lto"]:
        if name not in names:
            problems.add(where, "#! obj-lto: %s is not a '#! rows:' name" % name)
        if name not in bases:
            if name not in names:
                problems.add(where, "#! obj-lto: no line for the row %s" % name)
        elif not set(PROG_PIPELINES) <= bases[name]:
            problems.add(
                where, "#! obj-lto: the row %s does not list both obj and lto" % name
            )


def check_p3_paths(repo, headers, line_tests, problems):
    """The '#! p3-tests:' paths."""
    for path, where in headers["p3-tests"]:
        if not under(path, "msil/test"):
            problems.add(where, "#! p3-tests: %s is not under msil/test" % path)
        elif under(path, "msil/test/M3"):
            problems.add(
                where,
                "#! p3-tests: %s lies under msil/test/M3, which the gate legs run "
                "as a whole" % path,
            )
        elif under(path, "msil/test/harness"):
            problems.add(
                where,
                "#! p3-tests: %s lies under msil/test/harness, the harness leg's "
                "directory" % path,
            )
        if path in line_tests:
            problems.add(
                where, "#! p3-tests: %s is the test of %s" % (path, line_tests[path])
            )
        full = os.path.join(repo, path.replace("/", os.sep))
        if os.path.isdir(full):
            problems.add(where, "#! p3-tests: %s is a directory, not a test" % path)
        elif not os.path.isfile(full):
            problems.add(where, "#! p3-tests: %s does not exist" % path)


# --- legs --------------------------------------------------------------------


def lit_filters(args, bad):
    """(filter-out, filter) of a run's lit arguments, compiled as lit
    compiles them (case-insensitive; the last of each option counts), each
    None when absent; None for both after bad() on a malformed one."""
    found = {"--filter-out": None, "--filter": None}
    i = 0
    while i < len(args):
        arg = args[i]
        for opt in ("--filter-out", "--filter"):
            if arg == opt:
                if i + 1 >= len(args):
                    bad("%s without a regular expression" % opt)
                    return None
                found[opt] = args[i + 1]
                i += 1
                break
            if arg.startswith(opt + "="):
                found[opt] = arg[len(opt) + 1 :]
                break
        i += 1
    out = []
    for opt in ("--filter-out", "--filter"):
        if found[opt] is None:
            out.append(None)
            continue
        try:
            out.append(re.compile(found[opt], re.IGNORECASE))
        except re.error as e:
            bad("%s=%s is not a regular expression: %s" % (opt, found[opt], e))
            return None
    return tuple(out)


def norm_run_path(repo, path):
    """A run's path, repository-relative with '/' ('' for the repository
    itself), or None when it lies outside the repository."""
    p = path.replace("\\", "/")
    if os.path.isabs(p) or re.match(r"^[A-Za-z]:/", p):
        try:
            p = os.path.relpath(p, os.path.abspath(repo)).replace("\\", "/")
        except ValueError:
            return None
    p = posixpath.normpath(p)
    if p == "..":
        return None
    if p.startswith("../"):
        return None
    return "" if p == "." else p


class Run:
    """One lit run of a leg (plan 5.17.6)."""

    def __init__(self, leg, tag, paths, filters, pipeline, prog, triple, also, params):
        self.leg, self.tag, self.paths = leg, tag, paths
        self.filter_out, self.filter = filters
        self.pipeline, self.prog_pipeline, self.triple = pipeline, prog, triple
        self.also_run, self.params = also, params

    def runs(self, rel, name):
        if not any(p == "" or rel == p or under(rel, p) for p in self.paths):
            return False
        if name is not None:
            if self.filter_out is not None and self.filter_out.search(name):
                return False
            if self.filter is not None and not self.filter.search(name):
                return False
        return True

    def resolves(self, pipeline):
        if pipeline == "asm":
            return self.pipeline == "asm"
        return self.prog_pipeline == pipeline

    def gate_runs(self, rel, name, triple):
        if not (self.runs(rel, name) and self.triple == FULL_TRIPLES[triple]):
            return False
        if GATE_ALSO_RUN not in self.also_run:
            return False
        if self.params.get("msil_native_oracle", "").lower() != "required":
            return False
        if triple == "windows-msvc":
            return self.params.get("msil_run_netfx", "").lower() in (
                "1",
                "true",
                "yes",
                "on",
            )
        return True


def make_run(repo, script, leg, tag, data, bad):
    if not isinstance(data, dict):
        bad("not an object")
        return None
    paths, lit = data.get("paths"), data.get("lit", [])
    params = data.get("params", {})
    if not isinstance(paths, list) or not all(isinstance(p, str) for p in paths):
        bad("'paths' is not a list of strings")
        return None
    if not isinstance(lit, list) or not all(isinstance(a, str) for a in lit):
        bad("'lit' is not a list of strings")
        return None
    if not isinstance(params, dict):
        bad("'params' is not an object")
        return None
    params = {str(k): str(v) for k, v in params.items()}
    pipeline = data.get("pipeline") or params.get("msil_pipeline") or "obj"
    implied = "lto" if pipeline == "lto" else "obj"
    prog = data.get("prog_pipeline") or implied
    triple = (
        data.get("triple") or params.get("msil_triple") or DEFAULT_TRIPLES.get(script)
    )
    also = data.get("also_run")
    if also is None:
        also = [a.strip() for a in params.get("msil_also_run", "").split(",")]
        also = [a for a in also if a]
    ok = True
    if not isinstance(also, list) or not all(isinstance(a, str) for a in also):
        bad("'also_run' is not a list of strings")
        ok = False
    if triple is None:
        bad("no triple, and the script '%s' has no default" % script)
        ok = False
    elif triple not in FULL_TRIPLES.values():
        bad("unknown triple '%s'" % triple)
    if "msil_pipeline" in params and params["msil_pipeline"] != pipeline:
        bad(
            "'pipeline' %s contradicts msil_pipeline=%s"
            % (pipeline, params["msil_pipeline"])
        )
        ok = False
    if prog != implied:
        bad("'prog_pipeline' %s does not follow from pipeline %s" % (prog, pipeline))
        ok = False
    if "msil_triple" in params and params["msil_triple"] != triple:
        bad("'triple' %s contradicts msil_triple=%s" % (triple, params["msil_triple"]))
        ok = False
    filters = lit_filters(lit, bad)
    if filters is None:
        ok = False
    norm = []
    for p in paths:
        n = norm_run_path(repo, p)
        if n is None:
            bad("the path '%s' lies outside the repository" % p)
            ok = False
        else:
            norm.append(n)
    if not ok:
        return None
    return Run(leg, tag, norm, filters, pipeline, prog, triple, also, params)


class LegsFile:
    def __init__(self, path, script, runs, nlegs):
        self.path, self.script, self.runs, self.nlegs = path, script, runs, nlegs


def load_legs(repo, path, problems):
    """The lit runs of a legs file (plan 5.17.6's JSON), or None."""
    try:
        with open(path, encoding="utf-8-sig") as f:
            data = json.load(f)
    except (OSError, ValueError) as e:
        problems.add(path, "cannot read the legs file: %s" % e)
        return None
    if not isinstance(data, dict) or data.get("version") != 1:
        problems.add(path, "not a legs file of version 1 (plan 5.17.6's JSON)")
        return None
    script, legs = data.get("script"), data.get("legs")
    if not isinstance(script, str) or not isinstance(legs, list):
        problems.add(path, "no 'script' string or no 'legs' list")
        return None
    runs = []
    for i, leg in enumerate(legs, 1):
        if (
            not isinstance(leg, dict)
            or not isinstance(leg.get("name"), str)
            or not isinstance(leg.get("runs"), list)
        ):
            problems.add(path, "leg %d is not an object with a name and runs" % i)
            continue
        name, tag, kind = leg["name"], leg.get("tag") or "", leg.get("kind", "lit")
        if not isinstance(tag, str):
            problems.add(path, "leg %s: the tag is not a string" % name)
            continue
        if kind != "lit":
            if leg["runs"]:
                problems.add(path, "leg %s of kind %s has lit runs" % (name, kind))
            continue
        for j, data_run in enumerate(leg["runs"], 1):

            def bad(what, name=name, j=j):
                problems.add(path, "leg %s, run %d: %s" % (name, j, what))

            run = make_run(repo, script, name, tag, data_run, bad)
            if run is not None:
                runs.append(run)
    return LegsFile(path, script, runs, len(legs))


def asm_in_every_run(test):
    """True when the test builds its asm pipeline itself, with unguarded
    '%clang_msil ... -S' commands at -O0 and -O2 and a %msil_exec run, which
    every run of the test executes whatever its msil_pipeline (commands under
    '%if msil-pipeline=asm' or '%if msil-prog-pipeline=<p>' do not count)."""
    levels = set()
    runs = False
    for c in test.commands:
        if c.asm is not None or c.pipes is not None:
            continue
        if "%clang_msil" in c.body and re.search(r"\s-S(\s|$)", c.body):
            levels.update(re.findall(r"\s-(O[0-3sz])\b", c.body))
        if "%msil_exec" in c.body:
            runs = True
    return runs and set(LEVELS) <= levels


def check_legs(repo, rows, headers, tests, legs_files, problems):
    """The legs rule and the gate rule (5.17.12 item 1) over the TestFiles
    tests (by path); returns (runs, legs, whether the gate rule applied)."""
    files = [f for f in (load_legs(repo, p, problems) for p in legs_files) if f]
    phase = [r for f in files for r in f.runs if r.tag in PHASE_TAGS]
    gate_files = [f for f in files if f.script == GATE_SCRIPT]
    gate = [r for f in gate_files for r in f.runs if r.tag == GATE_TAG]
    for row in rows:
        for name in row["tests"]:
            test = tests.get(resolve(repo, name))
            if test is None:
                continue  # 'does not exist' is reported already
            rel = repo_rel(name)
            lname = lit_name(rel)
            own = under(rel, "msil/test")
            asm_anywhere = asm_in_every_run(test)
            for p in row["pipelines"]:
                for t in row["triples"]:
                    if not any(
                        r.runs(rel, lname)
                        and (
                            not own
                            or (
                                (r.resolves(p) or (p == "asm" and asm_anywhere))
                                and r.triple == FULL_TRIPLES[t]
                            )
                        )
                        for r in phase
                    ):
                        problems.add(
                            row["where"],
                            "no phase-gate leg runs %s under %s for %s" % (rel, p, t),
                        )
    if gate_files:
        wanted = []
        for row in rows:
            if not row["phase3"]:
                continue
            for name in row["tests"]:
                rel = repo_rel(name)
                if under(rel, "msil/test"):
                    wanted += [(row["where"], rel, t) for t in row["triples"]]
        for path, where in headers["p3-tests"]:
            wanted += [("%s: %s" % (where, path), path, t) for t in TRIPLES]
        for where, rel, t in wanted:
            lname = lit_name(rel)
            if not any(r.gate_runs(rel, lname, t) for r in gate):
                problems.add(
                    where,
                    "no gate leg runs %s with %s for %s" % (rel, GATE_ALSO_RUN, t),
                )
    return (
        sum(len(f.runs) for f in files),
        sum(f.nlegs for f in files),
        bool(gate_files),
    )


# --- the checks --------------------------------------------------------------


def check(repo, legs_files=()):
    """(problems, lines, tests, legs) of the matrix of repo: every problem as
    a string, the number of lines and of tests, and with legs_files (runs,
    legs, whether the gate rule applied), else None."""
    problems = Problems()
    mpath = os.path.join(repo, MATRIX)
    if not os.path.isfile(mpath):
        problems.add("matrix.txt", "missing: %s" % mpath)
        return problems.items, 0, 0, None
    rows, headers = parse_matrix(mpath, problems)
    tests = {}
    listed = set()
    on_rule_lines = set()
    line_tests = {}
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
        if row["p3"] and row["phase3"]:
            problems.add(
                row["where"],
                "names both a P3 label (P3, P3-A, P3-B) and a Phase 3 milestone "
                "(M24-M35); the milestone that builds the feature replaces the label",
            )
        if row["rules"]:
            if row["native_kind"] != "transcript" or row["kind"] != "transcript":
                problems.add(
                    row["where"],
                    "a '; rule <triple>:' clause needs native kind transcript and a "
                    "transcript Phase 2 column",
                )
            for t in sorted(row["rules"]):
                if t not in row["triples"]:
                    problems.add(
                        row["where"],
                        "'; rule %s:' names a triple the line does not list" % t,
                    )
        for name in row["tests"]:
            line_tests.setdefault(repo_rel(name), row["where"])
            path = resolve(repo, name)
            if not os.path.isfile(path):
                problems.add(row["where"], "%s does not exist" % name)
                continue
            key = os.path.normcase(os.path.abspath(path))
            listed.add(key)
            if row["rules"]:
                on_rule_lines.add(key)
            if path not in tests:
                tests[path] = TestFile(path)
            test = tests[path]
            if row["kind"] == "transcript":
                check_transcript(row, test, path, problems)
            elif row["kind"] == "diag":
                check_diag(row, test, path, problems)
            elif row["kind"] == "object":
                check_object(row, test, path, problems)
            check_rules(row, test, path, problems)
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
    check_row_lists(rows, headers, problems)
    check_p3_paths(repo, headers, line_tests, problems)
    sem = os.path.join(repo, SEMANTICS)
    for name in sorted(os.listdir(sem)) if os.path.isdir(sem) else []:
        path = os.path.join(sem, name)
        if not name.endswith(".test") or not os.path.isfile(path):
            continue
        if XFAIL_RE.search(open(path, encoding="utf-8", errors="replace").read()):
            problems.add(name, "has an XFAIL line")
        if name == COVERAGE_TEST:
            continue
        key = os.path.normcase(os.path.abspath(path))
        if key not in listed:
            problems.add(name, "is not listed in matrix.txt")
            continue
        test = next(
            (
                t
                for p, t in tests.items()
                if os.path.normcase(os.path.abspath(p)) == key
            ),
            None,
        )
        if test is not None and test.expected_rule()[0] and key not in on_rule_lines:
            problems.add(
                name,
                "redefines %s, but no line with a '; rule <triple>:' clause lists it"
                % EXPECTED_RULE,
            )
    sem_key = os.path.normcase(os.path.abspath(sem)) + os.sep
    for path, test in sorted(tests.items()):
        if test.xfail and not os.path.normcase(os.path.abspath(path)).startswith(
            sem_key
        ):
            problems.add(os.path.basename(path), "has an XFAIL line")
        check_guards(test, path, problems)
    legs = None
    if legs_files:
        legs = check_legs(repo, rows, headers, tests, legs_files, problems)
    return problems.items, len(rows), len(tests), legs


def p3_tests(repo):
    """(paths, problems) of --p3-tests (see the module docstring)."""
    problems = Problems()
    mpath = os.path.join(repo, MATRIX)
    if not os.path.isfile(mpath):
        problems.add("matrix.txt", "missing: %s" % mpath)
        return [], problems.items
    rows, headers = parse_matrix(mpath, problems)
    if problems.items:
        return [], problems.items
    out = []
    for row in rows:
        if not row["phase3"]:
            continue
        for name in row["tests"]:
            rel = repo_rel(name)
            if not under(rel, "msil/test") or under(rel, "msil/test/M3"):
                continue
            if not os.path.isfile(resolve(repo, name)):
                problems.add(row["where"], "%s does not exist" % name)
            elif rel not in out:
                out.append(rel)
    for path, where in headers["p3-tests"]:
        if not os.path.isfile(os.path.join(repo, path.replace("/", os.sep))):
            problems.add(where, "#! p3-tests: %s does not exist" % path)
        elif path not in out:
            out.append(path)
    if problems.items:
        return [], problems.items
    return out, []


# --- self-test -------------------------------------------------------------

FC = '--implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"'
DIAG_CHECKS = (
    "# OBJ: {{^}}msil-ld: error: undefined symbol: f{{$}}\n"
    "# LTO: {{^}}msil-ld: error: {{.*}}undefined symbol: f{{$}}\n"
)


def diag_test(o2="piped"):
    """d.test: a failing build and its FileCheck per pipeline and level; o2
    'piped' (the FileCheck reads the build's output), 'file' (both levels
    write a file that a FileCheck of their own reads), 'o0-file' (as 'file',
    but the -O2 FileCheck reads the -O0 file) or 'unchecked' (-O2's output
    goes to a file that nothing reads)."""
    lines = []
    for p in PROG_PIPELINES:

        def run(body):
            return "# RUN: %if msil-prog-pipeline=" + p + " %{ " + body + " %}\n"

        for lv in LEVELS:
            build = "%{msil_prog_fail_" + lv + "}"
            check = "FileCheck %s --check-prefix=" + p.upper() + " " + FC
            out = "%t." + p + "." + lv + ".txt"
            if o2 == "piped" or (o2 == "unchecked" and lv == "O0"):
                lines.append(run(build + " 2>&1 | " + check))
            elif o2 == "unchecked":
                lines.append(run(build + " > " + out + " 2>&1"))
            else:
                read = out if o2 == "file" else "%t." + p + ".O0.txt"
                lines.append(run(build + " > " + out + " 2>&1"))
                lines.append(run(check + " --input-file=" + read))
    return "".join(lines) + DIAG_CHECKS


GOOD_TESTS = {
    "a.test": "# RUN: rm -rf %t && split-file %s %t\n# REDEFINE: %{msil_srcs} = %t/a.c\n# RUN: %{msil_prog}\n",
    "a.expected": "[exit 0]\n",
    "d.test": diag_test(),
    "l.test": "# REQUIRES: msil-triple-linux\n# RUN: %{msil_prog}\n",
    "l.linux.expected": "[exit 0]\n",
    "s.test": "# REQUIRES: msil-triple-windows\n# RUN: %{msil_prog}\n# RUN: %if msil-pipeline=asm %{ %{msil_c} %}\n",
    "s.expected": "[exit 1]\n",
    "g.test": "# RUN: %{msil_prog}\n",
    "g.expected": "[exit 7]\n",
    "r.test": "# REDEFINE: %{msil_expected_rule} = windows: R13: written by hand\n# RUN: %{msil_prog}\n",
    "r.expected": "[exit 2]\n",
    "r.windows.expected": "[exit 2]\n",
    "msil/test/M1/x.c": "// RUN: %{msil_c}\n",
}
GOOD_MATRIX = """# a test matrix
#! rows: ra rd rl rp ro ru
#! rows: rs rg rr
#! obj-lto: ra rd rl
#! p3-tests: msil/test/M1/x.c
ra | 08-semantics/a.test | transcript: 0 | obj,lto | linux-gnu,windows-msvc | transcript | M20
rd | 08-semantics/d.test | link error | obj,lto | linux-gnu,windows-msvc | diag:undefined symbol: <s> | M20
rl | 08-semantics/l.test | transcript | obj,lto | linux-gnu | transcript | M20
rp | 08-semantics/d.test | transcript: later | obj,lto | linux-gnu,windows-msvc | diag:undefined symbol: f | P3-B
ro | llvm/test/o.ll | n/a | obj | linux-gnu | object: flags | M19
ru | - | transcript | obj | linux-gnu | untested: needs a tool | P3
rs | 08-semantics/s.test | transcript: 1 | asm,obj,lto | windows-msvc | transcript | M20
rg | 08-semantics/g.test | transcript: 7 | obj,lto | linux-gnu,windows-msvc | transcript | M26
rr | 08-semantics/r.test | transcript: 2; rule windows-msvc: R13 | obj,lto | linux-gnu,windows-msvc | transcript | M20
"""
OBJECT_TEST = "; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj %s -o %t.o\n; RUN: llvm-cil-dump --link-info %t.o | FileCheck %s\n"
GOOD_P3 = ["msil/test/M2/08-semantics/g.test", "msil/test/M1/x.c"]
WIN, LNX = FULL_TRIPLES["windows-msvc"], FULL_TRIPLES["linux-gnu"]
P3_PATHS = ["msil/test/M3"] + GOOD_P3


def leg_run(paths, pipeline="obj", triple=WIN, lit=(), also=(), **params):
    """A run of -Describe's JSON (plan 5.17.6)."""
    p = {"msil_pipeline": pipeline, "msil_require_hosted": "1"}
    if triple == LNX:
        p.update(msil_runner="wsl", msil_triple=LNX)
    if also:
        p["msil_also_run"] = ",".join(also)
    p.update(params)
    return {
        "paths": list(paths),
        "lit": list(lit),
        "params": p,
        "pipeline": pipeline,
        "prog_pipeline": "lto" if pipeline == "lto" else "obj",
        "triple": triple,
        "runner": "wsl" if triple == LNX else "local",
        "also_run": list(also),
    }


def leg(name, tag, *runs, kind="lit"):
    return {"name": name, "tag": tag, "kind": kind, "runs": list(runs)}


GOOD_LEGS = {
    "version": 1,
    "script": "lit-legs.ps1",
    "legs": [
        leg(
            "net8",
            "",
            leg_run(["llvm/test", "msil/test"], lit=["--filter-out=MSIL :: Programs/"]),
        ),
        leg("asm", "", leg_run(["msil/test/M2/08-semantics"], pipeline="asm")),
        leg("lto", "", leg_run(["msil/test/M2"], pipeline="lto")),
        leg("wsl-net10", "Wsl", leg_run(["msil/test/M2"], triple=LNX)),
        leg("lto-wsl", "Exit", leg_run(["msil/test/M2"], pipeline="lto", triple=LNX)),
        leg(
            "p3-full",
            "Quick",
            leg_run(
                P3_PATHS,
                also=["10+tc0"],
                msil_native_oracle="required",
                msil_run_netfx="1",
            ),
        ),
        leg(
            "wsl-p3",
            "Quick",
            leg_run(
                P3_PATHS, triple=LNX, also=["10+tc0"], msil_native_oracle="required"
            ),
            leg_run(
                ["msil/test/M2/08-semantics"],
                triple=LNX,
                lit=["--filter-out", r"MSIL :: (M2/08-semantics/g\.test|M1/x\.c)$"],
            ),
        ),
        leg("expected", "", kind="expected"),
    ],
}


def legs_edit(fn):
    """A legs case: fn changes a copy of GOOD_LEGS in place (and may return
    a list of legs files instead)."""

    def make():
        data = copy.deepcopy(GOOD_LEGS)
        got = fn(data)
        return got if isinstance(got, list) else [data]

    return make


def leg_named(data, name):
    return next(x for x in data["legs"] if x["name"] == name)


def split_files(data):
    """The good legs as two files: lit-legs.ps1's Quick legs, and the other
    legs as lit-legs.sh's."""
    sh = {"version": 1, "script": "lit-legs.sh", "legs": []}
    ps1 = {"version": 1, "script": "lit-legs.ps1", "legs": []}
    for x in data["legs"]:
        (ps1 if x["tag"] == "Quick" else sh)["legs"].append(x)
    return [ps1, sh]


def as_sh_without_quick(data):
    data["script"] = "lit-legs.sh"
    data["legs"] = [x for x in data["legs"] if x["tag"] != "Quick"]


# (what, files to change {name: text or None to delete}, matrix edit
# (old, new) or None, a function that gives the legs files (--legs) or None,
# the text one problem must contain, or None when the tree must pass)
SELF_CASES = [
    (
        "a missing file",
        {},
        ("08-semantics/l.test |", "08-semantics/nope.test |"),
        None,
        "nope.test does not exist",
    ),
    (
        "an unlisted test",
        {"x.test": "# RUN: %{msil_prog}\n"},
        None,
        None,
        "x.test: is not listed",
    ),
    (
        "UB with a transcript",
        {},
        ("ra | 08-semantics/a.test | transcript: 0", "ra | 08-semantics/a.test | UB"),
        None,
        "needs a diag: Phase 2 column",
    ),
    (
        "a composite under one pipeline",
        {"a.test": "# RUN: %if msil-prog-pipeline=obj %{ %{msil_prog} %}\n"},
        None,
        None,
        "%{msil_prog} runs under obj, the line lists lto/obj",
    ),
    (
        "no PLEASE submit",
        {"d.test": diag_test().replace(' --implicit-check-not="PLEASE submit"', "")},
        None,
        None,
        "lacks --implicit-check-not for 'PLEASE submit'",
    ),
    (
        "a diag text the checks lack",
        {},
        ("diag:undefined symbol: <s>", "diag:duplicate symbol: <s>"),
        None,
        "checks 'duplicate symbol: <s>'",
    ),
    (
        "a diag at -O0 only",
        {"d.test": diag_test().replace("fail_O2", "fail_O0")},
        None,
        None,
        "no failing build at -O2",
    ),
    (
        "a diag whose -O2 build no FileCheck reads",
        {"d.test": diag_test("unchecked")},
        None,
        None,
        "obj: no FileCheck of a failing build at -O2 checks 'undefined symbol: <s>'",
    ),
    (
        "a diag whose -O2 FileCheck reads the -O0 output",
        {"d.test": diag_test("o0-file")},
        None,
        None,
        "lto: no FileCheck of a failing build at -O2 checks 'undefined symbol: f'",
    ),
    (
        "a diag checked through --input-file at each level",
        {"d.test": diag_test("file")},
        None,
        None,
        None,
    ),
    (
        "a P3 transcript",
        {},
        ("| linux-gnu | transcript | M20", "| linux-gnu | transcript | P3-A"),
        None,
        "a P3 row pins its Phase 2 diagnostic",
    ),
    (
        "untested outside P3",
        {},
        ("untested: needs a tool | P3", "untested: needs a tool | M20"),
        None,
        "only a P3 row may be untested",
    ),
    (
        "a P3 label beside a Phase 3 milestone",
        {},
        ("diag:undefined symbol: f | P3-B", "diag:undefined symbol: f | P3-B, M28"),
        None,
        "rp: names both a P3 label (P3, P3-A, P3-B) and a Phase 3 milestone",
    ),
    (
        "an XFAIL line",
        {"a.test": GOOD_TESTS["a.test"] + "# XFAIL: *\n"},
        None,
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
        None,
        "guarded by msil-pipeline=<p>",
    ),
    (
        "a one-triple line without REQUIRES",
        {"l.test": "# RUN: %{msil_prog}\n"},
        None,
        None,
        "%{msil_prog} runs on linux/windows, the line names linux",
    ),
    ("a missing .expected", {"a.expected": None}, None, None, "no a.expected"),
    (
        "a FileCheck of a run without the JIT names",
        {"a.test": GOOD_TESTS["a.test"] + "# RUN: %msil_exec %t.exe | FileCheck %s\n"},
        (
            "rd | 08-semantics/d.test | link error",
            "rd | 08-semantics/a.test,08-semantics/d.test | link error",
        ),
        None,
        "a FileCheck of a run lacks --implicit-check-not for InvalidProgram",
    ),
    # The header lines.
    (
        "a '#! rows:' name without a line",
        {},
        ("#! rows: rs rg rr", "#! rows: rs rg rr rz"),
        None,
        "matrix.txt:3: #! rows: no line for the row rz",
    ),
    (
        "a '#! obj-lto:' row on obj only",
        {},
        (
            "ra | 08-semantics/a.test | transcript: 0 | obj,lto",
            "ra | 08-semantics/a.test | transcript: 0 | obj",
        ),
        None,
        "#! obj-lto: the row ra does not list both obj and lto",
    ),
    (
        "a '#! obj-lto:' name that '#! rows:' lacks",
        {},
        ("#! obj-lto: ra rd rl", "#! obj-lto: ra rd rl rv"),
        None,
        "#! obj-lto: rv is not a '#! rows:' name",
    ),
    (
        "no '#! rows:' header",
        {},
        ("#! rows: ra rd rl rp ro ru\n#! rows: rs rg rr\n", ""),
        None,
        "no '#! rows:' header line",
    ),
    (
        "an unknown header",
        {},
        ("#! obj-lto: ra rd rl", "#! obj-lto: ra rd rl\n#! row: ra"),
        None,
        "'#! row: ra' is not a header line",
    ),
    (
        "a missing '#! p3-tests:' path",
        {"msil/test/M1/x.c": None},
        None,
        None,
        "#! p3-tests: msil/test/M1/x.c does not exist",
    ),
    (
        "a '#! p3-tests:' path under msil/test/M3",
        {"msil/test/M3/y.test": "# RUN: true\n"},
        (
            "#! p3-tests: msil/test/M1/x.c",
            "#! p3-tests: msil/test/M1/x.c msil/test/M3/y.test",
        ),
        None,
        "#! p3-tests: msil/test/M3/y.test lies under msil/test/M3",
    ),
    (
        "a '#! p3-tests:' path under msil/test/harness",
        {"msil/test/harness/h.test": "# RUN: true\n"},
        ("#! p3-tests: msil/test/M1/x.c", "#! p3-tests: msil/test/harness/h.test"),
        None,
        "#! p3-tests: msil/test/harness/h.test lies under msil/test/harness",
    ),
    (
        "a '#! p3-tests:' path that a line names",
        {},
        (
            "#! p3-tests: msil/test/M1/x.c",
            "#! p3-tests: msil/test/M2/08-semantics/a.test",
        ),
        None,
        "#! p3-tests: msil/test/M2/08-semantics/a.test is the test of matrix.txt:6: ra",
    ),
    (
        "a '#! p3-tests:' path outside msil/test",
        {},
        ("#! p3-tests: msil/test/M1/x.c", "#! p3-tests: llvm/test/o.ll"),
        None,
        "#! p3-tests: llvm/test/o.ll is not under msil/test",
    ),
    # The '; rule <triple>:' clause.
    (
        "a rule clause on a diag line",
        {},
        (
            "rd | 08-semantics/d.test | link error |",
            "rd | 08-semantics/d.test | link error; rule linux-gnu: x |",
        ),
        None,
        "rd: a '; rule <triple>:' clause needs native kind transcript and a transcript Phase 2 column",
    ),
    (
        "a malformed rule clause",
        {},
        ("; rule windows-msvc: R13", "; rule windows: R13"),
        None,
        "'; rule windows: R13' is not '; rule <triple>: <why>'",
    ),
    (
        "a rule clause whose test does not redefine the rule",
        {"r.test": "# RUN: %{msil_prog}\n"},
        None,
        None,
        "the line's '; rule windows-msvc:' clause needs REDEFINE: %{msil_expected_rule}",
    ),
    (
        "a rule clause without its hand-written file",
        {"r.windows.expected": None, "r.expected": None},
        None,
        None,
        "no hand-written r.windows.expected or r.expected",
    ),
    (
        "an expected rule naming an OS without a clause",
        {"r.test": GOOD_TESTS["r.test"].replace("= windows:", "= windows,linux:")},
        None,
        None,
        "%{msil_expected_rule} names linux, but the line has no '; rule linux-gnu:' clause",
    ),
    (
        "an 08-semantics test that redefines the rule on no rule line",
        {
            "a.test": "# REDEFINE: %{msil_expected_rule} = linux: why\n"
            + GOOD_TESTS["a.test"]
        },
        None,
        None,
        "a.test: redefines %{msil_expected_rule}, but no line with a '; rule <triple>:' clause lists it",
    ),
    # --legs: the legs rule.
    ("the legs rule on the good legs", {}, None, legs_edit(lambda d: None), None),
    ("the legs of two files", {}, None, legs_edit(split_files), None),
    (
        "no phase-gate leg for lto on linux-gnu",
        {},
        None,
        legs_edit(lambda d: d["legs"].remove(leg_named(d, "lto-wsl"))),
        "ra: no phase-gate leg runs msil/test/M2/08-semantics/a.test under lto for linux-gnu",
    ),
    (
        "a Quick leg is no phase-gate leg",
        {},
        None,
        legs_edit(lambda d: leg_named(d, "lto-wsl").update(tag="Quick")),
        "rl: no phase-gate leg runs msil/test/M2/08-semantics/l.test under lto for linux-gnu",
    ),
    (
        "a --filter-out that drops the test",
        {},
        None,
        legs_edit(
            lambda d: leg_named(d, "lto-wsl")["runs"][0]["lit"].append(
                r"--filter-out=msil :: M2/08-SEMANTICS/a\.test$"
            )
        ),
        "ra: no phase-gate leg runs msil/test/M2/08-semantics/a.test under lto for linux-gnu",
    ),
    (
        "a path beside the test, not above it",
        {},
        None,
        legs_edit(
            lambda d: leg_named(d, "asm")["runs"][0].update(
                paths=["msil/test/M2/08-sem"]
            )
        ),
        "rs: no phase-gate leg runs msil/test/M2/08-semantics/s.test under asm for windows-msvc",
    ),
    (
        "an asm line without an asm run",
        {},
        None,
        legs_edit(lambda d: d["legs"].remove(leg_named(d, "asm"))),
        "rs: no phase-gate leg runs msil/test/M2/08-semantics/s.test under asm for windows-msvc",
    ),
    (
        "an asm line whose own -S builds run in every leg",
        {
            "s.test": "# REQUIRES: msil-triple-windows\n# RUN: %{msil_prog}\n"
            "# RUN: %clang_msil %{msil_cflags} -O0 -S %s -o %t.O0.il\n"
            "# RUN: %clang_msil %{msil_cflags} -O2 -S %s -o %t.O2.il\n"
            "# RUN: %msil_exec --expect %S/s --out %t.O0.out %t.O0.exe\n"
        },
        None,
        legs_edit(lambda d: d["legs"].remove(leg_named(d, "asm"))),
        None,
    ),
    (
        "a test outside msil/test that no run names",
        {},
        None,
        legs_edit(
            lambda d: leg_named(d, "net8")["runs"][0].update(paths=["msil/test"])
        ),
        "ro: no phase-gate leg runs llvm/test/o.ll under obj for linux-gnu",
    ),
    (
        "a legs file of another version",
        {},
        None,
        legs_edit(lambda d: d.update(version=2)),
        "not a legs file of version 1",
    ),
    (
        "a run whose pipeline contradicts its parameter",
        {},
        None,
        legs_edit(
            lambda d: leg_named(d, "lto")["runs"][0].update(
                pipeline="obj", prog_pipeline="obj"
            )
        ),
        "leg lto, run 1: 'pipeline' obj contradicts msil_pipeline=lto",
    ),
    # --legs: the gate rule.
    (
        "the gate rule on lit-legs.sh's legs",
        {},
        None,
        legs_edit(as_sh_without_quick),
        None,
    ),
    (
        "a gate run without 10+tc0",
        {},
        None,
        legs_edit(lambda d: leg_named(d, "p3-full")["runs"][0].update(also_run=[])),
        "rg: no gate leg runs msil/test/M2/08-semantics/g.test with 10+tc0 for windows-msvc",
    ),
    (
        "a Windows gate run without the Framework",
        {},
        None,
        legs_edit(
            lambda d: leg_named(d, "p3-full")["runs"][0]["params"].pop("msil_run_netfx")
        ),
        "rg: no gate leg runs msil/test/M2/08-semantics/g.test with 10+tc0 for windows-msvc",
    ),
    (
        "a gate run without the required oracle",
        {},
        None,
        legs_edit(
            lambda d: leg_named(d, "wsl-p3")["runs"][0]["params"].update(
                msil_native_oracle="1"
            )
        ),
        "rg: no gate leg runs msil/test/M2/08-semantics/g.test with 10+tc0 for linux-gnu",
    ),
    (
        "a '#! p3-tests:' path that no gate run takes",
        {},
        None,
        legs_edit(
            lambda d: leg_named(d, "wsl-p3")["runs"][0]["paths"].remove(
                "msil/test/M1/x.c"
            )
        ),
        "matrix.txt:5: msil/test/M1/x.c: no gate leg runs msil/test/M1/x.c with 10+tc0 for linux-gnu",
    ),
    (
        "a gate run that filters the test out",
        {},
        None,
        legs_edit(
            lambda d: leg_named(d, "p3-full")["runs"][0]["lit"].append(
                r"--filter-out=MSIL :: M1/x\.c$"
            )
        ),
        "matrix.txt:5: msil/test/M1/x.c: no gate leg runs msil/test/M1/x.c with 10+tc0 for windows-msvc",
    ),
]

# --p3-tests: (what, files to change, matrix edit or None, the paths it must
# print, or None when it must fail with a message containing the text)
P3_CASES = [
    ("--p3-tests on the good tree", {}, None, GOOD_P3, None),
    (
        "--p3-tests leaves out msil/test/M3, llvm/ and duplicates",
        {},
        (
            "rg | 08-semantics/g.test",
            "rh | msil/test/M3/h.test | transcript | obj,lto | linux-gnu | transcript | M27\n"
            "rk | llvm/test/o.ll | n/a | obj | linux-gnu | object: flags | M30\n"
            "rg2 | 08-semantics/g.test | transcript | obj | linux-gnu | transcript | M28\n"
            "rg | 08-semantics/g.test",
        ),
        GOOD_P3,
        None,
    ),
    (
        "--p3-tests with a missing '#! p3-tests:' path",
        {"msil/test/M1/x.c": None},
        None,
        None,
        "matrix.txt:5: #! p3-tests: msil/test/M1/x.c does not exist",
    ),
    (
        "--p3-tests with a missing test of a Phase 3 line",
        {},
        ("rg | 08-semantics/g.test", "rg | 08-semantics/gone.test"),
        None,
        "rg: 08-semantics/gone.test does not exist",
    ),
    (
        "--p3-tests on a matrix that does not parse",
        {},
        ("| transcript | M26", "| M26"),
        None,
        "matrix.txt:13: 6 columns, not 7",
    ),
]


def make_tree(root, files, matrix):
    sem = os.path.join(root, SEMANTICS)
    os.makedirs(sem)
    os.makedirs(os.path.join(root, "llvm", "test"))
    with open(os.path.join(root, "llvm", "test", "o.ll"), "w", encoding="utf-8") as f:
        f.write(OBJECT_TEST)
    for name, text in files.items():
        if text is None:
            continue
        path = (
            os.path.join(root, name.replace("/", os.sep))
            if "/" in name
            else os.path.join(sem, name)
        )
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
    with open(os.path.join(sem, "matrix.txt"), "w", encoding="utf-8") as f:
        f.write(matrix)


def tree_for(work, n, change, edit):
    """A tree of GOOD_TESTS and GOOD_MATRIX with a change and a matrix edit,
    or None when the edit does not apply."""
    files = dict(GOOD_TESTS)
    files.update(change)
    matrix = GOOD_MATRIX
    if edit:
        if edit[0] not in matrix:
            return None
        matrix = matrix.replace(edit[0], edit[1], 1)
    root = os.path.join(work, "case%d" % n)
    make_tree(root, files, matrix)
    return root


def write_legs(root, legs):
    paths = []
    for i, data in enumerate(legs):
        path = os.path.join(root, "legs%d.json" % i)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f)
        paths.append(path)
    return paths


def self_test():
    failures = checks = 0
    work = tempfile.mkdtemp(prefix="check-matrix-")
    try:
        checks += 1
        root = os.path.join(work, "good")
        make_tree(root, dict(GOOD_TESTS), GOOD_MATRIX)
        got = check(root)[0]
        if got:
            print(
                "self-test: the good tree: unexpected problems:\n  " + "\n  ".join(got)
            )
            failures += 1
        else:
            print("self-test: the good tree: OK")
        n = 0
        for what, change, edit, legs, want in SELF_CASES:
            checks += 1
            n += 1
            root = tree_for(work, n, change, edit)
            if root is None:
                print("self-test: %s: the matrix edit does not apply" % what)
                failures += 1
                continue
            legs_files = write_legs(root, legs()) if legs else ()
            got = check(root, legs_files)[0]
            if want is None:
                if got:
                    print(
                        "self-test: %s: unexpected problems:\n  %s"
                        % (what, "\n  ".join(got))
                    )
                    failures += 1
                else:
                    print("self-test: %s: OK" % what)
            elif any(want in p for p in got):
                print("self-test: %s: reported" % what)
            else:
                print(
                    "self-test: %s: not reported (want '%s'); got:\n  %s"
                    % (what, want, "\n  ".join(got) or "nothing")
                )
                failures += 1
        for what, change, edit, want_paths, want in P3_CASES:
            checks += 1
            n += 1
            root = tree_for(work, n, change, edit)
            if root is None:
                print("self-test: %s: the matrix edit does not apply" % what)
                failures += 1
                continue
            out, err = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
                status = main(["--p3-tests", root])
            if want is None:
                expected = "".join(p + "\n" for p in want_paths)
                ok = status == 0 and out.getvalue() == expected and not err.getvalue()
            else:
                ok = status == 1 and not out.getvalue() and want in err.getvalue()
            if ok:
                print(
                    "self-test: %s: %s" % (what, "OK" if want is None else "reported")
                )
            else:
                print(
                    "self-test: %s: exit %d, stdout %r, stderr %r"
                    % (what, status, out.getvalue(), err.getvalue())
                )
                failures += 1
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("self-test: %d of %d checks passed" % (checks - failures, checks))
    return 1 if failures else 0


USAGE = "usage: check-matrix.py [--legs FILE]... REPO | --p3-tests REPO | --self-test\n"


def print_p3_tests(repo):
    paths, problems = p3_tests(repo)
    if problems:
        for p in problems:
            sys.stderr.write("check-matrix: error: %s\n" % p)
        return 1
    for p in paths:
        sys.stdout.write(p + "\n")
    return 0


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    if argv and argv[0] == "--p3-tests":
        if len(argv) != 2 or argv[1].startswith("-"):
            sys.stderr.write(USAGE)
            return 2
        return print_p3_tests(argv[1])
    legs, repo, i = [], None, 0
    while i < len(argv):
        arg = argv[i]
        if arg == "--legs" and i + 1 < len(argv):
            legs.append(argv[i + 1])
            i += 2
            continue
        if arg.startswith("--legs="):
            legs.append(arg[len("--legs=") :])
            i += 1
            continue
        if arg.startswith("-") or repo is not None:
            sys.stderr.write(USAGE)
            return 2
        repo = arg
        i += 1
    if repo is None:
        sys.stderr.write(USAGE)
        return 2
    if not os.path.isdir(repo):
        sys.stderr.write("check-matrix.py: %s is not a directory\n" % repo)
        return 2
    for path in legs:
        if not os.path.isfile(path):
            sys.stderr.write(
                "check-matrix.py: the legs file %s does not exist\n" % path
            )
            return 2
    problems, nrows, ntests, legs_info = check(repo, legs)
    if legs_info is not None:
        print(
            "check-matrix: legs: %d lit runs of %d legs in %d files; the gate rule %s"
            % (
                legs_info[0],
                legs_info[1],
                len(legs),
                "applied" if legs_info[2] else "did not apply (no lit-legs.ps1 file)",
            )
        )
    for p in problems:
        print("check-matrix: error: " + p)
    if problems:
        print("check-matrix: %d problems" % len(problems))
        return 1
    print("check-matrix: %d lines, %d tests: OK" % (nrows, ntests))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

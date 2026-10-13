#!/usr/bin/env python3
# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##
"""Mechanical checks of llvm/docs/MSIL/ImplementationPlan.md for the agent farm.

  python msil/utils/plan-check.py ownership    owned files are disjoint inside every milestone
  python msil/utils/plan-check.py reads M<n>   every path a WP of M<n> is told to read exists
  python msil/utils/plan-check.py scope M<n>   every staged path ('git diff --cached HEAD')
                                               belongs to M<n> (run 'git add -A' first)
  python msil/utils/plan-check.py --self-test  parser and checks on an embedded sample plan

Options: --plan <file> checks another copy of the plan (for example a deliberately
conflicting one); --repo <dir> is the root that relative paths and git use (default: the
repository that contains this script).

What is parsed (plan section 1 and section 12):
  - '### M<n>. <title>' milestone headings and '#### M<n>.<x> <title>' work-package
    headings below them (fenced code blocks are skipped); a '### M<digit>' heading of
    another form, or a '#### M' heading inside a milestone that is not a WP heading, is an
    error, so a WP never silently drops out of the checks;
  - each WP's '- Owned files:' line, exactly one per WP and on one line (a missing line, a
    line ending in ',' or a list wrapped onto the next line is an error): the backquoted
    paths; a trailing '/' owns the whole directory; '(none; ...)' owns nothing;
  - each WP's '- Reads:' line: entries separated by ';' outside parentheses;
  - section 12 rows of exactly the form
      | <date> | M<n> | fix-loop ownership: `<path>` -> <agent> | <reason> |
    (a row whose Change column starts with 'fix-loop ownership' in any other form is an
    error, so a grant is never silently ignored).

Checks:
  ownership  inside every milestone no path is owned by two WPs and no directory entry of one
             WP contains a path of another (compared case-insensitively, as on Windows).
  reads      for every Reads entry of every WP of M<n>: the leading token, if it looks like a
             path (contains '/' and starts with llvm/, clang/, lld/, msil/, .github/ or a drive
             such as D:/), must exist after stripping trailing punctuation and a ':<line>' or
             ':<line>-<line>' suffix, brace expansion ('{h,cpp}') and globbing ('*'). Further
             path-like tokens of such an entry outside parentheses ('a.md and b/c/') are
             checked the same way. Entries such as 'this plan section 5' are skipped.
  scope      every staged path is owned by a WP of M<n>, is the path of an M<n> fix-loop row
             of section 12, or is on the allow-list (llvm/docs/MSIL/ImplementationPlan.md and
             llvm/docs/MSIL/recon/**). Paths are compared exactly (git paths are
             case-sensitive).

Exit status: 0 = OK, 1 = a check failed, 2 = usage error or malformed plan.
Python 3 standard library only.
"""

import argparse
import glob
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Sequence, Tuple

PLAN_RELPATH = "llvm/docs/MSIL/ImplementationPlan.md"
# Plan section 1.8: paths every milestone's commit may contain.
ALLOW_LIST = ("llvm/docs/MSIL/ImplementationPlan.md", "llvm/docs/MSIL/recon/")
PATH_PREFIXES = ("llvm/", "clang/", "lld/", "msil/", ".github/")
DRIVE_RE = re.compile(r"^[A-Za-z]:/")

MILESTONE_RE = re.compile(r"^### (M\d+)\. +(.*)$")
WP_RE = re.compile(r"^#### (M\d+)\.([0-9A-Za-z]+) +(.*)$")
OWNED_RE = re.compile(r"^- Owned files:(.*)$")
READS_RE = re.compile(r"^- Reads:(.*)$")
BACKQUOTED_RE = re.compile(r"`([^`]+)`")
SECTION12_RE = re.compile(r"^## 12\. ")
AMENDMENT_RE = re.compile(
    r"^\|\s*(\d{4}-\d{2}-\d{2})\s*\|\s*(M\d+)\s*\|\s*fix-loop ownership: "
    r"`([^`]+)` -> ([^|`\s][^|`]*?)\s*\|(.*)\|\s*$"
)
LINE_SUFFIX_RE = re.compile(r":\d+(-\d+)?$")
TRAILING_PUNCT = ",.;:)]'\"`"


@dataclass
class WorkPackage:
    wp_id: str
    title: str
    line: int
    owned: List[str] = field(default_factory=list)
    reads: List[str] = field(default_factory=list)
    has_owned_line: bool = False
    has_reads_line: bool = False


@dataclass
class Milestone:
    ms_id: str
    title: str
    line: int
    wps: List[WorkPackage] = field(default_factory=list)


@dataclass
class Amendment:
    line: int
    date: str
    milestone: str
    path: str
    agent: str


@dataclass
class Plan:
    milestones: Dict[str, Milestone] = field(default_factory=dict)
    amendments: List[Amendment] = field(default_factory=list)
    errors: List[str] = field(default_factory=list)


def normalize_path(p: str) -> str:
    p = p.strip().replace("\\", "/")
    while p.startswith("./"):
        p = p[2:]
    return p


def split_top_level(text: str, sep: str = ";") -> List[str]:
    """Split on sep outside parentheses."""
    parts, depth, cur = [], 0, []
    for ch in text:
        if ch == "(":
            depth += 1
        elif ch == ")" and depth > 0:
            depth -= 1
        if ch == sep and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    parts.append("".join(cur))
    return [p.strip() for p in parts if p.strip()]


def parse_plan(text: str) -> Plan:
    plan = Plan()
    ms: Optional[Milestone] = None
    wp: Optional[WorkPackage] = None
    in_fence = False
    in_section12 = False
    # The WP whose Owned-files line precedes this one.
    owned_line_of: Optional[WorkPackage] = None
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.rstrip()
        if owned_line_of is not None:
            # Only the one '- Owned files:' line is parsed, so a list wrapped onto the next line
            # (indented, or a lazy continuation starting with a backquote) would lose paths.
            if line.strip() and (line[0].isspace() or line.startswith("`")):
                plan.errors.append(
                    f"line {lineno}: {owned_line_of.wp_id} '- Owned files:' list continues on "
                    f"this line; put every owned path on the one '- Owned files:' line: "
                    f"{line.strip()}"
                )
            owned_line_of = None
        if line.lstrip().startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        if line.startswith("## "):
            ms, wp = None, None
            in_section12 = bool(SECTION12_RE.match(line))
            continue
        if line.startswith("### "):
            wp = None
            m = MILESTONE_RE.match(line)
            if m:
                if m.group(1) in plan.milestones:
                    plan.errors.append(
                        f"line {lineno}: milestone {m.group(1)} defined twice"
                    )
                ms = Milestone(m.group(1), m.group(2).strip(), lineno)
                plan.milestones[ms.ms_id] = ms
            else:
                if re.match(r"^### M\d", line):
                    plan.errors.append(
                        f"line {lineno}: malformed milestone heading (expected "
                        f"'### M<n>. <title>'): {line}"
                    )
                ms = None
            continue
        if line.startswith("#### "):
            wp = None
            m = WP_RE.match(line)
            if not m:
                # Inside a milestone every '#### M...' heading is a work package; a malformed one
                # must not make the WP (and its owned files) vanish from the checks.
                if ms is not None and line.startswith("#### M"):
                    plan.errors.append(
                        f"line {lineno}: malformed work package heading in {ms.ms_id} (expected "
                        f"'#### M<n>.<x> <title>'): {line}"
                    )
            elif ms is None:
                # A malformed milestone heading must not make its WPs vanish from the checks.
                plan.errors.append(
                    f"line {lineno}: work package heading outside a "
                    f"'### M<n>. ' milestone: {line}"
                )
            else:
                wp_id = f"{m.group(1)}.{m.group(2)}"
                if m.group(1) != ms.ms_id:
                    plan.errors.append(
                        f"line {lineno}: work package {wp_id} is under milestone {ms.ms_id}"
                    )
                elif any(w.wp_id == wp_id for w in ms.wps):
                    plan.errors.append(
                        f"line {lineno}: work package {wp_id} defined twice"
                    )
                wp = WorkPackage(wp_id, m.group(3).strip(), lineno)
                ms.wps.append(wp)
            continue
        if wp is not None:
            m = OWNED_RE.match(line)
            if m:
                if wp.has_owned_line:
                    plan.errors.append(
                        f"line {lineno}: second '- Owned files:' line in {wp.wp_id}"
                    )
                wp.has_owned_line = True
                owned_line_of = wp
                rest = m.group(1).strip()
                paths = [normalize_path(p) for p in BACKQUOTED_RE.findall(rest)]
                if not paths and not rest.lower().startswith("(none"):
                    plan.errors.append(
                        f"line {lineno}: {wp.wp_id} '- Owned files:' has no `backquoted` path "
                        f"and does not say '(none'"
                    )
                if rest.endswith(","):
                    plan.errors.append(
                        f"line {lineno}: {wp.wp_id} '- Owned files:' line ends with ','; put every "
                        f"owned path on the one '- Owned files:' line"
                    )
                for p in paths:
                    if (
                        not p
                        or p.startswith("/")
                        or DRIVE_RE.match(p)
                        or ".." in p.split("/")
                    ):
                        plan.errors.append(
                            f"line {lineno}: {wp.wp_id} owns '{p}': owned paths are relative "
                            f"to the repository root"
                        )
                wp.owned.extend(paths)
                continue
            m = READS_RE.match(line)
            if m:
                if wp.has_reads_line:
                    plan.errors.append(
                        f"line {lineno}: second '- Reads:' line in {wp.wp_id}"
                    )
                wp.has_reads_line = True
                wp.reads.extend(split_top_level(m.group(1)))
                continue
        if in_section12 and line.startswith("|"):
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            change = cells[2] if len(cells) >= 3 else ""
            if not change.startswith("fix-loop ownership"):
                continue
            m = AMENDMENT_RE.match(line.strip())
            if not m:
                plan.errors.append(
                    f"line {lineno}: malformed fix-loop row (expected "
                    f"'| <date> | M<n> | fix-loop ownership: `<path>` -> <agent> | <reason> |'): "
                    f"{line.strip()}"
                )
                continue
            plan.amendments.append(
                Amendment(
                    lineno,
                    m.group(1),
                    m.group(2),
                    normalize_path(m.group(3)),
                    m.group(4).strip(),
                )
            )
    for milestone in plan.milestones.values():
        for w in milestone.wps:
            if not w.has_owned_line:
                plan.errors.append(
                    f"line {w.line}: {w.wp_id} has no '- Owned files:' line (a WP that owns "
                    f"nothing says '- Owned files: (none; <reason>)')"
                )
    if not plan.milestones:
        plan.errors.append("no '### M<n>. ' milestone headings found")
    return plan


def covers(entry: str, path: str, fold: bool = False) -> bool:
    """True if owned entry (a file, or a directory ending in '/') contains path."""
    if fold:
        entry, path = entry.casefold(), path.casefold()
    if entry.endswith("/"):
        return path.startswith(entry) or path == entry.rstrip("/")
    return path == entry


def ownership_conflicts(plan: Plan) -> List[str]:
    conflicts = []
    for ms in plan.milestones.values():
        entries: List[Tuple[str, str]] = []
        for wp in ms.wps:
            for p in dict.fromkeys(wp.owned):
                entries.append((wp.wp_id, p))
        for i, (wa, a) in enumerate(entries):
            for wb, b in entries[i + 1 :]:
                if wa == wb:
                    continue
                if a.rstrip("/").casefold() == b.rstrip("/").casefold():
                    conflicts.append(
                        f"{ms.ms_id}: `{a}` is owned by both {wa} and {wb}"
                    )
                elif a.endswith("/") and covers(a, b, fold=True):
                    conflicts.append(
                        f"{ms.ms_id}: `{b}` ({wb}) is inside directory `{a}` ({wa})"
                    )
                elif b.endswith("/") and covers(b, a, fold=True):
                    conflicts.append(
                        f"{ms.ms_id}: `{a}` ({wa}) is inside directory `{b}` ({wb})"
                    )
    return conflicts


def looks_like_path(token: str) -> bool:
    return "/" in token and (
        token.startswith(PATH_PREFIXES) or bool(DRIVE_RE.match(token))
    )


def clean_path_token(token: str) -> str:
    token = token.strip().strip("`")
    while True:
        before = token
        token = token.rstrip(TRAILING_PUNCT)
        token = LINE_SUFFIX_RE.sub("", token)
        if token == before:
            return token


def expand_braces(pattern: str) -> List[str]:
    m = re.search(r"\{([^{}]*)\}", pattern)
    if not m:
        return [pattern]
    out: List[str] = []
    for alt in m.group(1).split(","):
        out.extend(expand_braces(pattern[: m.start()] + alt + pattern[m.end() :]))
    return out


def read_path_of_entry(entry: str) -> Optional[str]:
    """The path a Reads entry names (its leading token), or None for prose entries."""
    tokens = entry.split()
    if not tokens:
        return None
    token = tokens[0].strip("`")
    if not looks_like_path(token):
        return None
    return clean_path_token(token)


def read_paths_of_entry(entry: str) -> List[str]:
    """The leading path of a Reads entry plus every further path-like token outside
    parentheses ('a.md and b/dir/', 'x.cpp, y/z.h'); parenthesized text is description.
    """
    lead = read_path_of_entry(entry)
    if lead is None:
        return []
    depth, outside = 0, []
    for ch in entry:
        if ch == "(":
            depth += 1
        elif ch == ")" and depth > 0:
            depth -= 1
        elif depth == 0:
            outside.append(ch)
    paths = [lead]
    for token in "".join(outside).split()[1:]:
        token = token.strip("`")
        if looks_like_path(token) and clean_path_token(token) not in paths:
            paths.append(clean_path_token(token))
    return paths


def path_exists(repo: str, pattern: str) -> bool:
    full = pattern if DRIVE_RE.match(pattern) else os.path.join(repo, pattern)
    if any(ch in pattern for ch in "*?["):
        return bool(glob.glob(full, recursive=True))
    if pattern.endswith("/"):
        return os.path.isdir(full)
    return os.path.exists(full)


def missing_reads(plan: Plan, ms_id: str, repo: str) -> Tuple[int, List[str]]:
    checked, missing = 0, []
    for wp in plan.milestones[ms_id].wps:
        for entry in wp.reads:
            for path in read_paths_of_entry(entry):
                for alt in expand_braces(path):
                    checked += 1
                    if not path_exists(repo, alt):
                        missing.append(f"{wp.wp_id}: {alt}   (Reads entry: {entry})")
    return checked, missing


def scope_offenders(plan: Plan, ms_id: str, paths: Sequence[str]) -> List[str]:
    ms = plan.milestones[ms_id]
    allowed = [p for wp in ms.wps for p in wp.owned]
    allowed += [a.path for a in plan.amendments if a.milestone == ms_id]
    allowed += list(ALLOW_LIST)
    offenders = []
    for path in paths:
        path = normalize_path(path)
        if any(covers(e, path) for e in allowed):
            continue
        owners = [
            f"{w.wp_id}"
            for m in plan.milestones.values()
            if m.ms_id != ms_id
            for w in m.wps
            if any(covers(e, path) for e in w.owned)
        ]
        hint = f" (owned by {', '.join(owners)} in other milestones)" if owners else ""
        offenders.append(f"{path}{hint}")
    return offenders


def staged_paths(repo: str) -> List[str]:
    out = subprocess.run(
        [
            "git",
            "-C",
            repo,
            "diff",
            "--cached",
            "--name-only",
            "--no-renames",
            "-z",
            "HEAD",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if out.returncode != 0:
        raise RuntimeError(
            "git diff --cached failed: " + out.stderr.decode("utf-8", "replace")
        )
    return [p for p in out.stdout.decode("utf-8").split("\0") if p]


# --------------------------------------------------------------------------- self-test

SAMPLE_PLAN = """\
# Sample plan

## 5. Shared contracts

```text
### M9. A heading inside a code fence is not a milestone
#### M9.a Nor is this a work package
```

## 6. Milestones

### M7. First sample milestone

**Goal.** Owned files that do not overlap, and Reads entries of every shape.

#### M7.a Implementation (implement, ~10 lines)

- Owned files: `a/b/c.cpp`, `a/b/d/`, `a/b/d/inner.h`
- Reads: llvm/x/WebAssembly{One,Two}.{h,cpp} (braces); this plan section 5; llvm/x/r.txt:10-20; \
llvm/x/dir/ (a directory); llvm/x/*.td, more text; @ABS@/abs/*.ps1 (absolute glob); \
llvm/docs/plan.md (e15: what it covers; with a semicolon inside parentheses)
- Depends on: nothing

Prose that mentions - Owned files: `not/owned.cpp` is ignored because it is not at the start.

#### M7.b Tests (test, ~10 lines)

- Owned files: `a/b/e.cpp`
- Reads: llvm/missing.txt, then prose; clang/x/also-missing.cpp:5; msil/ (exists, msil/nothere/ is description); llvm/x/r.txt and llvm/x/second-missing.txt, llvm/x/a.td

### M8. Second sample milestone

#### M8.0 Build step (infra, no source changes)

- Owned files: (none; build agent, writes only the build tree)

#### M8.a Conflicts (implement, ~1 lines)

- Owned files: `a/b/c.cpp`, `x/`, `same/dir/`

#### M8.b More conflicts (implement, ~1 lines)

- Owned files: `A/B/C.cpp`, `x/y/z.txt`, `same/dir`

## 7. Future milestones

### P2-A. Not a milestone of this plan

## 12. Amendment log

| Date | Milestone | Change | Reason |
|---|---|---|---|
| 2026-10-12 | - | Plan v1, mentions fix-loop ownership in passing | text |
| 2026-10-20 | M7 | fix-loop ownership: `q/r.cpp` -> fix-1 | a finding |
| 2026-10-21 | M8 | fix-loop ownership: `x2/` -> fix-2 | a directory grant |
"""

BAD_ROW = (
    "| 2026-10-22 | M7 | fix-loop ownership: q/s.cpp -> fix-3 | backquotes missing |\n"
)


def self_test() -> int:
    failures: List[str] = []

    def expect(cond: bool, what: str) -> None:
        print(("ok    " if cond else "FAIL  ") + what)
        if not cond:
            failures.append(what)

    with tempfile.TemporaryDirectory(prefix="plan-check-") as tmp:
        repo = os.path.join(tmp, "repo")
        absdir = os.path.join(tmp, "abs").replace("\\", "/")
        for rel in (
            "llvm/x/WebAssemblyOne.h",
            "llvm/x/WebAssemblyOne.cpp",
            "llvm/x/WebAssemblyTwo.h",
            "llvm/x/WebAssemblyTwo.cpp",
            "llvm/x/r.txt",
            "llvm/x/dir/keep.txt",
            "llvm/x/a.td",
            "llvm/docs/plan.md",
            "msil/keep",
        ):
            os.makedirs(os.path.dirname(os.path.join(repo, rel)), exist_ok=True)
            open(os.path.join(repo, rel), "w").close()
        os.makedirs(os.path.join(tmp, "abs", "abs"), exist_ok=True)
        open(os.path.join(tmp, "abs", "abs", "s.ps1"), "w").close()
        if not DRIVE_RE.match(
            absdir
        ):  # POSIX: only drive paths count as absolute Reads paths
            absdir = "llvm"
            os.makedirs(os.path.join(repo, "llvm", "abs"), exist_ok=True)
            open(os.path.join(repo, "llvm", "abs", "s.ps1"), "w").close()
        text = SAMPLE_PLAN.replace("@ABS@", absdir)

        plan = parse_plan(text)
        expect(plan.errors == [], f"sample parses without errors {plan.errors}")
        expect(
            list(plan.milestones) == ["M7", "M8"],
            "milestones M7, M8 (fenced and P2-A skipped)",
        )
        expect(
            [w.wp_id for w in plan.milestones["M7"].wps] == ["M7.a", "M7.b"], "M7 WPs"
        )
        expect(
            [w.wp_id for w in plan.milestones["M8"].wps] == ["M8.0", "M8.a", "M8.b"],
            "M8 WPs",
        )
        m7a = plan.milestones["M7"].wps[0]
        expect(
            m7a.owned == ["a/b/c.cpp", "a/b/d/", "a/b/d/inner.h"], "M7.a owned files"
        )
        expect(len(m7a.reads) == 7, f"M7.a has 7 Reads entries ({len(m7a.reads)})")
        expect(plan.milestones["M8"].wps[0].owned == [], "'(none; ...)' owns nothing")
        expect(
            [(a.milestone, a.path, a.agent) for a in plan.amendments]
            == [("M7", "q/r.cpp", "fix-1"), ("M8", "x2/", "fix-2")],
            "fix-loop rows",
        )

        conflicts = ownership_conflicts(plan)
        expect(
            not any(c.startswith("M7:") for c in conflicts),
            "no conflict inside M7 (a WP may overlap itself)",
        )
        expect(
            any("`a/b/c.cpp` is owned by both M8.a and M8.b" in c for c in conflicts),
            "duplicate file across WPs (case-insensitive)",
        )
        expect(
            any(
                "`x/y/z.txt` (M8.b) is inside directory `x/` (M8.a)" in c
                for c in conflicts
            ),
            "file inside another WP's directory",
        )
        expect(
            any("`same/dir/` is owned by both" in c for c in conflicts),
            "directory entry vs the same path without '/'",
        )
        expect(len(conflicts) == 3, f"exactly three conflicts ({conflicts})")
        expect(
            not any("M7" in c for c in conflicts),
            "same file in different milestones is fine",
        )

        checked, missing = missing_reads(plan, "M7", repo)
        missing_paths = sorted(
            m.split(":", 1)[1].split("(Reads")[0].strip() for m in missing
        )
        expect(
            missing_paths
            == [
                "clang/x/also-missing.cpp",
                "llvm/missing.txt",
                "llvm/x/second-missing.txt",
            ],
            f"reads M7 reports exactly the three missing paths {missing_paths}",
        )
        expect(
            checked == 15, f"reads M7 checks 15 paths after brace expansion ({checked})"
        )
        expect(
            read_paths_of_entry(
                "llvm/a.md and llvm/b/ (see msil/c.txt), clang/d.cpp:4."
            )
            == ["llvm/a.md", "llvm/b/", "clang/d.cpp"],
            "further paths outside parentheses",
        )
        expect(
            read_paths_of_entry("this plan, llvm/a.md") == [],
            "an entry that does not start with a path is prose",
        )
        expect(
            read_path_of_entry("this plan section 5") is None,
            "prose entries are skipped",
        )
        expect(
            read_path_of_entry("llvm/a/b.cpp:122-123 (x)") == "llvm/a/b.cpp",
            "':<a>-<b>' stripped",
        )
        expect(
            read_path_of_entry("llvm/a/b.td:17,") == "llvm/a/b.td", "':<n>,' stripped"
        )
        expect(
            read_path_of_entry("D:/llvm-msil/build/x/*.ps1 (y)")
            == "D:/llvm-msil/build/x/*.ps1",
            "drive paths keep their colon",
        )
        expect(
            read_path_of_entry("recon/isel.md section 3") is None,
            "unknown prefix is prose",
        )
        expect(
            expand_braces("a/{b,c}.{h,cpp}")
            == ["a/b.h", "a/b.cpp", "a/c.h", "a/c.cpp"],
            "brace expansion",
        )

        staged = [
            "a/b/c.cpp",
            "a/b/d/deep/x.h",
            "a/b/e.cpp",
            "q/r.cpp",
            "llvm/docs/MSIL/ImplementationPlan.md",
            "llvm/docs/MSIL/recon/new.md",
            "llvm/docs/MSIL/Design.md",
            "a/b/dx",
            "x/y/z.txt",
            "x2/f",
            "A/B/C.cpp",
            "llvm/docs/MSIL/reconx.md",
        ]
        off = scope_offenders(plan, "M7", staged)
        expect(
            [o.split(" ")[0] for o in off]
            == [
                "llvm/docs/MSIL/Design.md",
                "a/b/dx",
                "x/y/z.txt",
                "x2/f",
                "A/B/C.cpp",
                "llvm/docs/MSIL/reconx.md",
            ],
            f"scope M7 offenders {off}",
        )
        expect(
            any(o.startswith("x/y/z.txt (owned by M8.a, M8.b") for o in off),
            "offender hint names the other milestone's owners",
        )
        off8 = scope_offenders(plan, "M8", ["x2/f", "q/r.cpp", "x/anything"])
        expect(
            [o.split(" ")[0] for o in off8] == ["q/r.cpp"],
            f"fix-loop rows count only for their milestone {off8}",
        )

        bad = parse_plan(text + BAD_ROW)
        expect(
            len(bad.errors) == 1 and "malformed fix-loop row" in bad.errors[0],
            "a malformed fix-loop row is an error",
        )
        nested = parse_plan("### M1. X\n#### M2.a Wrong parent\n- Owned files: `a`\n")
        expect(
            any("M2.a is under milestone M1" in e for e in nested.errors),
            "WP id must match milestone",
        )
        orphan = parse_plan(
            "### M1. X\n### M2 Missing dot\n#### M2.a Lost\n- Owned files: `a`\n"
        )
        expect(
            any("outside a '### M<n>. ' milestone" in e for e in orphan.errors),
            "a WP under a malformed milestone heading is an error, not silently dropped",
        )
        expect(
            any("malformed milestone heading" in e for e in orphan.errors),
            "a malformed '### M<n>' milestone heading is an error",
        )
        lost = parse_plan(
            "### M1. X\n### M2 Missing dot\n#### M2.a: Lost too\n"
            "- Owned files: `a`\n"
        )
        expect(
            len(lost.errors) == 1 and "malformed milestone heading" in lost.errors[0],
            f"a malformed milestone with a malformed WP heading is an error {lost.errors}",
        )
        badwp = parse_plan(
            "### M1. X\n#### M1.a Fine\n- Owned files: `a`\n#### Gate notes\n"
            "#### M1.b: Colon\n- Owned files: `a`\n#### M1c No dot\n"
            "- Owned files: `c`\n"
        )
        expect(
            len(badwp.errors) == 2
            and all("malformed work package heading in M1" in e for e in badwp.errors)
            and [w.wp_id for w in badwp.milestones["M1"].wps] == ["M1.a"],
            "a malformed '#### M' WP heading is an error, '#### Gate notes' is not "
            f"{badwp.errors}",
        )
        noowned = parse_plan(
            "### M1. X\n#### M1.a No owned line\n- Reads: llvm/x\n"
            "#### M1.0 Build step\n- Owned files: (none; build agent)\n"
        )
        expect(
            len(noowned.errors) == 1
            and "M1.a has no '- Owned files:' line" in noowned.errors[0]
            and noowned.milestones["M1"].wps[1].owned == [],
            "a WP without an Owned-files line is an error, '(none; ...)' is not "
            f"{noowned.errors}",
        )
        wrapped = parse_plan(
            "### M1. X\n#### M1.a Y\n- Owned files: `a`, `b`\n  `c`\n- Reads: x\n"
        )
        expect(
            len(wrapped.errors) == 1
            and "M1.a '- Owned files:' list continues" in wrapped.errors[0],
            f"an indented continuation of the Owned-files list is an error {wrapped.errors}",
        )
        lazy = parse_plan("### M1. X\n#### M1.a Y\n- Owned files: `a`,\n`b`\n")
        expect(
            len(lazy.errors) == 2
            and "ends with ','" in lazy.errors[0]
            and "list continues" in lazy.errors[1],
            f"a trailing ',' and an unindented continuation are errors {lazy.errors}",
        )
        absolute = parse_plan(
            "### M1. X\n#### M1.a Y\n- Owned files: `/etc/x`, `../y`\n"
        )
        expect(len(absolute.errors) == 2, "absolute and '..' owned paths are errors")

    print(
        f"plan-check self-test: {'PASS' if not failures else 'FAIL'} "
        f"({len(failures)} failure(s))"
    )
    return 0 if not failures else 1


# --------------------------------------------------------------------------- main


def main(argv: Sequence[str]) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    default_repo = os.path.dirname(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    )
    ap = argparse.ArgumentParser(
        prog="plan-check.py",
        description=__doc__.split("\n\n")[0],
        usage="plan-check.py [--plan FILE] [--repo DIR] (ownership | reads M<n> | scope M<n> | --self-test)",
    )
    ap.add_argument("command", nargs="?", choices=["ownership", "reads", "scope"])
    ap.add_argument("milestone", nargs="?", help="M<n> for 'reads' and 'scope'")
    ap.add_argument(
        "--self-test", action="store_true", help="run the embedded self-test"
    )
    ap.add_argument("--plan", help=f"plan file (default <repo>/{PLAN_RELPATH})")
    ap.add_argument(
        "--repo", default=default_repo, help="repository root (default: %(default)s)"
    )
    args = ap.parse_args(argv)

    if args.self_test:
        if args.command:
            ap.error("--self-test takes no command")
        return self_test()
    if not args.command:
        ap.error(
            "a command is required: ownership | reads M<n> | scope M<n> | --self-test"
        )
    if args.command == "ownership" and args.milestone:
        ap.error("'ownership' takes no milestone (it checks every milestone)")
    if args.command in ("reads", "scope"):
        if not args.milestone or not re.fullmatch(r"M\d+", args.milestone):
            ap.error(f"'{args.command}' needs a milestone such as M0")

    repo = os.path.abspath(args.repo)
    plan_path = args.plan or os.path.join(repo, PLAN_RELPATH)
    try:
        with open(plan_path, encoding="utf-8") as f:
            plan = parse_plan(f.read())
    except OSError as e:
        print(f"plan-check: cannot read the plan: {e}")
        return 2
    if plan.errors:
        print(f"plan-check: {plan_path} is malformed:")
        for e in plan.errors:
            print(f"  {e}")
        return 2
    if args.milestone and args.milestone not in plan.milestones:
        print(
            f"plan-check: {args.milestone} is not a milestone of the plan "
            f"(found: {', '.join(plan.milestones)})"
        )
        return 2
    n_wps = sum(len(m.wps) for m in plan.milestones.values())

    if args.command == "ownership":
        conflicts = ownership_conflicts(plan)
        for c in conflicts:
            print(f"CONFLICT {c}")
        print(
            f"ownership: {len(plan.milestones)} milestones, {n_wps} work packages, "
            f"{len(plan.amendments)} fix-loop rows: "
            + ("OK" if not conflicts else f"{len(conflicts)} conflict(s)")
        )
        return 1 if conflicts else 0

    if args.command == "reads":
        checked, missing = missing_reads(plan, args.milestone, repo)
        for m in missing:
            print(f"MISSING {m}")
        print(
            f"reads {args.milestone}: {checked} path(s) checked: "
            + ("OK" if not missing else f"{len(missing)} missing")
        )
        return 1 if missing else 0

    try:
        paths = staged_paths(repo)
    except (OSError, RuntimeError) as e:
        print(f"plan-check: {e}")
        return 2
    offenders = scope_offenders(plan, args.milestone, paths)
    for o in offenders:
        print(f"OUT OF SCOPE {o}")
    print(
        f"scope {args.milestone}: {len(paths)} staged path(s): "
        + (
            "OK"
            if not offenders
            else f"{len(offenders)} not owned by a WP of {args.milestone}, not in an "
            f"{args.milestone} fix-loop row and not on the allow-list"
        )
    )
    return 1 if offenders else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

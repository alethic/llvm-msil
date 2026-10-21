#!/usr/bin/env python3
# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##
"""Check msil/utils/upstream-hooks.txt, the manifest of the upstream files the fork changes.

  python msil/utils/ci/check-upstream-hooks.py --check
  python msil/utils/ci/check-upstream-hooks.py --list-lit
  python msil/utils/ci/check-upstream-hooks.py --list-compile
  python msil/utils/ci/check-upstream-hooks.py --lit-verdict <lit JSON report> [--min-pass N]
  python msil/utils/ci/check-upstream-hooks.py --self-test

Options: --manifest <file> reads another manifest (default <repo>/msil/utils/upstream-hooks.txt);
--repo <dir> is the repository that git and relative paths use (default: the repository that
contains this script). The manifest format is documented in the manifest's header.

--check        The manifest against the repository (plan M11, item 7):
               - the upstream base: $MSIL_UPSTREAM_BASE if set; else MERGE_HEAD when
                 'git rev-parse -q --verify MERGE_HEAD' succeeds (an upstream merge in progress:
                 the diff against MERGE_HEAD is the fork's footprint on the new upstream); else
                 the second parent of the last first-parent merge whose subject starts
                 '[MSIL] Merge upstream llvm-project main at '. The base and the reason are
                 printed; no base (a shallow clone, for example) is a failure naming the reason;
               - the changed paths: 'git diff --name-only --no-renames <base>' (the working
                 tree) plus 'git ls-files --others --exclude-standard'. Every changed path
                 outside FORK_PREFIXES that is not a fork-only line needs a hook block; a hook
                 block or fork-only line for an unchanged path is stale. A fork-only path must
                 not exist in the base ('git cat-file -e <base>:<path>' fails): an upstream
                 file needs a hook block, and the unlisted-hook message offers fork-only only
                 for a path that the base lacks;
               - every hook block has a lit, gtest, compile, utc or none line, every 'none'
                 has a reason, every lit pattern matches a file, every fork test exists, every
                 gtest binary is one the gtest leg of upstream-hooks.ps1 runs, and every utc
                 test exists and is one of the utc leg's tests: the list '$utcTests = @(...)'
                 of msil/utils/ci/lit-legs.ps1 and 'utc_tests=(...)' of
                 msil/utils/ci/lit-legs.sh (UTC_LEG_SCRIPTS), which must both parse and be
                 equal;
               - a 'compile' line sits only on a file that the compile leg compiles: one of
                 COMPILE_FILES (the translation units, --list-compile), or a header or other
                 file that one of them #includes directly (the include path is a suffix of the
                 hook's path: "llvm/MC/MCContext.h" for llvm/include/llvm/MC/MCContext.h).
                 Every COMPILE_FILES entry exists and has a hook block with a 'compile' line;
               - an allow line must name one lit input (no '*' or '?', no directory) and give a
                 reason;
               - every exhaustive object-format switch of the monorepo has an MSIL case. 'git
                 grep' finds the C/C++/Objective-C source files with a SWITCH_PATTERNS case
                 label ('case [llvm::]Triple::XCOFF:', 'case [llvm::]MCContext::IsXCOFF:' or the
                 unqualified 'case XCOFF:' / 'case IsXCOFF:'). Their comments and string
                 literals are blanked, and each such label in code needs an MSIL case label
                 ('case [llvm::]Triple::MSIL:', 'case [llvm::]MCContext::IsMSIL:', 'case MSIL:'
                 or 'case IsMSIL:') in the same switch: the innermost 'switch (...) {' body
                 around it, found by brace matching, and not a switch nested in it. A
                 Triple::MSIL anywhere else (a comparison, a comment, a case of another switch)
                 does not count. The exemptions are per switch, not per file: in a file with a
                 switch-default line a 'default:' label in the same switch also covers the
                 label (a 'default:' of a nested or an enclosing switch does not); a file with a
                 switch-other line exempts only its unqualified 'case XCOFF:' / 'case IsXCOFF:'
                 labels, and its qualified ones are checked as in any other file. When the
                 braces of a file do not match (preprocessor conditionals, for example), or a
                 label lies outside every switch body (a macro), those labels are counted
                 instead (the conservative fallback): there must be at least as many covering
                 labels in code (MSIL case labels, plus 'default:' labels in a switch-default
                 file) as XCOFF ones that need cover. A problem names the file and the line of
                 the uncovered XCOFF case. A switch-default line is stale when its file has no
                 XCOFF case label (git grep finds none), a switch-other line when its file has
                 no unqualified XCOFF case label in code.
--list-lit     The lit inputs: every lit line's files, without duplicates, sorted, one per line
               (repository-relative). Exit 1, after printing the files it found, if a pattern
               matches no file.
--list-compile The compile leg's translation units (COMPILE_FILES), one per line; upstream-hooks.ps1
               compiles exactly these. Reads no manifest.
--lit-verdict  Judges lit's JSON report ('lit -o <file>'): test names such as
               'Clang :: Driver/cl-options.c' map to repository paths (clang/test/Driver/
               cl-options.c). Fails when a FAIL is not on the allow-list, an allow-listed test
               passed (a stale entry) or has no result, a test is UNRESOLVED, TIMEOUT or XPASS,
               a lit input has no result (lit did not run it as a test), or there are fewer PASS
               results (XFAIL does not count) than the manifest's min-pass or --min-pass. One
               line per problem, then the counts.
--self-test    Plants every defect above in a temporary git repository (with synthetic leg
               scripts) and synthetic lit reports outside the repository (removed afterwards)
               and checks that each one is reported, and that the clean variants pass.

Exit status: 0 = OK, 1 = a check failed, 2 = usage error, malformed manifest or unreadable lit
report. Python 3 standard library only.
"""

import argparse
import contextlib
import glob
import io
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from typing import Dict, List, Mapping, NamedTuple, Optional, Sequence, Tuple

MANIFEST_RELPATH = "msil/utils/upstream-hooks.txt"
# Changed paths under these prefixes are fork files, never hooks.
FORK_PREFIXES = (
    "llvm/lib/Target/MSIL/",
    "llvm/test/CodeGen/MSIL/",
    "llvm/docs/MSIL/",
    "llvm/unittests/Target/MSIL/",
    "clang/test/CodeGen/MSIL/",
    "msil/",
    ".github/msil/",
)
MERGE_SUBJECT = "[MSIL] Merge upstream llvm-project main at "
MERGE_GREP = "^" + re.sub(r"([\[\]])", r"\\\1", MERGE_SUBJECT)
# The exhaustive switches over Triple::ObjectFormatType and MCContext::Environment (POSIX ERE,
# for git grep). The unqualified form is how a switch inside the class or after 'using enum'
# is written (llvm/lib/TargetParser/Triple.cpp); 'case XCOFF::...' is not one.
SWITCH_PATTERNS = (
    r"case (llvm::)?Triple::XCOFF *:",
    r"case (llvm::)?MCContext::IsXCOFF *:",
    r"case +(XCOFF|IsXCOFF) *:([^:]|$)",
)
# Only source files: the plan and the recon reports quote these patterns.
SWITCH_PATHSPECS = ("*.c", "*.cc", "*.cpp", "*.cxx", "*.h", "*.hh", "*.hpp", "*.hxx", "*.inc",
                    "*.def", "*.m", "*.mm")
# The same labels in code (after strip_code), and the MSIL labels that cover them in one switch.
XCOFF_LABEL_RE = re.compile(r"\bcase\s+(?:(?:llvm::)?Triple::XCOFF|(?:llvm::)?MCContext::IsXCOFF"
                            r"|XCOFF|IsXCOFF)\s*:(?!:)")
MSIL_LABEL_RE = re.compile(r"\bcase\s+(?:(?:llvm::)?Triple::MSIL|(?:llvm::)?MCContext::IsMSIL"
                           r"|MSIL|IsMSIL)\s*:(?!:)")
# A 'default:' label in code: it covers the XCOFF case labels of its own switch in a file with a
# switch-default line.
DEFAULT_LABEL_RE = re.compile(r"\bdefault\s*:(?!:)")
# The binaries the gtest leg of upstream-hooks.ps1 runs.
GTEST_BINARIES = ("unittests/TargetParser/TargetParserTests.exe", "unittests/MC/MCTests.exe")
# The translation units of the compile leg of upstream-hooks.ps1, which reads this list through
# --list-compile: the hooked upstream files that the main build never compiles. A 'compile' line
# is valid on these and on the files they #include directly.
COMPILE_FILES = ("llvm/lib/Target/ARM/AsmParser/ARMAsmParser.cpp",
                 "lldb/source/Plugins/Process/gdb-remote/GDBRemoteCommunicationClient.cpp")
# A 'compile' line on a file with one of these extensions must name a COMPILE_FILES entry.
SOURCE_EXTS = (".c", ".cc", ".cpp", ".cxx", ".m", ".mm")
INCLUDE_RE = re.compile(r'^[ \t]*(#)[ \t]*include[ \t]*[<"]([^">\n]+)[">]', re.M)
UTC_DIR = "llvm/test/tools/UpdateTestChecks/update_llc_test_checks"
# The utc leg's test list lives in both leg scripts (M11.e owns them): the script and the regex
# whose group 1 is the body of its list ('$utcTests = @(...)' may span lines).
UTC_LEG_SCRIPTS = (
    ("msil/utils/ci/lit-legs.ps1", "$utcTests = @(...)",
     re.compile(r"^[ \t]*\$utcTests[ \t]*=[ \t]*@\(([^)]*)\)", re.M | re.I)),
    ("msil/utils/ci/lit-legs.sh", "utc_tests=(...)",
     re.compile(r"^[ \t]*utc_tests=\(([^)]*)\)", re.M)),
)
UTC_NAME_RE = re.compile(r"[\w.+-]+")
# A quoted or bare word, or a comma, of such a list.
_UTC_TOKEN_RE = re.compile(r"'([^'\n]*)'|\"([^\"\n]*)\"|[^\s,'\"]+|,")
# lit suite name -> source root of the suite, for --lit-verdict.
SUITE_ROOTS = {"LLVM": "llvm/test", "Clang": "clang/test", "MSIL": "msil/test"}
KINDS = ("lit", "gtest", "compile", "utc", "none")
GLOB_CHARS = "*?["


class ManifestError(Exception):
    """The manifest cannot be parsed (exit 2)."""


class GitError(Exception):
    pass


@dataclass
class Hook:
    path: str
    line: int
    lit: List[str] = field(default_factory=list)
    gtest: List[str] = field(default_factory=list)
    compile: bool = False
    utc: List[str] = field(default_factory=list)
    none: List[str] = field(default_factory=list)
    fork: List[str] = field(default_factory=list)

    def has_kind(self) -> bool:
        return bool(self.lit or self.gtest or self.compile or self.utc or self.none)


@dataclass
class Allow:
    test: str
    reason: str
    line: int


@dataclass
class Manifest:
    name: str
    hooks: Dict[str, Hook] = field(default_factory=dict)
    fork_only: Dict[str, int] = field(default_factory=dict)
    switch_default: Dict[str, int] = field(default_factory=dict)
    switch_other: Dict[str, Tuple[int, str]] = field(default_factory=dict)
    allow: List[Allow] = field(default_factory=list)
    min_pass: Optional[int] = None


# --------------------------------------------------------------------------- manifest


def _check_path(token: str, where: str) -> str:
    if ("\\" in token or token.startswith("/") or re.match(r"^[A-Za-z]:", token)
            or ".." in token.split("/")):
        raise ManifestError(f"{where}: '{token}' is not a repository-relative path with '/'")
    return token


def parse_manifest(text: str, name: str) -> Manifest:
    """Parses the manifest; raises ManifestError with every syntax problem."""
    m = Manifest(name=name)
    errors: List[str] = []
    cur: Optional[Hook] = None
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.rstrip()
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        where = f"{name}:{n}"
        word, _, rest = stripped.partition(" ")
        rest = rest.strip()
        tokens = rest.split()
        try:
            if not line[0].isspace():
                cur = None
                if word == "hook":
                    if len(tokens) != 1:
                        raise ManifestError(f"{where}: 'hook' takes one path")
                    path = _check_path(tokens[0], where)
                    if path in m.hooks:
                        raise ManifestError(
                            f"{where}: second hook block for {path} (line {m.hooks[path].line})")
                    cur = m.hooks[path] = Hook(path, n)
                elif word in ("fork-only", "switch-default"):
                    if len(tokens) != 1:
                        raise ManifestError(f"{where}: '{word}' takes one path")
                    table = m.fork_only if word == "fork-only" else m.switch_default
                    path = _check_path(tokens[0], where)
                    if path in table:
                        raise ManifestError(f"{where}: second '{word} {path}' line")
                    table[path] = n
                elif word == "switch-other":
                    if not tokens:
                        raise ManifestError(f"{where}: 'switch-other' takes a path and a reason")
                    path = _check_path(tokens[0], where)
                    if path in m.switch_other:
                        raise ManifestError(f"{where}: second 'switch-other {path}' line")
                    m.switch_other[path] = (n, " ".join(tokens[1:]))
                elif word == "allow":
                    if not tokens:
                        raise ManifestError(f"{where}: 'allow' takes a test and a reason")
                    m.allow.append(Allow(tokens[0], " ".join(tokens[1:]), n))
                elif word == "min-pass":
                    if len(tokens) != 1 or not tokens[0].isdigit():
                        raise ManifestError(f"{where}: 'min-pass' takes one number")
                    if m.min_pass is not None:
                        raise ManifestError(f"{where}: second 'min-pass' line")
                    m.min_pass = int(tokens[0])
                else:
                    raise ManifestError(
                        f"{where}: unknown entry '{word}' (an indented line belongs to the hook "
                        "block above it)")
            else:
                if cur is None:
                    raise ManifestError(f"{where}: indented '{word}' outside a hook block")
                if word == "lit":
                    if len(tokens) != 1:
                        raise ManifestError(f"{where}: 'lit' takes one path or glob")
                    cur.lit.append(_check_path(tokens[0], where))
                elif word == "gtest":
                    if not 1 <= len(tokens) <= 2:
                        raise ManifestError(
                            f"{where}: 'gtest' takes a binary and an optional filter")
                    cur.gtest.append(_check_path(tokens[0], where))
                elif word == "compile":
                    if tokens:
                        raise ManifestError(f"{where}: 'compile' takes no argument")
                    cur.compile = True
                elif word == "utc":
                    if not tokens:
                        raise ManifestError(f"{where}: 'utc' takes test names")
                    cur.utc.extend(tokens)
                elif word == "none":
                    cur.none.append(rest)
                elif word == "fork":
                    if len(tokens) != 1:
                        raise ManifestError(f"{where}: 'fork' takes one path")
                    cur.fork.append(_check_path(tokens[0], where))
                else:
                    raise ManifestError(f"{where}: unknown hook line '{word}'")
        except ManifestError as e:
            errors.append(str(e))
    if m.min_pass is None:
        errors.append(f"{name}: no 'min-pass <N>' line")
    if errors:
        raise ManifestError("\n".join(errors))
    return m


def read_manifest(path: str) -> Manifest:
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
    except OSError as e:
        raise ManifestError(f"cannot read the manifest: {e}")
    return parse_manifest(text, path)


def _rel(repo: str, path: str) -> str:
    return os.path.relpath(path, repo).replace(os.sep, "/")


def expand_lit(m: Manifest, repo: str) -> Tuple[List[str], List[str]]:
    """Returns (sorted lit inputs, problems for patterns that match no file)."""
    inputs = set()
    problems = []
    for hook in m.hooks.values():
        for pat in hook.lit:
            full = os.path.join(repo, pat)
            hits = glob.glob(full) if any(c in pat for c in GLOB_CHARS) else [full]
            files = [_rel(repo, h) for h in hits if os.path.isfile(h)]
            if not files:
                dirs = [h for h in hits if os.path.isdir(h)]
                what = "matches only directories (name test files)" if dirs else "matches no file"
                problems.append(f"hook {hook.path}: lit '{pat}' {what}")
            inputs.update(files)
    return sorted(inputs), problems


def allow_problems(m: Manifest, repo: str, inputs: Sequence[str]) -> List[str]:
    problems = []
    known = set(inputs)
    seen = set()
    for a in m.allow:
        where = f"allow '{a.test}' (line {a.line})"
        if any(c in a.test for c in GLOB_CHARS):
            problems.append(f"{where}: a glob is not allowed; name one test file")
        elif a.test.endswith("/") or os.path.isdir(os.path.join(repo, a.test)):
            problems.append(f"{where}: names a directory; name one test file")
        elif a.test not in known:
            problems.append(f"{where}: not one of the lit inputs (--list-lit)")
        if a.test in seen:
            problems.append(f"{where}: second allow line for the same test")
        seen.add(a.test)
        if not a.reason.strip():
            problems.append(f"{where}: no reason (name the missing tool or runtime)")
    return problems


# --------------------------------------------------------------------------- git


def git(repo: str, args: Sequence[str], env: Optional[Mapping[str, str]] = None,
        ok_codes: Sequence[int] = (0,)) -> Tuple[int, str]:
    try:
        p = subprocess.run(["git", "-C", repo, "-c", "core.quotepath=off"] + list(args),
                           capture_output=True, env=dict(env) if env is not None else None)
    except OSError as e:
        raise GitError(f"cannot run git: {e}")
    out = p.stdout.decode("utf-8", errors="replace")
    if p.returncode not in ok_codes:
        err = p.stderr.decode("utf-8", errors="replace").strip()
        raise GitError(f"git {' '.join(args)} exited {p.returncode}: {err}")
    return p.returncode, out


def resolve_base(repo: str, env: Mapping[str, str]) -> Tuple[str, str]:
    """Returns (base commit, why); raises GitError naming the reason when there is none."""
    override = env.get("MSIL_UPSTREAM_BASE", "").strip()
    if override:
        rc, out = git(repo, ["rev-parse", "-q", "--verify", override + "^{commit}"], env,
                      (0, 1, 128))
        if rc != 0:
            raise GitError(f"MSIL_UPSTREAM_BASE={override} is not a commit of this repository")
        return out.strip(), f"$MSIL_UPSTREAM_BASE={override}"
    rc, out = git(repo, ["rev-parse", "-q", "--verify", "MERGE_HEAD"], env, (0, 1))
    if rc == 0 and out.strip():
        return out.strip(), ("MERGE_HEAD: a merge is in progress, so the base is the commit being "
                             "merged")
    _, out = git(repo, ["log", "--first-parent", "--merges", "-1", "--grep=" + MERGE_GREP,
                        "--format=%H %P%x00%s", "HEAD"], env)
    out = out.strip()
    if not out:
        _, shallow = git(repo, ["rev-parse", "--is-shallow-repository"], env)
        hint = " (this is a shallow clone)" if shallow.strip() == "true" else ""
        raise GitError(f"no first-parent merge commit whose subject starts '{MERGE_SUBJECT}' "
                       f"in the history of HEAD{hint}; set MSIL_UPSTREAM_BASE")
    shas, _, subject = out.partition("\0")
    parts = shas.split()
    if len(parts) < 3:
        raise GitError(f"merge {parts[0][:12]} '{subject}' has no second parent in this clone")
    merge, base = parts[0], parts[2]
    rc, _ = git(repo, ["cat-file", "-e", base + "^{commit}"], env, (0, 1, 128))
    if rc != 0:
        raise GitError(f"the second parent {base[:12]} of merge {merge[:12]} is not in this "
                       "clone (a shallow clone?); set MSIL_UPSTREAM_BASE")
    return base, f"second parent of {merge[:12]} '{subject}'"


def changed_paths(repo: str, base: str, env: Mapping[str, str]) -> List[str]:
    _, diff = git(repo, ["diff", "--name-only", "--no-renames", "-z", base, "--"], env)
    _, others = git(repo, ["ls-files", "--others", "--exclude-standard", "-z"], env)
    return sorted(set(p for p in (diff + others).split("\0") if p))


def in_base(repo: str, base: str, path: str, env: Mapping[str, str]) -> bool:
    """Whether path exists in the base commit ('git cat-file -e <base>:<path>')."""
    rc, _ = git(repo, ["cat-file", "-e", f"{base}:{path}"], env, (0, 1, 128))
    return rc == 0


def switch_files(repo: str, env: Mapping[str, str]) -> List[str]:
    args = ["grep", "-l", "-z", "-E", "--untracked"]
    for p in SWITCH_PATTERNS:
        args += ["-e", p]
    args += ["--"] + list(SWITCH_PATHSPECS)
    rc, out = git(repo, args, env, (0, 1))
    return sorted(set(p for p in out.split("\0") if p)) if rc == 0 else []


def _read_text(repo: str, path: str) -> Optional[str]:
    try:
        with open(os.path.join(repo, path), encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return None


def utc_leg_tests(repo: str) -> Tuple[Optional[List[str]], List[str]]:
    """The utc leg's tests, as both leg scripts of UTC_LEG_SCRIPTS list them, and the problems:
    a script without exactly one such list, a list that does not parse (only quoted names in
    lit-legs.ps1, names quoted or bare in lit-legs.sh) or is empty, and lists that differ. The
    list is None when no script's list could be read; when both are read but differ, it is the
    names they share."""
    problems: List[str] = []
    lists: List[Tuple[str, int, List[str]]] = []
    for script, what, regex in UTC_LEG_SCRIPTS:
        text = _read_text(repo, script)
        if text is None:
            problems.append(f"utc leg: cannot read {script} (the list {what})")
            continue
        found = list(regex.finditer(text))
        if len(found) != 1:
            problems.append(f"utc leg: {script} has {len(found)} lists {what}; the check needs "
                            "exactly one")
            continue
        n = text.count("\n", 0, found[0].start()) + 1
        body = found[0].group(1)
        ps1 = script.endswith(".ps1")
        names, bad = [], []
        for tok in _UTC_TOKEN_RE.finditer(body):
            word = tok.group(0)
            if word == "," and ps1:
                continue                                 # the separator of @(...)
            quoted = tok.group(1) if tok.group(1) is not None else tok.group(2)
            name = word if quoted is None else quoted
            # In @(...) a bare word is a command; in bash a comma is part of a word.
            if (ps1 and quoted is None) or not UTC_NAME_RE.fullmatch(name):
                bad.append(word)
            else:
                names.append(name)
        rest = _UTC_TOKEN_RE.sub(" ", body).strip()     # an unterminated quote, for example
        if rest:
            bad.append(rest)
        if bad or not names:
            why = f"cannot parse {', '.join(repr(b) for b in bad)}" if bad else "it is empty"
            problems.append(f"utc leg: {script}:{n}: the list {what}: {why}; write test names "
                            "only, " + ("each quoted, separated by commas" if ps1
                                        else "separated by blanks"))
            continue
        lists.append((f"{script}:{n}", n, names))
    if len(lists) == 2 and set(lists[0][2]) != set(lists[1][2]):
        (a, _, la), (b, _, lb) = lists
        only_a = sorted(set(la) - set(lb))
        only_b = sorted(set(lb) - set(la))
        problems.append(f"utc leg: the two lists of its tests differ: only in {a}: "
                        f"{' '.join(only_a) or '-'}; only in {b}: {' '.join(only_b) or '-'}; "
                        "keep them equal")
    if not lists:
        return None, problems
    shared = set(lists[0][2])
    for _, _, names in lists[1:]:
        shared &= set(names)
    return sorted(shared), problems


# --------------------------------------------------------------------------- C/C++ source


_NEXT_LEXEME = re.compile(r"/[/*]|[\"']")
_RAW_PREFIXES = ("R", "LR", "uR", "UR", "u8R")


def strip_code(text: str) -> str:
    """Returns text with every comment and string or character literal (raw strings included)
    replaced by blanks; newlines stay, so offsets and line numbers do not change."""
    out = list(text)
    n = len(text)

    def blank(a: int, b: int) -> None:
        for k in range(a, min(b, n)):
            if out[k] != "\n":
                out[k] = " "

    def token_before(i: int, chars: str) -> str:
        k = i
        while k > 0 and (text[k - 1].isalnum() or text[k - 1] in chars):
            k -= 1
        return text[k:i]

    i = 0
    while True:
        m = _NEXT_LEXEME.search(text, i)
        if not m:
            break
        i = m.start()
        lexeme = m.group(0)
        if lexeme == "//":
            j = text.find("\n", i)
            while j > 0 and text[j - 1 - (text[j - 1] == "\r")] == "\\":   # '\' continues it
                j = text.find("\n", j + 1)
            j = n if j < 0 else j
        elif lexeme == "/*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
        elif lexeme == "'" and token_before(i, "_.'")[:1].isdigit():
            i += 1                                       # a digit separator: 0xFF'FF, 1'000
            continue
        elif lexeme == '"' and token_before(i, "_") in _RAW_PREFIXES:
            open_paren = text.find("(", i + 1)
            if open_paren < 0:
                j = n
            else:
                close = text.find(")" + text[i + 1:open_paren] + '"', open_paren)
                j = n if close < 0 else close + open_paren - i + 1
        else:                                            # "..." or '...', up to the line end
            j = i + 1
            while j < n and text[j] != lexeme and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n) if j < n and text[j] == lexeme else j
        blank(i, j)
        i = j
    return "".join(out)


def _switch_bodies(code: str) -> Optional[List[Tuple[int, int]]]:
    """The (open, close) brace offsets of every 'switch (...) {' body of stripped code, or None
    when the braces do not match."""
    stack: List[int] = []
    bodies: List[Tuple[int, int]] = []
    for m in re.finditer(r"[{}]", code):
        if m.group(0) == "{":
            stack.append(m.start())
            continue
        if not stack:
            return None
        o = stack.pop()
        k = o - 1
        while k >= 0 and code[k].isspace():
            k -= 1
        if k < 0 or code[k] != ")":
            continue
        depth = 0
        while k >= 0:
            depth += {")": 1, "(": -1}.get(code[k], 0)
            if depth == 0:
                break
            k -= 1
        if k <= 0:
            continue
        k -= 1
        while k >= 0 and code[k].isspace():
            k -= 1
        if code[max(k - 5, 0):k + 1] == "switch" and (k < 6 or not (code[k - 6].isalnum()
                                                                   or code[k - 6] == "_")):
            bodies.append((o, m.start()))
    return None if stack else bodies


class SwitchResult(NamedTuple):
    problems: List[str]
    xcoff: int                 # XCOFF case labels in code
    unqualified: int           # of them, the unqualified 'case XCOFF:' and 'case IsXCOFF:'


def switch_problems(path: str, text: str, default_covers: bool = False,
                    exempt_unqualified: bool = False) -> SwitchResult:
    """Per-switch coverage of one file (see --check). default_covers: the file has a
    switch-default line, so a 'default:' label of the same switch covers an XCOFF label as an
    MSIL case label does. exempt_unqualified: the file has a switch-other line, so its
    unqualified XCOFF labels need no cover."""
    code = strip_code(text)
    found = [(m.start(), " ".join(m.group(0).split())) for m in XCOFF_LABEL_RE.finditer(code)]
    unqualified = sum(1 for _, label in found if "::" not in label)
    xcoff = [(p, label) for p, label in found if "::" in label or not exempt_unqualified]
    if not xcoff:
        return SwitchResult([], len(found), unqualified)
    cover = [m.start() for m in MSIL_LABEL_RE.finditer(code)]
    if default_covers:
        cover += [m.start() for m in DEFAULT_LABEL_RE.finditer(code)]
    what = "MSIL case label or 'default:' label" if default_covers else "MSIL case label"

    def line(p: int) -> int:
        return code.count("\n", 0, p) + 1

    def fix(label: str) -> str:
        if default_covers:
            hint = "add an MSIL case (or 'default:') to that switch"
        else:
            hint = "add an MSIL case to that switch, or a switch-default line if it has 'default:'"
        if "::" not in label:
            hint += ", or a switch-other line if the label belongs to another enumeration"
        return hint

    bodies = _switch_bodies(code)

    def owner(p: int) -> Optional[int]:
        inner = [o for o, c in (bodies or []) if o < p < c]
        return max(inner) if inner else None

    problems = []
    if bodies is None:
        loose_x, loose_c, why = xcoff, cover, "the braces of the file do not match"
    else:
        covered = {owner(p) for p in cover}
        for p, label in xcoff:
            o = owner(p)
            if o is not None and o not in covered:
                problems.append(f"switch coverage: {path}:{line(p)}: '{label}' in the switch at "
                                f"line {line(o)} has no {what} in the same switch; {fix(label)}")
        loose_x = [(p, label) for p, label in xcoff if owner(p) is None]
        loose_c = [p for p in cover if owner(p) is None]
        why = "outside every switch body"
    if loose_x and len(loose_c) < len(loose_x):
        lines = ", ".join(str(line(p)) for p, _ in loose_x)
        problems.append(f"switch coverage: {path}:{line(loose_x[0][0])}: {len(loose_x)} XCOFF "
                        f"case label(s) (line(s) {lines}; {why}, so counted) but "
                        f"{len(loose_c)} {what}(s); add the missing MSIL cases"
                        + ("" if default_covers else ", or a switch-default line")
                        + ("" if exempt_unqualified else " or a switch-other line"))
    return SwitchResult(problems, len(found), unqualified)


def direct_includes(text: str) -> List[str]:
    """The paths of the #include directives of a C/C++ file, outside comments."""
    code = strip_code(text)
    return [m.group(2) for m in INCLUDE_RE.finditer(text) if code[m.start(1)] == "#"]


def compile_problems(m: Manifest, repo: str, compile_files: Sequence[str]) -> List[str]:
    """Every 'compile' line names a compile-leg file or a file one of them #includes directly,
    and every compile-leg file exists and has a hook block with a 'compile' line."""
    problems = []
    includes: Dict[str, List[str]] = {}
    for f in compile_files:
        text = _read_text(repo, f)
        if text is None:
            problems.append(f"compile-leg file {f} (COMPILE_FILES) does not exist")
            continue
        includes[f] = direct_includes(text)
        if f not in m.hooks or not m.hooks[f].compile:
            problems.append(f"compile-leg file {f} (COMPILE_FILES) has no hook block with a "
                            "'compile' line")
    for p, h in m.hooks.items():
        if not h.compile or p in compile_files:
            continue
        if p.lower().endswith(SOURCE_EXTS):
            problems.append(f"hook {p} (line {h.line}): 'compile', but the compile leg does not "
                            f"compile it (only {', '.join(compile_files)}); use lit, gtest, utc "
                            "or none")
        elif not any(p == i or p.endswith("/" + i) for inc in includes.values() for i in inc):
            problems.append(f"hook {p} (line {h.line}): 'compile', but no compile-leg file "
                            f"#includes it directly ({', '.join(compile_files)}); use lit, gtest, "
                            "utc or none")
    return problems


# --------------------------------------------------------------------------- --check


@dataclass
class CheckResult:
    problems: List[str]
    summary: str


def check(m: Manifest, repo: str, env: Mapping[str, str],
          compile_files: Sequence[str] = COMPILE_FILES) -> CheckResult:
    problems: List[str] = []
    try:
        base, why = resolve_base(repo, env)
    except GitError as e:
        return CheckResult([f"no upstream base: {e}"], "no upstream base")
    try:
        changed = changed_paths(repo, base, env)
        switches = switch_files(repo, env)
        changed_set = set(changed)
        hooked = [p for p in changed
                  if not p.startswith(FORK_PREFIXES) and p not in m.fork_only]
        unlisted = [p for p in hooked if p not in m.hooks]
        # A fork-only path, or an unlisted one, that the base has is an upstream file.
        upstream = {p for p in list(m.fork_only) + unlisted if in_base(repo, base, p, env)}
    except GitError as e:
        return CheckResult([str(e)], f"base {base[:12]} ({why})")
    hooked_set = set(hooked)

    for p in unlisted:
        if p in upstream:
            problems.append(f"unlisted hook: {p} differs from the upstream base; add a "
                            f"'hook {p}' block (the base {base[:12]} has it, so it is not "
                            "fork-only)")
        else:
            problems.append(f"unlisted hook: {p} differs from the upstream base; add a "
                            f"'hook {p}' block (or 'fork-only {p}' for a fork file: the base "
                            f"{base[:12]} lacks it)")
    for p, h in m.hooks.items():
        if p in m.fork_only:
            problems.append(f"hook {p} (line {h.line}) is also a fork-only line")
        elif p.startswith(FORK_PREFIXES):
            problems.append(f"stale hook block: {p} (line {h.line}) is under a fork-only prefix")
        elif p not in hooked_set:
            problems.append(f"stale hook block: {p} (line {h.line}) does not differ from the "
                            "upstream base")
    for p, n in m.fork_only.items():
        if p not in changed_set:
            problems.append(f"stale fork-only line: {p} (line {n}) does not differ from the "
                            "upstream base")
        elif p in upstream:
            problems.append(f"fork-only {p} (line {n}) exists in the upstream base {base[:12]}; "
                            f"it needs a hook block ('hook {p}' with its upstream tests)")

    inputs, lit_problems = expand_lit(m, repo)
    problems += lit_problems
    utc_tests, utc_problems = utc_leg_tests(repo)
    problems += utc_problems
    for p, h in m.hooks.items():
        if not h.has_kind():
            problems.append(f"hook {p} (line {h.line}): no lit, gtest, compile, utc or none line")
        for reason in h.none:
            if not reason:
                problems.append(f"hook {p}: 'none' needs a reason")
        for f in h.fork:
            if not os.path.isfile(os.path.join(repo, f)):
                problems.append(f"hook {p}: fork test '{f}' does not exist")
        for exe in h.gtest:
            if exe not in GTEST_BINARIES:
                problems.append(f"hook {p}: gtest '{exe}' is not a binary the gtest leg runs "
                                f"({', '.join(GTEST_BINARIES)})")
        for t in h.utc:
            if not os.path.isfile(os.path.join(repo, UTC_DIR, t + ".test")):
                problems.append(f"hook {p}: utc test '{t}' does not exist ({UTC_DIR}/{t}.test)")
            if utc_tests is not None and t not in utc_tests:
                problems.append(f"hook {p}: utc test '{t}' is not one of the utc leg's tests "
                                f"({' '.join(utc_tests)}: the lists of "
                                f"{' and '.join(s for s, _, _ in UTC_LEG_SCRIPTS)})")
    problems += compile_problems(m, repo, compile_files)
    problems += allow_problems(m, repo, inputs)

    switch_set = set(switches)
    unqualified: Dict[str, int] = {}
    for f in switches:
        text = _read_text(repo, f)
        if text is None:
            problems.append(f"switch coverage: cannot read {f}")
            continue
        r = switch_problems(f, text, default_covers=f in m.switch_default,
                            exempt_unqualified=f in m.switch_other)
        problems += r.problems
        unqualified[f] = r.unqualified
    for p, n in m.switch_default.items():
        if p not in switch_set:
            problems.append(f"stale switch-default line: {p} (line {n}) has no object-format "
                            "switch")
    for p, (n, reason) in m.switch_other.items():
        if p not in switch_set or unqualified.get(p) == 0:
            problems.append(f"stale switch-other line: {p} (line {n}) has no unqualified "
                            "'case XCOFF:' or 'case IsXCOFF:' label in code")
        if not reason:
            problems.append(f"switch-other {p} (line {n}): no reason")

    summary = (f"base {base[:12]} ({why}); {len(changed)} changed path(s), {len(hooked)} hooked, "
               f"{len(m.hooks)} hook block(s), {len(inputs)} lit input(s), "
               f"{len(switches)} switch file(s)")
    return CheckResult(problems, summary)


# --------------------------------------------------------------------------- --lit-verdict


def test_path(name: str) -> str:
    suite, sep, rel = name.partition(" :: ")
    if sep and suite in SUITE_ROOTS:
        return f"{SUITE_ROOTS[suite]}/{rel}"
    return name


@dataclass
class Verdict:
    problems: List[str]
    counts: Dict[str, int]
    n_pass: int
    min_pass: int


class ReportError(Exception):
    """The lit report cannot be read (exit 2)."""


def read_report(path: str) -> List[Tuple[str, str]]:
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
        tests = data["tests"]
        return [(test_path(t["name"]), t["code"]) for t in tests]
    except (OSError, ValueError, KeyError, TypeError) as e:
        raise ReportError(f"cannot read the lit JSON report {path}: {e!r}")


def lit_verdict(m: Manifest, repo: str, results: Sequence[Tuple[str, str]],
                min_pass: Optional[int]) -> Verdict:
    problems: List[str] = []
    inputs, _ = expand_lit(m, repo)
    problems += allow_problems(m, repo, inputs)
    allowed = {a.test for a in m.allow}
    counts: Dict[str, int] = {}
    seen: Dict[str, str] = {}
    for path, code in results:
        counts[code] = counts.get(code, 0) + 1
        seen[path] = code
        if code == "FAIL" and path not in allowed:
            problems.append(f"unexpected FAIL: {path} (not on the allow-list)")
        elif code == "PASS" and path in allowed:
            problems.append(f"stale allow entry: {path} passed; remove its allow line")
        elif code in ("UNRESOLVED", "TIMEOUT", "XPASS"):
            problems.append(f"unexpected {code}: {path}")
    for a in m.allow:
        if a.test not in seen:
            problems.append(f"allow '{a.test}' (line {a.line}): no result in the report")
    for i in inputs:
        if i not in seen:
            problems.append(f"no result for the lit input {i} (lit did not run it as a test)")
    n_pass = counts.get("PASS", 0)
    want = m.min_pass if min_pass is None else min_pass
    if n_pass < want:
        problems.append(f"{n_pass} PASS result(s), fewer than min-pass {want}")
    return Verdict(problems, counts, n_pass, want)


def format_counts(counts: Mapping[str, int]) -> str:
    return ", ".join(f"{k.lower()} {counts[k]}" for k in sorted(counts)) or "no results"


# --------------------------------------------------------------------------- self-test


def _rmtree(path: str) -> None:
    def retry(func, p, _exc):
        os.chmod(p, stat.S_IWRITE)
        func(p)
    if sys.version_info >= (3, 12):
        shutil.rmtree(path, onexc=retry)
    else:
        shutil.rmtree(path, onerror=retry)


def _write(root: str, files: Mapping[str, str]) -> None:
    for rel, text in files.items():
        full = os.path.join(root, rel)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)


# The case labels are assembled at run time, so this file never matches SWITCH_PATTERNS itself.
_X, _M = "XCOFF", "MSIL"
SELF_TEST_UPSTREAM = {
    "llvm/lib/Hooked.cpp": "int hooked() { return 0; }\n",
    "llvm/lib/Unchanged.cpp": "int unchanged() { return 0; }\n",
    "llvm/lib/Unchanged2.cpp": "int unchanged2() { return 0; }\n",
    "llvm/lib/Upstream.cpp": "int upstream() { return 0; }\n",
    f"{UTC_DIR}/x86-basic.test": "# x86-basic\n",
    f"{UTC_DIR}/asm-show-inst.test": "# asm-show-inst\n",
    f"{UTC_DIR}/arm-function-name.test": "# arm-function-name, which the utc leg does not run\n",
    "llvm/lib/Covered.cpp":
        f"int f(Triple::ObjectFormatType F) {{ switch (F) {{ case Triple::{_X}: return 1; }} }}\n",
    "llvm/lib/Default.cpp":
        f"int g(Triple::ObjectFormatType F) {{ switch (F) {{ case llvm::Triple::{_X}: return 1;\n"
        "  default: return 0; } }\n",
    "llvm/lib/Other.cpp": f"int h(Kind K) {{ switch (K) {{ case {_X}: return 1; }} }}\n",
    "polly/NotASwitch.cpp": f"int k(int K) {{ switch (K) {{ case {_X}::Foo: return 1; }} }}\n",
    "llvm/lib/CompileLeg.cpp": '#include "llvm/Inc/Included.h"\n'
                               '/*\n#include "llvm/Inc/Commented.h"\n*/\n'
                               "int compileLeg() { return 0; }\n",
    "llvm/include/llvm/Inc/Included.h": "int included();\n",
    "llvm/include/llvm/Inc/NotIncluded.h": "int notIncluded();\n",
    "llvm/include/llvm/Inc/Commented.h": "int commented();\n",
    "llvm/test/T/a.ll": "; a\n",
    "llvm/test/T/b.ll": "; b\n",
    "clang/test/X/c.c": "// c\n",
    "clang/test/X/notinput.c": "// not a lit input\n",
}
# Covered.cpp: qualified and unqualified MSIL labels, each in the switch of its XCOFF label; a
# nested switch; labels, braces and quotes in comments and literals, which must not count.
SELF_TEST_COVERED = f"""\
int f(Triple::ObjectFormatType F) {{
  switch (F) {{
  case Triple::{_X}: return 1;
  case llvm::Triple::{_M}: return 2;
  }}
}}
int g(MCContext::Environment E) {{
  // case Is{_X}: in a comment; '{{' and "{{" in literals; 1'000 and 0xFF'FF are numbers
  const char *S = "case Triple::{_X}: {{";
  char C = '{{';
  switch (E) {{
  case Is{_X}: {{ int K = 1'000 + 0xFF'FF; return K + C; }}
  case Is{_M}: return S[0];
  }}
}}
int h(Triple::ObjectFormatType F, int I) {{
  switch (F) {{
  case Triple::{_M}: return 4;
  case llvm::Triple::{_X}:
    switch (I) {{ case 0: return R"d(case Triple::{_X}: }})d"[0]; default: return 5; }}
  case MCContext::Is{_X}: {{ if (I) {{ case MCContext::Is{_M}: return 6; }} }}
  }}
  return 0;
}}
"""
SELF_TEST_FORK = {
    "llvm/lib/Hooked.cpp": "int hooked() { return 1; } // MSIL hook\n",
    "llvm/lib/Covered.cpp": SELF_TEST_COVERED,
    "llvm/lib/CompileLeg.cpp": '#include "llvm/Inc/Included.h"\n'
                               '/*\n#include "llvm/Inc/Commented.h"\n*/\n'
                               "int compileLeg() { return 1; } // MSIL hook\n",
    "llvm/include/llvm/Inc/Included.h": "int included(); // MSIL hook\n",
    "llvm/include/MSILOnly.h": "// a fork file in an upstream directory\n",
    "llvm/lib/Target/MSIL/Fork.cpp": "// under a fork-only prefix\n",
    "msil/test/fork-test.c": "// a fork test\n",
    # The utc leg's list, as lit-legs.ps1 (over two lines) and lit-legs.sh write it.
    "msil/utils/ci/lit-legs.ps1": "# the utc leg\n$utcTests = @('x86-basic',\n"
                                  "              \"asm-show-inst\")\n",
    "msil/utils/ci/lit-legs.sh": "# the utc leg\nutc_tests=(asm-show-inst 'x86-basic')\n",
}
# The compile leg's translation units in the self-test repository (COMPILE_FILES in the real one).
SELF_TEST_COMPILE = ("llvm/lib/CompileLeg.cpp",)
SELF_TEST_MANIFEST = """\
# self-test manifest
min-pass 2
fork-only llvm/include/MSILOnly.h
switch-default llvm/lib/Default.cpp
switch-other llvm/lib/Other.cpp its case belongs to Kind
allow clang/test/X/c.c needs llvm-foo, which the build lacks
hook llvm/lib/Hooked.cpp
  lit llvm/test/T/*.ll
  lit clang/test/X/c.c
  gtest unittests/MC/MCTests.exe
  fork msil/test/fork-test.c
  utc x86-basic asm-show-inst
hook llvm/lib/Covered.cpp
  none switch coverage only
hook llvm/lib/CompileLeg.cpp
  compile
hook llvm/include/llvm/Inc/Included.h
  compile
"""
# Planted switch files that must be reported (item (2), per switch). The value is the line of
# the uncovered XCOFF case.
SELF_TEST_SWITCH_DEFECTS = {
    # (a) two switches, one MSIL case: the second switch is uncovered.
    "lld/TwoSwitches.cpp": (
        f"int f(Triple::ObjectFormatType F) {{\n"
        f"  switch (F) {{ case Triple::{_X}: return 1; case Triple::{_M}: return 2; }}\n"
        f"}}\n"
        f"int g(Triple::ObjectFormatType F) {{\n"
        f"  switch (F) {{\n"
        f"  case Triple::{_X}: return 3;\n"
        f"  }}\n"
        f"}}\n", 6),
    # (b) one XCOFF-only switch; MSIL is named only in a comment.
    "lld/CommentOnly.cpp": (
        f"// TODO: Triple::{_M} is handled elsewhere\n"
        f"int f(Triple::ObjectFormatType F) {{\n"
        f"  switch (F) {{\n"
        f"  case llvm::Triple::{_X}: return 1;\n"
        f"  }}\n"
        f"}}\n", 4),
    # (c) MSIL in a comparison, not in the switch.
    "bolt/IfOnly.cpp": (
        f"int f(const Triple &T) {{\n"
        f"  if (T.getObjectFormat() == Triple::{_M})\n"
        f"    return 0;\n"
        f"  switch (T.getObjectFormat()) {{\n"
        f"  case Triple::{_X}: return 1;\n"
        f"  }}\n"
        f"}}\n", 5),
    # An MSIL case in a nested switch does not cover the outer one.
    "lld/Nested.cpp": (
        f"int f(MCContext::Environment E, Triple::ObjectFormatType F) {{\n"
        f"  switch (E) {{\n"
        f"  case MCContext::Is{_X}:\n"
        f"    switch (F) {{ case Triple::{_M}: return 1; default: return 2; }}\n"
        f"  }}\n"
        f"}}\n", 3),
    # An MSIL case label in a string literal does not count.
    "lld/InString.cpp": (
        f"const char *S = \"case Triple::{_M}:\";\n"
        f"int f(Triple::ObjectFormatType F) {{ switch (F) {{ case Triple::{_X}: return 1; }} }}\n",
        2),
}
# Planted switch files with a switch-default or switch-other line: the exemptions are per
# switch. file -> (text, its manifest line, the lines of the XCOFF cases that must be reported,
# the lines of those that must not be).
SELF_TEST_EXEMPT_DEFECTS = {
    # switch-default: the first switch ends in 'default:'; the second has none; in the third
    # the only 'default:' belongs to a nested switch; in the fourth the XCOFF switch is nested
    # in a switch with 'default:'.
    "lld/DefaultTwo.cpp": (
        "int f(Triple::ObjectFormatType F) {\n"                        # 1
        "  switch (F) {\n"                                             # 2
        f"  case Triple::{_X}: return 1;\n"                            # 3
        "  default: return 0;\n"                                       # 4
        "  }\n"                                                        # 5
        "}\n"                                                          # 6
        "int g(Triple::ObjectFormatType F) {\n"                        # 7
        "  switch (F) {\n"                                             # 8
        "  case Triple::ELF: return 2;\n"                              # 9
        f"  case Triple::{_X}: return 3;\n"                            # 10
        "  }\n"                                                        # 11
        "  return 0;\n"                                                # 12
        "}\n"                                                          # 13
        "int h(Triple::ObjectFormatType F, int I) {\n"                 # 14
        "  switch (F) {\n"                                             # 15
        f"  case Triple::{_X}:\n"                                      # 16
        "    switch (I) { default: return 4; }\n"                      # 17
        "  }\n"                                                        # 18
        "  switch (I) {\n"                                             # 19
        "  default:\n"                                                 # 20
        f"    switch (F) {{ case llvm::Triple::{_X}: return 5; }}\n"   # 21
        "  }\n"                                                        # 22
        "  return 0;\n"                                                # 23
        "}\n",                                                         # 24
        "switch-default lld/DefaultTwo.cpp", (10, 16, 21), (3,)),
    # switch-default on a file without any 'default:'.
    "lld/NoDefault.cpp": (
        "int g(MCContext::Environment E) {\n"
        "  switch (E) {\n"
        "  case MCContext::IsELF: return 2;\n"
        f"  case MCContext::Is{_X}: return 3;\n"
        "  }\n"
        "  return 0;\n"
        "}\n",
        "switch-default lld/NoDefault.cpp", (4,), ()),
    # switch-other: the unqualified label of another enumeration is exempt; a qualified one is
    # checked, also in a switch that ends in 'default:' (that takes a switch-default line), and
    # an MSIL case covers it.
    "lld/OtherQualified.cpp": (
        "int s(StringTableBuilder::Kind K) {\n"                        # 1
        "  switch (K) {\n"                                             # 2
        f"  case {_X}: return 4;\n"                                    # 3
        "  case ELF: return 1;\n"                                      # 4
        "  }\n"                                                        # 5
        "  return 0;\n"                                                # 6
        "}\n"                                                          # 7
        "int t(Triple::ObjectFormatType F) {\n"                        # 8
        "  switch (F) {\n"                                             # 9
        f"  case Triple::{_X}: return 1;\n"                            # 10
        "  default: return 0;\n"                                       # 11
        "  }\n"                                                        # 12
        "}\n"                                                          # 13
        "int u(MCContext::Environment E) {\n"                          # 14
        "  switch (E) {\n"                                             # 15
        f"  case MCContext::Is{_X}: return 1;\n"                       # 16
        f"  case MCContext::Is{_M}: return 2;\n"                       # 17
        "  }\n"                                                        # 18
        "  return 0;\n"                                                # 19
        "}\n",                                                         # 20
        "switch-other lld/OtherQualified.cpp its unqualified case belongs to Kind", (10,),
        (3, 16)),
}


def self_test() -> int:
    failures: List[str] = []

    def expect(cond: bool, what: str) -> None:
        print(("ok    " if cond else "FAIL  ") + what)
        if not cond:
            failures.append(what)

    def has(problems: Sequence[str], *needles: str) -> bool:
        return any(all(n in p for n in needles) for p in problems)

    tmp = tempfile.mkdtemp(prefix="check-upstream-hooks-")
    try:
        # A git configuration of its own: no system or user settings (autocrlf, hooks,
        # signing) reach the temporary repositories.
        gitconfig = os.path.join(tmp, "gitconfig")
        open(gitconfig, "w").close()
        env = {k: v for k, v in os.environ.items()
               if k != "MSIL_UPSTREAM_BASE" and not k.startswith("GIT_")}
        env.update({"GIT_CONFIG_NOSYSTEM": "1", "GIT_CONFIG_GLOBAL": gitconfig,
                    "GIT_AUTHOR_NAME": "self-test", "GIT_AUTHOR_EMAIL": "self-test@invalid",
                    "GIT_COMMITTER_NAME": "self-test",
                    "GIT_COMMITTER_EMAIL": "self-test@invalid",
                    "GIT_TERMINAL_PROMPT": "0"})
        repo = os.path.join(tmp, "repo")
        os.makedirs(repo)

        def g(*args: str) -> str:
            return git(repo, list(args), env)[1].strip()

        def chk(man: Manifest, e: Optional[Mapping[str, str]] = None,
                compile_files: Sequence[str] = SELF_TEST_COMPILE) -> CheckResult:
            return check(man, repo, env if e is None else e, compile_files)

        def run(manifest_text: str, name: str) -> Tuple[int, List[str]]:
            # run_check (the exit status of --check) on a manifest text, its output captured.
            _write(tmp, {name: manifest_text})
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = run_check(os.path.join(tmp, name), repo, env, SELF_TEST_COMPILE)
            lines = buf.getvalue().splitlines()
            for line in lines:
                print("      " + line)
            return rc, lines

        # Upstream A; the fork's commit; upstream B; the merge of B with the merge subject.
        g("init", "-q")
        g("checkout", "-q", "-b", "main")
        _write(repo, SELF_TEST_UPSTREAM)
        g("add", "-A")
        g("commit", "-q", "-m", "upstream A")
        g("branch", "upstream")
        _write(repo, SELF_TEST_FORK)
        g("add", "-A")
        g("commit", "-q", "-m", "[MSIL] fork work")
        g("checkout", "-q", "upstream")
        _write(repo, {"llvm/lib/FromB.cpp": "int fromB;\n"})
        g("add", "-A")
        g("commit", "-q", "-m", "upstream B")
        b = g("rev-parse", "HEAD")
        g("checkout", "-q", "main")
        g("merge", "-q", "--no-ff", "-m", MERGE_SUBJECT + b[:12], "upstream")

        man_path = os.path.join(tmp, "upstream-hooks.txt")
        _write(tmp, {"upstream-hooks.txt": SELF_TEST_MANIFEST})
        clean = read_manifest(man_path)

        # 1. The clean case.
        r = chk(clean)
        expect(r.problems == [], f"clean manifest passes {r.problems}")
        expect(r.summary.startswith(f"base {b[:12]} (second parent of "),
               f"base is the merged upstream commit ({r.summary})")
        inputs, unmatched = expand_lit(clean, repo)
        expect(inputs == ["clang/test/X/c.c", "llvm/test/T/a.ll", "llvm/test/T/b.ll"]
               and not unmatched, f"lit inputs expanded and sorted {inputs}")
        expect(direct_includes(SELF_TEST_FORK["llvm/lib/CompileLeg.cpp"])
               == ["llvm/Inc/Included.h"], "an #include inside a comment does not count")

        # 1a. 'compile' only on a compile-leg file or a header one of them #includes directly.
        # A lone 'compile' on a .cpp file the leg never compiles (as llvm/lib/MC/MCContext.cpp
        # would be) fails --check.
        lone_compile = (SELF_TEST_MANIFEST
                        .replace("allow clang/test/X/c.c needs llvm-foo, which the build lacks\n",
                                 "")
                        .replace("  lit llvm/test/T/*.ll\n  lit clang/test/X/c.c\n"
                                 "  gtest unittests/MC/MCTests.exe\n", "  compile\n"))
        rc, out = run(lone_compile, "compile-cpp.txt")
        fails = [line for line in out if line.startswith("FAIL ")]
        expect(rc == 1 and len(fails) == 1
               and has(fails, "hook llvm/lib/Hooked.cpp", "'compile', but the compile leg does "
                       "not compile it"),
               f"'compile' on a .cpp file outside the compile leg: exit 1 (exit {rc}, {fails})")
        rc, out = run(SELF_TEST_MANIFEST, "clean.txt")
        expect(rc == 0, f"--check of the clean manifest exits 0 (exit {rc})")
        r = chk(clean, compile_files=SELF_TEST_COMPILE + ("llvm/lib/Hooked.cpp",
                                                           "llvm/lib/NoSuchFile.cpp"))
        expect(has(r.problems, "compile-leg file llvm/lib/Hooked.cpp", "no hook block with a "
                   "'compile' line"), f"a compile-leg file without a 'compile' line {r.problems}")
        expect(has(r.problems, "compile-leg file llvm/lib/NoSuchFile.cpp", "does not exist"),
               f"a compile-leg file that does not exist {r.problems}")

        # 2. A merge in progress: the base is MERGE_HEAD, not the previous merge.
        g("checkout", "-q", "upstream")
        _write(repo, {"llvm/lib/FromC.cpp": "int fromC;\n"})
        g("add", "-A")
        g("commit", "-q", "-m", "upstream C")
        c = g("rev-parse", "HEAD")
        g("checkout", "-q", "main")
        g("merge", "-q", "--no-ff", "--no-commit", "upstream")
        r = chk(clean)
        expect(r.summary.startswith(f"base {c[:12]} (MERGE_HEAD"),
               f"MERGE_HEAD is the base during a merge ({r.summary})")
        expect(r.problems == [], f"clean manifest passes during the merge {r.problems}")
        g("merge", "--abort")

        # 3. MSIL_UPSTREAM_BASE wins.
        a = g("rev-list", "--max-parents=0", "HEAD")
        r = chk(clean, dict(env, MSIL_UPSTREAM_BASE=a))
        expect(r.summary.startswith(f"base {a[:12]} ($MSIL_UPSTREAM_BASE"),
               f"MSIL_UPSTREAM_BASE overrides ({r.summary})")
        r = chk(clean, dict(env, MSIL_UPSTREAM_BASE="no-such-commit"))
        expect(has(r.problems, "no upstream base", "MSIL_UPSTREAM_BASE"),
               f"a bad MSIL_UPSTREAM_BASE fails {r.problems}")

        # 3a. The utc leg's lists in the leg scripts: lists that differ, a list that does not
        # parse, an unterminated quote and no list fail; the restored scripts pass.
        ps1, sh = (s for s, _, _ in UTC_LEG_SCRIPTS)
        for what, files, needles in (
                ("lists that differ", {sh: "utc_tests=(asm-show-inst x86-basic x86-mir-only)\n"},
                 ("utc leg: the two lists of its tests differ", f"only in {ps1}:2: -;",
                  f"only in {sh}:1: x86-mir-only")),
                ("a list that does not parse", {ps1: "$utcTests = @(Get-UtcTests)\n"},
                 (f"utc leg: {ps1}:1: ", "cannot parse 'Get-UtcTests'")),
                ("an unterminated quote", {sh: "utc_tests=(x86-basic 'asm-show-inst)\n"},
                 (f"utc leg: {sh}:1: ", "cannot parse")),
                ("no list", {ps1: "# the utc leg moved\n"},
                 (f"utc leg: {ps1} has 0 lists",))):
            _write(repo, files)
            r = chk(clean)
            expect(has(r.problems, *needles), f"utc leg, {what}: reported {r.problems}")
            _write(repo, {s: SELF_TEST_FORK[s] for s in (ps1, sh)})
        r = chk(clean)
        expect(r.problems == [], f"utc leg scripts restored: clean {r.problems}")

        # 4. Planted defects.
        _write(repo, {
            "llvm/lib/Unchanged2.cpp": "int unchanged2() { return 2; }\n",   # modified, unlisted
            "llvm/lib/Untracked.cpp": "int untracked;\n",                     # new, unlisted
            "llvm/lib/Upstream.cpp": "int upstream() { return 2; }\n",       # fork-only below
            "lld/Qual.cpp": f"void q(Triple::ObjectFormatType F) {{ switch (F) {{\n"
                            f"  case llvm::Triple::{_X}: break; }} }}\n",
            "bolt/Unqual.cpp":
                f"void u(Environment E) {{ switch (E) {{ case Is{_X}: break; }} }}\n",
            "lld/OtherComment.cpp": f"// case {_X}: is only in a comment here\nint x;\n",
        })
        _write(repo, {f: text for f, (text, _) in SELF_TEST_SWITCH_DEFECTS.items()})
        _write(repo, {f: v[0] for f, v in SELF_TEST_EXEMPT_DEFECTS.items()})
        defects = SELF_TEST_MANIFEST.replace("allow clang/test/X/c.c needs llvm-foo, which the "
                                             "build lacks\n", "") + "".join(
            f"hook {f}\n  none planted\n"
            for f in list(SELF_TEST_SWITCH_DEFECTS) + list(SELF_TEST_EXEMPT_DEFECTS)) + "".join(
            v[1] + "\n" for v in SELF_TEST_EXEMPT_DEFECTS.values()) + """\
hook llvm/include/llvm/Inc/NotIncluded.h
  compile
hook llvm/include/llvm/Inc/Commented.h
  compile
fork-only llvm/lib/NotChanged.h
fork-only llvm/lib/Upstream.cpp
allow llvm/test/T/*.ll a glob
allow llvm/test/T a directory
allow llvm/test/T/a.ll
allow clang/test/X/notinput.c not a lit input
hook llvm/lib/Unchanged.cpp
  lit llvm/test/T/*.nomatch
  fork clang/test/MSIL/missing.c
  gtest unittests/Other/OtherTests.exe
  utc no-such-utc-test arm-function-name
hook lld/Qual.cpp
  none planted
hook bolt/Unqual.cpp
  none planted
hook lld/OtherComment.cpp
  none planted
hook llvm/lib/Empty.cpp
switch-default llvm/lib/Unchanged.cpp
switch-other lld/Qual.cpp its only label is qualified
switch-other lld/OtherComment.cpp its label is in a comment
"""
        _write(tmp, {"defects.txt": defects})
        r = chk(read_manifest(os.path.join(tmp, "defects.txt")))
        for p in r.problems:
            print("      reported: " + p)
        expect(has(r.problems, "unlisted hook: llvm/lib/Unchanged2.cpp", "so it is not fork-only")
               and not has(r.problems, "unlisted hook: llvm/lib/Unchanged2.cpp", "'fork-only"),
               "modified unlisted hook (the base has it: no fork-only offered)")
        expect(has(r.problems, "unlisted hook: llvm/lib/Untracked.cpp",
                   "or 'fork-only llvm/lib/Untracked.cpp' for a fork file"),
               "untracked unlisted hook (the base lacks it: fork-only offered)")
        expect(has(r.problems, "fork-only llvm/lib/Upstream.cpp (line ", "exists in the upstream "
                   f"base {b[:12]}; it needs a hook block")
               and not has(r.problems, "unlisted hook: llvm/lib/Upstream.cpp"),
               "fork-only line for a modified file of the upstream base")
        expect(has(r.problems, "stale hook block: llvm/lib/Unchanged.cpp"), "stale hook block")
        expect(has(r.problems, "stale hook block: llvm/lib/Empty.cpp"), "stale block (2)")
        expect(has(r.problems, "llvm/lib/Empty.cpp", "no lit, gtest, compile, utc or none"),
               "hook block without a test line")
        expect(has(r.problems, "stale fork-only line: llvm/lib/NotChanged.h"), "stale fork-only")
        expect(has(r.problems, "fork test 'clang/test/MSIL/missing.c' does not exist"),
               "missing fork test")
        expect(has(r.problems, "lit 'llvm/test/T/*.nomatch' matches no file"),
               "lit glob without a match")
        expect(has(r.problems, "gtest 'unittests/Other/OtherTests.exe'"), "unknown gtest binary")
        expect(has(r.problems, "utc test 'no-such-utc-test' does not exist")
               and has(r.problems, "utc test 'no-such-utc-test' is not one of the utc leg's "
                       "tests (asm-show-inst x86-basic"), "missing utc test")
        expect(has(r.problems, "hook llvm/lib/Unchanged.cpp: utc test 'arm-function-name' is not "
                   "one of the utc leg's tests")
               and not has(r.problems, "utc test 'arm-function-name' does not exist")
               and not has(r.problems, "utc test 'x86-basic'"),
               "a utc test outside the utc leg's list")
        expect(has(r.problems, "switch coverage: lld/Qual.cpp"), "qualified switch without MSIL")
        expect(has(r.problems, "switch coverage: bolt/Unqual.cpp"),
               "unqualified switch without MSIL")
        for f, (_, line) in SELF_TEST_SWITCH_DEFECTS.items():
            expect(has(r.problems, f"switch coverage: {f}:{line}: ", "no MSIL case label in the "
                       "same switch"), f"uncovered switch in {f} reported at line {line}")
        expect(not has(r.problems, "polly/NotASwitch.cpp"), "'case XCOFF::Foo:' is no switch")
        expect(not has(r.problems, "llvm/lib/Covered.cpp")
               and not has(r.problems, "llvm/lib/Default.cpp")
               and not has(r.problems, "llvm/lib/Other.cpp"),
               "MSIL case, switch-default and switch-other files pass")
        for f, (_, entry, bad_lines, good_lines) in SELF_TEST_EXEMPT_DEFECTS.items():
            for line in bad_lines:
                expect(has(r.problems, f"switch coverage: {f}:{line}: "),
                       f"'{entry.split(' ')[0]}' file {f}: uncovered XCOFF case at line {line} "
                       "reported")
            expect(not any(has(r.problems, f"switch coverage: {f}:{line}: ")
                           for line in good_lines)
                   and not has(r.problems, f"stale switch-default line: {f}")
                   and not has(r.problems, f"stale switch-other line: {f}"),
                   f"'{entry.split(' ')[0]}' file {f}: covered or exempt lines {good_lines} and "
                   "the line itself pass")
        expect(has(r.problems, "switch coverage: lld/DefaultTwo.cpp:10: ", "no MSIL case label "
                   "or 'default:' label in the same switch"),
               "switch-default: the message names 'default:'")
        expect(has(r.problems, "stale switch-default line: llvm/lib/Unchanged.cpp (line ",
                   "has no object-format switch"), "stale switch-default line")
        expect(has(r.problems, "stale switch-other line: lld/Qual.cpp (line ", "no unqualified")
               and has(r.problems, "switch coverage: lld/Qual.cpp:2: "),
               "stale switch-other line (a qualified label only, still checked)")
        expect(has(r.problems, "stale switch-other line: lld/OtherComment.cpp (line ",
                   "no unqualified"), "stale switch-other line (the label is in a comment)")
        expect(not has(r.problems, "hook llvm/lib/CompileLeg.cpp")
               and not has(r.problems, "compile-leg file llvm/lib/CompileLeg.cpp")
               and not has(r.problems, "hook llvm/include/llvm/Inc/Included.h"),
               "'compile' on the compile-leg file and on a header it #includes passes")
        for h in ("NotIncluded.h", "Commented.h"):
            expect(has(r.problems, f"hook llvm/include/llvm/Inc/{h}", "no compile-leg file "
                       "#includes it directly"), f"'compile' on a header nobody includes ({h})")
        expect(has(r.problems, "allow 'llvm/test/T/*.ll'", "glob"), "allow with a glob")
        expect(has(r.problems, "allow 'llvm/test/T'", "directory"), "allow with a directory")
        expect(has(r.problems, "allow 'llvm/test/T/a.ll'", "no reason"), "allow without a reason")
        expect(has(r.problems, "allow 'clang/test/X/notinput.c'", "not one of the lit inputs"),
               "allow of a test that is not a lit input")

        # 5. No merge commit at all (as in a shallow clone): a failure that names the reason.
        lone = os.path.join(tmp, "lone")
        os.makedirs(lone)
        git(lone, ["init", "-q"], env)
        _write(lone, {"README": "x\n"})
        git(lone, ["add", "-A"], env)
        git(lone, ["commit", "-q", "-m", "only"], env)
        r = check(clean, lone, env, SELF_TEST_COMPILE)
        expect(has(r.problems, "no upstream base", "no first-parent merge commit"),
               f"no base without a merge commit {r.problems}")

        # 6. Malformed manifests (exit 2).
        for text, what in (("min-pass 1\nfrobnicate x\n", "unknown entry"),
                           ("min-pass 1\n  lit x.ll\n", "outside a hook block"),
                           ("hook a.cpp\n  none r\n", "no 'min-pass"),
                           ("min-pass 1\nhook a\\b.cpp\n  none r\n", "repository-relative"),
                           ("min-pass 1\nhook a.cpp\n  none r\nhook a.cpp\n  none r\n",
                            "second hook block")):
            try:
                parse_manifest(text, "bad")
                expect(False, f"malformed manifest rejected: {what}")
            except ManifestError as e:
                expect(what in str(e), f"malformed manifest rejected: {what}")

        # 7. Lit verdicts of synthetic reports (manifest: three inputs, c.c allow-listed,
        # min-pass 2).
        def report(codes: Mapping[str, str]) -> List[Tuple[str, str]]:
            names = {"a": "LLVM :: T/a.ll", "b": "LLVM :: T/b.ll", "c": "Clang :: X/c.c"}
            path = os.path.join(tmp, "report.json")
            with open(path, "w", encoding="utf-8") as f:
                json.dump({"__version__": [0], "elapsed": 1.0,
                           "tests": [{"name": names[k], "code": v, "output": "",
                                      "elapsed": 0.1} for k, v in codes.items()]}, f)
            return read_report(path)

        cases = (
            ({"a": "PASS", "b": "PASS", "c": "FAIL"}, None, None),
            ({"a": "FAIL", "b": "PASS", "c": "FAIL"}, None, "unexpected FAIL: llvm/test/T/a.ll"),
            ({"a": "PASS", "b": "PASS", "c": "PASS"}, None, "stale allow entry: clang/test/X/c.c"),
            ({"a": "UNRESOLVED", "b": "PASS", "c": "FAIL"}, None,
             "unexpected UNRESOLVED: llvm/test/T/a.ll"),
            ({"a": "PASS", "b": "XPASS", "c": "FAIL"}, None, "unexpected XPASS: llvm/test/T/b.ll"),
            ({"a": "TIMEOUT", "b": "PASS", "c": "FAIL"}, None,
             "unexpected TIMEOUT: llvm/test/T/a.ll"),
            ({"a": "PASS", "b": "UNSUPPORTED", "c": "FAIL"}, None, "fewer than min-pass 2"),
            ({"a": "XFAIL", "b": "PASS", "c": "FAIL"}, None, "fewer than min-pass 2"),
            ({"a": "PASS", "b": "PASS"}, None, "no result for the lit input clang/test/X/c.c"),
            ({"a": "PASS", "b": "PASS", "c": "FAIL"}, 3, "fewer than min-pass 3"),
            ({"a": "PASS", "b": "UNSUPPORTED", "c": "FAIL"}, 1, None),
        )
        for codes, override, needle in cases:
            v = lit_verdict(clean, repo, report(codes), override)
            what = f"lit verdict {codes} min-pass {override}: " + (
                "PASS" if needle is None else f"FAIL with '{needle}'")
            if needle is None:
                expect(v.problems == [], f"{what} {v.problems}")
            else:
                expect(has(v.problems, needle), f"{what} {v.problems}")
        try:
            read_report(os.path.join(tmp, "missing.json"))
            expect(False, "a missing report is an error")
        except ReportError:
            expect(True, "a missing report is an error")

        # 8. strip_code, switch attribution and the counting fallback.
        sample = ('a = "}\\"{"; b = \'{\'; c = 1\'000 + 0xFF\'FF; // }\n'
                  'd = R"x()"{)x"; /* { */ e = u8"{"; f = L\'}\'; switch (g) { }\n')
        code = strip_code(sample)
        expect(len(code) == len(sample) and code.count("\n") == 2
               and code.count("{") == 1 and code.count("}") == 1
               and "c = 1'000 + 0xFF'FF;" in code and "switch (g) { }" in code,
               f"strip_code blanks comments and literals only {code!r}")
        covered_code = strip_code(SELF_TEST_COVERED)
        bodies = _switch_bodies(covered_code)
        labels = [mm.start() for mm in XCOFF_LABEL_RE.finditer(covered_code)]
        expect(bodies is not None and len(bodies) == 4 and len(labels) == 4
               and all(any(o < p < c for o, c in bodies) for p in labels),
               f"Covered.cpp: 4 switch bodies, every XCOFF label inside one ({bodies}, {labels})")
        unbalanced = (f"#if A\nswitch (F) {{\n#else\nswitch (G) {{\n#endif\n"
                      f"case Triple::{_X}: return 1;\n}}\n")
        sr = switch_problems("u.cpp", unbalanced)
        expect(sr.xcoff == 1 and has(sr.problems, "switch coverage: u.cpp:6: ", "braces of the "
                                     "file do not match"),
               f"unmatched braces: labels counted {sr.problems}")
        sr = switch_problems("u.cpp", unbalanced.replace(
            "return 1;", f"return 1;\ncase Triple::{_M}: return 2;"))
        expect(sr.problems == [], f"unmatched braces, as many MSIL as XCOFF labels {sr.problems}")
        with_default = unbalanced.replace("return 1;", "return 1;\ndefault: return 2;")
        sr = switch_problems("u.cpp", with_default, default_covers=True)
        expect(sr.problems == [], f"unmatched braces, switch-default: 'default:' counted "
                                  f"{sr.problems}")
        sr = switch_problems("u.cpp", with_default)
        expect(has(sr.problems, "switch coverage: u.cpp:6: "),
               f"unmatched braces: 'default:' counts only with switch-default {sr.problems}")
        sr = switch_problems("u.cpp", unbalanced, default_covers=True)
        expect(has(sr.problems, "switch coverage: u.cpp:6: ", "0 MSIL case label or 'default:' "
                   "label(s)"), f"unmatched braces, switch-default, no 'default:' {sr.problems}")
        unq = unbalanced.replace(f"Triple::{_X}", _X)
        sr = switch_problems("u.cpp", unq, exempt_unqualified=True)
        expect(sr.problems == [] and sr.xcoff == 1 and sr.unqualified == 1,
               f"unmatched braces, switch-other: the unqualified label is exempt {sr}")
        sr = switch_problems("u.cpp", unbalanced, exempt_unqualified=True)
        expect(has(sr.problems, "switch coverage: u.cpp:6: ") and sr.unqualified == 0,
               f"unmatched braces, switch-other: a qualified label is counted {sr}")
        macro = f"#define CASES case Triple::{_X}: case Triple::{_M}:\n"
        expect(switch_problems("m.h", macro).problems == [],
               "labels outside a switch body (a macro) are counted")
        expect(has(switch_problems("m.h", f"#define CASES case Triple::{_X}:\n").problems,
                   "switch coverage: m.h:1: ", "outside every switch body"),
               "an XCOFF label outside a switch body without an MSIL one")
    except GitError as e:
        expect(False, f"git: {e}")
    finally:
        _rmtree(tmp)
    expect(not os.path.exists(tmp), "temporary directory removed")
    print(f"check-upstream-hooks self-test: {'PASS' if not failures else 'FAIL'} "
          f"({len(failures)} failure(s))")
    return 0 if not failures else 1


# --------------------------------------------------------------------------- main


def main(argv: Sequence[str]) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    default_repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__)))))
    ap = argparse.ArgumentParser(
        prog="check-upstream-hooks.py", description=__doc__.split("\n\n")[0],
        usage="check-upstream-hooks.py [--manifest FILE] [--repo DIR] (--check | --list-lit | "
              "--list-compile | --lit-verdict REPORT [--min-pass N] | --self-test)")
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="the manifest against the repository")
    mode.add_argument("--list-lit", action="store_true", help="print the lit inputs")
    mode.add_argument("--list-compile", action="store_true",
                      help="print the compile leg's translation units")
    mode.add_argument("--lit-verdict", metavar="REPORT", help="judge a lit JSON report")
    mode.add_argument("--self-test", action="store_true", help="run the self-test")
    ap.add_argument("--min-pass", type=int, help="override the manifest's min-pass")
    ap.add_argument("--manifest", help=f"manifest (default <repo>/{MANIFEST_RELPATH})")
    ap.add_argument("--repo", default=default_repo, help="repository root (default: %(default)s)")
    args = ap.parse_args(argv)
    if args.min_pass is not None and not args.lit_verdict:
        ap.error("--min-pass needs --lit-verdict")
    if args.min_pass is not None and args.min_pass < 0:
        ap.error("--min-pass must not be negative")
    if args.self_test:
        if args.manifest:
            ap.error("--self-test takes no --manifest")
        return self_test()
    if args.list_compile:
        for f in COMPILE_FILES:
            print(f)
        return 0

    repo = os.path.abspath(args.repo)
    if args.check:
        return run_check(args.manifest or os.path.join(repo, MANIFEST_RELPATH), repo, os.environ)
    try:
        m = read_manifest(args.manifest or os.path.join(repo, MANIFEST_RELPATH))
    except ManifestError as e:
        print(f"check-upstream-hooks: malformed manifest:\n{e}")
        return 2

    if args.list_lit:
        inputs, problems = expand_lit(m, repo)
        for i in inputs:
            print(i)
        for p in problems:
            print(f"check-upstream-hooks --list-lit: {p}", file=sys.stderr)
        return 1 if problems or not inputs else 0

    if args.lit_verdict:
        try:
            results = read_report(args.lit_verdict)
        except ReportError as e:
            print(f"check-upstream-hooks: {e}")
            return 2
        v = lit_verdict(m, repo, results, args.min_pass)
        for p in v.problems:
            print(f"FAIL {p}")
        print(f"check-upstream-hooks --lit-verdict: {'OK' if not v.problems else 'FAIL'}: "
              f"{v.n_pass} passed (min-pass {v.min_pass}); {format_counts(v.counts)}; "
              f"{len(v.problems)} problem(s)")
        return 1 if v.problems else 0
    raise AssertionError("no mode")


def run_check(manifest: str, repo: str, env: Mapping[str, str],
              compile_files: Sequence[str] = COMPILE_FILES) -> int:
    """--check: prints one line per problem and the summary; returns the exit status."""
    try:
        m = read_manifest(manifest)
    except ManifestError as e:
        print(f"check-upstream-hooks: malformed manifest:\n{e}")
        return 2
    r = check(m, repo, env, compile_files)
    for p in r.problems:
        print(f"FAIL {p}")
    print(f"check-upstream-hooks --check: {'OK' if not r.problems else 'FAIL'}: {r.summary}; "
          f"{len(r.problems)} problem(s)")
    return 1 if r.problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

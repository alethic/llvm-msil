#!/usr/bin/env python3
# ===-- check-il-forms-diff.py - Test edits are Q26 forms or nomangle ----===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Check that the edits of FileCheck tests are mechanical (plan M18, 5.12.4).

    python msil/utils/check-il-forms-diff.py [--repo DIR] [--base REV] PATH...
    python msil/utils/check-il-forms-diff.py --self-test

M18 changes the IL that '-S', llc and '--lto-emit-asm' print only by the
encoding forms of plan Q26 (short branches, the shortest 'ldc.i4' forms) and
by 'nomangle' in every 'pinvokeimpl' (5.10.11), and its tests may change
only to follow. This script reads 'git diff -U0 REV' (default HEAD) of the
working tree under each PATH (repository-relative, or absolute inside the
repository given by --repo, default the one that holds this script) and
accepts a changed file only when it is a modified .ll, .c, .s, .test, .mir or
.expected file whose every hunk passes. A hunk passes when its changed lines
fall into these kinds:

  * forms: the lines it removes and the lines it adds, taken in order, pair
    up one to one and each pair is equal after il-snapshot.py's
    '--normalize forms' (normalize_forms(): short branches become long,
    'ldc.i4.m1', 'ldc.i4.<n>' and 'ldc.i4.s <n>' become 'ldc.i4 <n>',
    'nomangle cdecl' becomes 'cdecl'), applied to CHECK patterns too, and
    applied in escaped spelling inside FileCheck's '{{...}}' regular
    expressions (normalize_check_line()): 'ldc\\.i4\\.s <n>', 'ldc\\.i4\\.m1'
    and 'ldc\\.i4\\.<0-8>' become 'ldc\\.i4 <n>'; a '.s' or '\\.s' suffix
    right after a branch mnemonic, or right after a group '(...)' or a
    '{{...}}' block that lists only branch mnemonics ('{{brtrue}}.s',
    '(brfalse|brtrue)\\.s'), is dropped; an optional-suffix group '(\\.s)?'
    is dropped, a whole '{{(\\.s)?}}' block with it. A rewritten mnemonic
    stands alone: no word character, '.' or '\\' right before it, and the
    end of the block, a blank, '$', ')', '|' or '[' right after it (never a
    quantifier). Nothing else in a regular expression (labels, other
    mnemonics, operands) and nothing in a '[[...]]' block is rewritten;
  * implicit-check-not: a RUN or DEFINE line (a lit 'RUN:' or 'DEFINE:'
    keyword) that differs from its old line only by removed
    '--implicit-check-not=<pattern>' arguments (one shell word each, with
    the blanks on one side of it) whose pattern names a short form (as for
    CHECK-NOT below, or a '.s' or '\\.s' suffix right after ']', ')', '}',
    '+', '*' or '?' of a regular expression, such as Phase 1's
    '{{^ +b[a-z.]+\\.s }}') or 'nomangle' as a whole word, the rest of the
    line unchanged; a last continuation line (no trailing '\\') that holds
    nothing but its keyword and one such argument may go with the argument,
    when the line before it (the same keyword) is changed in the same hunk
    and loses only its trailing ' \\';
  * prose: a comment line (its first non-blank characters are a comment
    leader of the part it is in: ';' in .ll and .def, ';' or '#' in .mir,
    '//' in .c, .h and .il, '#', '//' or ';' in .s, '#' in .test and .py,
    the extension before '.expected' for an .expected file; a split-file
    part, a line '<c>--- <name>' or '//--- <name>', takes the leaders of
    its name's extension, and a part of another kind has none) that holds
    neither a
    lit keyword (RUN:, REQUIRES:, UNSUPPORTED:, XFAIL:, ALLOW_RETRIES:,
    DEFINE:, REDEFINE:, END., UTC_ARGS:) nor a FileCheck directive (CHECK or
    a prefix that a --check-prefix or --check-prefixes option anywhere in
    either version of the file names, with or without -NEXT, -SAME, -NOT,
    -DAG, -LABEL, -EMPTY or -COUNT-<n>, and any upper-case name with one of
    those suffixes), anywhere in the line, as lit and FileCheck find them;
    split-file markers are not prose, and neither are executed comment
    lines (next item);
  * executed comment lines: a comment line that a RUN or DEFINE line of the
    file (either version; a REDEFINE line counts as a DEFINE line) reads
    as program text is not prose. That is a line that starts, also after
    blanks, with a string that the command quotes ('...' or "...", also
    one inside the other) and that holds '%s', the command being a RUN or
    DEFINE line with its continuation lines joined and every '%{name}' in
    it replaced by the values that DEFINE lines give name (switch.ll's
    '%{jtsize}' executes every line of open(r'%s') that
    startswith(';PY ')). Such a line passes only paired one to one with a
    line of the other version that is equal after normalize_program():
    '--normalize forms' extended for program text, where a quoted short
    branch mnemonic ('br.s', "blt.s") becomes the long one and then, when
    it equals the quoted literal right before it in the same parenthesized
    or bracketed list, is dropped with its comma (so ('br', 'br.s', 'ret')
    equals ('br', 'ret')); it is never set aside as a removed CHECK-NOT
    line or a continuation line. A line whose program text, after the
    string and blanks, is a comment of the program's language ('#' when
    the command runs %python or python; no other language is known) stays
    prose, as a program comment line;
  * a removed CHECK-NOT line (a directive '<prefix>-NOT:') whose pattern
    names a short form ('br.s' ... 'leave.s', 'ldc.i4.m1', 'ldc.i4.<n>',
    'ldc.i4.s', also spelled as a regular expression such as 'ldc\\.i4\\.s'
    or '(lt|ge)\\.s') or 'nomangle', which pinned the absence of what M18
    now prints.

The prose lines and those CHECK-NOT removals are set aside first, and the
other lines must then pair up, each pair as forms, as implicit-check-not
or, when one of its lines is an executed comment line, as program text: a
removed CHECK-NOT line is set aside, and a removed last continuation line
joined to the line before it, only when the hunk does not pass without.
Every other change fails: a file added (also an untracked one) or deleted,
a file of another kind, a binary or mode-only change, a hunk with other
lines.

Output: 'FAIL: <file>: ...' with the hunk for each failure, then one line
per PATH ('<path>: <n> files changed, <h> hunks: <p> line pairs equal after
--normalize forms, <c> comment lines, <r> CHECK-NOT lines removed, <i>
--implicit-check-not arguments removed, <x> line pairs equal only with
regex blocks normalized, <e> executed comment line pairs, <k> program
comment lines; <f> failures', where the <x> and <e> pairs are some of the
<p> and the <k> lines some of the <c>) and a total. Exit status: 0 when every change passes (also when
nothing changed), 1 when one fails, 2 on a usage error (an unknown option, a
directory that is not in a git work tree, a REV that is not a commit, a PATH
outside the repository or in neither REV nor the working tree) or when git
fails.

--self-test builds a temporary git repository and checks every rule above,
each comment leader, an lld-style .test with '# CHECK:' lines and
split-file parts, the failing changes (for the regex-block and
implicit-check-not rules: a label, a '[[...]]' variable, a non-branch
mnemonic or a quantified suffix changed in a regular expression, an
'--implicit-check-not' that names anything else removed, one added, a
removal with another change of the line, and continuation lines removed
the wrong way), switch.ll-style executed comment lines (a tuple that
gains 'br.s' and a reworded program comment pass; the reachability test
replaced by 'if True:', the marker replaced by "over = ''", any other
change of a program line, a short mnemonic whose long form is not the
literal before it, a removed program line and a change of a program that
a RUN line reaches through a '%{name}' fail) and the exit statuses; it
prints
'ok: <case>' or 'FAIL: <case>: <why>' and exits 0 or 1. Python 3 standard
library only (il-snapshot.py and the msil-run.py it loads are too).
"""

import argparse
import contextlib
import importlib.util
import io
import itertools
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = "check-il-forms-diff"


def _load_il_snapshot():
    """il-snapshot.py as a module, for normalize_forms() (one definition of
    '--normalize forms'), without a __pycache__ in the source tree."""
    saved, sys.dont_write_bytecode = sys.dont_write_bytecode, True
    try:
        spec = importlib.util.spec_from_file_location(
            "il_snapshot", os.path.join(HERE, "il-snapshot.py")
        )
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
    finally:
        sys.dont_write_bytecode = saved
    return module


_IL_SNAPSHOT = _load_il_snapshot()
normalize_forms = _IL_SNAPSHOT.normalize_forms
normalize_plain = _IL_SNAPSHOT.normalize_plain
FILECHECK_BLOCK_RE = _IL_SNAPSHOT.FILECHECK_BLOCK_RE

# The kinds of files whose hunks are read (plan 5.12.4: .ll, .c, .s, .test
# and .expected; .mir too, whose tests can check IL).
TEST_EXTS = (".ll", ".c", ".s", ".test", ".expected", ".mir")
# Comment leaders by the extension of a file or split-file part. A part of
# another kind (.txt, .yaml, .rsp, ...) has none, so no line of it is prose.
LEADERS = {
    ".ll": (";",),
    ".mir": (";", "#"),
    ".c": ("//",),
    ".h": ("//",),
    ".s": ("#", "//", ";"),
    ".il": ("//",),
    ".test": ("#",),
    ".py": ("#",),
    ".def": (";",),
}
LIT_KEYWORD_RE = re.compile(
    r"(?<![\w-])(?:(?:RUN|REQUIRES|UNSUPPORTED|XFAIL|ALLOW_RETRIES|DEFINE|"
    r"REDEFINE|UTC_ARGS):|END\.)"
)
CHECK_SUFFIX = r"(?:-(?:NEXT|SAME|NOT|DAG|LABEL|EMPTY|COUNT-\d+))"
# Any upper-case name with a FileCheck suffix is a directive, declared or not.
SUFFIXED_DIRECTIVE_RE = re.compile(
    r"(?<![\w-])[A-Z][A-Z0-9_]*(?:-[A-Z0-9_]+)*?" + CHECK_SUFFIX + r":"
)
PREFIX_OPTION_RE = re.compile(r"-check-prefix(?:es)?(?:=|\s+)([A-Za-z0-9_,-]+)")
# split-file's part separator (llvm/utils/split-file/split-file.cpp): '//'
# or one other character, then '--- ' and the part name.
SPLIT_MARKER_RE = re.compile(r"^(?://|[^/\n])--- (\S(?:.*\S)?)$")
# A pattern that names a short form or 'nomangle' (plain or as a regular
# expression, where '.' may be written '\.').
SHORT_FORM_ALTERNATIVES = (
    r"(?<![\w.])(?:br|brfalse|brtrue|beq|bge|bgt|ble|blt|bne\\?\.un|bge\\?\.un|"
    r"bgt\\?\.un|ble\\?\.un|blt\\?\.un|leave)\\?\.s(?![\w.])",
    r"\)\\?\.s(?![\w.])",
    r"(?<![\w.])ldc\\?\.i4\\?\.(?:[0-8]|[mM]1|s)(?![\w.])",
)
SHORT_FORM_NAME_RE = re.compile("|".join(SHORT_FORM_ALTERNATIVES + ("nomangle",)))
# The pattern of a removed '--implicit-check-not' argument: the same short
# forms, a '.s' suffix of a regular-expression part ('b[a-z.]+\.s'), or
# 'nomangle' as a whole word.
ICN_SHORT_FORM_RE = re.compile(
    "|".join(
        SHORT_FORM_ALTERNATIVES
        + (r"[\]})+*?]\\?\.s(?![\w.])", r"(?<![\w.])nomangle(?![\w.])")
    )
)
HUNK_RE = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")
MAX_HUNK_LINES = 20  # lines of a failing hunk that are printed

# --- forms inside FileCheck regular expressions ('{{...}}'), in escaped
# spelling. Q26's branch mnemonics, '.' plain or escaped.
BRANCH_MNEMONICS = (
    "brfalse",
    "brtrue",
    "bne.un",
    "bge.un",
    "bgt.un",
    "ble.un",
    "blt.un",
    "leave",
    "beq",
    "bge",
    "bgt",
    "ble",
    "blt",
    "br",
)
RX_BRANCH = "(?:%s)" % "|".join(m.replace(".", r"\\?\.") for m in BRANCH_MNEMONICS)
RX_BRANCH_LIST = r"%s(?:\|%s)*" % (RX_BRANCH, RX_BRANCH)
# A block or group that lists only branch mnemonics: 'a|b' or '(a|b)'.
ONLY_BRANCHES_RE = re.compile(r"%s|\(%s\)" % (RX_BRANCH_LIST, RX_BRANCH_LIST))
# A mnemonic stands alone: nothing of a name or an escape before it, and
# after it the end of the block, a blank, '$', ')', '|' or a bracket
# expression, never a quantifier or more of a name.
RX_BEFORE = r"(?<![\w.\\])"
RX_AFTER = r"(?=\Z|[\s$)|\[])"
RX_OPTIONAL_S_RE = re.compile(r"\(\\\.s\)\?")
RX_LDC_S_RE = re.compile(RX_BEFORE + r"ldc\\\.i4\\\.s" + RX_AFTER)
RX_LDC_M1_RE = re.compile(RX_BEFORE + r"ldc\\\.i4\\\.[mM]1" + RX_AFTER)
RX_LDC_N_RE = re.compile(RX_BEFORE + r"ldc\\\.i4\\\.([0-8])" + RX_AFTER)
RX_GROUP_S_RE = re.compile(r"(\(%s\))\\?\.s" % RX_BRANCH_LIST + RX_AFTER)
RX_BRANCH_S_RE = re.compile(RX_BEFORE + "(" + RX_BRANCH + r")\\?\.s" + RX_AFTER)
# The '.s' that starts the plain text right after a '{{...}}' block that
# lists only branch mnemonics ('{{brtrue}}.s @BB...').
PLAIN_S_AFTER_BLOCK_RE = re.compile(r"\.s(?!\S)")


def normalize_regex(body):
    """The body of a '{{...}}' block with Q26's forms rewritten in escaped
    spelling (the module docstring lists the rewrites)."""
    body = RX_OPTIONAL_S_RE.sub("", body)
    body = RX_LDC_S_RE.sub(r"ldc\\.i4", body)
    body = RX_LDC_M1_RE.sub(r"ldc\\.i4 -1", body)
    body = RX_LDC_N_RE.sub(r"ldc\\.i4 \1", body)
    body = RX_GROUP_S_RE.sub(r"\1", body)
    return RX_BRANCH_S_RE.sub(r"\1", body)


def normalize_check_line(text):
    """normalize_forms() of a line, extended to the '{{...}}' blocks: their
    bodies get normalize_regex(), a whole '{{(\\.s)?}}' block is dropped
    (the plain texts around it join), and a '.s' right after a block that
    lists only branch mnemonics is dropped. '[[...]]' blocks stay."""
    pieces, pos = [], 0
    for m in FILECHECK_BLOCK_RE.finditer(text):
        pieces.append((False, text[pos : m.start()]))
        block = m.group(0)
        if not (block.startswith("{{") and RX_OPTIONAL_S_RE.fullmatch(block[2:-2])):
            pieces.append((True, block))
        pos = m.end()
    pieces.append((False, text[pos:]))
    out, plain, after_branches = [], [], False

    def flush():
        seg = "".join(plain)
        del plain[:]
        if after_branches and PLAIN_S_AFTER_BLOCK_RE.match(seg):
            seg = seg[2:]
        out.append(normalize_plain(seg))

    for is_block, piece in pieces:
        if not is_block:
            plain.append(piece)
            continue
        flush()
        if piece.startswith("{{"):
            body = normalize_regex(piece[2:-2])
            out.append("{{" + body + "}}")
            after_branches = bool(ONLY_BRANCHES_RE.fullmatch(body))
        else:
            out.append(piece)
            after_branches = False
    flush()
    return "".join(out)


# --- '--implicit-check-not' arguments of RUN and DEFINE lines.
RUN_DEFINE_RE = re.compile(r"(?<![\w-])(RUN|DEFINE):")
# One shell word as lit splits a command line: quoted parts, escapes and
# other characters up to a blank or a shell operator.
SHELL_WORD_RE = re.compile(r"""(?:"(?:[^"\\]|\\.)*"|'[^']*'|\\.|[^\s"'\\|;&<>])+""")
ICN_PREFIX_RE = re.compile(r"--?implicit-check-not=")
MAX_ICN_ARGS = 8  # short-form arguments on one line that removal tries


def icn_args(line):
    """[(start, end)] of the words after the line's RUN: or DEFINE: keyword
    that are '--implicit-check-not=<pattern>' arguments whose pattern names
    a short form or 'nomangle'; [] when the line has no keyword or an
    unbalanced quote."""
    kw = RUN_DEFINE_RE.search(line)
    if not kw:
        return []
    spans, pos = [], kw.end()
    while pos < len(line):
        if line[pos].isspace():
            pos += 1
            continue
        m = SHELL_WORD_RE.match(line, pos)
        if not m:
            if line[pos] in "\"'":
                return []
            pos += 1  # a shell operator, or a lone '\' at the end
            continue
        word = m.group(0)
        prefix = ICN_PREFIX_RE.match(word)
        if prefix:
            pattern = re.sub(r"[\"']", "", word[prefix.end() :])
            if ICN_SHORT_FORM_RE.search(pattern):
                spans.append((m.start(), m.end()))
        pos = m.end()
    return spans


def icn_removed(old, new):
    """The fewest short-form '--implicit-check-not' arguments of the RUN or
    DEFINE line old whose removal, each with the blanks on one side of it,
    gives new exactly; None when no such removal does."""
    spans = icn_args(old)
    if not spans or len(spans) > MAX_ICN_ARGS:
        return None
    options = []
    for start, end in spans:
        cuts = [None]
        before = start
        while before > 0 and old[before - 1] in " \t":
            before -= 1
        if before < start:
            cuts.append((before, end))
        after = end
        while after < len(old) and old[after] in " \t":
            after += 1
        if after > end:
            cuts.append((start, after))
        options.append(cuts)
    best = None
    for combo in itertools.product(*options):
        cuts = [c for c in combo if c]
        if not cuts or any(a[1] > b[0] for a, b in zip(cuts, cuts[1:])):
            continue
        parts, last = [], 0
        for start, end in cuts:
            parts.append(old[last:start])
            last = end
        parts.append(old[last:])
        if "".join(parts) == new and (best is None or len(cuts) < best):
            best = len(cuts)
    return best


def alone_icn_keyword(line):
    """The keyword of a last continuation line that holds nothing but its
    RUN: or DEFINE: keyword and one short-form '--implicit-check-not'
    argument (no trailing '\\'), else None."""
    kw = RUN_DEFINE_RE.search(line)
    spans = icn_args(line)
    if not kw or len(spans) != 1:
        return None
    start, end = spans[0]
    if line[kw.end() : start].strip() or line[end:].strip():
        return None
    return kw.group(1)


def join_continuations(lines, executed=()):
    """[(text, joined, False)] of the removed lines [(line number, text)]
    with every last continuation line that alone_icn_keyword() accepts
    joined to the removed line right before it (the same keyword, ending in
    ' \\'), which then stands without its ' \\' and joined=1; None when no
    line joins. Lines whose numbers are in executed (executed comment
    lines) never join, and their items are (text, 0, True)."""
    items, numbers, any_joined = [], [], False
    for n, text in lines:
        kw = None if n in executed else alone_icn_keyword(text)
        if kw and items and numbers[-1] == n - 1 and not items[-1][1]:
            prev = items[-1][0]
            prev_kw = RUN_DEFINE_RE.search(prev)
            if (
                not items[-1][2]
                and prev_kw
                and prev_kw.group(1) == kw
                and prev.endswith(" \\")
            ):
                items[-1] = (prev[:-2], 1, False)
                any_joined = True
                continue
        items.append((text, 0, n in executed))
        numbers.append(n)
    return items if any_joined else None


# --- comment lines that a RUN or DEFINE line executes as program text.
# A command line (lit's RUN, DEFINE and REDEFINE keywords) and its text.
COMMAND_RE = re.compile(r"(?<![\w-])(RUN|DEFINE|REDEFINE):(.*)$")
# DEFINE's '%{name} = value', and a use of a '%{name}' substitution.
SUBST_DEF_RE = re.compile(r"^\s*(%\{[^{}\s]+\})\s*=(.*)$")
SUBST_USE_RE = re.compile(r"%\{[^{}\s]+\}")
MAX_SUBST_ROUNDS = 4  # substitutions inside substitutions that are replaced
QUOTED_RES = (
    re.compile(r"'((?:[^'\\]|\\.)*)'"),
    re.compile(r'"((?:[^"\\]|\\.)*)"'),
)
MAX_QUOTE_DEPTH = 3  # quotes inside quotes that are read
# The comment leaders of a program by the language the command runs it in
# (a '%python' substitution or a 'python' command word).
PROGRAM_LANGUAGES = ((re.compile(r"(?:^|[\s|;&(])%?python3?(?:\.exe)?(?=\s)"), ("#",)),)
# In program text: a quoted short branch mnemonic, and any quoted literal.
QUOTED_SHORT_BRANCH_RE = re.compile(
    r"""(['"])(br|brfalse|brtrue|beq|bge|bgt|ble|blt|bne\.un|bge\.un|"""
    r"""bgt\.un|ble\.un|blt\.un|leave)\.s\1"""
)
PROGRAM_LITERAL_RE = re.compile(r"""'(?:[^'\\]|\\.)*'|"(?:[^"\\]|\\.)*\"""")
LIST_SEPARATOR_RE = re.compile(r"\s*,\s*")


def quoted_strings(text, depth=MAX_QUOTE_DEPTH):
    """The strings that text quotes ('...' or "...") and that are not
    blank, as written and with backslash escapes resolved, and the strings
    they quote in turn."""
    found = set()
    for rx in QUOTED_RES:
        for m in rx.finditer(text):
            inner = m.group(1)
            for s in {inner, re.sub(r"\\(.)", r"\1", inner)}:
                if s.strip():
                    found.add(s)
                if depth:
                    found |= quoted_strings(s, depth - 1)
    return found


def program_prefixes(versions):
    """{prefix: comment leaders of the program's language} of the strings
    that a RUN or DEFINE command of the file versions quotes next to a
    '%s': each command with its continuation lines joined and every
    '%{name}' replaced by the values that DEFINE lines give name. A prefix
    that several commands quote keeps only the leaders they all have."""
    commands = []  # [kind, text]
    for version in versions:
        pending = {}  # kind -> the index of a command that continues
        for line in version.lines:
            m = COMMAND_RE.search(line)
            if not m:
                continue
            kind = "RUN" if m.group(1) == "RUN" else "DEFINE"
            text = m.group(2).strip()
            i = pending.pop(kind, None)
            if i is None:
                commands.append([kind, text])
                i = len(commands) - 1
            else:
                commands[i][1] += " " + text
            if commands[i][1].endswith("\\"):
                commands[i][1] = commands[i][1][:-1].rstrip()
                pending[kind] = i
    defs, texts = {}, []
    for kind, text in commands:
        d = SUBST_DEF_RE.match(text) if kind == "DEFINE" else None
        if d:
            defs.setdefault(d.group(1), []).append(d.group(2).strip())
            text = d.group(2)
        texts.append(text)
    prefixes = {}
    for text in texts:
        for _ in range(MAX_SUBST_ROUNDS):
            expanded = SUBST_USE_RE.sub(
                lambda u: " ".join(defs.get(u.group(0), [u.group(0)])), text
            )
            if expanded == text:
                break
            text = expanded
        if "%s" not in text:
            continue
        leaders = ()
        for rx, lang_leaders in PROGRAM_LANGUAGES:
            if rx.search(text):
                leaders = lang_leaders
        for s in quoted_strings(text):
            if s in prefixes:
                prefixes[s] = tuple(l for l in prefixes[s] if l in leaders)
            else:
                prefixes[s] = leaders
    return prefixes


def normalize_program(text):
    """normalize_forms() of an executed comment line, extended for program
    text: a quoted short branch mnemonic becomes the long one, and is then
    dropped with the comma before it when it equals the quoted literal
    right before it in the same parenthesized or bracketed list."""
    text = normalize_forms(text)
    out, last, prev = [], 0, None  # prev: (literal, start, end)
    for m in PROGRAM_LITERAL_RE.finditer(text):
        lit = m.group(0)
        short = QUOTED_SHORT_BRANCH_RE.fullmatch(lit)
        if short:
            lit = short.group(1) + short.group(2) + short.group(1)
            if (
                prev is not None
                and prev[0] == lit
                and LIST_SEPARATOR_RE.fullmatch(text, prev[2], m.start())
                and text[: prev[1]].rstrip().endswith(("(", "[", ","))
                and text[m.end() :].lstrip().startswith((",", ")", "]"))
            ):
                last = m.end()
                continue
        out.append(text[last : m.start()])
        out.append(lit)
        last = m.end()
        prev = (lit, m.start(), m.end())
    out.append(text[last:])
    return "".join(out)


class UsageError(Exception):
    """A usage error or a git failure: exit status 2."""


def git(repo, args, input_bytes=None):
    """stdout of 'git -C repo <args>' as bytes; UsageError when git fails."""
    try:
        res = subprocess.run(
            ["git", "-C", repo, "-c", "core.quotepath=off", "--literal-pathspecs"]
            + args,
            input=input_bytes,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except OSError as e:
        raise UsageError("cannot run git: %s" % e)
    if res.returncode != 0:
        raise UsageError(
            "git %s failed (status %d): %s"
            % (
                " ".join(args),
                res.returncode,
                res.stderr.decode("utf-8", "replace").strip(),
            )
        )
    return res.stdout


def text_lines(data):
    """The lines of file bytes, decoded, without their line endings."""
    text = data.decode("utf-8", errors="surrogateescape")
    if text.endswith("\n"):
        text = text[:-1]
    return [l[:-1] if l.endswith("\r") else l for l in text.split("\n")] if text else []


def kind_of(name):
    """The extension that picks the leaders of a file or part name: the
    lower-case extension, the one before it for '.expected'."""
    base, ext = os.path.splitext(name.lower())
    if ext == ".expected":
        ext = os.path.splitext(base)[1]
    return ext


class FileVersion:
    """One version (base or working tree) of a test file: its lines, the
    leaders of each line's part, and which lines are split-file markers."""

    def __init__(self, path, data):
        self.lines = text_lines(data)
        self.markers = set()
        self.leaders = []
        current = LEADERS.get(kind_of(path), ())
        for i, line in enumerate(self.lines):
            m = SPLIT_MARKER_RE.match(line)
            if m:
                self.markers.add(i + 1)
                current = LEADERS.get(kind_of(m.group(1)), ())
            self.leaders.append(current)

    def leaders_at(self, lineno):
        return self.leaders[lineno - 1] if 0 < lineno <= len(self.leaders) else ()


class Classifier:
    """Sorts the changed lines of one file into prose, CHECK-NOT lines that
    name a short form, and other lines."""

    def __init__(self, old, new):
        self.old, self.new = old, new
        prefixes = {"CHECK"}
        for version in (old, new):
            for line in version.lines:
                for m in PREFIX_OPTION_RE.finditer(line):
                    prefixes.update(p for p in m.group(1).split(",") if p)
        alternatives = "|".join(
            re.escape(p) for p in sorted(prefixes, key=lambda p: (-len(p), p))
        )
        self.directive_re = re.compile(
            r"(?<![\w-])(?:%s)%s?:" % (alternatives, CHECK_SUFFIX)
        )
        # A CHECK-NOT line of a declared prefix, or of any upper-case name.
        self.not_re = re.compile(
            r"(?<![\w-])(?:%s|[A-Z][A-Z0-9_]*(?:-[A-Z0-9_]+)*?)-NOT:(.*)$"
            % alternatives
        )
        # The strings that a RUN or DEFINE line of either version executes
        # the lines that start with (switch.ll's ';PY ').
        self.programs = program_prefixes((old, new))

    def is_directive(self, line):
        return bool(
            LIT_KEYWORD_RE.search(line)
            or self.directive_re.search(line)
            or SUFFIXED_DIRECTIVE_RE.search(line)
        )

    def is_comment(self, line, version, lineno):
        if lineno in version.markers:
            return False
        stripped = line.lstrip()
        return any(stripped.startswith(l) for l in version.leaders_at(lineno))

    def program_comment(self, line):
        """None when no RUN or DEFINE line reads line as program text, else
        whether its program text is a comment of the program's language for
        every prefix that it starts with (also after blanks)."""
        found = None
        for text in {line, line.lstrip()}:
            for prefix, leaders in self.programs.items():
                if text.startswith(prefix):
                    rest = text[len(prefix) :].lstrip()
                    ok = bool(leaders) and rest.startswith(tuple(leaders))
                    found = ok and found is not False
        return found

    def is_executed(self, line, version, lineno):
        """A comment line that a RUN or DEFINE line executes as program
        text, other than a comment of the program's language."""
        return (
            self.is_comment(line, version, lineno)
            and self.program_comment(line) is False
        )

    def is_prose(self, line, version, lineno):
        if not self.is_comment(line, version, lineno):
            return False
        if self.program_comment(line) is False:
            return False
        return not self.is_directive(line)

    def is_short_form_check_not(self, line):
        m = self.not_re.search(line)
        return bool(m and SHORT_FORM_NAME_RE.search(m.group(1)))


class Stats:
    def __init__(self):
        self.files = self.hunks = self.pairs = self.prose = self.check_not = 0
        self.icn = self.regex = self.executed = self.program = self.failures = 0


def pair_up(items, added):
    """Pairs the removed items [(text, joined, executed)] with the added
    lines [(text, executed)] in order: (forms pairs, of them regex-block
    pairs, --implicit-check-not arguments removed, executed comment line
    pairs) when every pair passes, else the reason (None when the numbers
    of lines differ)."""
    if len(items) != len(added):
        return None
    forms = regex = icn = executed = 0
    for i, ((r, joined, r_executed), (a, a_executed)) in enumerate(zip(items, added)):
        if r_executed or a_executed:
            # An executed comment line: program text, never a RUN line's
            # removed argument.
            nr, na = normalize_program(r), normalize_program(a)
            if nr != na:
                return (
                    "line pair %d differs after --normalize forms for program "
                    "text (an executed comment line):\n  - %s\n  + %s" % (i + 1, nr, na)
                )
            forms += 1
            executed += 1
            continue
        if joined:
            # The line before a removed last continuation line: only its
            # trailing ' \' goes.
            if r != a:
                return (
                    "line pair %d differs: the line before a removed last "
                    "continuation line may lose only its trailing ' \\':\n"
                    "  - %s \\\n  + %s" % (i + 1, r, a)
                )
            icn += joined
            continue
        nr, na = normalize_check_line(r), normalize_check_line(a)
        if nr == na:
            forms += 1
            regex += normalize_forms(r) != normalize_forms(a)
            continue
        removed = icn_removed(r, a)
        if not removed:
            return "line pair %d differs after --normalize forms:\n  - %s\n  + %s" % (
                i + 1,
                nr,
                na,
            )
        icn += removed
    return (forms, regex, icn, executed)


def check_hunk(cls, removed, added, stats):
    """None when the hunk passes (and its counts are added to stats), else
    the reason. removed and added are [(line number, text)]."""
    prose_r = [cls.is_prose(t, cls.old, n) for n, t in removed]
    prose_a = [cls.is_prose(t, cls.new, n) for n, t in added]
    prose = sum(prose_r) + sum(prose_a)
    program = sum(
        cls.program_comment(t) is True
        for (_, t), p in list(zip(removed, prose_r)) + list(zip(added, prose_a))
        if p
    )
    rest_r = [(n, t) for (n, t), p in zip(removed, prose_r) if not p]
    rest_a = [
        (t, cls.is_executed(t, cls.new, n))
        for (n, t), p in zip(added, prose_a)
        if not p
    ]
    executed = {n for n, t in rest_r if cls.is_executed(t, cls.old, n)}
    # The removed CHECK-NOT lines that name a short form are set aside, and
    # the removed last continuation lines joined, only when the hunk does
    # not pass without; an executed comment line never is.
    kept = [
        (n, t) for n, t in rest_r if n in executed or not cls.is_short_form_check_not(t)
    ]
    variants = []
    for lines in (rest_r, kept) if len(kept) != len(rest_r) else (rest_r,):
        variants.append((lines, [(t, 0, n in executed) for n, t in lines]))
        joined = join_continuations(lines, executed)
        if joined is not None:
            variants.append((lines, joined))
    reason = None
    for lines, items in variants:
        result = pair_up(items, rest_a)
        if isinstance(result, tuple):
            forms, regex, icn, executed_pairs = result
            stats.pairs += forms
            stats.regex += regex
            stats.icn += icn
            stats.executed += executed_pairs
            stats.prose += prose
            stats.program += program
            stats.check_not += len(rest_r) - len(lines)
            return None
        reason = reason or result
    if reason:
        return reason
    return (
        "%d other line(s) removed and %d added (comment prose, removed CHECK-NOT "
        "lines that name a short form and removed last continuation lines that "
        "hold only a short-form --implicit-check-not set aside), so they cannot "
        "pair up" % (len(variants[-1][1]), len(rest_a))
    )


def parse_hunks(patch):
    """[(header, [(old line, text)], [(new line, text)])] of the 'git diff
    -U0' patch of one file."""
    hunks = []
    cur = None
    for raw in patch.split(b"\n"):
        line = raw.decode("utf-8", errors="surrogateescape")
        if line.endswith("\r"):
            line = line[:-1]
        m = HUNK_RE.match(line)
        if m:
            cur = (line, [], [], [int(m.group(1))], [int(m.group(3))])
            hunks.append(cur)
        elif cur is None:
            continue
        elif line.startswith("-"):
            cur[1].append((cur[3][0], line[1:]))
            cur[3][0] += 1
        elif line.startswith("+"):
            cur[2].append((cur[4][0], line[1:]))
            cur[4][0] += 1
    return [(h, r, a) for h, r, a, _, _ in hunks]


def split_patch(data):
    """{path: patch} of a multi-file 'git diff' of modified files. A file
    whose 'diff --git a/<p> b/<p>' header git quoted (an unusual name) is
    left out."""
    sections, cur = [], None
    for line in data.split(b"\n"):
        if line.startswith(b"diff --git "):
            cur = [line]
            sections.append(cur)
        elif cur is not None:
            cur.append(line)
    result = {}
    for s in sections:
        rest = s[0][len(b"diff --git ") :].decode("utf-8", "surrogateescape")
        n = (len(rest) - 5) // 2
        path = rest[2 : 2 + n]
        if n > 0 and rest == "a/" + path + " b/" + path:
            result[path] = b"\n".join(s)
    return result


def chunks(paths, limit=16000):
    """paths in groups whose total length stays below a command line's."""
    group, size = [], 0
    for p in paths:
        if group and size + len(p) + 1 > limit:
            yield group
            group, size = [], 0
        group.append(p)
        size += len(p) + 1
    if group:
        yield group


DIFF_ARGS = ["diff", "-U0", "--no-color", "--no-ext-diff", "--text", "--no-renames"]


class Checker:
    """Checks the changes under the repository-relative paths against base;
    git runs a fixed number of times whatever the number of files."""

    def __init__(self, repo, base, paths, out):
        self.repo, self.base, self.out = repo, base, out
        self.paths = paths
        self.stats = {p: Stats() for p in paths}
        self.failed = 0

    def say(self, text):
        self.out.write(text + "\n")

    def owner(self, path):
        for p in self.paths:
            if p == "" or path == p or path.startswith(p + "/"):
                return p
        return self.paths[0]

    def fail(self, path, text, lines=()):
        self.failed += 1
        self.stats[self.owner(path)].failures += 1
        self.say("FAIL: %s: %s" % (path, text))
        lines = list(lines)
        for l in lines[:MAX_HUNK_LINES]:
            self.say("    " + l)
        if len(lines) > MAX_HUNK_LINES:
            self.say("    ... (%d more lines)" % (len(lines) - MAX_HUNK_LINES))

    def changed_files(self):
        """[(status, path)] of every change under the paths: git diff's
        name-status against the base, and '?' for an untracked file."""
        specs = [p or "." for p in self.paths]
        out = git(
            self.repo,
            ["diff", "--name-status", "-z", "--no-renames", self.base, "--"] + specs,
        )
        fields = [f.decode("utf-8", "surrogateescape") for f in out.split(b"\0") if f]
        changes = list(zip(fields[0::2], fields[1::2]))
        untracked = git(
            self.repo,
            ["ls-files", "-z", "--others", "--exclude-standard", "--"] + specs,
        )
        for f in untracked.split(b"\0"):
            if f:
                changes.append(("?", f.decode("utf-8", "surrogateescape")))
        return sorted(changes, key=lambda c: c[1])

    def patches(self, files):
        """{path: 'git diff -U0' patch} of the modified files."""
        result = {}
        for group in chunks(files):
            result.update(
                split_patch(git(self.repo, DIFF_ARGS + [self.base, "--"] + group))
            )
        for f in files:
            if f not in result:  # a name that git quotes: a run of its own
                result[f] = git(self.repo, DIFF_ARGS + [self.base, "--", f])
        return result

    def base_contents(self, files):
        """{path: bytes} of the files in the base (one 'git cat-file --batch')."""
        if not files:
            return {}
        request = "".join("%s:%s\n" % (self.base, f) for f in files)
        data = git(
            self.repo,
            ["cat-file", "--batch"],
            request.encode("utf-8", "surrogateescape"),
        )
        result, pos = {}, 0
        for f in files:
            nl = data.find(b"\n", pos)
            header = data[pos:nl].split() if nl >= 0 else []
            if len(header) != 3 or header[1] != b"blob":
                raise UsageError("git cat-file found no blob %s:%s" % (self.base, f))
            start = nl + 1
            result[f] = data[start : start + int(header[2])]
            pos = start + int(header[2]) + 1
        return result

    def check_file(self, path, patch, base_data):
        stats = self.stats[self.owner(path)]
        hunks = parse_hunks(patch)
        if not hunks:
            self.fail(path, "changed without a text hunk (a mode or binary change)")
            return
        with open(os.path.join(self.repo, *path.split("/")), "rb") as f:
            new = FileVersion(path, f.read())
        cls = Classifier(FileVersion(path, base_data), new)
        for header, removed, added in hunks:
            stats.hunks += 1
            why = check_hunk(cls, removed, added, stats)
            if why:
                shown = ["-" + t for _, t in removed] + ["+" + t for _, t in added]
                self.fail(path, "%s: %s" % (header, why), [header] + shown)

    def run(self):
        changes = self.changed_files()
        readable = [p for s, p in changes if s == "M" and p.endswith(TEST_EXTS)]
        patches = self.patches(readable)
        bases = self.base_contents(readable)
        for status, path in changes:
            self.stats[self.owner(path)].files += 1
            if status in ("A", "?"):
                what = "untracked" if status == "?" else "added"
                self.fail(path, "%s file: M18 adds no test files" % what)
            elif status == "D":
                self.fail(path, "deleted file: M18 deletes no test files")
            elif status != "M":
                self.fail(path, "change of kind %s (not a text modification)" % status)
            elif not path.endswith(TEST_EXTS):
                self.fail(
                    path,
                    "not a %s file, whose changes this script reads; it may not change"
                    % ", ".join(TEST_EXTS),
                )
            else:
                self.check_file(path, patches[path], bases[path])
        total = Stats()
        for p in self.paths:
            s = self.stats[p]
            self.say(
                "%s: %s: %d files changed, %d hunks: %d line pairs equal after "
                "--normalize forms, %d comment lines, %d CHECK-NOT lines removed, "
                "%d --implicit-check-not arguments removed, %d line pairs equal "
                "only with regex blocks normalized, %d executed comment line "
                "pairs, %d program comment lines; %d failures"
                % (
                    SCRIPT,
                    p or ".",
                    s.files,
                    s.hunks,
                    s.pairs,
                    s.prose,
                    s.check_not,
                    s.icn,
                    s.regex,
                    s.executed,
                    s.program,
                    s.failures,
                )
            )
            for k in ("files", "hunks", "failures"):
                setattr(total, k, getattr(total, k) + getattr(s, k))
        self.say(
            "%s: %s against %s: %d files changed, %d hunks, %d failures"
            % (
                SCRIPT,
                "FAIL" if self.failed else "PASS",
                self.base,
                total.files,
                total.hunks,
                self.failed,
            )
        )
        return 1 if self.failed else 0


def resolve(repo_arg, base, paths):
    """(repository root, repository-relative paths); raises UsageError."""
    repo = os.path.abspath(repo_arg or HERE)
    if not os.path.isdir(repo):
        raise UsageError("--repo %s is not a directory" % repo)
    top = git(repo, ["rev-parse", "--show-toplevel"]).decode("utf-8").strip()
    top = os.path.normpath(top)
    try:
        git(top, ["rev-parse", "--verify", "--quiet", base + "^{commit}"])
    except UsageError:
        raise UsageError("--base %s is not a commit of %s" % (base, top))
    rel_paths = []
    for p in paths:
        full = os.path.abspath(p if os.path.isabs(p) else os.path.join(top, p))
        try:
            rel = os.path.relpath(full, top)
        except ValueError:  # another drive
            rel = None
        if rel is None or rel == os.pardir or rel.startswith(os.pardir + os.sep):
            raise UsageError("%s is not inside the repository %s" % (p, top))
        rel = "" if rel == "." else rel.replace(os.sep, "/").rstrip("/")
        if not os.path.exists(full):
            listed = git(top, ["ls-tree", "--name-only", base, "--", rel or "."])
            if not listed.strip():
                raise UsageError(
                    "%s exists neither in the working tree nor in %s" % (p, base)
                )
        if rel not in rel_paths:
            rel_paths.append(rel)
    return top, rel_paths


def check(repo_arg, base, paths, out=None):
    """Run the check; returns the exit status (0, 1 or 2)."""
    out = out or sys.stdout
    try:
        repo, rel = resolve(repo_arg, base, paths)
        return Checker(repo, base, rel, out).run()
    except UsageError as e:
        sys.stderr.write("%s: error: %s\n" % (SCRIPT, e))
        return 2


# ------------------------------------------------------------------ self-test

# The base version of each case directory of the self-test repository: an
# llc test (';'), a clang test ('//', with a preprocessor line), an
# lld-style .test ('#', split-file parts of three kinds), an
# update_llc_test_checks .expected file and a file of another kind.
BASE_FILES = {
    "llvm/a.ll": """\
; Long branches only: every branch is 'br', never a short form.
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil %s -o - \\
; RUN:   | FileCheck %s --check-prefixes=CHECK,LNX
; CHECK-LABEL: .method {{.*}}'f'(
; CHECK:       ldc.i4 5
; CHECK-NEXT:  ldc.i4 -1
; CHECK-NEXT:  ldc.i4 100
; CHECK-NEXT:  br @BB0_2
; CHECK-NEXT:  blt.un @BB0_1
; CHECK:       {{^}}  br {{@BB[0-9_]+}}{{$}}
; LNX:         .method public hidebysig static pinvokeimpl("libc" cdecl) int32 'puts'(native int)
; LNX:         pinvokeimpl("libm.so.6" as "sqrtf" cdecl)
; CHECK-NOT:   br.s
; LNX-NOT:     {{ldc\\.i4\\.s 16|ldc\\.i4\\.8}}
; CHECK-NOT:   call
; NOTE: a prose line that starts with an upper-case word.
define i32 @f() {
  ret i32 0
}
""",
    "clang/b.c": """\
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -S %s -o - | FileCheck %s
// The constants print in their long forms.
#define VALUE 7
// CHECK: ldc.i4 7
int f(void) { return VALUE; } // CHECK: ret
""",
    "lld/c.test": """\
# The P/Invokes of the linked text.
# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: msil-ld --lto-emit-asm main.o -o out.il
# RUN: FileCheck %s --check-prefix IL --input-file=out.il --implicit-check-not=nomangle
# IL: pinvokeimpl("libfoo.so.1" cdecl) int32 'foo'(int32)
# IL: br @BB0_1
# IL-NONE-NOT: nomangle

#--- main.ll
; The main module.
define i32 @main() {
  ret i32 0
}
#--- lib.c
// The library.
#define X 1
int foo(int x) { return x + X; }
#--- syms.txt
# a data line
foo
""",
    "llvm/d.ll.expected": """\
; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc < %s | FileCheck %s
; CHECK:    ldc.i4 0
; CHECK-NEXT:    br @BB0_1
""",
    "lld/e.yaml": "--- !ELF\nFileHeader:\n  Class: ELFCLASS64\n",
    # Forms inside regular expressions, and '--implicit-check-not' pins of
    # short forms on DEFINE and RUN lines (with continuation lines).
    "llvm/f.ll": r"""; Regular expressions and the pins of the long forms.
; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 \
; DEFINE:   --implicit-check-not="{{^ +b[a-z.]+\.s }}"
; RUN: llc -O0 %s -o - | %{check} --implicit-check-not=nomangle
; RUN: llc -O2 %s -o - \
; RUN:   | FileCheck %s --implicit-check-not="{{^ +b[a-z.]+\.s }}" --implicit-check-not=throw \
; RUN:   --implicit-check-not=call
; CHECK:       {{^}}  {{beq|brfalse}} @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}  {{(brfalse|brtrue) @BB[0-9]+_[0-9]+$}}
; CHECK:       {{^  ldloc\.0[[:space:]]+ldc\.i4 8[[:space:]]+add$}}
; CHECK:       {{^  ldloc\.1[[:space:]]+ldc\.i4 24[[:space:]]+add$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4(\.s)? 8$}}
; CHECK:       {{^}}  bge @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; CHECK:       {{^}}  {{ldarg}} {{[0-9]+$}}
; CHECK:       {{^  ldarg 1[[:space:]]+ret$}}
; CHECK:       {{^}}  {{ldc\.i4 -1|brtrue @BB0_1}}{{$}}
; CHECK:       {{^}}  {{bne\.un|brtrue}} @BB{{[0-9_]+$}}
; CHECK:       {{^  mybr @BB0_2$}}
; Continuation lines that may not go with their argument.
; RUN: llc -O1 %s -o - | %{check} \
; DEFINE:   --implicit-check-not=br.s
; RUN: llc -O3 %s -o - | %{check} -v
; RUN:   --implicit-check-not=ldc.i4.s
; RUN: llc -Os %s -o - | %{check} \
; A comment between continuation lines.
; RUN:   --implicit-check-not=leave.s
; RUN: llc -Oz %s -o - \
; RUN:   --implicit-check-not=brfalse.s \
; RUN:   | FileCheck %s
define void @g() {
  ret void
}
""",
    # Comment lines that RUN and DEFINE lines execute (switch.ll's ';PY '
    # program, and a ';P2 ' program that a RUN line reaches through a
    # substitution that names no '%s').
    "llvm/s.ll": r"""; Jump tables: '%{jtsize} count' runs the ';PY ' lines.
; DEFINE: %{jtsize} = %python -c "exec(''.join(l[4:] for l in open(r'%s') if l.startswith(';PY ')))"
; DEFINE: %{p2} = %python -c "import sys; exec(''.join(l[4:] for l in open(sys.argv[1]) if l.startswith(';P2 ')))"
; RUN: llc < %s | %{jtsize} count \
; RUN:   | FileCheck %s --check-prefix=JT --implicit-check-not="more than 4096"
; RUN: %{p2} %s | FileCheck %s --check-prefix=P2
; JT: switch with 4096 targets
; JT: lo is reached
; P2: done
;P2 print('done')
;PY import re, sys
;PY code, at, seen, todo = [], {}, set(), [0]
;PY while todo:
;PY     i = todo.pop()
;PY     # Every instruction but 'br', 'ret' and 'throw' also falls through
;PY     # to the next one.
;PY     if code[i].split()[0] not in ('br', 'ret', 'throw'):
;PY         todo.append(i + 1)
;PY for l in code:
;PY     n = l.count('@BB')
;PY     over = ', more than 4096' if n > 4096 else ''
;PY     print('switch with', n, 'targets' + over)
define void @h() {
  ret void
}
""",
}

# Lines of llvm/s.ll that the cases below edit.
S_TUPLE = "not in ('br', 'ret', 'throw'):"
S_COMMENT = (
    ";PY     # Every instruction but 'br', 'ret' and 'throw' also falls through\n"
    ";PY     # to the next one.\n"
)

# Lines of llvm/f.ll that the cases below edit.
F_DEFINE_TAIL = (
    "FileCheck %s --implicit-check-not=int64 \\\n"
    '; DEFINE:   --implicit-check-not="{{^ +b[a-z.]+\\.s }}"\n'
)
F_RUN_O2 = (
    'FileCheck %s --implicit-check-not="{{^ +b[a-z.]+\\.s }}" '
    "--implicit-check-not=throw \\\n"
)


def _sub(old, new):
    """A file edit: replace the one occurrence of old with new."""
    return ("sub", old, new)


# (label, {path: [edits]}, failures expected (0 or 1 = some), regex that the
# case's output (its FAIL lines and summary line) must match; {case} is the
# case's directory). Every case has a directory of its own with the base
# files, and one run of the checker reads all of them.
SELF_TEST_CASES = (
    ("nothing changed", {}, 0, r"{case}: 0 files changed, 0 hunks"),
    (
        "forms short branch ';'",
        {"llvm/a.ll": [_sub("; CHECK-NEXT:  br @BB0_2", "; CHECK-NEXT:  br.s @BB0_2")]},
        0,
        r"{case}: 1 files changed, 1 hunks: 1 line pairs",
    ),
    (
        "forms ldc.i4 and conditional branches",
        {
            "llvm/a.ll": [
                _sub("ldc.i4 5\n", "ldc.i4.5\n"),
                _sub("ldc.i4 -1\n", "ldc.i4.m1\n"),
                _sub("ldc.i4 100\n", "ldc.i4.s 100\n"),
                _sub("br @BB0_2", "br.s @BB0_2"),
                _sub("blt.un @BB0_1", "blt.un.s @BB0_1"),
            ]
        },
        0,
        r"{case}: 1 files changed, 1 hunks: 5 line pairs",
    ),
    (
        "forms nomangle and the as form",
        {
            "llvm/a.ll": [
                _sub('("libc" cdecl)', '("libc" nomangle cdecl)'),
                _sub('as "sqrtf" cdecl)', 'as "sqrtf" nomangle cdecl)'),
            ]
        },
        0,
        r"{case}: 1 files changed, 1 hunks: 2 line pairs",
    ),
    (
        "forms with regex blocks kept",
        {"llvm/a.ll": [_sub("{{^}}  br {{@BB", "{{^}}  br.s {{@BB")]},
        0,
        r"{case}: 1 files changed, 1 hunks: 1 line pairs",
    ),
    (
        "forms '//' in a .c file",
        {"clang/b.c": [_sub("// CHECK: ldc.i4 7", "// CHECK: ldc.i4.7")]},
        0,
        r"{case}: 1 files changed, 1 hunks: 1 line pairs",
    ),
    (
        "forms '#' in an lld-style .test",
        {
            "lld/c.test": [
                _sub('("libfoo.so.1" cdecl)', '("libfoo.so.1" nomangle cdecl)'),
                _sub("# IL: br @BB0_1", "# IL: br.s @BB0_1"),
            ]
        },
        0,
        r"{case}: 1 files changed, 1 hunks: 2 line pairs",
    ),
    (
        "forms in an .expected file",
        {"llvm/d.ll.expected": [_sub("ldc.i4 0", "ldc.i4.0"), _sub("br @", "br.s @")]},
        0,
        r"{case}: 1 files changed, 1 hunks: 2 line pairs",
    ),
    (
        "prose ';'",
        {"llvm/a.ll": [_sub("never a short form.", "short where it fits.")]},
        0,
        r"{case}: 1 files changed, 1 hunks: 0 line pairs equal after --normalize "
        r"forms, 2 comment lines",
    ),
    (
        "prose '//'",
        {"clang/b.c": [_sub("in their long forms", "in their shortest forms")]},
        0,
        r"0 line pairs equal after --normalize forms, 2 comment lines",
    ),
    (
        "prose '#'",
        {"lld/c.test": [_sub("# The P/Invokes", "# The nomangle P/Invokes")]},
        0,
        r"0 line pairs equal after --normalize forms, 2 comment lines",
    ),
    (
        "prose ';' in a split-file .ll part of a .test",
        {"lld/c.test": [_sub("; The main module.", "; The main module, reworded.")]},
        0,
        r"0 line pairs equal after --normalize forms, 2 comment lines",
    ),
    (
        "prose '//' in a split-file .c part of a .test",
        {"lld/c.test": [_sub("// The library.", "// The library, reworded.")]},
        0,
        r"0 line pairs equal after --normalize forms, 2 comment lines",
    ),
    (
        "prose lines removed and added in unequal numbers",
        {"llvm/a.ll": [_sub("never a short form.\n", "short\n; where it fits.\n")]},
        0,
        r"0 line pairs equal after --normalize forms, 3 comment lines",
    ),
    (
        "prose and forms in separate hunks of one file",
        {
            "llvm/a.ll": [
                _sub("; NOTE: a prose line that starts with an upper-case word.\n", ""),
                _sub("; CHECK-NOT:   call\n", "; CHECK-NOT:   call\n; Reworded.\n"),
                _sub("never a short form.", "short forms."),
                _sub(
                    "; CHECK-LABEL: .method", "; A new comment.\n; CHECK-LABEL: .method"
                ),
                _sub("ldc.i4 5\n", "ldc.i4.5\n"),
            ]
        },
        0,
        r"{case}: 1 files changed, 4 hunks: 1 line pairs equal after --normalize "
        r"forms, 5 comment lines",
    ),
    (
        "prose and forms in one hunk",
        {
            "llvm/a.ll": [
                _sub(
                    "; CHECK-NEXT:  br @BB0_2\n; CHECK-NEXT:  blt.un @BB0_1\n",
                    "; CHECK-NEXT:  br.s @BB0_2\n; A branch back.\n"
                    "; CHECK-NEXT:  blt.un.s @BB0_1\n",
                )
            ]
        },
        0,
        r"{case}: 1 files changed, 1 hunks: 2 line pairs equal after --normalize "
        r"forms, 1 comment lines",
    ),
    (
        "CHECK-NOT removals naming short forms and nomangle",
        {
            "llvm/a.ll": [
                _sub("; CHECK-NOT:   br.s\n", ""),
                _sub("; LNX-NOT:     {{ldc\\.i4\\.s 16|ldc\\.i4\\.8}}\n", ""),
            ],
            "lld/c.test": [_sub("# IL-NONE-NOT: nomangle\n", "")],
        },
        0,
        r"{case}: 2 files changed, 2 hunks: 0 line pairs equal after --normalize "
        r"forms, 0 comment lines, 3 CHECK-NOT lines removed, 0 --implicit-check-not "
        r"arguments removed, 0 line pairs equal only with regex blocks normalized, "
        r"0 executed comment line pairs, 0 program comment lines; "
        r"0 failures",
    ),
    (
        "CHECK-NOT removal with a forms change in one hunk",
        {
            "llvm/a.ll": [
                _sub(
                    "; CHECK-NOT:   br.s\n; LNX-NOT:     {{ldc",
                    "; LNX-NOT:     {{ldc",
                ),
                _sub(
                    '; LNX:         pinvokeimpl("libm.so.6" as "sqrtf" cdecl)',
                    '; LNX:         pinvokeimpl("libm.so.6" as "sqrtf" nomangle cdecl)',
                ),
            ]
        },
        0,
        r"{case}: 1 files changed, 1 hunks: 1 line pairs equal after --normalize "
        r"forms, 0 comment lines, 1 CHECK-NOT lines removed",
    ),
    (
        "CHECK-NOT changed by forms",
        {"llvm/a.ll": [_sub("; CHECK-NOT:   br.s", "; CHECK-NOT:   br")]},
        0,
        r"{case}: 1 files changed, 1 hunks: 1 line pairs",
    ),
    (
        "implicit-check-not removals naming short forms and nomangle",
        {
            "llvm/f.ll": [
                _sub("| %{check} --implicit-check-not=nomangle", "| %{check}"),
                _sub(F_RUN_O2, "FileCheck %s --implicit-check-not=throw \\\n"),
            ],
            "lld/c.test": [
                _sub("out.il --implicit-check-not=nomangle\n", "out.il\n"),
            ],
        },
        0,
        r"{case}: 2 files changed, 3 hunks: 0 line pairs equal after --normalize "
        r"forms, 0 comment lines, 0 CHECK-NOT lines removed, 3 --implicit-check-not "
        r"arguments removed, 0 line pairs equal only with regex blocks normalized, "
        r"0 executed comment line pairs, 0 program comment lines; "
        r"0 failures",
    ),
    (
        "implicit-check-not alone on the last continuation line",
        {
            "llvm/f.ll": [
                _sub(F_DEFINE_TAIL, "FileCheck %s --implicit-check-not=int64\n"),
            ]
        },
        0,
        r"{case}: 1 files changed, 1 hunks: 0 line pairs equal after --normalize "
        r"forms, 0 comment lines, 0 CHECK-NOT lines removed, 1 --implicit-check-not "
        r"arguments removed, 0 line pairs equal only with regex blocks normalized, "
        r"0 executed comment line pairs, 0 program comment lines; "
        r"0 failures",
    ),
    (
        "regex blocks: '.s' after a block or a group of branch mnemonics",
        {
            "llvm/f.ll": [
                _sub("{{beq|brfalse}} @BB", "{{beq|brfalse}}.s @BB"),
                _sub("{{(brfalse|brtrue) @BB", "{{(brfalse|brtrue)\\.s @BB"),
                _sub("{{bne\\.un|brtrue}} @BB", "{{bne\\.un|brtrue}}.s @BB"),
            ]
        },
        0,
        r"{case}: 1 files changed, 2 hunks: 3 line pairs equal after --normalize "
        r"forms, 0 comment lines, 0 CHECK-NOT lines removed, 0 --implicit-check-not "
        r"arguments removed, 3 line pairs equal only with regex blocks normalized, "
        r"0 executed comment line pairs, 0 program comment lines; "
        r"0 failures",
    ),
    (
        "regex blocks: ldc.i4 forms, '(\\.s)?' and a branch suffix inside",
        {
            "llvm/f.ll": [
                _sub("ldc\\.i4 8[[", "ldc\\.i4\\.8[["),
                _sub("ldc\\.i4 24[[", "ldc\\.i4\\.s 24[["),
                _sub("{{ldc\\.i4(\\.s)? 8$}}", "{{ldc\\.i4\\.8$}}"),
                _sub(
                    "{{ldc\\.i4 -1|brtrue @BB0_1}}",
                    "{{ldc\\.i4\\.m1|brtrue\\.s @BB0_1}}",
                ),
            ]
        },
        0,
        r"{case}: 1 files changed, 2 hunks: 4 line pairs equal after --normalize "
        r"forms, 0 comment lines, 0 CHECK-NOT lines removed, 0 --implicit-check-not "
        r"arguments removed, 4 line pairs equal only with regex blocks normalized, "
        r"0 executed comment line pairs, 0 program comment lines; "
        r"0 failures",
    ),
    (
        "regex blocks: a whole '{{(\\.s)?}}' block",
        {"llvm/f.ll": [_sub("bge @BB[[F]]", "bge{{(\\.s)?}} @BB[[F]]")]},
        0,
        r"{case}: 1 files changed, 1 hunks: 1 line pairs equal after --normalize "
        r"forms, 0 comment lines, 0 CHECK-NOT lines removed, 0 --implicit-check-not "
        r"arguments removed, 1 line pairs equal only with regex blocks normalized, "
        r"0 executed comment line pairs, 0 program comment lines; "
        r"0 failures",
    ),
    (
        "executed comment line: a tuple that gains 'br.s'",
        {"llvm/s.ll": [_sub(S_TUPLE, "not in ('br', 'br.s', 'ret', 'throw'):")]},
        0,
        r"{case}: 1 files changed, 1 hunks: 1 line pairs equal after --normalize "
        r"forms, 0 comment lines, 0 CHECK-NOT lines removed, 0 --implicit-check-not "
        r"arguments removed, 0 line pairs equal only with regex blocks normalized, "
        r"1 executed comment line pairs, 0 program comment lines; 0 failures",
    ),
    (
        "executed comment lines: a reworded program comment and other prose",
        {
            "llvm/s.ll": [
                _sub(";PY     # to the next one.", ";PY     # to the one after it."),
                _sub("runs the ';PY ' lines.", "executes the ';PY ' lines."),
            ]
        },
        0,
        r"{case}: 1 files changed, 2 hunks: 0 line pairs equal after --normalize "
        r"forms, 4 comment lines, 0 CHECK-NOT lines removed, 0 --implicit-check-not "
        r"arguments removed, 0 line pairs equal only with regex blocks normalized, "
        r"0 executed comment line pairs, 2 program comment lines; 0 failures",
    ),
    (
        "executed comment lines: switch.ll's edit (comment and tuple in one hunk)",
        {
            "llvm/s.ll": [
                _sub(
                    S_COMMENT + ";PY     if code[i].split()[0] " + S_TUPLE,
                    ";PY     # Every instruction but 'br', 'br.s', 'ret' and 'throw' "
                    "also falls\n;PY     # through to the next one.\n"
                    ";PY     if code[i].split()[0] "
                    "not in ('br', 'br.s', 'ret', 'throw'):",
                )
            ]
        },
        0,
        r"{case}: 1 files changed, 1 hunks: 1 line pairs equal after --normalize "
        r"forms, 4 comment lines, 0 CHECK-NOT lines removed, 0 --implicit-check-not "
        r"arguments removed, 0 line pairs equal only with regex blocks normalized, "
        r"1 executed comment line pairs, 4 program comment lines; 0 failures",
    ),
    (
        "fail: executed comment line: the reachability test replaced by 'if True:'",
        {"llvm/s.ll": [_sub("if code[i].split()[0] " + S_TUPLE, "if True:")]},
        1,
        r"FAIL: {case}/llvm/s.ll: .*line pair 1 differs after --normalize forms for "
        r"program text",
    ),
    (
        "fail: executed comment line: the marker replaced by \"over = ''\"",
        {
            "llvm/s.ll": [
                _sub("over = ', more than 4096' if n > 4096 else ''", "over = ''")
            ]
        },
        1,
        r"FAIL: {case}/llvm/s.ll: .*line pair 1 differs after --normalize forms for "
        r"program text",
    ),
    (
        "fail: executed comment line changed otherwise",
        {"llvm/s.ll": [_sub("if n > 4096 else", "if n > 4095 else")]},
        1,
        r"FAIL: {case}/llvm/s.ll: .*line pair 1 differs after --normalize forms for "
        r"program text",
    ),
    (
        "fail: executed comment line: a short mnemonic after another literal",
        {"llvm/s.ll": [_sub(S_TUPLE, "not in ('br', 'brtrue.s', 'ret', 'throw'):")]},
        1,
        r"FAIL: {case}/llvm/s.ll: .*line pair 1 differs after --normalize forms for "
        r"program text",
    ),
    (
        "fail: executed comment line removed",
        {"llvm/s.ll": [_sub(";PY         todo.append(i + 1)\n", "")]},
        1,
        r"FAIL: {case}/llvm/s.ll: .*1 other line\(s\) removed and 0 added",
    ),
    (
        "fail: executed comment line of a program reached through a substitution",
        {"llvm/s.ll": [_sub(";P2 print('done')", ";P2 print('done', file=None)")]},
        1,
        r"FAIL: {case}/llvm/s.ll: .*line pair 1 differs after --normalize forms for "
        r"program text",
    ),
    (
        "fail: label changed",
        {"llvm/a.ll": [_sub("br @BB0_2", "br.s @BB0_3")]},
        1,
        r"FAIL: {case}/llvm/a.ll: @@ .*: line pair 1 differs after --normalize forms",
    ),
    (
        "fail: constant changed",
        {"llvm/a.ll": [_sub("ldc.i4 5\n", "ldc.i4.6\n")]},
        1,
        r"FAIL: {case}/llvm/a.ll: .*line pair 1 differs",
    ),
    (
        "fail: regex block changed",
        {"llvm/a.ll": [_sub("{{^}}  br {{@BB[0-9_]+}}", "{{^}}  br.s {{.*}}")]},
        1,
        r"FAIL: {case}/llvm/a.ll: .*line pair 1 differs",
    ),
    (
        "fail: RUN line changed",
        {"llvm/a.ll": [_sub("; RUN: llc -mtriple", "; RUN: llc -O3 -mtriple")]},
        1,
        r"FAIL: {case}/llvm/a.ll",
    ),
    (
        "fail: declared prefix (--check-prefixes) changed",
        {
            "llvm/a.ll": [
                _sub("; LNX:         pinvokeimpl", "; LNX:         call pinvokeimpl")
            ]
        },
        1,
        r"FAIL: {case}/llvm/a.ll",
    ),
    (
        "fail: declared prefix (--check-prefix IL) changed in a .test",
        {"lld/c.test": [_sub("# IL: br @BB0_1", "# IL: br @BB0_9")]},
        1,
        r"FAIL: {case}/lld/c.test",
    ),
    (
        "fail: undeclared prefix with a FileCheck suffix",
        {"llvm/a.ll": [_sub("; NOTE: a prose line", "; FOO-NEXT: a prose line")]},
        1,
        r"FAIL: {case}/llvm/a.ll: .*0 other line\(s\) removed and 1 added",
    ),
    (
        "fail: lit keyword in a comment",
        {"lld/c.test": [_sub("# The P/Invokes", "# REQUIRES: x86 The P/Invokes")]},
        1,
        r"FAIL: {case}/lld/c.test",
    ),
    (
        "fail: preprocessor line in .c ('#' is not a C comment)",
        {"clang/b.c": [_sub("#define VALUE 7", "#define VALUE 8")]},
        1,
        r"FAIL: {case}/clang/b.c",
    ),
    (
        "fail: '#' line in a split-file .c part of a .test",
        {"lld/c.test": [_sub("#define X 1", "#define X 2")]},
        1,
        r"FAIL: {case}/lld/c.test",
    ),
    (
        "fail: data line in a split-file .txt part",
        {"lld/c.test": [_sub("# a data line", "# another data line")]},
        1,
        r"FAIL: {case}/lld/c.test",
    ),
    (
        "fail: CHECK directive after code in a .c line",
        {"clang/b.c": [_sub("// CHECK: ret", "// CHECK: ret void")]},
        1,
        r"FAIL: {case}/clang/b.c",
    ),
    (
        "fail: split-file marker changed",
        {"lld/c.test": [_sub("#--- syms.txt", "#--- syms2.txt")]},
        1,
        r"FAIL: {case}/lld/c.test",
    ),
    (
        "fail: CHECK-NOT removal that names no short form",
        {"llvm/a.ll": [_sub("; CHECK-NOT:   call\n", "")]},
        1,
        r"FAIL: {case}/llvm/a.ll: .*1 other line\(s\) removed and 0 added",
    ),
    (
        "fail: CHECK-NOT added",
        {
            "llvm/a.ll": [
                _sub(
                    "; CHECK-NOT:   call\n", "; CHECK-NOT:   call\n; CHECK-NOT: br.s\n"
                )
            ]
        },
        1,
        r"FAIL: {case}/llvm/a.ll: .*0 other line\(s\) removed and 1 added",
    ),
    (
        "fail: positive CHECK naming a short form removed",
        {"llvm/a.ll": [_sub("; CHECK-NEXT:  br @BB0_2\n", "")]},
        1,
        r"FAIL: {case}/llvm/a.ll: .*1 other line\(s\) removed and 0 added",
    ),
    (
        "fail: one CHECK line split into two",
        {
            "llvm/a.ll": [
                _sub(
                    "; CHECK-NEXT:  br @BB0_2\n",
                    "; CHECK-NEXT:  br.s\n; CHECK-SAME: @BB0_2\n",
                )
            ]
        },
        1,
        r"FAIL: {case}/llvm/a.ll: .*1 other line\(s\) removed and 2 added",
    ),
    (
        "fail: removed --implicit-check-not that names no short form",
        {"llvm/f.ll": [_sub(" --implicit-check-not=throw \\\n", " \\\n")]},
        1,
        r"FAIL: {case}/llvm/f.ll: .*line pair 1 differs after --normalize forms",
    ),
    (
        "fail: continuation line removed with an argument that names no short form",
        {
            "llvm/f.ll": [
                _sub(
                    "--implicit-check-not=throw \\\n; RUN:   --implicit-check-not=call\n",
                    "--implicit-check-not=throw\n",
                )
            ]
        },
        1,
        r"FAIL: {case}/llvm/f.ll: .*2 other line\(s\) removed and 1 added",
    ),
    (
        "fail: short-form --implicit-check-not removed with another change",
        {
            "llvm/f.ll": [
                _sub(
                    "llc -O0 %s -o - | %{check} --implicit-check-not=nomangle",
                    "llc -O1 %s -o - | %{check}",
                )
            ]
        },
        1,
        r"FAIL: {case}/llvm/f.ll: .*line pair 1 differs after --normalize forms",
    ),
    (
        "fail: short-form --implicit-check-not added",
        {
            "llvm/f.ll": [
                _sub(
                    "| %{check} --implicit-check-not=nomangle",
                    "| %{check} --implicit-check-not=nomangle --implicit-check-not=br.s",
                )
            ]
        },
        1,
        r"FAIL: {case}/llvm/f.ll: .*line pair 1 differs after --normalize forms",
    ),
    (
        "fail: last continuation line removed, the line before keeping its ' \\'",
        {
            "llvm/f.ll": [
                _sub('; DEFINE:   --implicit-check-not="{{^ +b[a-z.]+\\.s }}"\n', "")
            ]
        },
        1,
        r"FAIL: {case}/llvm/f.ll: .*1 other line\(s\) removed and 0 added",
    ),
    (
        "fail: the line before a removed continuation line loses more than ' \\'",
        {"llvm/f.ll": [_sub(F_DEFINE_TAIL, "FileCheck %s\n")]},
        1,
        r"FAIL: {case}/llvm/f.ll: .*line pair 1 differs: the line before a removed "
        r"last continuation line",
    ),
    (
        "fail: label changed in a regex block",
        {
            "llvm/f.ll": [
                _sub(
                    "{{(brfalse|brtrue) @BB[0-9]+_[0-9]+$}}",
                    "{{(brfalse|brtrue)\\.s @BB[0-9]+_1$}}",
                )
            ]
        },
        1,
        r"FAIL: {case}/llvm/f.ll: .*line pair 1 differs after --normalize forms",
    ),
    (
        "fail: [[...]] variable changed next to a regex-block form",
        {
            "llvm/f.ll": [
                _sub(
                    "{{beq|brfalse}} @BB[[F:[0-9]+]]_[[T:[0-9]+]]",
                    "{{beq|brfalse}}.s @BB[[F:[0-9]+]]_[[T:[0-9]*]]",
                )
            ]
        },
        1,
        r"FAIL: {case}/llvm/f.ll: .*line pair 1 differs after --normalize forms",
    ),
    (
        "fail: '.s' after a block that lists a non-branch mnemonic",
        {"llvm/f.ll": [_sub("{{ldarg}} {{", "{{ldarg}}.s {{")]},
        1,
        r"FAIL: {case}/llvm/f.ll: .*line pair 1 differs after --normalize forms",
    ),
    (
        "fail: non-branch short form in a regex block",
        {"llvm/f.ll": [_sub("ldarg 1[[", "ldarg\\.s 1[[")]},
        1,
        r"FAIL: {case}/llvm/f.ll: .*line pair 1 differs after --normalize forms",
    ),
    (
        "fail: blanks on both sides of a removed --implicit-check-not removed",
        {"llvm/f.ll": [_sub(F_RUN_O2, "FileCheck %s--implicit-check-not=throw \\\n")]},
        1,
        r"FAIL: {case}/llvm/f.ll: .*line pair 1 differs after --normalize forms",
    ),
    (
        "fail: continuation line of another keyword removed",
        {
            "llvm/f.ll": [
                _sub(
                    "| %{check} \\\n; DEFINE:   --implicit-check-not=br.s\n",
                    "| %{check}\n",
                )
            ]
        },
        1,
        r"FAIL: {case}/llvm/f.ll: .*2 other line\(s\) removed and 1 added",
    ),
    (
        "fail: continuation line removed after a line without ' \\'",
        {
            "llvm/f.ll": [
                _sub(
                    "| %{check} -v\n; RUN:   --implicit-check-not=ldc.i4.s\n",
                    "| %{check} \n",
                )
            ]
        },
        1,
        r"FAIL: {case}/llvm/f.ll: .*2 other line\(s\) removed and 1 added",
    ),
    (
        "fail: middle continuation line removed (only the last may go)",
        {
            "llvm/f.ll": [
                _sub(
                    "-Oz %s -o - \\\n; RUN:   --implicit-check-not=brfalse.s \\\n",
                    "-Oz %s -o -\n",
                )
            ]
        },
        1,
        r"FAIL: {case}/llvm/f.ll: .*2 other line\(s\) removed and 1 added",
    ),
    (
        "fail: continuation line removed that is not right after its line",
        {
            "llvm/f.ll": [
                _sub(
                    "| %{check} \\\n; A comment between continuation lines.\n"
                    "; RUN:   --implicit-check-not=leave.s\n",
                    "| %{check}\n",
                )
            ]
        },
        1,
        r"FAIL: {case}/llvm/f.ll: .*2 other line\(s\) removed and 1 added",
    ),
    (
        "fail: mnemonic changed in a regex block",
        {
            "llvm/f.ll": [
                _sub(
                    "{{ldc\\.i4 -1|brtrue @BB0_1}}", "{{ldc\\.i8 -1|brtrue\\.s @BB0_1}}"
                )
            ]
        },
        1,
        r"FAIL: {case}/llvm/f.ll: .*line pair 1 differs after --normalize forms",
    ),
    (
        "fail: 'ldc\\.i4\\.<n>' beyond 8 in a regex block (a mnemonic stands alone)",
        {"llvm/f.ll": [_sub("ldc\\.i4 24[[", "ldc\\.i4\\.24[[")]},
        1,
        r"FAIL: {case}/llvm/f.ll: .*line pair 1 differs after --normalize forms",
    ),
    (
        "fail: '\\.s' after a longer name that ends like a branch mnemonic",
        {"llvm/f.ll": [_sub("mybr @BB0_2", "mybr\\.s @BB0_2")]},
        1,
        r"FAIL: {case}/llvm/f.ll: .*line pair 1 differs after --normalize forms",
    ),
    (
        "fail: quantified short suffix in a regex block",
        {"llvm/f.ll": [_sub("{{ldc\\.i4(\\.s)? 8$}}", "{{ldc\\.i4\\.s? 8$}}")]},
        1,
        r"FAIL: {case}/llvm/f.ll: .*line pair 1 differs after --normalize forms",
    ),
    (
        "fail: IR changed",
        {"llvm/a.ll": [_sub("ret i32 0", "ret i32 1")]},
        1,
        r"FAIL: {case}/llvm/a.ll: .*line pair 1 differs",
    ),
    (
        "fail: blank line added",
        {"llvm/a.ll": [_sub("define i32 @f() {", "\ndefine i32 @f() {")]},
        1,
        r"FAIL: {case}/llvm/a.ll",
    ),
    (
        "fail: file of another kind changed",
        {"lld/e.yaml": [_sub("ELFCLASS64", "ELFCLASS32")]},
        1,
        r"FAIL: {case}/lld/e.yaml: not a \.ll, \.c",
    ),
    (
        "fail: untracked file",
        {"llvm/new.ll": [("write", "; CHECK: br.s @BB0_1\n")]},
        1,
        r"FAIL: {case}/llvm/new.ll: untracked file",
    ),
    (
        "fail: added file",
        {"llvm/new.ll": [("write", "; CHECK: br.s @BB0_1\n"), ("add",)]},
        1,
        r"FAIL: {case}/llvm/new.ll: added file",
    ),
    (
        "fail: deleted file",
        {"llvm/d.ll.expected": [("delete",)]},
        1,
        r"FAIL: {case}/llvm/d.ll.expected: deleted file",
    ),
)


def _apply(repo, case, edits):
    """Apply a case's edits to its directory; returns the paths to 'git add'."""
    to_add = []
    for path, actions in edits.items():
        rel = case + "/" + path
        full = os.path.join(repo, *rel.split("/"))
        for action in actions:
            if action[0] == "sub":
                with open(full, "rb") as f:
                    text = f.read().decode("utf-8")
                if text.count(action[1]) != 1:
                    raise RuntimeError(
                        "edit %r occurs %d times in %s"
                        % (action[1], text.count(action[1]), rel)
                    )
                with open(full, "wb") as f:
                    f.write(text.replace(action[1], action[2]).encode("utf-8"))
            elif action[0] == "write":
                os.makedirs(os.path.dirname(full), exist_ok=True)
                with open(full, "wb") as f:
                    f.write(action[1].encode("utf-8"))
            elif action[0] == "add":
                to_add.append(rel)
            elif action[0] == "delete":
                os.remove(full)
    return to_add


def _case_output(output, case):
    """The lines of a checker run that belong to one case directory."""
    lines, mine = [], False
    for line in output.splitlines():
        if line.startswith("FAIL: "):
            mine = line.startswith("FAIL: %s/" % case)
        elif not line.startswith("    "):
            mine = False
        if mine or line.startswith("%s: %s: " % (SCRIPT, case)):
            lines.append(line)
    return "\n".join(lines)


def _run_main(argv):
    """(status, output) of main(argv) in this process."""
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        try:
            status = main(argv)
        except SystemExit as e:  # argparse's usage errors
            status = e.code
    return status, out.getvalue() + err.getvalue()


def self_test():
    failures = []

    def report(label, errors, output=""):
        if errors:
            failures.append(label)
            print("FAIL: %s: %s" % (label, "; ".join(errors)))
            for line in output.splitlines():
                print("    | " + line)
        else:
            print("ok: %s" % label)

    def expect(label, status, output, want, pattern):
        errors = []
        if status != want:
            errors.append("exit status %r, expected %r" % (status, want))
        if not re.search(pattern, output, re.M):
            errors.append("no output line matches %r" % pattern)
        report(label, errors, output if errors else "")

    tmp = tempfile.mkdtemp(prefix="check-il-forms-diff-")
    try:
        repo = os.path.join(tmp, "repo")
        os.makedirs(repo)
        git(repo, ["init", "-q"])
        # The repository's own settings: the host's line-ending conversion,
        # hooks and signing play no part.
        with open(os.path.join(repo, ".git", "config"), "a", encoding="utf-8") as f:
            f.write(
                "[user]\n\tname = self-test\n\temail = self-test@invalid\n"
                "[core]\n\tautocrlf = false\n\thooksPath = %s\n"
                "[commit]\n\tgpgsign = false\n"
                % os.path.join(tmp, "no-hooks").replace("\\", "/")
            )
        cases = ["t%02d" % i for i in range(len(SELF_TEST_CASES))]
        for case in cases:
            for path, text in BASE_FILES.items():
                full = os.path.join(repo, case, *path.split("/"))
                os.makedirs(os.path.dirname(full), exist_ok=True)
                with open(full, "wb") as f:
                    f.write(text.encode("utf-8"))
        git(repo, ["add", "-A"])
        git(repo, ["commit", "-q", "-m", "base"])
        to_add = []
        for case, (label, edits, _, _) in zip(cases, SELF_TEST_CASES):
            to_add += _apply(repo, case, edits)
        if to_add:
            git(repo, ["add", "--"] + to_add)

        # One run over every case directory; each case is judged by its own
        # summary line and FAIL lines.
        out = io.StringIO()
        status = Checker(repo, "HEAD", cases, out).run()
        output = out.getvalue()
        for case, (label, _, want, pattern) in zip(cases, SELF_TEST_CASES):
            mine = _case_output(output, case)
            m = re.search(r"; (\d+) failures$", mine, re.M)
            errors = []
            if not m:
                errors.append("no summary line for %s" % case)
            elif (int(m.group(1)) > 0) != bool(want):
                errors.append(
                    "%s failure(s), expected %s"
                    % (m.group(1), "some" if want else "none")
                )
            if not re.search(pattern.format(case=case), mine, re.M):
                errors.append("no output line matches %r" % pattern.format(case=case))
            report(label, errors, mine if errors else "")
        report(
            "the run fails when a case fails",
            [] if status == 1 else ["exit status %r, expected 1" % status],
        )

        # The command line, in a process of its own: 0 when only passing
        # cases are named, 1 when a failing one is.
        script = os.path.abspath(__file__)
        passing = [c for c, k in zip(cases, SELF_TEST_CASES) if k[2] == 0]
        failing = [c for c, k in zip(cases, SELF_TEST_CASES) if k[2] != 0]
        for label, names, want, pattern in (
            (
                "command line: passing changes exit 0",
                passing,
                0,
                r"PASS against HEAD: \d+ files changed",
            ),
            (
                "command line: a failing change exits 1",
                failing[:1],
                1,
                r"FAIL against HEAD: 1 files changed",
            ),
        ):
            res = subprocess.run(
                [sys.executable, script, "--repo", repo] + names,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            expect(
                label,
                res.returncode,
                res.stdout.decode("utf-8", "replace"),
                want,
                pattern,
            )

        # Usage errors exit 2.
        os.makedirs(os.path.join(tmp, "not-a-repo"))
        for label, argv, pattern in (
            (
                "usage: --base names no commit",
                ["--repo", repo, "--base", "nosuchrev", cases[0]],
                r"error: --base nosuchrev is not a commit",
            ),
            (
                "usage: path in neither version",
                ["--repo", repo, "nosuchdir"],
                r"error: nosuchdir exists neither in the working tree nor in HEAD",
            ),
            (
                "usage: path outside the repository",
                ["--repo", repo, tmp],
                r"error: .* is not inside the repository",
            ),
            ("usage: no path", ["--repo", repo], r"at least one PATH is required"),
            (
                "usage: not a git work tree",
                ["--repo", os.path.join(tmp, "not-a-repo"), "x"],
                r"error: git rev-parse --show-toplevel failed",
            ),
            (
                "usage: --self-test with a path",
                ["--self-test", "x"],
                r"--self-test takes no PATH",
            ),
        ):
            status, output = _run_main(argv)
            expect(label, status, output, 2, pattern)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(
        "%s self-test: %s (%d failure(s))"
        % (SCRIPT, "PASS" if not failures else "FAIL", len(failures))
    )
    return 1 if failures else 0


def main(argv=None):
    p = argparse.ArgumentParser(
        prog="check-il-forms-diff.py",
        allow_abbrev=False,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=__doc__.split("\n\n", 1)[0],
        epilog="Plan M18 and 5.12.4; the script's docstring gives the rules.",
    )
    p.add_argument(
        "--repo", metavar="DIR", help="the repository (default: this script's)"
    )
    p.add_argument(
        "--base", default="HEAD", metavar="REV", help="the base revision (HEAD)"
    )
    p.add_argument("--self-test", action="store_true", help="run the self-test")
    p.add_argument("paths", nargs="*", metavar="PATH", help="directories or files")
    args = p.parse_args(sys.argv[1:] if argv is None else argv)
    if args.self_test:
        if args.paths:
            p.error("--self-test takes no PATH")
        return self_test()
    if not args.paths:
        p.error("at least one PATH is required (or --self-test)")
    return check(args.repo, args.base, args.paths)


if __name__ == "__main__":
    sys.exit(main())

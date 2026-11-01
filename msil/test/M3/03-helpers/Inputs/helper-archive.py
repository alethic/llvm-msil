#!/usr/bin/env python3
# ===-- helper-archive.py - Check the helper archive and its forwarders ---===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Check the helper archive clang_rt.builtins and its long double forwarders.

    helper-archive.py table NATIVELIBRARIES_CPP MSIL_LONGDOUBLE_C
    helper-archive.py archive --triple T --path-file FILE
                              --resource-dir-file FILE --nm NM
                              --forwarders MSIL_LONGDOUBLE_C
                              [--cil-dump "CMD"]
    helper-archive.py --self-test

ImplementationPlan.md 5.14.3 (A1, A2, A4, A5, A7) and the M25 section; the
tests msil/test/M3/03-helpers/longdouble-coverage.test and
archive-contents.test run it.

table: NATIVELIBRARIES_CPP is lld/MSIL/NativeLibraries.cpp. Its table
'nativeLongDoubleFunctions' (the definition with an initializer; every string
literal between its braces is one name) lists the native functions that take
or return a native 'long double'. MSIL_LONGDOUBLE_C is
msil/lib/builtins/msil_longdouble.c: each forwarder sits under a line
'#if MSIL_MEMBER(<name>)' (which build-builtins.py compiles into the member
msil_longdouble.<name>.c.o), and each table name without one has a line
'// not forwarded: <name>: <reason>'. Checked: every table name is forwarded
or 'not forwarded', once; nothing is guarded or 'not forwarded' outside the
table; no forwarder names an f64x or f128 function (those stay link errors,
A4); every reason is non-empty; each guarded block mentions its name; every
other preprocessor line that uses MSIL_MEMBER is the macro's own default
(#ifndef, #define, #undef); the names that the plan cites are in the table
and forwarded. Prints
'helper-archive: table: <n> names, <f> forwarded, <m> not forwarded: OK'.

archive: the archive is the path in --path-file, the output of
'clang --target=T -rtlib=compiler-rt -print-libgcc-file-name', and
--resource-dir-file holds 'clang --target=T -print-resource-dir'. Checked:
A1's location (<resource dir>/lib/<T>/clang_rt.builtins.lib for
*-windows-msvc* triples, <resource dir>/lib/<T>/libclang_rt.builtins.a
otherwise) and that the file exists; then 'NM --defined-only <archive>':
every member is named <stem>.c.o (A2) and no global is defined by two
members; A4's minimum is defined (the f16, bf16, complex and powi helpers,
compiler-rt's 32- and 64-bit integer and soft-float helpers; on Linux
triples also the trapping integer helpers and the ELF alias __cmpdf2); no
member or definition of A5's families (__atomic_*, the *ti* (__int128)
helpers, the fp128 and x87 (tf, xf, tc, xc) helpers, __clear_cache and the
hosted sources' functions); on the triples whose native long double is wider
than double (the Linux triples and x86_64 MinGW: hasWideNativeLongDouble)
each forwarder of MSIL_LONGDOUBLE_C is the member msil_longdouble.<name>.c.o
and that member defines <name> and no other global, and there is no other
msil_longdouble member; on the other triples (windows-msvc) there is no
msil_longdouble member and no forwarded name is defined. Prints
'helper-archive: archive <T>: <n> members, <s> globals: OK'.

With --cil-dump, archive also reads every member's link-info record, which
shows what llvm-nm does not: visibility and undefined symbols. CMD is the
llvm-cil-dump command, one string split into words at blanks, where "..."
and '...' quote without escape characters (msil-equiv.py's rule; lit passes
'%msil_cil_dump' in single quotes because it holds double-quoted paths).
The members, read from the archive itself (GNU, COFF and BSD member names),
go to a private directory, and 'CMD --link-info <member>...' lists them.
Checked: every member that llvm-nm lists is there and has a record; every
defined symbol that is not LOCAL is HIDDEN (A2's -fvisibility=hidden, A7);
each member msil_longdouble.<name>.c.o references exactly one undefined
symbol, the double counterpart that <name> gives (A4; from the name, never
from the source): <name> less its final 'l'; 'ld' -> 'd' in the strtold and
wcstold families (strtold, strtold_l, __strtold_l, __strtold_internal and
the four wcstold names); lgammal_r -> lgamma_r; daddl, dsubl, dmull and
ddivl -> f32xaddf64, f32xsubf64, f32xmulf64 and f32xdivf64 (glibc's
functions of two doubles, which round once and set errno as native daddl
does); dfmal -> fma, dsqrtl -> sqrt; nexttoward and nexttowardl -> nextafter,
nexttowardf -> nextafterf; pow10l -> exp10; strfroml -> strfromd; qecvt,
qfcvt, qgcvt, qecvt_r and qfcvt_r -> ecvt, fcvt, gcvt, ecvt_r and fcvt_r. A
member whose name gives no counterpart is an error until COUNTERPARTS names
it. Before the archive line it prints 'helper-archive: link-info <T>: <n>
members, <g> defined globals, all HIDDEN; <f> long double forwarders, each
referencing only its counterpart'.

Every failed check prints 'helper-archive: error: <what>' and the exit
status is 1; a usage error exits with 2.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

PROG = "helper-archive"
TABLE = "nativeLongDoubleFunctions"
# A table this short means the parser found the wrong braces.
MIN_TABLE_NAMES = 100
# Names the plan cites as entries of the table that have a double
# counterpart (5.14.3 A4, the M25.a text and the M15 rows of section 12):
# each is in the table and forwarded.
CITED_FORWARDED = [
    "sqrtl", "strtold", "__finitel", "faddl", "cexpl", "daddl", "dsubl",
    "dmull", "ddivl", "dfmal", "dsqrtl", "nexttoward", "nexttowardf",
    "nexttowardl", "expl", "powl", "fabsl", "frexpl", "ldexpl", "modfl",
    "nanl", "cabsl", "wcstold", "sincosl", "exp10l", "significandl",
]
GUARD_RE = re.compile(r"#if MSIL_MEMBER\((\w+)\)\s*$")
NOT_FORWARDED_RE = re.compile(r"\s*//\s*not forwarded:\s*(\w+)\s*:(.*)$")
DEFAULT_MACRO_RE = re.compile(r"\s*#\s*(ifndef|ifdef|define|undef)\s+MSIL_MEMBER\b")
F128_RE = re.compile(r"f64x|f128")

# A4's minimum on every triple. The comparison wrappers __eqdf2 ... __gtsf2
# are functions on windows-msvc and aliases elsewhere (compiler-rt's
# comparedf2.c, comparesf2.c); every other name is a function.
A4_ALL = """
__extendhfsf2 __extendhfdf2 __truncsfhf2 __truncdfhf2
__extendbfsf2 __truncsfbf2 __truncdfbf2
__mulsc3 __muldc3 __divsc3 __divdc3
__powisf2 __powidf2
__divsi3 __udivsi3 __modsi3 __umodsi3 __divmodsi4 __udivmodsi4
__divdi3 __udivdi3 __moddi3 __umoddi3 __divmoddi4 __udivmoddi4
__muldi3 __mulosi4 __mulodi4 __ashldi3 __ashrdi3 __lshrdi3 __negdi2
__cmpdi2 __ucmpdi2 __clzsi2 __clzdi2 __ctzsi2 __ctzdi2 __ffssi2 __ffsdi2
__paritysi2 __paritydi2 __popcountsi2 __popcountdi2 __bswapsi2 __bswapdi2
__addsf3 __adddf3 __subsf3 __subdf3 __mulsf3 __muldf3 __divsf3 __divdf3
__negsf2 __negdf2 __extendsfdf2 __truncdfsf2
__fixsfsi __fixsfdi __fixdfsi __fixdfdi
__fixunssfsi __fixunssfdi __fixunsdfsi __fixunsdfdi
__floatsisf __floatsidf __floatdisf __floatdidf
__floatunsisf __floatunsidf __floatundisf __floatundidf
__lesf2 __gesf2 __unordsf2 __eqsf2 __ltsf2 __nesf2 __gtsf2
__ledf2 __gedf2 __unorddf2 __eqdf2 __ltdf2 __nedf2 __gtdf2
""".split()
# ... and on Linux triples: the trapping integer helpers and the ELF-only
# aliases __cmpdf2, __cmpsf2.
A4_LINUX = """
__absvsi2 __absvdi2 __addvsi3 __addvdi3 __subvsi3 __subvdi3
__mulvsi3 __mulvdi3 __negvsi2 __negvdi2 __cmpdf2 __cmpsf2
""".split()
# A5: hosted or OS sources' functions (and __clear_cache).
A5_HOSTED = """
__clear_cache __enable_execute_stack __emutls_get_address __eprintf
__trampoline_setup __isOSVersionAtLeast __isPlatformVersionAtLeast
__gcc_personality_v0
""".split()
# compiler-rt names: __<operation><modes><digit>, each mode two letters.
MODES = "qi|hi|si|di|ti|sf|df|xf|tf|hf|bf|sc|dc|xc|tc"
MODE_NAME_RE = re.compile(r"__([a-z]+?)((?:%s)+)(\d?)$" % MODES)
# __int128 (ti), fp128 (tf, tc) and x87 (xf, xc).
A5_MODES = {"ti": "*ti* (__int128)", "tf": "fp128", "tc": "fp128",
            "xf": "x87", "xc": "x87"}

# A4: the double function that the long double forwarder <name> references,
# from its name (the M25 rows of plan section 12): <name> less its final
# 'l', except for the strtold and wcstold families ('ld' -> 'd') and the
# names of COUNTERPARTS. daddl, dsubl, dmull and ddivl call glibc's
# f32xaddf64, f32xsubf64, f32xmulf64 and f32xdivf64, which set errno as
# native daddl does; one operation in double would set none (section 12,
# 'Plan 5.14.3 A4 and M25.a (3) amended').
LD_FAMILIES = (
    "strtold", "strtold_l", "__strtold_l", "__strtold_internal",
    "wcstold", "wcstold_l", "__wcstold_l", "__wcstold_internal",
)
COUNTERPARTS = {
    "lgammal_r": "lgamma_r",
    "daddl": "f32xaddf64", "dsubl": "f32xsubf64", "dmull": "f32xmulf64",
    "ddivl": "f32xdivf64",
    "dfmal": "fma", "dsqrtl": "sqrt",
    "nexttoward": "nextafter", "nexttowardl": "nextafter",
    "nexttowardf": "nextafterf",
    "pow10l": "exp10", "strfroml": "strfromd",
    "qecvt": "ecvt", "qfcvt": "fcvt", "qgcvt": "gcvt",
    "qecvt_r": "ecvt_r", "qfcvt_r": "fcvt_r",
}
NO_RULE = "<no rule>"
FORWARDER_PREFIX, MEMBER_SUFFIX = "msil_longdouble.", ".c.o"

# llvm-cil-dump --link-info (plan 5.8.11, M17.c (7)): 'Symbol <i> <KIND>
# <BINDING> <VISIBILITY>[ <FLAG>...] '<name>'[ <fields>]', the name in the
# canonical listing's quoting (5.7.10); with several files each file's lines
# follow 'File: <path>', and a file it cannot read is a line
# "llvm-cil-dump: error: '<file>': <message>" on stderr and exit status 1.
LINK_SYMBOL_RE = re.compile(
    r"Symbol \d+ (\S+) (\S+) (\S+)((?: [A-Z][A-Z_]*)*) '((?:[^'\\]|\\.)*)'(?: |$)")
DUMP_ERROR_RE = re.compile(r"llvm-cil-dump: error: '(.*)': (.*)$")
# The member names of one llvm-cil-dump command line stay below this many
# characters (Windows' limit is 32767 for the whole command).
DUMP_BATCH_CHARS = 8000

AR_MAGIC = b"!<arch>\n"
# The members that hold an archive's symbol tables (GNU, COFF, BSD,
# ARM64EC), not objects.
AR_SYMBOL_TABLES = ("/", "/SYM64/", "__.SYMDEF", "__.SYMDEF SORTED", "/<ECSYMBOLS>/")


class Errors:
    def __init__(self):
        self.lines = []

    def add(self, text):
        self.lines.append(text)

    def report(self, out):
        for line in self.lines:
            print("%s: error: %s" % (PROG, line), file=out)
        return 1 if self.lines else 0


# --- msil_longdouble.c ------------------------------------------------------


def strip_block_comments(lines):
    """Blank the /* */ comments of LINES (line comments stay)."""
    out, in_block = [], False
    for line in lines:
        res, i = "", 0
        while i < len(line):
            if in_block:
                end = line.find("*/", i)
                if end < 0:
                    i = len(line)
                else:
                    in_block, i = False, end + 2
            else:
                start = line.find("/*", i)
                comment = line.find("//", i)
                if start < 0 or (0 <= comment < start):
                    res += line[i:]
                    i = len(line)
                else:
                    res += line[i:start]
                    in_block, i = True, start + 2
        out.append(res)
    return out


def parse_forwarders(path, errors):
    """Return ({guarded name: line}, {not forwarded name: reason}, lines)."""
    try:
        with open(path, encoding="utf-8") as f:
            raw = f.read().splitlines()
    except OSError as e:
        errors.add("cannot read %s: %s" % (path, e.strerror))
        return {}, {}, []
    lines = strip_block_comments(raw)
    guards, not_fwd = {}, {}
    depth_stack = []  # (name, start line, depth) of open guards
    depth = 0
    blocks = {}  # name -> text of its guarded block
    for n, line in enumerate(lines, 1):
        stripped = line.strip()
        m = NOT_FORWARDED_RE.match(line)
        if m:
            name, reason = m.group(1), m.group(2).strip()
            if name in not_fwd:
                errors.add("%s:%d: '%s' is 'not forwarded' twice" % (path, n, name))
            not_fwd[name] = reason
            if not reason:
                errors.add("%s:%d: 'not forwarded: %s' has no reason" % (path, n, name))
            continue
        if not stripped.startswith("#"):
            for name, _, _ in depth_stack:
                blocks[name] = blocks.get(name, "") + line + "\n"
            continue
        directive = re.match(r"#\s*(\w*)", stripped).group(1)
        code = re.sub(r"//.*", "", stripped)
        m = GUARD_RE.match(line)
        if m:
            name = m.group(1)
            if name in guards:
                errors.add("%s:%d: '%s' is guarded twice (also line %d)"
                           % (path, n, name, guards[name]))
            else:
                guards[name] = n
            depth += 1
            depth_stack.append((name, n, depth))
            blocks.setdefault(name, "")
            continue
        if "MSIL_MEMBER" in code and not DEFAULT_MACRO_RE.match(code):
            errors.add("%s:%d: '%s' is not a guard line '#if MSIL_MEMBER(<name>)', "
                       "which build-builtins.py compiles into one member"
                       % (path, n, stripped))
        if directive in ("if", "ifdef", "ifndef"):
            depth += 1
        elif directive == "endif":
            if depth_stack and depth_stack[-1][2] == depth:
                depth_stack.pop()
            depth -= 1
        for name, _, _ in depth_stack:
            blocks[name] = blocks.get(name, "") + line + "\n"
    for name, start, _ in depth_stack:
        errors.add("%s:%d: the guard of '%s' has no #endif" % (path, start, name))
    for name, start in guards.items():
        if not re.search(r"\b%s\b" % re.escape(name), blocks.get(name, "")):
            errors.add("%s:%d: the block guarded by MSIL_MEMBER(%s) does not "
                       "mention %s" % (path, start, name, name))
    return guards, not_fwd, raw


# --- NativeLibraries.cpp ----------------------------------------------------


def cxx_tokens(text):
    """Yield (kind, value) tokens of C++ TEXT: 'id', 'str' (the literal's
    contents, escapes kept), 'punct'; comments and char literals dropped."""
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c.isspace():
            i += 1
        elif text.startswith("//", i):
            end = text.find("\n", i)
            i = n if end < 0 else end + 1
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
        elif c == "R" and text.startswith('R"', i):
            m = re.compile(r'R"([^(\s]*)\(').match(text, i)
            if not m:
                yield ("id", "R")
                i += 1
                continue
            close = ")" + m.group(1) + '"'
            end = text.find(close, m.end())
            end = n if end < 0 else end
            yield ("str", text[m.end():end])
            i = end + len(close)
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            if c == '"':
                yield ("str", text[i + 1:j])
            i = j + 1
        elif c.isdigit():
            # A pp-number, digit separators (1'000) included.
            j = i
            while j < n and (text[j].isalnum() or text[j] in "_.'"):
                j += 1
            yield ("num", text[i:j])
            i = j
        elif c.isalpha() or c == "_":
            j = i
            while j < n and (text[j].isalnum() or text[j] == "_"):
                j += 1
            # A string literal prefix (u8"...", L"...") belongs to the string.
            yield ("id", text[i:j])
            i = j
        else:
            yield ("punct", c)
            i += 1


def parse_table(path, errors):
    """Return the names of nativeLongDoubleFunctions in PATH, in order."""
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
    except OSError as e:
        errors.add("cannot read %s: %s" % (path, e.strerror))
        return []
    toks = list(cxx_tokens(text))
    found = []
    for k, (kind, value) in enumerate(toks):
        if kind != "id" or value != TABLE:
            continue
        j = k + 1
        if j < len(toks) and toks[j] == ("punct", "["):
            while j < len(toks) and toks[j] != ("punct", "]"):
                j += 1
            j += 1
        if j < len(toks) and toks[j] == ("punct", "="):
            j += 1
        if j < len(toks) and toks[j] == ("punct", "{"):
            depth, names, prev_str = 0, [], False
            for kind2, value2 in toks[j:]:
                if (kind2, value2) == ("punct", "{"):
                    depth += 1
                elif (kind2, value2) == ("punct", "}"):
                    depth -= 1
                    if depth == 0:
                        break
                if kind2 == "str":
                    if prev_str:  # adjacent literals concatenate
                        names[-1] += value2
                    else:
                        names.append(value2)
                prev_str = kind2 == "str"
            found.append(names)
    if not found:
        errors.add("%s: no definition of %s with an initializer" % (path, TABLE))
        return []
    if len(found) > 1:
        errors.add("%s: %d definitions of %s with an initializer"
                   % (path, len(found), TABLE))
    names = found[0]
    seen = set()
    for name in names:
        if not re.match(r"[A-Za-z_]\w*$", name):
            errors.add("%s: %s entry \"%s\" is not a C name" % (path, TABLE, name))
        if name in seen:
            errors.add("%s: %s lists '%s' twice" % (path, TABLE, name))
        seen.add(name)
    if len(names) < MIN_TABLE_NAMES:
        errors.add("%s: %s has %d names, fewer than %d: not the table"
                   % (path, TABLE, len(names), MIN_TABLE_NAMES))
    return names


def check_table(table_path, fwd_path, out=sys.stdout, err=sys.stderr):
    errors = Errors()
    names = parse_table(table_path, errors)
    guards, not_fwd, _ = parse_forwarders(fwd_path, errors)
    table = set(names)
    for name in names:
        if name in guards and name in not_fwd:
            errors.add("'%s' is both forwarded and 'not forwarded'" % name)
        elif name not in guards and name not in not_fwd:
            errors.add("'%s' of %s is neither forwarded nor 'not forwarded'"
                       % (name, TABLE))
    for name in sorted(guards):
        if F128_RE.search(name):
            errors.add("forwarder '%s' names an f64x/f128 function, which stays "
                       "a link error" % name)
        if names and name not in table:
            errors.add("forwarder '%s' is not in %s" % (name, TABLE))
    for name in sorted(not_fwd):
        if names and name not in table:
            errors.add("'not forwarded: %s' is not in %s" % (name, TABLE))
    for name in CITED_FORWARDED:
        if names and name not in table:
            errors.add("'%s' (cited by the plan) is not in %s" % (name, TABLE))
        elif name not in guards:
            errors.add("'%s' (cited by the plan) is not forwarded" % name)
    if errors.lines:
        return errors.report(err)
    print("%s: table: %d names, %d forwarded, %d not forwarded: OK"
          % (PROG, len(names), len(guards), len(not_fwd)), file=out)
    return 0


# --- the archive ------------------------------------------------------------


def is_msvc(triple):
    return "-windows-msvc" in triple


def is_wide(triple):
    """hasWideNativeLongDouble: the Linux triples and x86_64 MinGW."""
    return "-linux-" in triple or (triple.startswith("x86_64-") and "-windows-gnu" in triple)


def norm(path):
    return os.path.normcase(os.path.normpath(path.replace("\\", "/")))


def read_one_line(path, errors, what):
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read().strip()
    except OSError as e:
        errors.add("cannot read %s (%s): %s" % (path, what, e.strerror))
        return None
    if not text or "\n" in text:
        errors.add("%s (%s) does not hold one line: %r" % (path, what, text))
        return None
    return text


def parse_nm(text, errors, archive):
    """Return [(member, [(type, name)])] from 'llvm-nm --defined-only'."""
    members, cur = [], None
    for line in text.splitlines():
        if not line.strip():
            continue
        if line.endswith(":") and " " not in line:
            cur = (line[:-1], [])
            members.append(cur)
            continue
        parts = line.split()
        if cur is None or len(parts) < 2 or len(parts[-2]) != 1:
            errors.add("%s: unexpected llvm-nm line: %r" % (archive, line))
            continue
        cur[1].append((parts[-2], parts[-1]))
    return members


def a5_family(name):
    """The A5 family of a compiler-rt NAME (or member stem), or None."""
    if name.startswith("__atomic_"):
        return "__atomic_*"
    if name in A5_HOSTED or name.startswith("__emutls_"):
        return "hosted"
    m = MODE_NAME_RE.match(name)
    if m:
        modes = re.findall("..", m.group(2))
        for mode in modes:
            if mode in A5_MODES:
                return A5_MODES[mode]
    return None


def counterpart(name):
    """The function that the forwarder NAME references, or NO_RULE when its
    name gives none."""
    if name in COUNTERPARTS:
        return COUNTERPARTS[name]
    if name in LD_FAMILIES:
        return name.replace("ld", "d", 1)
    if name.endswith("l"):
        return name[:-1]
    return NO_RULE


def split_words(text):
    """Split a command string into words at blanks; "..." and '...' quote
    without escape characters, and a quoted part joins the text next to it
    (msil-equiv.py's and msil-prog.py's rule). None for an unterminated
    quote."""
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
        return None
    if started:
        words.append("".join(cur))
    return words


def read_archive(path, errors):
    """Return [(member name, bytes)] of the ar archive PATH in member order
    (GNU, COFF and BSD names; the symbol tables and the long name table are
    left out), or None after an error."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        errors.add("cannot read %s: %s" % (path, e.strerror))
        return None
    if not data.startswith(AR_MAGIC):
        errors.add("%s: not an ar archive" % path)
        return None
    off, longnames, members = len(AR_MAGIC), b"", []
    while off < len(data):
        hdr = data[off:off + 60]
        size = hdr[48:58].decode("ascii", "replace").strip()
        if len(hdr) < 60 or hdr[58:60] != b"`\n" or not size.isdigit():
            errors.add("%s: malformed member header at offset %d" % (path, off))
            return None
        size = int(size)
        body = data[off + 60:off + 60 + size]
        if len(body) != size:
            errors.add("%s: the member at offset %d is truncated" % (path, off))
            return None
        raw = hdr[:16].decode("utf-8", "replace").rstrip(" ")
        start, off = off, off + 60 + size + (size & 1)
        if raw in AR_SYMBOL_TABLES:
            continue
        if raw == "//":
            longnames = body
            continue
        if raw.startswith("#1/") and raw[3:].isdigit():
            # BSD: the name is the first <n> bytes of the member.
            n = int(raw[3:])
            name, body = body[:n].rstrip(b"\0").decode("utf-8", "replace"), body[n:]
        elif raw.startswith("/") and raw[1:].isdigit():
            # An offset into the long name table, where a COFF archive ends
            # each name with a NUL and a GNU one with "/\n".
            rest = longnames[int(raw[1:]):]
            ends = [i for i in (rest.find(b"\0"), rest.find(b"\n")) if i >= 0]
            name = rest[:min(ends) if ends else len(rest)].decode("utf-8", "replace")
        else:
            name = raw
        if name.endswith("/"):
            name = name[:-1]
        if not name:
            errors.add("%s: the member at offset %d has no name" % (path, start))
            return None
        members.append((name, body))
    return members


def unquote(text):
    """A name in the canonical listing's quoting (5.7.10): \\\\, \\' and
    \\xHH for the control bytes; everything else is raw UTF-8."""
    out, i = bytearray(), 0
    while i < len(text):
        if text[i] == "\\" and text[i + 1:i + 2] == "x":
            out.append(int(text[i + 2:i + 4], 16))
            i += 4
        elif text[i] == "\\":
            out += text[i + 1:i + 2].encode("utf-8")
            i += 2
        else:
            out += text[i].encode("utf-8")
            i += 1
    return out.decode("utf-8", "replace")


def dump_link_info(cmd, path, members, errors):
    """Write each of MEMBERS ([(name, bytes)]) to a private directory, run
    'CMD --link-info' on them and return a list parallel to MEMBERS: the
    member's symbols [(kind, binding, visibility, {flags}, name)], or None
    when it has no record (an error names it)."""
    result = [None] * len(members)
    tmp = tempfile.mkdtemp(prefix="helper-archive-")
    try:
        files = []
        for i, (_, data) in enumerate(members):
            files.append("m%04d.o" % i)
            with open(os.path.join(tmp, files[-1]), "wb") as fh:
                fh.write(data)
        batches, size = [], DUMP_BATCH_CHARS
        for i, f in enumerate(files):
            if size + len(f) + 1 > DUMP_BATCH_CHARS:
                batches.append([])
                size = 0
            batches[-1].append(i)
            size += len(f) + 1
        for batch in batches:
            names = [files[i] for i in batch]
            try:
                res = subprocess.run(cmd + ["--link-info"] + names, cwd=tmp,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                     universal_newlines=True)
            except OSError as e:
                errors.add("cannot run %s: %s" % (cmd[0], e.strerror))
                return result
            index = {files[i]: i for i in batch}
            failed = {}
            for line in res.stderr.splitlines():
                m = DUMP_ERROR_RE.match(line.strip())
                if m and m.group(1) in index:
                    failed[index[m.group(1)]] = m.group(2)
            if res.returncode != 0 and not failed:
                errors.add("'%s --link-info' exited with %d: %s"
                           % (" ".join(cmd), res.returncode, res.stderr.strip()))
                continue
            for i, why in sorted(failed.items()):
                errors.add("%s: cannot read the link-info record of %s: %s"
                           % (path, members[i][0], why))
            records = {}  # member index -> (has a LinkInfo line, symbols)
            cur = batch[0] if len(batch) == 1 else None
            for line in res.stdout.splitlines():
                if line.startswith("File: "):
                    cur = index.get(line[len("File: "):].strip())
                    continue
                if cur is None:
                    continue
                rec = records.setdefault(cur, [False, []])
                if line.startswith("LinkInfo "):
                    rec[0] = True
                elif line.startswith("Symbol "):
                    m = LINK_SYMBOL_RE.match(line)
                    if not m:
                        errors.add("%s: %s: unexpected link-info line: %r"
                                   % (path, members[cur][0], line))
                        continue
                    kind, binding, vis, flags, name = m.groups()
                    rec[1].append((kind, binding, vis, set(flags.split()), unquote(name)))
            for i in batch:
                if i in failed:
                    continue
                if not records.get(i, [False])[0]:
                    errors.add("%s: '%s --link-info' printed no link-info record "
                               "for %s" % (path, " ".join(cmd), members[i][0]))
                    continue
                result[i] = records[i][1]
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return result


def check_link_info(triple, path, cmd, nm_members, errors):
    """The --cil-dump checks of the archive PATH; returns the summary line,
    or None when the members could not be read."""
    members = read_archive(path, errors)
    if members is None:
        return None
    present = {name for name, _ in members}
    for name in nm_members:
        if name not in present:
            errors.add("%s: llvm-nm lists the member %s, but the archive as read "
                       "here has no such member" % (path, name))
    infos = dump_link_info(cmd, path, members, errors)
    n_globals = n_forwarders = 0
    for (member, _), syms in zip(members, infos):
        if syms is None:
            continue
        for kind, binding, vis, flags, name in syms:
            if "UNDEFINED" in flags or binding == "LOCAL":
                continue
            n_globals += 1
            if vis != "HIDDEN":
                errors.add("%s: %s defines %s with %s visibility, not HIDDEN "
                           "(plan 5.14.3 A2, A7)" % (path, member, name, vis))
        if not (member.startswith(FORWARDER_PREFIX) and member.endswith(MEMBER_SUFFIX)):
            continue
        n_forwarders += 1
        fwd = member[len(FORWARDER_PREFIX):-len(MEMBER_SUFFIX)]
        want = counterpart(fwd)
        found = sorted({name for _, _, _, flags, name in syms if "UNDEFINED" in flags})
        if want is NO_RULE:
            errors.add("%s: %s: the name %s gives no counterpart (references %s; "
                       "add it to COUNTERPARTS of %s)"
                       % (path, member, fwd, ", ".join(found) or "nothing",
                          os.path.basename(__file__)))
        elif found != [want]:
            errors.add("%s: %s references %s, expected exactly %s (plan 5.14.3 A4)"
                       % (path, member, ", ".join(found) or "nothing", want))
    return ("%s: link-info %s: %d members, %d defined globals, all HIDDEN; %d long "
            "double forwarders, each referencing only its counterpart"
            % (PROG, triple, len(members), n_globals, n_forwarders))


def check_archive(triple, path_file, rdir_file, nm_cmd, fwd_path, out=sys.stdout,
                  err=sys.stderr, cil_dump_cmd=None):
    """NM_CMD is the llvm-nm command line (a list) before its options;
    CIL_DUMP_CMD, when given, the llvm-cil-dump command line (a list)."""
    errors = Errors()
    path = read_one_line(path_file, errors, "-print-libgcc-file-name")
    rdir = read_one_line(rdir_file, errors, "-print-resource-dir")
    guards, _, _ = parse_forwarders(fwd_path, errors)
    if path is None or rdir is None or errors.lines:
        return errors.report(err)
    want = "clang_rt.builtins.lib" if is_msvc(triple) else "libclang_rt.builtins.a"
    p = path.replace("\\", "/")
    base, tdir = os.path.basename(p), os.path.dirname(p)
    libdir, top = os.path.dirname(tdir), os.path.dirname(os.path.dirname(tdir))
    if (base != want or os.path.basename(tdir) != triple
            or os.path.basename(libdir) != "lib" or norm(top) != norm(rdir)):
        errors.add("%s: not <resource dir>/lib/%s/%s (resource dir %s; plan "
                   "5.14.3 A1)" % (path, triple, want, rdir))
    if not os.path.isfile(path):
        errors.add("%s: no such file: build the helper archives with "
                   "'python msil/utils/build/build-builtins.py' (plan 5.14.3 A2)"
                   % path)
    if errors.lines:
        return errors.report(err)
    try:
        res = subprocess.run(nm_cmd + ["--defined-only", path], stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, universal_newlines=True)
    except OSError as e:
        errors.add("cannot run %s: %s" % (nm_cmd[0], e.strerror))
        return errors.report(err)
    if res.returncode != 0:
        errors.add("'%s --defined-only %s' exited with %d: %s"
                   % (" ".join(nm_cmd), path, res.returncode, res.stderr.strip()))
        return errors.report(err)
    members = parse_nm(res.stdout, errors, path)
    defined = {}  # global name -> member
    names_of = {}  # member -> [global names]
    for member, syms in members:
        if member in names_of:
            errors.add("%s: member %s appears twice" % (path, member))
        if not member.endswith(".c.o"):
            errors.add("%s: member %s is not named <stem>.c.o (plan 5.14.3 A2)"
                       % (path, member))
        fam = a5_family("__" + member[:-4]) if member.endswith(".c.o") else None
        if fam and not member.startswith("msil_"):
            errors.add("%s: member %s comes from a source that A5 keeps out (%s)"
                       % (path, member, fam))
        globs = [name for kind, name in syms if kind.isupper()]
        names_of[member] = globs
        for name in globs:
            if name in defined and defined[name] != member:
                errors.add("%s: %s is defined by %s and %s"
                           % (path, name, defined[name], member))
            defined.setdefault(name, member)
            fam = a5_family(name)
            if fam:
                errors.add("%s: %s defines %s, which A5 keeps out (%s)"
                           % (path, member, name, fam))
    required = A4_ALL + (A4_LINUX if "-linux-" in triple else [])
    for name in required:
        if name not in defined:
            errors.add("%s: %s is not defined (plan 5.14.3 A4)" % (path, name))
    ld_members = {m for m in names_of if m.startswith("msil_longdouble.")}
    if is_wide(triple):
        for name in sorted(guards):
            member = "msil_longdouble.%s.c.o" % name
            if member not in names_of:
                errors.add("%s: no member %s for the forwarder %s" % (path, member, name))
            elif names_of[member] != [name]:
                errors.add("%s: %s defines %s, not exactly the forwarder %s"
                           % (path, member, ", ".join(names_of[member]) or "nothing", name))
        for member in sorted(ld_members):
            stem = member[len("msil_longdouble."):-len(".c.o")]
            if stem not in guards:
                errors.add("%s: member %s has no guard in msil_longdouble.c"
                           % (path, member))
    else:
        for member in sorted(ld_members):
            errors.add("%s: member %s on a triple whose native long double is "
                       "double" % (path, member))
        for name in sorted(guards):
            if name in defined:
                errors.add("%s: %s defines the forwarder name %s on a triple whose "
                           "native long double is double" % (path, defined[name], name))
    link_line = None
    if cil_dump_cmd:
        link_line = check_link_info(triple, path, cil_dump_cmd, names_of, errors)
    if errors.lines:
        return errors.report(err)
    if link_line:
        print(link_line, file=out)
    print("%s: archive %s: %d members, %d globals: OK"
          % (PROG, triple, len(names_of), len(defined)), file=out)
    return 0


# --- self-test --------------------------------------------------------------

GOOD_TABLE = """
// The table (a comment with "quoted" text and nativeLongDoubleFunctions).
static const char *const nativeLongDoubleFunctions[] = {
    "__finitel", /* a "comment" */ "cabsl", "cexpl", "daddl", "ddivl",
    "dfmal", "dmull", "dsqrtl", "dsubl", "exp10l", "expl", "fabsl", "faddl",
    "frexpl", "fromfpl", "ldexpl", "modfl", "nanl", "nexttoward",
    "nexttowardf", "nexttowardl", "pow" "l", "significandl", "sincosl",
    "sqrtl", "strtold", "wcstold",
%s};
bool isNativeLongDoubleFunction(StringRef Name) {
  return llvm::binary_search(nativeLongDoubleFunctions, Name);
}
"""
FILLER = ["zz%03dl" % i for i in range(80)]
GOOD_FWD_HEAD = """// The long double forwarders.
// not forwarded: fromfpl: no double counterpart with this signature
#ifndef MSIL_MEMBER
#define MSIL_MEMBER(n) 1
#endif
"""


def self_test_inputs(table_extra="", fwd_extra="", drop=(), fwd_edit=None):
    names = ["__finitel", "cabsl", "cexpl", "daddl", "ddivl", "dfmal", "dmull",
             "dsqrtl", "dsubl", "exp10l", "expl", "fabsl", "faddl", "frexpl",
             "ldexpl", "modfl", "nanl", "nexttoward", "nexttowardf",
             "nexttowardl", "powl", "significandl", "sincosl", "sqrtl",
             "strtold", "wcstold"] + FILLER
    table = GOOD_TABLE % "".join('    "%s",\n' % n for n in FILLER + [table_extra] if n)
    fwd = GOOD_FWD_HEAD
    for name in names:
        if name in drop:
            continue
        fwd += "#if MSIL_MEMBER(%s)\ndouble %s(double x) { return x; }\n#endif\n" % (name, name)
    fwd += fwd_extra
    if fwd_edit:
        fwd = fwd_edit(fwd)
    return table, fwd


def run_self_test():
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        def write(name, text):
            p = os.path.join(tmp, name)
            with open(p, "w", encoding="utf-8") as f:
                f.write(text)
            return p

        def expect(label, table, fwd, status, want):
            nonlocal failures
            from io import StringIO
            o, e = StringIO(), StringIO()
            got = check_table(write("t.cpp", table), write("f.c", fwd), o, e)
            text = o.getvalue() + e.getvalue()
            ok = got == status and all(w in text for w in want)
            print("self-test: %s: %s" % ("ok" if ok else "FAIL", label))
            if not ok:
                failures += 1
                print(text, end="")

        t, f = self_test_inputs()
        expect("good", t, f, 0, ["table: 107 names, 106 forwarded, 1 not forwarded: OK"])
        t, f = self_test_inputs(drop=("sqrtl",))
        expect("unlisted table name", t, f, 1,
               ["'sqrtl' of nativeLongDoubleFunctions is neither forwarded nor 'not forwarded'"])
        t, f = self_test_inputs(fwd_extra="#if MSIL_MEMBER(foo)\nfoo\n#endif\n")
        expect("guard outside the table", t, f, 1, ["forwarder 'foo' is not in nativeLongDoubleFunctions"])
        t, f = self_test_inputs(fwd_extra="#if MSIL_MEMBER(sqrtf128)\nsqrtf128\n#endif\n")
        expect("f128 forwarder", t, f, 1, ["forwarder 'sqrtf128' names an f64x/f128 function"])
        t, f = self_test_inputs(fwd_extra="#if MSIL_MEMBER(expf64x)\nexpf64x\n#endif\n")
        expect("f64x forwarder", t, f, 1, ["forwarder 'expf64x' names an f64x/f128 function"])
        t, f = self_test_inputs(fwd_extra="// not forwarded: bar: reason\n")
        expect("'not forwarded' outside the table", t, f, 1, ["'not forwarded: bar' is not in"])
        t, f = self_test_inputs(fwd_extra="// not forwarded: sqrtl: reason\n")
        expect("forwarded and not forwarded", t, f, 1, ["'sqrtl' is both forwarded and 'not forwarded'"])
        t, f = self_test_inputs(fwd_edit=lambda s: s.replace("no double counterpart with this signature", ""))
        expect("empty reason", t, f, 1, ["'not forwarded: fromfpl' has no reason"])
        t, f = self_test_inputs(fwd_extra="#if MSIL_MEMBER(expl)\nexpl\n#endif\n")
        expect("guarded twice", t, f, 1, ["'expl' is guarded twice"])
        t, f = self_test_inputs(fwd_edit=lambda s: s.replace(
            "#if MSIL_MEMBER(cabsl)", "#if MSIL_MEMBER(cabsl) || MSIL_MEMBER(cexpl)"))
        expect("non-canonical guard", t, f, 1, ["is not a guard line '#if MSIL_MEMBER(<name>)'"])
        t, f = self_test_inputs(fwd_edit=lambda s: s.replace(
            "#if MSIL_MEMBER(nanl)\ndouble nanl(", "#if MSIL_MEMBER(nanl)\ndouble other("))
        expect("block without its name", t, f, 1, ["the block guarded by MSIL_MEMBER(nanl) does not mention nanl"])
        t, f = self_test_inputs(drop=("strtold",), fwd_extra="// not forwarded: strtold: reason\n")
        expect("cited name not forwarded", t, f, 1, ["'strtold' (cited by the plan) is not forwarded"])
        t, f = self_test_inputs(table_extra="a b")
        expect("entry that is not a name", t, f, 1, ['entry "a b" is not a C name'])
        t, f = self_test_inputs(table_extra="sqrtl")
        expect("table name twice", t, f, 1, ["lists 'sqrtl' twice"])
        t, f = self_test_inputs()
        expect("no table", t.replace(TABLE, "otherTable"), f, 1,
               ["no definition of nativeLongDoubleFunctions with an initializer"])
        expect("short table", "static const char *nativeLongDoubleFunctions[] = {\"sqrtl\"};", f, 1,
               ["has 1 names, fewer than 100: not the table"])

        def expect_equal(label, got, want):
            nonlocal failures
            ok = got == want
            print("self-test: %s: %s" % ("ok" if ok else "FAIL", label))
            if not ok:
                failures += 1
                print("got %r, expected %r" % (got, want))

        def link_info(triple, member, syms, edit):
            """The link-info text of MEMBER: its llvm-nm globals as HIDDEN
            FUNCTION symbols, its locals as LOCAL DATA ones, then the
            undefined symbols, by default a forwarder's counterpart. EDIT:
            'visibility' {name: visibility}, 'undefined' [names], 'text'."""
            if "text" in edit:
                return edit["text"]
            lines = ["LinkInfo v1.0 triple=%s abi=1 abiflags=0x0 features=0x0 "
                     "datamodel=8,8,4,8" % triple]
            for kind, name in syms:
                if kind.isupper():
                    vis = edit.get("visibility", {}).get(name, "HIDDEN")
                    lines.append("FUNCTION GLOBAL %s '%s' method=0x%08X"
                                 % (vis, name, 0x06000001 + len(lines)))
                else:
                    lines.append("DATA LOCAL DEFAULT '%s' field=0x%08X segment=0 "
                                 "offset=0 size=1" % (name.replace("'", "\\'"),
                                                      0x04000001 + len(lines)))
            undefined = edit.get("undefined")
            if undefined is None:
                want = None
                if member.startswith(FORWARDER_PREFIX):
                    want = counterpart(member[len(FORWARDER_PREFIX):-len(MEMBER_SUFFIX)])
                undefined = [want] if want and want is not NO_RULE else []
            lines += ["FUNCTION GLOBAL DEFAULT UNDEFINED '%s'" % n for n in undefined]
            return "\n".join(lines[:1] + ["Symbol %d %s" % (i, line)
                                          for i, line in enumerate(lines[1:])]) + "\n"

        def write_ar(path, members, coff):
            """An ar archive of MEMBERS ([(name, bytes)]) with a symbol
            table (two in COFF form) and long names that end with a NUL
            (COFF) or with "/\\n" (GNU)."""
            def member(name, data):
                pad = b"\n" if len(data) % 2 else b""
                return ("%-16s%-12d%-6d%-6d%-8d%-10d`\n" % (name, 0, 0, 0, 644, len(data))
                        ).encode("ascii") + data + pad

            longnames, entries = b"", []
            for name, data in members:
                if len(name) >= 16:
                    entries.append(member("/%d" % len(longnames), data))
                    longnames += name.encode("utf-8") + (b"\0" if coff else b"/\n")
                else:
                    entries.append(member(name + "/", data))
            tables = [member("/", b"\0\0\0\0")] * (2 if coff else 1)
            if longnames:
                tables.append(member("//", longnames))
            with open(path, "wb") as fh:
                fh.write(AR_MAGIC + b"".join(tables + entries))

        cil_dump = write("cil-dump.py", """import sys
# A stand-in llvm-cil-dump --link-info: each file holds its listing, or
# 'CORRUPT', which it cannot read.
assert sys.argv[1] == "--link-info", sys.argv
files, failed = sys.argv[2:], False
for f in files:
    if len(files) > 1:
        print("File: " + f)
    text = open(f, encoding="utf-8").read()
    if text.startswith("CORRUPT"):
        sys.stderr.write("llvm-cil-dump: error: '%s': invalid link-info record\\n" % f)
        failed = True
    else:
        sys.stdout.write(text)
sys.exit(1 if failed else 0)
""")

        # The archive checks, on llvm-nm output from a stand-in script; with
        # LINK (a dict {member: edit} for link_info, {} for none) also on
        # link-info listings from the stand-in llvm-cil-dump, which reads
        # the members of a real archive (GNU names with GNU, else COFF).
        def archive(label, triple, members, status, want, base=None, rdir=None,
                    link=None, gnu=False, missing=()):
            nonlocal failures
            from io import StringIO
            top = os.path.join(tmp, "rd")
            name = base or ("clang_rt.builtins.lib" if is_msvc(triple) else "libclang_rt.builtins.a")
            ar = os.path.join(top, "lib", triple, name)
            os.makedirs(os.path.dirname(ar), exist_ok=True)
            if link is None:
                with open(ar, "w") as fh:
                    fh.write("!<arch>\n")
            else:
                write_ar(ar, [(m, link_info(triple, m, syms, link.get(m, {})).encode("utf-8"))
                              for m, syms in members if m not in missing], not gnu)
            nm_out = "".join("\n%s:\n%s" % (m, "".join("0 %s %s\n" % s for s in syms))
                             for m, syms in members)
            listing = write("nm.txt", nm_out)
            nm = write("nm.py", "import sys\nsys.stdout.write(open(%r).read())\n" % listing)
            pf = write("path.txt", ar + "\n")
            rf = write("rdir.txt", (rdir or top) + "\n")
            fwd = write("f.c", fwd_text)
            o, e = StringIO(), StringIO()
            got = check_archive(triple, pf, rf, [sys.executable, nm], fwd, o, e,
                                None if link is None else [sys.executable, cil_dump])
            text = o.getvalue() + e.getvalue()
            ok = got == status and all(w in text for w in want)
            print("self-test: %s: %s" % ("ok" if ok else "FAIL", label))
            if not ok:
                failures += 1
                print(text, end="")

        base = [("%s.c.o" % n[2:], [("T", n)]) for n in A4_ALL if n not in
                ("__eqdf2", "__ltdf2", "__nedf2", "__gtdf2", "__ledf2", "__gedf2", "__unorddf2")]
        base.append(("comparedf2.c.o", [("T", n) for n in ("__eqdf2", "__ltdf2", "__nedf2", "__gtdf2",
                                                          "__ledf2", "__gedf2", "__unorddf2")]))
        linux = base + [("%s.c.o" % n[2:], [("T", n)]) for n in A4_LINUX]
        # The forwarders of the archive cases: the cited names, without the
        # table's filler (the archive check has no minimum).
        _, fwd_text = self_test_inputs(drop=FILLER)
        fwd_names = sorted(set(re.findall(r"#if MSIL_MEMBER\((\w+)\)", fwd_text)))
        forwarders = [("msil_longdouble.%s.c.o" % n, [("T", n), ("t", "local")]) for n in fwd_names]
        win = "x86_64-pc-windows-msvc-msil"
        lin = "x86_64-unknown-linux-gnu-msil"
        archive("windows-msvc good", win, base, 0, ["archive %s: " % win])
        archive("linux good", lin, linux + forwarders, 0, ["archive %s: " % lin])
        archive("missing A4 helper", win, base[1:], 1, ["is not defined (plan 5.14.3 A4)"])
        archive("missing Linux helper", lin, base + forwarders, 1, ["__absvsi2 is not defined"])
        archive("atomic definition", win, base + [("atomic.c.o", [("T", "__atomic_load_4")])], 1,
                ["defines __atomic_load_4, which A5 keeps out (__atomic_*)"])
        archive("ti definition", win, base + [("x.c.o", [("T", "__udivmodti4")])], 1,
                ["defines __udivmodti4, which A5 keeps out (*ti* (__int128))"])
        archive("ti member", win, base + [("fixdfti.c.o", [])], 1,
                ["member fixdfti.c.o comes from a source that A5 keeps out (*ti* (__int128))"])
        archive("tf definition", win, base + [("y.c.o", [("T", "__trunctfdf2")])], 1,
                ["defines __trunctfdf2, which A5 keeps out (fp128)"])
        archive("xc definition", win, base + [("z.c.o", [("T", "__mulxc3")])], 1,
                ["defines __mulxc3, which A5 keeps out (x87)"])
        archive("clear_cache", lin, linux + forwarders + [("clear_cache.c.o", [("T", "__clear_cache")])], 1,
                ["defines __clear_cache, which A5 keeps out (hosted)"])
        archive("forwarder on windows-msvc", win, base + forwarders[:1], 1,
                ["member %s on a triple whose native long double is double" % forwarders[0][0]])
        archive("forwarder missing on linux", lin, linux + forwarders[1:], 1,
                ["no member %s for the forwarder" % forwarders[0][0]])
        archive("forwarder member with two globals", lin,
                linux + [(forwarders[0][0], [("T", fwd_names[0]), ("T", "extra")])] + forwarders[1:], 1,
                ["defines %s, extra, not exactly the forwarder" % fwd_names[0]])
        archive("duplicate definition", win, base + [("dup.c.o", [("T", "__muldc3")])], 1,
                ["__muldc3 is defined by muldc3.c.o and dup.c.o"])
        archive("member name", win, base + [("odd.o", [("T", "x")])], 1,
                ["member odd.o is not named <stem>.c.o"])
        archive("location", win, base, 1, ["not <resource dir>/lib/%s/clang_rt.builtins.lib" % win],
                base="libclang_rt.builtins.a")
        archive("resource dir", win, base, 1, ["(resource dir"], rdir=os.path.join(tmp, "elsewhere"))

        # --cil-dump: visibility and the forwarders' undefined symbols.
        rules = {
            "sqrtl": "sqrt", "__finitel": "__finite", "cexpl": "cexp", "llogbl": "llogb",
            "strtold": "strtod", "strtold_l": "strtod_l", "__strtold_l": "__strtod_l",
            "__strtold_internal": "__strtod_internal", "wcstold": "wcstod",
            "wcstold_l": "wcstod_l", "__wcstold_l": "__wcstod_l",
            "__wcstold_internal": "__wcstod_internal", "lgammal_r": "lgamma_r",
            "daddl": "f32xaddf64", "dsubl": "f32xsubf64", "dmull": "f32xmulf64",
            "ddivl": "f32xdivf64", "dfmal": "fma",
            "dsqrtl": "sqrt", "nexttoward": "nextafter", "nexttowardl": "nextafter",
            "nexttowardf": "nextafterf", "pow10l": "exp10", "strfroml": "strfromd",
            "qecvt": "ecvt", "qfcvt": "fcvt", "qgcvt": "gcvt", "qecvt_r": "ecvt_r",
            "qfcvt_r": "fcvt_r", "frexpf": NO_RULE,
        }
        expect_equal("counterparts", {n: counterpart(n) for n in rules}, rules)
        expect_equal("command words",
                     [split_words('"C:/a b/py.exe" "x.py" --tool -- \'c d\'e'), split_words("'x")],
                     [["C:/a b/py.exe", "x.py", "--tool", "--", "c de"], None])
        quoted = [("comparedf2.c.o", base[-1][1] + [("d", "it's")])]
        archive("windows-msvc good (link-info)", win, base[:-1] + quoted, 0,
                ["link-info %s: %d members, " % (win, len(base)),
                 "all HIDDEN; 0 long double forwarders", "archive %s: " % win], link={})
        archive("linux good (link-info)", lin, linux + forwarders, 0,
                ["link-info %s: %d members, " % (lin, len(linux) + len(forwarders)),
                 "; %d long double forwarders, each referencing only its counterpart"
                 % len(forwarders), "archive %s: " % lin], link={})
        archive("linux good (link-info, GNU long names)", lin, linux + forwarders, 0,
                ["link-info %s: " % lin], link={}, gnu=True)
        archive("default visibility", lin, linux + forwarders, 1,
                ["muldc3.c.o defines __muldc3 with DEFAULT visibility, not HIDDEN"],
                link={"muldc3.c.o": {"visibility": {"__muldc3": "DEFAULT"}}})
        archive("forwarder calling the wrong counterpart", lin, linux + forwarders, 1,
                ["msil_longdouble.expl.c.o references exp2, expected exactly exp"],
                link={"msil_longdouble.expl.c.o": {"undefined": ["exp2"]}})
        archive("forwarder calling two functions", lin, linux + forwarders, 1,
                ["msil_longdouble.sqrtl.c.o references fabs, sqrt, expected exactly sqrt"],
                link={"msil_longdouble.sqrtl.c.o": {"undefined": ["sqrt", "fabs"]}})
        archive("forwarder calling nothing", lin, linux + forwarders, 1,
                ["msil_longdouble.strtold.c.o references nothing, expected exactly strtod"],
                link={"msil_longdouble.strtold.c.o": {"undefined": []}})
        # daddl, dsubl, dmull and ddivl: glibc's f32x*f64 functions (they set
        # errno), never another function and never one operation in double.
        archive("narrowing forwarder calling the wrong function", lin, linux + forwarders, 1,
                ["msil_longdouble.daddl.c.o references fma, expected exactly f32xaddf64"],
                link={"msil_longdouble.daddl.c.o": {"undefined": ["fma"]}})
        archive("narrowing forwarder as one operation in double", lin, linux + forwarders, 1,
                ["msil_longdouble.ddivl.c.o references nothing, expected exactly f32xdivf64"],
                link={"msil_longdouble.ddivl.c.o": {"undefined": []}})
        odd = [("msil_longdouble.zz.c.o", [("T", "zz")])]
        archive("forwarder without a counterpart rule", lin, linux + forwarders + odd, 1,
                ["msil_longdouble.zz.c.o: the name zz gives no counterpart (references nothing"],
                link={})
        archive("member without a record", lin, linux + forwarders, 1,
                ["cannot read the link-info record of muldc3.c.o: invalid link-info record"],
                link={"muldc3.c.o": {"text": "CORRUPT"}})
        archive("member that llvm-nm lists and the archive lacks", lin, linux + forwarders, 1,
                ["llvm-nm lists the member muldc3.c.o, but the archive as read here has no "
                 "such member"], link={}, missing=("muldc3.c.o",))
    print("self-test: %s" % ("FAILED" if failures else "passed"))
    return 1 if failures else 0


def main(argv):
    if argv[1:] == ["--self-test"]:
        return run_self_test()
    parser = argparse.ArgumentParser(prog=PROG, description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="mode")
    t = sub.add_parser("table")
    t.add_argument("native_libraries")
    t.add_argument("msil_longdouble")
    a = sub.add_parser("archive")
    a.add_argument("--triple", required=True)
    a.add_argument("--path-file", required=True)
    a.add_argument("--resource-dir-file", required=True)
    a.add_argument("--nm", required=True)
    a.add_argument("--forwarders", required=True)
    a.add_argument("--cil-dump", help="the llvm-cil-dump command (one string)")
    try:
        args = parser.parse_args(argv[1:])
    except SystemExit as e:
        return 2 if e.code else 0
    if args.mode == "table":
        return check_table(args.native_libraries, args.msil_longdouble)
    if args.mode == "archive":
        cil_dump = None
        if args.cil_dump is not None:
            cil_dump = split_words(args.cil_dump)
            if not cil_dump:
                parser.print_usage(sys.stderr)
                print("%s: error: --cil-dump: %r is not a command" % (PROG, args.cil_dump),
                      file=sys.stderr)
                return 2
        return check_archive(args.triple, args.path_file, args.resource_dir_file,
                             [args.nm], args.forwarders, cil_dump_cmd=cil_dump)
    parser.print_usage(sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))

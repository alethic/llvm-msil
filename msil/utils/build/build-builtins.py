#!/usr/bin/env python3
# ===-- build-builtins.py - The MSIL helper archives clang_rt.builtins ----===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Build the helper archives clang_rt.builtins of the MSIL triples.

  python msil/utils/build/build-builtins.py [--build-dir DIR] [--triple T]...
      [--all] [--jobs N] [--out DIR] [--check] [--check-list] [--list]
      [--native-oracle DIR] [--self-test]

Each MSIL triple gets one archive of CIL objects (plan 5.14.3): compiler-rt's
builtins and the toolchain's own C files, as msil/lib/builtins/sources.txt
lists them. The clang driver passes the archive to msil-ld when the file
exists, and msil-ld extracts a member only for an undefined symbol, so a
program carries exactly the helpers it calls; the members are hidden
(-fvisibility=hidden), so no output exports one.

--build-dir DIR gives bin/clang and bin/llvm-ar to a build, --check and
--native-oracle; without it they use $MSIL_BUILD_DIR when it is set and not
empty, else <repository>/build, the shared build (the directory rule of the
other build scripts, msil/utils/build/README.md). --list, --check-list and
--self-test use no build directory. Triples: x86_64-pc-windows-msvc-msil and
x86_64-unknown-linux-gnu-msil by default, or those given with --triple
(repeatable); --all adds x86_64-w64-windows-gnu-msil,
x86_64-unknown-linux-musl-msil, aarch64-unknown-linux-gnu-msil and
aarch64-pc-windows-msvc-msil (compiled and archived, never run). An MSIL
triple has the object format suffix -msil or an msil arch (msil32, msil64).
The portable triple msil64-unknown-dotnet (plan 5.18.8) is built only when
--triple names it: it is in neither the default set nor --all, and --check
and --list cover it only when named (msil32-unknown-dotnet is accepted here,
but its compiles fail until P-4, as clang's backend refuses it). Each archive
is plan 5.14.3 A1's per-target file: <resource dir>/lib/<T>/
clang_rt.builtins.lib on *-windows-msvc* triples and <resource dir>/lib/<T>/
libclang_rt.builtins.a otherwise, with the resource directory from
'clang -print-resource-dir' (the script creates lib/<T>/), or
<DIR>/<T>/<that name> with --out DIR (private builds outside the shared
build). --jobs N runs N compiles at a time (default: one per processor).

The driver passes the archive to msil-ld through 'clang --target=<T>
-rtlib=compiler-rt -print-libgcc-file-name', which names the per-target file
once it exists. Until then clang names the old layout's
<resource dir>/lib/<os>/[lib]clang_rt.builtins-<arch>.<ext> when that file
exists, and that file is the native target's compiler-rt (a compiler-rt
build with LLVM_ENABLE_PER_TARGET_RUNTIME_DIR=OFF, the default on Windows
hosts and the layout of LLVM's Windows releases). So the script never asks
clang where to write, and a build, --check and a failure's cleanup never
write, check, replace or remove an old-layout file. A build into the
resource directory (not --out) then asks clang again for each triple, also
for an archive that was up to date, and when clang names another file, which
shadows the helper archive, the script fails with a usage error naming it.

sources.txt has one line per source, '<repository-relative path>[ <triple
glob>[,<triple glob>...]]' (no glob: every triple; each glob is an fnmatch
pattern for the triple as named here), and '#' starts a comment. Every .c
file of GENERIC_SOURCES and BF16_SOURCES in
compiler-rt/lib/builtins/CMakeLists.txt is either listed or named by a line
'# excluded <path>: <category>: <reason>', where <category> is int128, fp128,
x87, hosted or atomic. Only .c files are listed (MSIL has no assembler). A
path listed twice, listed and excluded, a malformed line or an unknown
category is an error.

Each listed source is compiled once, from the repository root and with
relative, '/'-separated paths, so that neither __FILE__ nor any object depends
on the host, the checkout or the output directory:

  clang --target=<T> -c -O2 -std=c11 -ffreestanding -fno-builtin
      -fvisibility=hidden -DCOMPILER_RT_HAS_FLOAT16 -Wno-c2y-extensions
      -I compiler-rt/lib/builtins -o <obj> <src>

with -fms-compatibility-version=19.33 after --target on *-windows-msvc*
triples: clang writes the MSVC version it finds into such a triple, and the
object records it, so without the pin a build inside a Visual Studio
environment differs from one outside it (msil/utils/ci/det-manifest.py pins
it for the same reason). The member of <dir>/<stem>.c is <stem>.c.o. A source
whose functions sit under '#if MSIL_MEMBER(<name>)' lines (a line of exactly
that form) is compiled once per guarded name instead, into
<stem>.<name>.c.o, with -D"MSIL_MEMBER(n)=MSIL_ONLY_##n" -DMSIL_ONLY_<name>=1
added before -o, so that each such member defines one function and references
only what that function calls (msil_longdouble.c's forwarders). The members
follow sources.txt's order (a split source's members follow its guards'
order), and two sources that give one member name for any triple of --all,
any portable triple or any triple of the command line are an error. 'llvm-ar
rcsD' writes the archive (from the object directory, with bare member names),
so equal objects give a byte-identical archive. The archive is replaced only
when every member compiled, and the replace is retried for a few hundred
milliseconds while another process holds the old archive open (Windows). A
failure removes that triple's archive and stamp, so a link fails loudly
instead of using a stale archive; an archive that another process still holds
open stays without its stamp, so --check reports it, and the message names it
and says to remove it once it is no longer in use. The temporary object directory goes in every
case, the later triples are still built, and the script exits 1.

<archive>.stamp records the SHA-256 of this script, of the clang and llvm-ar
binaries, of sources.txt, of every member's source and of every .h and .inc
file in the source directories, and the command lines. An archive whose stamp
matches is up to date and is not rebuilt; any other is rebuilt (the build
agent runs this script after every build, plan 5.14.3 A10).

A build, --check and --native-oracle run the build's tools under the shared
lock of msil/utils/build/msil-lock.ps1 (Windows): they wait while
msil-build.ps1 builds in --build-dir, whose relink of a running clang would
fail, and a build waits for them. So never run this script while holding
that directory's exclusive lock.

Modes (at most one; without one the script builds):
  --check         builds nothing; exits 1 when an archive is missing or its
                  stamp is missing or differs.
  --list          prints one line '<triple> <member> <source>' per member, in
                  archive order, for each triple; needs no build.
  --check-list    exits 1 unless every .c file of GENERIC_SOURCES and
                  BF16_SOURCES (every set(...) and list(APPEND ...) of the two)
                  is listed or excluded, naming each that is not, and each
                  listed or excluded path whose file is gone (the drift check
                  after an upstream merge); needs no build.
  --native-oracle DIR
                  builds the native compiler-rt archive of the Linux oracle
                  E7h (plan 5.17.3) as DIR/libclang_rt.builtins-x86_64.a, for
                  native x86_64-unknown-linux-gnu: the compiler-rt lines of
                  sources.txt for x86_64-unknown-linux-gnu-msil, the
                  GENERIC_SOURCES and BF16_SOURCES files that sources.txt
                  excludes as int128, fp128 or x87, CMakeLists.txt's
                  GENERIC_TF_SOURCES, x86_80_BIT_SOURCES and x86_ARCH_SOURCES,
                  and the x86_64 sources that native x86_64 Linux builds
                  (x86_64/floatdidf.c, floatdisf.c, floatundidf.S,
                  floatundisf.S, floatdixf.c, floatundixf.S), where an
                  architecture file supersedes the generic file of its name
                  (filter_builtin_sources), and never msil/lib/builtins/, a
                  hosted or an atomic source (atomic.c). E7h has no libgcc,
                  so the oracle carries the *ti*, TF and x87 helpers too.
                  Flags: -O2 -fPIC -fno-builtin -fvisibility=hidden
                  -fomit-frame-pointer -ffreestanding -std=c11
                  -DCOMPILER_RT_HAS_FLOAT16 -Wno-c2y-extensions
                  -DVISIBILITY_HIDDEN -I compiler-rt/lib/builtins (CMake's
                  flags and BUILTIN_DEFS; VISIBILITY_HIDDEN hides the .S
                  members as -fvisibility=hidden hides the C ones). A stamp
                  beside it skips an up-to-date rebuild.
  --self-test     checks this script offline, in a temporary directory
                  outside the repository, with stand-ins for clang and
                  llvm-ar: parsing, globs, exclusions, the portable
                  triple (only when named), --check-list on a
                  synthetic CMakeLists.txt, stale stamps, the member split,
                  duplicate members, failed compiles, the native oracle's
                  source list, A1's per-target files beside the old layout's
                  native files, the default build directory, the replace's
                  retries and, on Windows, an archive held open during a
                  rebuild and the build lock.

Exit status: 0 on success, 1 for a failed check or compile (or a broken
sources.txt), 2 for a usage error (also a missing clang or llvm-ar, or a file
that shadows a helper archive).
"""

import argparse
import concurrent.futures
import contextlib
import fnmatch
import hashlib
import io
import os
import posixpath
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
PROG = "build-builtins.py"
EXE = ".exe" if os.name == "nt" else ""

SOURCES_TXT = "msil/lib/builtins/sources.txt"
BUILTINS = "compiler-rt/lib/builtins"
CMAKE_LISTS = BUILTINS + "/CMakeLists.txt"

DEFAULT_TRIPLES = ("x86_64-pc-windows-msvc-msil", "x86_64-unknown-linux-gnu-msil")
ALL_TRIPLES = DEFAULT_TRIPLES + (
    "x86_64-w64-windows-gnu-msil",
    "x86_64-unknown-linux-musl-msil",
    "aarch64-unknown-linux-gnu-msil",
    "aarch64-pc-windows-msvc-msil",
)
# The portable triples (plan 5.18.8 PH1): built, checked and listed only when
# --triple names one, never by default or with --all; the duplicate-member
# check of sources.txt covers them always.
PORTABLE_TRIPLES = ("msil64-unknown-dotnet",)
# The arches of the portable triples, which need no -msil suffix.
MSIL_ARCHES = ("msil32", "msil64")
TRIPLE_FORM_RE = re.compile(r"^[A-Za-z0-9_.]+(-[A-Za-z0-9_.]+){2,4}$")

# The compile command of plan 5.14.3 A2, after clang and --target=<T>.
CFLAGS = (
    "-c",
    "-O2",
    "-std=c11",
    "-ffreestanding",
    "-fno-builtin",
    "-fvisibility=hidden",
    "-DCOMPILER_RT_HAS_FLOAT16",
    "-Wno-c2y-extensions",
    "-I",
    BUILTINS,
)
MSVC_VERSION_FLAG = "-fms-compatibility-version=19.33"
MEMBER_FLAG = "-DMSIL_MEMBER(n)=MSIL_ONLY_##n"
AR_OPERATION = "rcsD"

# Exclusion categories; True when the native oracle adds the source back.
CATEGORIES = {
    "int128": True,
    "fp128": True,
    "x87": True,
    "hosted": False,
    "atomic": False,
}

NATIVE_TRIPLE = "x86_64-unknown-linux-gnu"
NATIVE_FROM_TRIPLE = "x86_64-unknown-linux-gnu-msil"
NATIVE_ARCHIVE = "libclang_rt.builtins-x86_64.a"
NATIVE_CFLAGS = (
    "-c",
    "-O2",
    "-fPIC",
    "-fno-builtin",
    "-fvisibility=hidden",
    "-fomit-frame-pointer",
    "-ffreestanding",
    "-std=c11",
    "-DCOMPILER_RT_HAS_FLOAT16",
    "-Wno-c2y-extensions",
    "-DVISIBILITY_HIDDEN",
    "-I",
    BUILTINS,
)
NATIVE_CMAKE_LISTS = ("GENERIC_TF_SOURCES", "x86_80_BIT_SOURCES", "x86_ARCH_SOURCES")
# What CMakeLists.txt's 'if (NOT MSVC)' branch adds for x86_64 on Linux.
NATIVE_X86_64_SOURCES = (
    "x86_64/floatdidf.c",
    "x86_64/floatdisf.c",
    "x86_64/floatundidf.S",
    "x86_64/floatundisf.S",
    "x86_64/floatdixf.c",
    "x86_64/floatundixf.S",
)

COMPILE_TIMEOUT = 900
# Waits between the tries of an archive's replace while another process holds
# the old archive open (375 ms in all).
REPLACE_RETRY_DELAYS = (0.025, 0.05, 0.1, 0.1, 0.1)
STAMP_VERSION = 1
LOCK_FILE = ".msil-build.lock"
LOCK_WAIT_SECONDS = 240 * 60

GUARD_RE = re.compile(
    r"^[ \t]*#[ \t]*if[ \t]+MSIL_MEMBER[ \t]*\([ \t]*([A-Za-z_][A-Za-z0-9_]*)"
    r"[ \t]*\)[ \t]*(?:(?://|/\*).*)?$",
    re.M,
)
EXCLUDED_START_RE = re.compile(r"^#\s*excluded\b")
EXCLUDED_RE = re.compile(r"^#\s*excluded\s+(\S+?):\s+([A-Za-z0-9_]+):\s*(\S.*?)\s*$")


class UsageError(Exception):
    """A usage error: exit status 2."""


class CheckError(Exception):
    """A failed check or a broken input: exit status 1."""


def say(msg, file=None):
    print("%s: %s" % (PROG, msg), file=file or sys.stdout)
    (file or sys.stdout).flush()


# --------------------------------------------------------------------------
# Inputs: sources.txt, the guarded sources, CMakeLists.txt
# --------------------------------------------------------------------------


class Source:
    def __init__(self, path, globs, line):
        self.path = path
        self.globs = globs
        self.line = line

    def applies_to(self, triple):
        return not self.globs or any(fnmatch.fnmatchcase(triple, g) for g in self.globs)


class Exclusion:
    def __init__(self, path, category, reason, line):
        self.path = path
        self.category = category
        self.reason = reason
        self.line = line


class SourceList:
    def __init__(self, sources, exclusions, digest):
        self.sources = sources
        self.exclusions = exclusions  # in line order
        self.digest = digest

    def excluded(self):
        return {e.path: e for e in self.exclusions}


class Member:
    def __init__(self, name, path, guard):
        self.name = name
        self.path = path
        self.guard = guard


def check_path(path, where):
    parts = path.split("/")
    if (
        "\\" in path
        or path.startswith("/")
        or re.match(r"^[A-Za-z]:", path)
        or any(p in ("", ".", "..") for p in parts)
    ):
        raise CheckError(
            "%s: '%s' is not a repository-relative path with '/' separators" % (where, path)
        )


def parse_sources(repo):
    """Reads sources.txt; raises CheckError for a broken line."""
    path = os.path.join(repo, SOURCES_TXT)
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        raise CheckError("%s: cannot read it: %s" % (SOURCES_TXT, e.strerror))
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        raise CheckError("%s: not UTF-8" % SOURCES_TXT)
    sources, exclusions = [], []
    listed, excluded = {}, {}
    for number, raw in enumerate(text.splitlines(), 1):
        where = "%s:%d" % (SOURCES_TXT, number)
        line = raw.strip()
        if line.startswith("#"):
            if not EXCLUDED_START_RE.match(line):
                continue
            m = EXCLUDED_RE.match(line)
            if not m:
                raise CheckError(
                    "%s: malformed exclusion; the form is "
                    "'# excluded <path>: <category>: <reason>'" % where
                )
            epath, category, reason = m.groups()
            check_path(epath, where)
            if category not in CATEGORIES:
                raise CheckError(
                    "%s: unknown category '%s' (one of %s)"
                    % (where, category, ", ".join(CATEGORIES))
                )
            if epath in excluded:
                raise CheckError(
                    "%s: %s is excluded twice (also on line %d)" % (where, epath, excluded[epath])
                )
            excluded[epath] = number
            exclusions.append(Exclusion(epath, category, reason, number))
            continue
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        fields = line.split()
        if len(fields) > 2:
            raise CheckError("%s: expected '<path>[ <triple glob>[,<triple glob>...]]'" % where)
        spath = fields[0]
        check_path(spath, where)
        if not spath.endswith(".c"):
            raise CheckError("%s: %s is not a C source (.c)" % (where, spath))
        globs = ()
        if len(fields) == 2:
            globs = tuple(fields[1].split(","))
            if any(not g for g in globs):
                raise CheckError("%s: empty triple glob in '%s'" % (where, fields[1]))
        if spath in listed:
            raise CheckError(
                "%s: %s is listed twice (also on line %d)" % (where, spath, listed[spath])
            )
        listed[spath] = number
        sources.append(Source(spath, globs, number))
    for epath, number in excluded.items():
        if epath in listed:
            raise CheckError(
                "%s:%d: %s is both listed (line %d) and excluded"
                % (SOURCES_TXT, number, epath, listed[epath])
            )
    return SourceList(sources, exclusions, hashlib.sha256(data).hexdigest())


class FileCache:
    """Reads, hashes and scans the files of one run once."""

    def __init__(self, repo):
        self.repo = repo
        self.digests = {}
        self.guards = {}

    def digest(self, path):
        """SHA-256 of a repository-relative or absolute path."""
        full = path if os.path.isabs(path) else os.path.join(self.repo, path)
        if full not in self.digests:
            h = hashlib.sha256()
            with open(full, "rb") as f:
                for chunk in iter(lambda: f.read(1 << 20), b""):
                    h.update(chunk)
            self.digests[full] = h.hexdigest()
        return self.digests[full]

    def member_guards(self, path, where):
        if path not in self.guards:
            full = os.path.join(self.repo, path)
            try:
                with open(full, "rb") as f:
                    text = f.read().decode("utf-8", "replace")
            except OSError:
                raise CheckError("%s: listed source %s does not exist" % (where, path))
            names = GUARD_RE.findall(text)
            seen = set()
            for name in names:
                if name in seen:
                    raise CheckError("%s: MSIL_MEMBER(%s) guards two blocks" % (path, name))
                seen.add(name)
            self.guards[path] = names
        return self.guards[path]

    def headers(self, dirs):
        """The .h and .inc files of each directory, sorted."""
        out = []
        for d in sorted(set(dirs)):
            full = os.path.join(self.repo, d)
            try:
                names = sorted(os.listdir(full))
            except OSError:
                continue
            for name in names:
                if name.endswith((".h", ".inc")) and os.path.isfile(os.path.join(full, name)):
                    out.append(d + "/" + name)
        return out


def member_name(path, guard=None):
    base = posixpath.basename(path)
    if guard is None:
        return base + ".o"
    stem, ext = posixpath.splitext(base)
    return "%s.%s%s.o" % (stem, guard, ext)


def resolve(srclist, triple, cache):
    """The members of one triple's archive, in order."""
    members, owner = [], {}
    for src in srclist.sources:
        if not src.applies_to(triple):
            continue
        where = "%s:%d" % (SOURCES_TXT, src.line)
        guards = cache.member_guards(src.path, where)
        for guard in guards or [None]:
            m = Member(member_name(src.path, guard), src.path, guard)
            if m.name in owner:
                raise CheckError(
                    "%s: member %s comes from both %s and %s"
                    % (triple, m.name, owner[m.name], src.path)
                )
            owner[m.name] = src.path
            members.append(m)
    return members


def resolve_all(srclist, triples, cache):
    """Resolves the given triples, checking every triple of --all and the
    portable triples too."""
    result = {}
    checked = list(ALL_TRIPLES) + [t for t in PORTABLE_TRIPLES if t not in ALL_TRIPLES]
    for t in checked + [t for t in triples if t not in checked]:
        members = resolve(srclist, t, cache)
        if t in triples:
            result[t] = members
    return result


def strip_cmake_comments(text):
    text = re.sub(r"#\[(=*)\[.*?\]\1\]", " ", text, flags=re.S)
    return re.sub(r"#[^\n]*", "", text)


def parse_cmake(repo):
    """Every set(<NAME> ...) and list(APPEND <NAME> ...) of CMakeLists.txt.

    Returns {name: [items in order, without ${...} references]}.
    """
    path = os.path.join(repo, CMAKE_LISTS)
    try:
        with open(path, encoding="utf-8") as f:
            text = strip_cmake_comments(f.read())
    except OSError as e:
        raise CheckError("%s: cannot read it: %s" % (CMAKE_LISTS, e.strerror))
    lists = {}
    for m in re.finditer(r"\b(set|list)\s*\(", text, re.I):
        depth, i = 1, m.end()
        while i < len(text) and depth:
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
            i += 1
        words = [w.strip('"') for w in text[m.end() : i - 1].split()]
        if m.group(1).lower() == "list":
            if len(words) < 2 or words[0] != "APPEND":
                continue
            words = words[1:]
        if not words:
            continue
        name, items = words[0], words[1:]
        if name.startswith("${"):
            continue
        items = [w for w in items if w and "${" not in w and "$<" not in w]
        lists.setdefault(name, []).extend(items)
    return lists


def generic_sources(lists):
    """GENERIC_SOURCES and BF16_SOURCES .c files as repository paths."""
    out = {}
    for name in ("GENERIC_SOURCES", "BF16_SOURCES"):
        if name not in lists:
            raise CheckError("%s has no set(%s ...)" % (CMAKE_LISTS, name))
        for item in lists[name]:
            if item.endswith(".c"):
                out.setdefault(BUILTINS + "/" + item, name)
    return out


# --------------------------------------------------------------------------
# Tools, compiles, archives, stamps
# --------------------------------------------------------------------------


class Context:
    """Where the repository and the tools are (the self-test substitutes)."""

    def __init__(self, repo, clang, ar, clang_file, ar_file, script_file, build_dir=None):
        self.repo = repo
        self.clang = clang
        self.ar = ar
        self.clang_file = clang_file
        self.ar_file = ar_file
        self.script_file = script_file
        self.build_dir = build_dir  # whose lock a run holds; None: no lock

    @staticmethod
    def from_build_dir(build_dir, origin=None):
        """ORIGIN says where build_dir came from in errors (default: --build-dir)."""
        build_dir = os.path.abspath(build_dir)
        clang = os.path.join(build_dir, "bin", "clang" + EXE)
        ar = os.path.join(build_dir, "bin", "llvm-ar" + EXE)
        for tool in (clang, ar):
            if not os.path.isfile(tool):
                raise UsageError(
                    "%s does not exist (%s)" % (tool, origin or "--build-dir " + build_dir)
                )
        return Context(REPO, [clang], [ar], clang, ar, os.path.abspath(__file__), build_dir)


def build_dir_choice(build_dir):
    """(directory, origin for Context.from_build_dir) of a build, --check and
    --native-oracle: --build-dir, else $MSIL_BUILD_DIR when it is set and not
    empty, else the shared build <repository>/build."""
    if build_dir:
        return build_dir, None
    env = os.environ.get("MSIL_BUILD_DIR")
    if env:
        return env, "MSIL_BUILD_DIR=" + env
    return os.path.join(REPO, "build"), None


def enter_build_lock(build_dir, exclusive=False, wait_seconds=None):
    """Takes msil-lock.ps1's lock on <build_dir>/.msil-build.lock.

    Returns a function that releases it. On Windows a running executable
    cannot be relinked, so the build's tools never run while ninja writes:
    readers hold the file open for reading and share it for reading only,
    writers (msil-build.ps1) open it for reading and writing, and the system's
    sharing rules allow many readers or one writer, released when the holder
    exits. A reader waits while a writer holds it, as msil-lock.ps1 does
    (without its wait for orphaned ninja or cmake processes). Elsewhere a
    running executable can be replaced, and there is nothing to lock.
    """
    if os.name != "nt":
        return lambda: None
    if wait_seconds is None:
        wait_seconds = LOCK_WAIT_SECONDS
    import ctypes
    from ctypes import wintypes

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateFileW.restype = wintypes.HANDLE
    kernel32.CreateFileW.argtypes = (
        wintypes.LPCWSTR,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.LPVOID,
        wintypes.DWORD,
        wintypes.DWORD,
        wintypes.HANDLE,
    )
    kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
    generic_read, generic_write, file_share_read = 0x80000000, 0x40000000, 0x1
    open_always, file_attribute_normal, error_sharing_violation = 4, 0x80, 32
    invalid = ctypes.c_void_p(-1).value
    path = os.path.join(build_dir, LOCK_FILE)
    access = generic_read | (generic_write if exclusive else 0)
    deadline = time.monotonic() + wait_seconds
    announced = False
    while True:
        handle = kernel32.CreateFileW(
            path, access, file_share_read, None, open_always, file_attribute_normal, None
        )
        if handle not in (None, invalid):
            return lambda: kernel32.CloseHandle(handle)
        error = ctypes.get_last_error()
        if error != error_sharing_violation:
            raise UsageError("cannot open %s: %s" % (path, ctypes.FormatError(error).strip()))
        if not announced:
            holder = " ".join((read_text(path) or "?").split())
            say("waiting for %s (last writer: %s)" % (path, holder))
            announced = True
        left = deadline - time.monotonic()
        if left <= 0:
            raise CheckError("timed out after %d s waiting for %s" % (wait_seconds, path))
        time.sleep(min(3.0, left))


def run_tool(argv, cwd=None, timeout=300):
    try:
        p = subprocess.run(
            argv,
            cwd=cwd,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        return None, "", "timed out after %d s" % timeout
    except OSError as e:
        return None, "", str(e)
    return (
        p.returncode,
        p.stdout.decode("utf-8", "replace"),
        p.stderr.decode("utf-8", "replace"),
    )


def check_tools(*tools):
    """Usage error when a tool does not run ('<tool> --version')."""
    for argv in tools:
        code, _, err = run_tool(argv + ["--version"])
        if code != 0:
            raise UsageError("'%s --version' failed: %s" % (" ".join(argv), err.strip()[:300]))


def is_msvc(triple):
    return fnmatch.fnmatchcase(triple, "*-windows-msvc*")


def archive_path(base, triple):
    """Plan 5.14.3 A1's per-target archive under base, which is <resource
    dir>/lib or --out's directory: <base>/<T>/clang_rt.builtins.lib on
    *-windows-msvc* triples, <base>/<T>/libclang_rt.builtins.a otherwise."""
    name = "clang_rt.builtins.lib" if is_msvc(triple) else "libclang_rt.builtins.a"
    return os.path.join(base, triple, name)


def resource_lib_dir(ctx):
    """<resource dir>/lib, with the resource directory from clang."""
    code, out, err = run_tool(ctx.clang + ["-print-resource-dir"])
    printed = out.strip()
    if code != 0 or not printed:
        raise UsageError("clang -print-resource-dir failed: %s" % err.strip()[:300])
    return os.path.join(printed, "lib")


def same_file(a, b):
    if os.path.normcase(os.path.abspath(a)) == os.path.normcase(os.path.abspath(b)):
        return True
    try:
        return os.path.samefile(a, b)
    except OSError:
        return False


def shadowing_file(ctx, triple, archive):
    """The file that 'clang --target=<T> -rtlib=compiler-rt
    -print-libgcc-file-name' names when that is not archive, else None: the
    driver passes that file to msil-ld, so it shadows the helper archive."""
    code, out, err = run_tool(
        ctx.clang + ["--target=" + triple, "-rtlib=compiler-rt", "-print-libgcc-file-name"]
    )
    printed = out.strip()
    if code != 0 or not printed:
        raise UsageError(
            "clang -print-libgcc-file-name failed for %s: %s" % (triple, err.strip()[:300])
        )
    return None if same_file(printed, archive) else printed


def msil_argv(clang, triple, member, obj):
    argv = list(clang) + ["--target=" + triple]
    if is_msvc(triple):
        argv.append(MSVC_VERSION_FLAG)
    argv += CFLAGS
    if member.guard is not None:
        argv += [MEMBER_FLAG, "-DMSIL_ONLY_%s=1" % member.guard]
    return argv + ["-o", obj, member.path]


def native_argv(clang, member, obj):
    return (
        list(clang) + ["--target=" + NATIVE_TRIPLE] + list(NATIVE_CFLAGS) + ["-o", obj, member.path]
    )


def read_text(path):
    try:
        with open(path, encoding="utf-8") as f:
            return f.read()
    except (OSError, UnicodeDecodeError):
        return None


def os_error_text(e):
    return e.strerror or str(e)


def remove_quietly(path, errors=None):
    """Removes path; True when it is gone (or was never there).

    Any other failure, such as another process holding the file open on
    Windows, returns False and appends the error's text to errors (a list)."""
    try:
        os.remove(path)
    except FileNotFoundError:
        pass
    except OSError as e:
        if errors is not None:
            errors.append(os_error_text(e))
        return False
    return True


def replace_retrying(src, dst, replace=os.replace):
    """os.replace(src, dst), retried for a few hundred milliseconds on a
    PermissionError: on Windows a replace fails while another process (a
    link, llvm-nm, a virus scanner) has dst open, until it closes the file."""
    for delay in REPLACE_RETRY_DELAYS + (None,):
        try:
            replace(src, dst)
            return
        except PermissionError:
            if delay is None:
                raise
            time.sleep(delay)


def stamp_text(ctx, cache, kind, command, members, extra):
    lines = [
        "%s stamp %d" % (PROG, STAMP_VERSION),
        kind,
        "script %s" % cache.digest(ctx.script_file),
        "clang %s" % cache.digest(ctx.clang_file),
        "llvm-ar %s" % cache.digest(ctx.ar_file),
        "command %s" % command,
        "archive llvm-ar %s" % AR_OPERATION,
    ]
    lines += extra
    dirs = [BUILTINS] + [posixpath.dirname(m.path) for m in members]
    for h in cache.headers(dirs):
        lines.append("header %s %s" % (h, cache.digest(h)))
    for m in members:
        lines.append("member %s %s %s" % (m.name, m.path, cache.digest(m.path)))
    return "\n".join(lines) + "\n"


def msil_command_template(triple):
    argv = ["clang", "--target=" + triple]
    if is_msvc(triple):
        argv.append(MSVC_VERSION_FLAG)
    argv += list(CFLAGS) + ["-o", "<obj>", "<src>"]
    return " ".join(argv) + "; split: %s -DMSIL_ONLY_<name>=1" % MEMBER_FLAG


def native_command_template():
    argv = ["clang", "--target=" + NATIVE_TRIPLE] + list(NATIVE_CFLAGS)
    return " ".join(argv + ["-o", "<obj>", "<src>"])


def compile_members(ctx, members, argv_of, objdir, jobs):
    """Compiles every member into objdir; returns [(member, ok, argv, stderr)]."""

    def one(m):
        argv = argv_of(m, os.path.join(objdir, m.name))
        code, _, err = run_tool(argv, cwd=ctx.repo, timeout=COMPILE_TIMEOUT)
        return m, code == 0, argv, err

    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        return list(ex.map(one, members))


def make_archive(ctx, label, archive, stamp, members, argv_of, jobs):
    """Builds one archive; returns True on success. Prints its outcome.

    The stamp goes first and comes back last, so an archive without its stamp
    is never up to date; a failure, an interruption included, removes both
    (discard_archive), and the temporary object directory goes in every case.
    Only an interruption raises.
    """
    objdir = None
    try:
        failure = None
        try:
            os.makedirs(os.path.dirname(archive), exist_ok=True)
            errors = []
            if not remove_quietly(archive + ".stamp", errors):
                failure = "cannot remove the old stamp (%s)" % errors[0]
            else:
                objdir = tempfile.mkdtemp(prefix=".build-builtins-", dir=os.path.dirname(archive))
                failure = archive_members(
                    ctx, label, archive, stamp, members, argv_of, jobs, objdir
                )
        except OSError as e:
            failure = "%s: %s" % (e.filename, os_error_text(e)) if e.filename else str(e)
        except BaseException:
            discard_archive(label, archive, None)
            raise
        if failure is None:
            return True
        discard_archive(label, archive, failure)
        return False
    finally:
        if objdir is not None:
            shutil.rmtree(objdir, ignore_errors=True)


def discard_archive(label, archive, failure):
    """A failed build's cleanup: removes the archive and its stamp, and prints
    failure (when given) with what became of them. An archive that another
    process holds open stays without its stamp, so --check reports it."""
    stamp = archive + ".stamp"
    archive_errors, stamp_errors = [], []
    archive_gone = remove_quietly(archive, archive_errors)
    stamp_gone = remove_quietly(stamp, stamp_errors)
    if archive_gone and stamp_gone:
        if failure is None:
            return
        outcome = "removed %s and its stamp" % archive
    elif stamp_gone:
        outcome = (
            "%s could not be removed (%s) and stays, stale and without its stamp, so "
            "--check reports it: remove it once it is no longer in use"
            % (archive, archive_errors[0])
        )
    elif archive_gone:
        outcome = (
            "removed %s, but its stamp could not be removed (%s): remove %s once it is no "
            "longer in use" % (archive, stamp_errors[0], stamp)
        )
    else:
        outcome = (
            "neither %s nor its stamp could be removed (%s; %s): remove both once they are "
            "no longer in use" % (archive, archive_errors[0], stamp_errors[0])
        )
    say(
        "error: %s: %s" % (label, outcome if failure is None else failure + "; " + outcome),
        sys.stderr,
    )


def archive_members(ctx, label, archive, stamp, members, argv_of, jobs, objdir):
    """make_archive's compiles and llvm-ar run in objdir; returns None, or
    what failed."""
    start = time.monotonic()
    results = compile_members(ctx, members, argv_of, objdir, jobs)
    failed = [r for r in results if not r[1]]
    for m, ok, argv, err in results:
        if ok and err.strip():
            say("%s: warnings from %s (member %s):" % (label, m.path, m.name))
            sys.stdout.write(err if err.endswith("\n") else err + "\n")
    if failed:
        for m, ok, argv, err in failed:
            say("error: %s: %s did not compile (member %s):" % (label, m.path, m.name), sys.stderr)
            print("  " + " ".join(shlex.quote(a) for a in argv), file=sys.stderr)
            sys.stderr.write(err if err.endswith("\n") else err + "\n")
        return "%d of %d members failed" % (len(failed), len(members))
    tmp = os.path.join(objdir, "archive.tmp")
    code, out, err = run_tool(ctx.ar + [AR_OPERATION, tmp] + [m.name for m in members], cwd=objdir)
    if code != 0:
        return "llvm-ar failed: %s" % (out + err).strip()[:500]
    # The new stamp is written in objdir too, so a failure leaves no temporary
    # file behind.
    dst = archive
    try:
        replace_retrying(tmp, archive)
        dst = archive + ".stamp"
        tmp_stamp = os.path.join(objdir, "stamp.tmp")
        with open(tmp_stamp, "w", encoding="utf-8", newline="\n") as f:
            f.write(stamp)
        replace_retrying(tmp_stamp, dst)
    except OSError as e:
        return "cannot write %s: %s" % (dst, os_error_text(e))
    say(
        "%s: %d members, %d bytes, %.1f s: %s"
        % (label, len(members), os.path.getsize(archive), time.monotonic() - start, archive)
    )
    return None


# --------------------------------------------------------------------------
# Modes
# --------------------------------------------------------------------------


def msil_plans(ctx, triples, out_dir):
    """[(triple, archive, expected stamp, members)] for the MSIL archives."""
    srclist = parse_sources(ctx.repo)
    cache = FileCache(ctx.repo)
    resolved = resolve_all(srclist, triples, cache)
    base = os.path.abspath(out_dir) if out_dir else resource_lib_dir(ctx)
    plans = []
    for t in triples:
        archive = archive_path(base, t)
        stamp = stamp_text(
            ctx,
            cache,
            "triple %s" % t,
            msil_command_template(t),
            resolved[t],
            ["sources.txt %s" % srclist.digest],
        )
        plans.append((t, archive, stamp, resolved[t]))
    return plans


def stamp_state(archive, stamp):
    if not os.path.isfile(archive):
        return "missing"
    current = read_text(archive + ".stamp")
    if current is None:
        return "no stamp"
    return "up to date" if current == stamp else "stale (stamp differs)"


def do_build(ctx, triples, out_dir, jobs):
    """Builds every triple, a failed one included; 2 when a file shadows an
    archive in the resource directory, else 1 when a triple failed."""
    check_tools(ctx.clang, ctx.ar)
    failed = shadowed = 0
    for t, archive, stamp, members in msil_plans(ctx, triples, out_dir):
        if stamp_state(archive, stamp) == "up to date":
            say("%s: up to date: %s" % (t, archive))
        else:
            clang = ctx.clang
            if not make_archive(
                ctx,
                t,
                archive,
                stamp,
                members,
                lambda m, obj, t=t: msil_argv(clang, t, m, obj),
                jobs,
            ):
                failed += 1
                continue
        other = None if out_dir else shadowing_file(ctx, t, archive)
        if other is not None:
            say(
                "error: %s: 'clang --target=%s -rtlib=compiler-rt -print-libgcc-file-name' "
                "names %s, which shadows the helper archive %s (plan 5.14.3 A1)"
                % (t, t, other, archive),
                sys.stderr,
            )
            shadowed += 1
    return 2 if shadowed else 1 if failed else 0


def do_check(ctx, triples, out_dir):
    bad = 0
    for t, archive, stamp, members in msil_plans(ctx, triples, out_dir):
        state = stamp_state(archive, stamp)
        say("%s: %s: %s" % (t, state, archive))
        bad += state != "up to date"
    if bad:
        say(
            "%d archive(s) missing or stale; run 'python %s' to build them"
            % (bad, "msil/utils/build/" + PROG)
        )
        return 1
    return 0


def do_list(ctx, triples):
    srclist = parse_sources(ctx.repo)
    resolved = resolve_all(srclist, triples, FileCache(ctx.repo))
    for t in triples:
        for m in resolved[t]:
            print("%s %s %s" % (t, m.name, m.path))
    return 0


def do_check_list(ctx):
    srclist = parse_sources(ctx.repo)
    wanted = generic_sources(parse_cmake(ctx.repo))
    listed = {s.path: s for s in srclist.sources}
    excluded = srclist.excluded()
    problems = []
    for path, name in wanted.items():
        if path not in listed and path not in excluded:
            problems.append(
                "unlisted: %s: in %s of %s, but neither listed nor excluded in %s"
                % (path, name, CMAKE_LISTS, SOURCES_TXT)
            )
    for s in srclist.sources:
        if not os.path.isfile(os.path.join(ctx.repo, s.path)):
            problems.append(
                "gone: %s: listed on line %d of %s, but the file does not exist"
                % (s.path, s.line, SOURCES_TXT)
            )
    for e in srclist.exclusions:
        if not os.path.isfile(os.path.join(ctx.repo, e.path)):
            problems.append(
                "gone: %s: excluded on line %d of %s, but the file does not exist"
                % (e.path, e.line, SOURCES_TXT)
            )
    for p in problems:
        say(p)
    n_listed = sum(1 for p in wanted if p in listed)
    n_excluded = sum(1 for p in wanted if p in excluded)
    say(
        "check-list: %d .c files in GENERIC_SOURCES and BF16_SOURCES: %d listed, "
        "%d excluded, %d problem(s)" % (len(wanted), n_listed, n_excluded, len(problems))
    )
    return 1 if problems else 0


def native_oracle_sources(srclist, lists):
    """The native oracle's sources, in archive order (see --native-oracle)."""
    order = []

    def add(path):
        if path not in order:
            order.append(path)

    for s in srclist.sources:
        if s.path.startswith(BUILTINS + "/") and s.applies_to(NATIVE_FROM_TRIPLE):
            add(s.path)
    generic = generic_sources(lists)
    for e in srclist.exclusions:
        if CATEGORIES[e.category] and e.path in generic:
            add(e.path)
    for name in NATIVE_CMAKE_LISTS:
        if name not in lists:
            raise CheckError("%s has no set(%s ...)" % (CMAKE_LISTS, name))
        for item in lists[name]:
            if item.endswith((".c", ".S")):
                add(BUILTINS + "/" + item)
    for item in NATIVE_X86_64_SOURCES:
        add(BUILTINS + "/" + item)
    never = {e.path for e in srclist.exclusions if not CATEGORIES[e.category]}
    order = [p for p in order if p not in never]
    # filter_builtin_sources: an architecture file supersedes the generic file
    # of its name (.S counting as .c).
    superseded = set()
    for p in order:
        rel = p[len(BUILTINS) + 1 :]
        if "/" in rel:
            superseded.add(BUILTINS + "/" + re.sub(r"\.S$", ".c", posixpath.basename(rel)))
    return [p for p in order if p not in superseded]


def do_native_oracle(ctx, out_dir, jobs):
    check_tools(ctx.clang, ctx.ar)
    srclist = parse_sources(ctx.repo)
    lists = parse_cmake(ctx.repo)
    paths = native_oracle_sources(srclist, lists)
    members, owner = [], {}
    for p in paths:
        if not os.path.isfile(os.path.join(ctx.repo, p)):
            raise CheckError("native oracle source %s does not exist" % p)
        m = Member(member_name(p), p, None)
        if m.name in owner:
            raise CheckError(
                "native oracle: member %s comes from both %s and %s" % (m.name, owner[m.name], p)
            )
        owner[m.name] = p
        members.append(m)
    cache = FileCache(ctx.repo)
    stamp = stamp_text(
        ctx,
        cache,
        "native %s" % NATIVE_TRIPLE,
        native_command_template(),
        members,
        [
            "sources.txt %s" % srclist.digest,
            "CMakeLists.txt %s" % cache.digest(CMAKE_LISTS),
        ],
    )
    archive = os.path.join(os.path.abspath(out_dir), NATIVE_ARCHIVE)
    label = "native %s" % NATIVE_TRIPLE
    if stamp_state(archive, stamp) == "up to date":
        say("%s: up to date: %s" % (label, archive))
        return 0
    clang = ctx.clang
    ok = make_archive(
        ctx, label, archive, stamp, members, lambda m, obj: native_argv(clang, m, obj), jobs
    )
    return 0 if ok else 1


# --------------------------------------------------------------------------
# Command line
# --------------------------------------------------------------------------


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise UsageError(message)


def parse_args(argv):
    ap = Parser(prog=PROG, description=__doc__.splitlines()[0], add_help=True)
    ap.add_argument(
        "--build-dir",
        metavar="DIR",
        help="the build whose bin/clang and bin/llvm-ar a build, --check and "
        "--native-oracle run (default: $MSIL_BUILD_DIR when it is set and not "
        "empty, else <repository>/build)",
    )
    ap.add_argument(
        "--triple",
        metavar="T",
        action="append",
        default=[],
        help="an MSIL triple, repeatable (default: %s; the portable %s only when named)"
        % (" and ".join(DEFAULT_TRIPLES), " and ".join(PORTABLE_TRIPLES)),
    )
    ap.add_argument(
        "--all", action="store_true", help="add the other MSIL triples (compiled, never run)"
    )
    ap.add_argument(
        "--jobs", metavar="N", type=int, help="compiles at a time (default: one per processor)"
    )
    ap.add_argument(
        "--out",
        metavar="DIR",
        help="write DIR/<T>/<archive> instead of <resource dir>/lib/<T>/<archive>",
    )
    ap.add_argument(
        "--check",
        action="store_true",
        help="build nothing; exit 1 when an archive is missing or its stamp is missing or differs",
    )
    ap.add_argument(
        "--check-list",
        action="store_true",
        help="exit 1 unless sources.txt lists or excludes every .c file of compiler-rt's "
        "GENERIC_SOURCES and BF16_SOURCES",
    )
    ap.add_argument(
        "--list", action="store_true", help="print '<triple> <member> <source>' per member"
    )
    ap.add_argument(
        "--native-oracle",
        metavar="DIR",
        help="build the native compiler-rt archive of the Linux oracle E7h as "
        "DIR/" + NATIVE_ARCHIVE,
    )
    ap.add_argument(
        "--self-test", action="store_true", help="check this script offline, with stand-ins"
    )
    args = ap.parse_args(argv)
    modes = [
        name
        for name, on in (
            ("--check", args.check),
            ("--check-list", args.check_list),
            ("--list", args.list),
            ("--native-oracle", args.native_oracle is not None),
            ("--self-test", args.self_test),
        )
        if on
    ]
    if len(modes) > 1:
        raise UsageError("%s cannot be combined" % " and ".join(modes))
    mode = modes[0] if modes else "build"
    if (args.triple or args.all) and mode not in ("build", "--check", "--list"):
        raise UsageError("--triple and --all go with a build, --check or --list")
    if args.out is not None and mode not in ("build", "--check"):
        raise UsageError("--out goes with a build or --check")
    if args.jobs is not None and args.jobs < 1:
        raise UsageError("--jobs must be at least 1")
    if mode == "--self-test" and (args.build_dir or args.jobs):
        raise UsageError("--self-test takes no other option")
    for t in args.triple:
        if not is_msil_triple(t):
            raise UsageError(
                "--triple %s is not an MSIL triple (<arch>-...-msil, or an msil32 or msil64 "
                "triple such as %s)" % (t, PORTABLE_TRIPLES[0])
            )
    return args, mode


def is_msil_triple(triple):
    """An MSIL triple: the object format suffix -msil (the host MSIL triples)
    or an msil arch (the portable triples, plan 5.18.8 PH1)."""
    if not TRIPLE_FORM_RE.match(triple):
        return False
    return triple.endswith("-msil") or triple.split("-")[0] in MSIL_ARCHES


def selected_triples(args):
    triples = list(dict.fromkeys(args.triple)) if args.triple else list(DEFAULT_TRIPLES)
    if args.all:
        triples += [t for t in ALL_TRIPLES if t not in triples]
    return triples


def main(argv=None, ctx=None):
    try:
        args, mode = parse_args(sys.argv[1:] if argv is None else argv)
        if mode == "--self-test":
            return self_test()
        jobs = args.jobs or os.cpu_count() or 4
        if mode == "--check-list":
            return do_check_list(ctx or Context(REPO, None, None, None, None, None))
        if mode == "--list":
            return do_list(
                ctx or Context(REPO, None, None, None, None, None), selected_triples(args)
            )
        if ctx is None:
            ctx = Context.from_build_dir(*build_dir_choice(args.build_dir))
        release = enter_build_lock(ctx.build_dir) if ctx.build_dir else (lambda: None)
        try:
            if mode == "--native-oracle":
                return do_native_oracle(ctx, args.native_oracle, jobs)
            if mode == "--check":
                return do_check(ctx, selected_triples(args), args.out)
            return do_build(ctx, selected_triples(args), args.out, jobs)
        finally:
            release()
    except UsageError as e:
        say("error: %s" % e, sys.stderr)
        return 2
    except CheckError as e:
        say("error: %s" % e, sys.stderr)
        return 1


# --------------------------------------------------------------------------
# --self-test
# --------------------------------------------------------------------------

STANDIN_CLANG = r"""
import hashlib, os, sys
args = sys.argv[1:]
if args == ["--version"]:
    print("stand-in clang"); sys.exit(0)
res = os.environ["BB_STANDIN_RES"]
if args == ["-print-resource-dir"]:
    print(res); sys.exit(0)
target = [a for a in args if a.startswith("--target=")][0][len("--target="):]
if "-print-libgcc-file-name" in args:
    # As ToolChain::getCompilerRT: the per-target file when it exists, else
    # the old layout's lib/<os>/[lib]clang_rt.builtins-<arch>.<ext> when that
    # exists; BB_STANDIN_SHADOW names an existing old-layout file regardless.
    arch, os_name = target.split("-")[0], target.split("-")[2]
    if "-windows-msvc" in target:
        new_name, old_name = "clang_rt.builtins.lib", "clang_rt.builtins-%s.lib" % arch
    else:
        new_name, old_name = "libclang_rt.builtins.a", "libclang_rt.builtins-%s.a" % arch
    new = os.path.join(res, "lib", target, new_name)
    old = os.path.join(res, "lib", os_name, old_name)
    if os.path.exists(old) and (os.environ.get("BB_STANDIN_SHADOW") or not os.path.exists(new)):
        print(old)
    else:
        print(new)
    sys.exit(0)
log = os.environ.get("BB_STANDIN_LOG")
if log:
    import time
    name = os.path.join(log, "%d-%d.txt" % (os.getpid(), time.time_ns()))
    with open(os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL), "w") as f:
        f.write(" ".join(args) + "\n")
cwd = os.path.normcase(os.path.abspath(os.getcwd()))
if cwd != os.path.normcase(os.path.abspath(os.environ["BB_STANDIN_REPO"])):
    sys.stderr.write("stand-in: not run from the repository root: %s\n" % cwd); sys.exit(3)
i = args.index("-o")
obj, src = args[i + 1], args[-1]
if os.path.isabs(src) or "\\" in src or args[args.index("-I") + 1] != "compiler-rt/lib/builtins":
    sys.stderr.write("stand-in: not a relative '/' path: %s\n" % src); sys.exit(3)
data = open(src, "rb").read()
if b"STANDIN_FAIL" in data:
    sys.stderr.write("%s:1:1: error: stand-in failure\n" % src); sys.exit(1)
kept = args[:i] + args[i + 2:]
with open(obj, "w", newline="\n") as f:
    f.write("obj\n" + "\n".join(kept) + "\nsrc " + hashlib.sha256(data).hexdigest() + "\n")
"""

STANDIN_AR = r"""
import os, sys
args = sys.argv[1:]
if args == ["--version"]:
    print("stand-in llvm-ar"); sys.exit(0)
if args[0] != "rcsD" or any(os.sep in m or "/" in m for m in args[2:]):
    sys.stderr.write("stand-in: unexpected command %r\n" % args); sys.exit(3)
with open(args[1], "w", newline="\n") as out:
    out.write("!<stand-in>\n")
    for m in args[2:]:
        out.write("--- %s\n" % m + open(m).read())
"""


class SelfTest:
    def __init__(self):
        self.failures = 0
        self.root = tempfile.mkdtemp(prefix="build-builtins-self-test-")
        self.repo = os.path.join(self.root, "repo")
        tools = os.path.join(self.root, "tools")
        os.makedirs(tools)
        self.clang_file = os.path.join(tools, "clang.py")
        self.ar_file = os.path.join(tools, "llvm-ar.py")
        for path, text in ((self.clang_file, STANDIN_CLANG), (self.ar_file, STANDIN_AR)):
            with open(path, "w", newline="\n") as f:
                f.write(text)
        self.log = os.path.join(self.root, "clang-calls")
        os.makedirs(self.log)
        os.environ["BB_STANDIN_RES"] = os.path.join(self.root, "res")
        os.environ["BB_STANDIN_REPO"] = self.repo
        os.environ["BB_STANDIN_LOG"] = self.log
        self.ctx = Context(
            self.repo,
            [sys.executable, "-I", "-S", self.clang_file],
            [sys.executable, "-I", "-S", self.ar_file],
            self.clang_file,
            self.ar_file,
            os.path.abspath(__file__),
        )

    def write(self, rel, text):
        path = os.path.join(self.repo, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", newline="\n") as f:
            f.write(text)

    def run(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = main(list(argv), self.ctx)
        return code, out.getvalue(), err.getvalue()

    def check(self, name, cond, detail=""):
        if cond:
            print("ok: " + name)
        else:
            self.failures += 1
            print("FAIL: %s%s" % (name, (": " + detail) if detail else ""))

    def expect(self, name, argv, code, *needles, absent=()):
        got, out, err = self.run(*argv)
        text = out + err
        missing = [n for n in needles if n not in text]
        present = [n for n in absent if n in text]
        self.check(
            name,
            got == code and not missing and not present,
            "exit %s (expected %d), missing %r, unexpected %r; output:\n%s"
            % (got, code, missing, present, text),
        )
        return text

    def clang_calls(self):
        """The stand-in clang's compiles since the last reset_calls()."""
        calls = []
        for name in sorted(os.listdir(self.log)):
            with open(os.path.join(self.log, name)) as f:
                calls += f.read().splitlines()
        return calls

    def reset_calls(self):
        for name in os.listdir(self.log):
            os.remove(os.path.join(self.log, name))


SELF_TEST_CMAKE = """\
set(GENERIC_SOURCES
  absvsi2.c
  adddf3.c   # an inline comment
  divti3.c
  emutls.c
  floatdidf.c
  fp_mode.c
  int_util.c
  muldc3.c
)
# set(GENERIC_SOURCES commented.c)
if (NOT FUCHSIA)
  set(GENERIC_SOURCES
    ${GENERIC_SOURCES}
    atomic.c
  )
endif()
list(APPEND GENERIC_SOURCES clear_cache.c)
set(BF16_SOURCES
    truncsfbf2.c
    truncxfbf2.c
)
set(GENERIC_TF_SOURCES addtf3.c)
set(x86_80_BIT_SOURCES
  floatdixf.c
  floatundixf.c
)
set(x86_ARCH_SOURCES cpu_model/x86.c)
if (NOT MSVC)
  set(x86_ARCH_SOURCES ${x86_ARCH_SOURCES} i386/fp_mode.c)
endif()
set(OTHER_SOURCES other.c)
"""

SELF_TEST_SOURCES = """\
# A synthetic list.
compiler-rt/lib/builtins/absvsi2.c *-linux-*
compiler-rt/lib/builtins/adddf3.c
# excluded compiler-rt/lib/builtins/divti3.c: int128: passes __int128
# excluded compiler-rt/lib/builtins/emutls.c: hosted: needs <stdlib.h>
compiler-rt/lib/builtins/floatdidf.c   # trailing comment
compiler-rt/lib/builtins/fp_mode.c
compiler-rt/lib/builtins/int_util.c *-linux-*,*-unknown-dotnet
compiler-rt/lib/builtins/muldc3.c
# excluded compiler-rt/lib/builtins/atomic.c: atomic: user decisions R4/R5
# excluded compiler-rt/lib/builtins/clear_cache.c: hosted: needs <assert.h>
compiler-rt/lib/builtins/truncsfbf2.c
# excluded compiler-rt/lib/builtins/truncxfbf2.c: x87: x87 long double

msil/lib/builtins/split.c *-linux-*,x86_64-*-windows-gnu-*
"""

SELF_TEST_SPLIT = """\
#ifndef MSIL_MEMBER
#define MSIL_MEMBER(name) 1
#endif
#if MSIL_MEMBER(foo)
int foo(void) { return 1; }
#endif
#if MSIL_MEMBER(bar) // a comment
int bar(void) { return 2; }
#endif
"""


def self_test():
    saved_env = {
        k: os.environ.get(k)
        for k in (
            "BB_STANDIN_RES",
            "BB_STANDIN_REPO",
            "BB_STANDIN_LOG",
            "BB_STANDIN_SHADOW",
            "MSIL_BUILD_DIR",
        )
    }
    t = SelfTest()
    try:
        _self_test(t)
    finally:
        for k, v in saved_env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        shutil.rmtree(t.root, ignore_errors=True)
    if t.failures:
        print("%s --self-test: %d check(s) failed" % (PROG, t.failures))
        return 1
    print("%s --self-test: all checks passed" % PROG)
    return 0


def _self_test(t):
    b = BUILTINS + "/"
    t.write(CMAKE_LISTS, SELF_TEST_CMAKE)
    for name in (
        "absvsi2.c adddf3.c divti3.c emutls.c floatdidf.c fp_mode.c int_util.c "
        "muldc3.c atomic.c clear_cache.c truncsfbf2.c truncxfbf2.c addtf3.c "
        "floatdixf.c floatundixf.c cpu_model/x86.c i386/fp_mode.c other.c"
    ).split() + list(NATIVE_X86_64_SOURCES):
        t.write(b + name, "/* %s */\n" % name)
    t.write(b + "int_lib.h", "/* header */\n")
    t.write(SOURCES_TXT, SELF_TEST_SOURCES)
    t.write("msil/lib/builtins/split.c", SELF_TEST_SPLIT)

    # Parsing, globs and the member split (--list needs no tools).
    text = t.expect("--list parses comments, globs and exclusions", ["--list", "--all"], 0)
    lines = text.splitlines()
    t.check(
        "--list prints one line per member, in sources.txt order",
        [l for l in lines if l.startswith("x86_64-pc-windows-msvc-msil ")]
        == [
            "x86_64-pc-windows-msvc-msil adddf3.c.o " + b + "adddf3.c",
            "x86_64-pc-windows-msvc-msil floatdidf.c.o " + b + "floatdidf.c",
            "x86_64-pc-windows-msvc-msil fp_mode.c.o " + b + "fp_mode.c",
            "x86_64-pc-windows-msvc-msil muldc3.c.o " + b + "muldc3.c",
            "x86_64-pc-windows-msvc-msil truncsfbf2.c.o " + b + "truncsfbf2.c",
        ],
        text,
    )
    for triple in ALL_TRIPLES:
        linux = "-linux-" in triple
        wide = linux or triple.startswith("x86_64-") and "-windows-gnu-" in triple
        have = lambda s: any(l.startswith(triple + " ") and l.endswith(s) for l in lines)
        t.check(
            "globs on %s: absvsi2.c %s, split.c %s, adddf3.c always"
            % (triple, "listed" if linux else "not listed", "listed" if wide else "not listed"),
            have("absvsi2.c") == linux and have("split.c") == wide and have("adddf3.c"),
        )
    t.check(
        "--all leaves out the portable triple",
        not any(l.startswith(PORTABLE_TRIPLES[0] + " ") for l in lines),
        text,
    )
    portable = PORTABLE_TRIPLES[0]
    text_p = t.expect(
        "--list --triple %s needs no -msil suffix" % portable, ["--list", "--triple", portable], 0
    )
    t.check(
        "the portable triple takes the unglobbed and *-unknown-dotnet lines only (int_util.c, "
        "not absvsi2.c or split.c)",
        text_p.splitlines()
        == [
            portable + " adddf3.c.o " + b + "adddf3.c",
            portable + " floatdidf.c.o " + b + "floatdidf.c",
            portable + " fp_mode.c.o " + b + "fp_mode.c",
            portable + " int_util.c.o " + b + "int_util.c",
            portable + " muldc3.c.o " + b + "muldc3.c",
            portable + " truncsfbf2.c.o " + b + "truncsfbf2.c",
        ],
        text_p,
    )
    t.expect(
        "--list --triple msil32-unknown-dotnet is accepted (an msil arch)",
        ["--list", "--triple", "msil32-unknown-dotnet"],
        0,
        "msil32-unknown-dotnet int_util.c.o",
    )
    t.check(
        "a guarded source splits into one member per MSIL_MEMBER name, in order",
        [
            l.split()[1]
            for l in lines
            if l.startswith("x86_64-unknown-linux-gnu-msil ") and "split" in l
        ]
        == ["split.foo.c.o", "split.bar.c.o"],
        text,
    )
    t.check(
        "excluded and commented-out sources are not members",
        not any(
            s in text for s in ("divti3", "emutls", "atomic", "clear_cache", "truncxfbf2", "other")
        ),
    )

    def broken(name, line, needle):
        t.write(SOURCES_TXT, SELF_TEST_SOURCES + line + "\n")
        t.expect("sources.txt error: " + name, ["--list"], 1, needle)

    broken("malformed exclusion", "# excluded " + b + "other.c no category", "malformed exclusion")
    broken(
        "unknown category", "# excluded " + b + "other.c: vague: why", "unknown category 'vague'"
    )
    broken("listed twice", b + "adddf3.c", "listed twice")
    broken("listed and excluded", "# excluded " + b + "adddf3.c: hosted: x", "both listed")
    broken("excluded twice", "# excluded " + b + "emutls.c: hosted: x", "excluded twice")
    broken("three fields", b + "other.c *-linux-* extra", "expected '<path>")
    broken("backslash path", "compiler-rt\\lib\\builtins\\other.c", "repository-relative")
    broken("absolute path", "/abs/other.c", "repository-relative")
    broken("not a .c file", b + "x86_64/floatundidf.S", "not a C source")
    broken("empty glob", b + "other.c *-linux-*,", "empty triple glob")
    broken("listed source gone", b + "gone.c", "does not exist")
    t.write("msil/lib/builtins/dup.c", "#if MSIL_MEMBER(x)\n#endif\n#if MSIL_MEMBER(x)\n#endif\n")
    broken("a name guarded twice", "msil/lib/builtins/dup.c", "MSIL_MEMBER(x) guards two blocks")
    broken(
        "two sources with one member name",
        b + "x86_64/floatdidf.c",
        "member floatdidf.c.o comes from both " + b + "floatdidf.c and " + b + "x86_64/floatdidf.c",
    )
    t.write(SOURCES_TXT, SELF_TEST_SOURCES)

    # --check-list on the synthetic CMakeLists.txt.
    t.expect(
        "--check-list: every GENERIC/BF16 .c listed or excluded (set, set ${...}, list(APPEND))",
        ["--check-list"],
        0,
        "12 .c files in GENERIC_SOURCES and BF16_SOURCES: 7 listed, 5 excluded, 0 problem",
    )
    t.write(CMAKE_LISTS, SELF_TEST_CMAKE + "list(APPEND GENERIC_SOURCES newer.c)\n")
    t.expect(
        "--check-list names an unlisted, unexcluded .c",
        ["--check-list"],
        1,
        "unlisted: " + b + "newer.c",
    )
    t.write(CMAKE_LISTS, SELF_TEST_CMAKE)
    os.rename(os.path.join(t.repo, b + "muldc3.c"), os.path.join(t.repo, b + "muldc3.c.away"))
    os.rename(os.path.join(t.repo, b + "emutls.c"), os.path.join(t.repo, b + "emutls.c.away"))
    t.expect(
        "--check-list names listed and excluded paths that are gone",
        ["--check-list"],
        1,
        "gone: " + b + "muldc3.c: listed",
        "gone: " + b + "emutls.c: excluded",
    )
    os.rename(os.path.join(t.repo, b + "muldc3.c.away"), os.path.join(t.repo, b + "muldc3.c"))
    os.rename(os.path.join(t.repo, b + "emutls.c.away"), os.path.join(t.repo, b + "emutls.c"))

    # Builds, determinism, stamps.
    out_a = os.path.join(t.root, "out-a")
    out_b = os.path.join(t.root, "out-b")
    win = os.path.join(out_a, "x86_64-pc-windows-msvc-msil", "clang_rt.builtins.lib")
    lin = os.path.join(out_a, "x86_64-unknown-linux-gnu-msil", "libclang_rt.builtins.a")
    t.expect(
        "--out builds both default triples",
        ["--out", out_a, "--jobs", "4"],
        0,
        "x86_64-pc-windows-msvc-msil: 5 members",
        "x86_64-unknown-linux-gnu-msil: 9 members",
    )
    t.check(
        "archives and stamps at <out>/<triple>/<name>",
        all(os.path.isfile(p) for p in (win, lin, win + ".stamp", lin + ".stamp")),
    )
    with open(lin) as f:
        archive = f.read()
    t.check(
        "members in sources.txt order, split members included",
        re.findall(r"^--- (\S+)$", archive, re.M)
        == [
            "absvsi2.c.o",
            "adddf3.c.o",
            "floatdidf.c.o",
            "fp_mode.c.o",
            "int_util.c.o",
            "muldc3.c.o",
            "truncsfbf2.c.o",
            "split.foo.c.o",
            "split.bar.c.o",
        ],
        archive,
    )
    t.check(
        "a split member is compiled with the MSIL_MEMBER defines",
        "\n" + MEMBER_FLAG + "\n-DMSIL_ONLY_bar=1\n" in archive
        and "-DMSIL_ONLY_foo=1\n-o" not in archive,
    )
    calls = t.clang_calls()
    t.check(
        "windows-msvc compiles pin the MSVC version, linux ones do not",
        calls
        and all((MSVC_VERSION_FLAG in c) == ("windows-msvc" in c) for c in calls)
        and all(c.split()[-1].startswith(("compiler-rt/", "msil/")) for c in calls),
        "\n".join(calls),
    )
    t.expect("--out into a second directory", ["--out", out_b], 0)
    same = all(
        open(os.path.join(out_a, *rel), "rb").read() == open(os.path.join(out_b, *rel), "rb").read()
        for rel in (
            ("x86_64-pc-windows-msvc-msil", "clang_rt.builtins.lib"),
            ("x86_64-unknown-linux-gnu-msil", "libclang_rt.builtins.a"),
        )
    )
    t.check("two output directories give byte-identical archives", same)
    t.check(
        "a default build writes no portable archive",
        not os.path.exists(os.path.join(out_a, portable)),
    )

    # The portable triple, only when named (plan 5.18.8).
    out_p = os.path.join(t.root, "out-p")
    out_q = os.path.join(t.root, "out-q")
    port = os.path.join(out_p, portable, "libclang_rt.builtins.a")
    t.reset_calls()
    t.expect(
        "--triple %s --out builds the portable archive alone" % portable,
        ["--triple", portable, "--out", out_p],
        0,
        portable + ": 6 members",
        absent=("x86_64-",),
    )
    t.check(
        "the portable archive and stamp at <out>/%s/libclang_rt.builtins.a" % portable,
        os.path.isfile(port)
        and os.path.isfile(port + ".stamp")
        and sorted(os.listdir(out_p)) == [portable],
    )
    calls = t.clang_calls()
    t.check(
        "portable compiles: --target=%s, plan 5.14.3 A2's command, no MSVC version" % portable,
        len(calls) == 6
        and all(
            c.startswith("--target=%s -c -O2 -std=c11 -ffreestanding " % portable) for c in calls
        )
        and not any(MSVC_VERSION_FLAG in c for c in calls),
        "\n".join(calls),
    )
    t.expect(
        "the portable archive into a second directory", ["--triple", portable, "--out", out_q], 0
    )
    t.check(
        "two output directories give byte-identical portable archives",
        open(port, "rb").read()
        == open(os.path.join(out_q, portable, "libclang_rt.builtins.a"), "rb").read(),
    )
    t.expect(
        "--check --triple %s passes on a fresh portable archive" % portable,
        ["--check", "--triple", portable, "--out", out_p],
        0,
        portable + ": up to date",
    )
    t.expect(
        "--check without --triple does not cover the portable archive",
        ["--check", "--out", out_a],
        0,
        absent=(portable,),
    )
    t.expect(
        "--check --triple %s on a directory without it exits 1" % portable,
        ["--check", "--triple", portable, "--out", out_a],
        1,
        portable + ": missing",
    )
    t.reset_calls()
    t.expect("an up-to-date archive is not rebuilt", ["--out", out_a], 0, "up to date")
    t.check("no compile for up-to-date archives", not t.clang_calls())
    t.expect(
        "--check passes on fresh archives",
        ["--check", "--out", out_a],
        0,
        "x86_64-unknown-linux-gnu-msil: up to date",
    )
    t.expect(
        "--check on an empty --out exits 1",
        ["--check", "--out", os.path.join(t.root, "empty")],
        1,
        "missing",
    )

    def stale(name, change, undo):
        change()
        t.expect(
            "stale stamp after " + name, ["--check", "--out", out_a], 1, "stale (stamp differs)"
        )
        undo()
        t.expect("fresh again after undoing " + name, ["--check", "--out", out_a], 0)

    def edit(path, suffix):
        def do():
            with open(path, "a", newline="\n") as f:
                f.write(suffix)

        return do

    def restore(path, text):
        def do():
            with open(path, "w", newline="\n") as f:
                f.write(text)

        return do

    src = os.path.join(t.repo, b + "adddf3.c")
    stale("a source changes", edit(src, "/* edit */\n"), restore(src, "/* adddf3.c */\n"))
    lst = os.path.join(t.repo, SOURCES_TXT)
    stale(
        "sources.txt changes (a comment)",
        edit(lst, "# a comment\n"),
        restore(lst, SELF_TEST_SOURCES),
    )
    hdr = os.path.join(t.repo, b + "int_lib.h")
    stale("a header changes", edit(hdr, "/* edit */\n"), restore(hdr, "/* header */\n"))
    stale("clang changes", edit(t.clang_file, "# edit\n"), restore(t.clang_file, STANDIN_CLANG))
    stale("llvm-ar changes", edit(t.ar_file, "# edit\n"), restore(t.ar_file, STANDIN_AR))
    os.remove(lin + ".stamp")
    t.expect("--check without a stamp exits 1", ["--check", "--out", out_a], 1, "no stamp")
    t.expect(
        "a missing stamp rebuilds", ["--out", out_a], 0, "x86_64-unknown-linux-gnu-msil: 9 members"
    )

    edit(src, "/* STANDIN_FAIL */\n")()
    t.expect(
        "a failed compile exits 1 and names the source",
        ["--out", out_a],
        1,
        "error: x86_64-pc-windows-msvc-msil: " + b + "adddf3.c did not compile (member adddf3.c.o)",
        "removed " + win + " and its stamp",
    )
    t.check(
        "a failed compile removes the archive and the stamp",
        not any(os.path.exists(p) for p in (win, win + ".stamp", lin, lin + ".stamp")),
    )
    t.check(
        "no object directory is left behind",
        not [n for n in os.listdir(os.path.dirname(win)) if n.startswith(".build-builtins-")],
    )
    restore(src, "/* adddf3.c */\n")()
    t.write(b + "int_util.c", "/* STANDIN_FAIL */\n")
    t.expect(
        "a Linux-only failure keeps the windows-msvc archive",
        ["--out", out_a],
        1,
        "int_util.c did not compile",
    )
    t.check(
        "only the failing triple's archive is gone", os.path.isfile(win) and not os.path.exists(lin)
    )
    t.write(b + "int_util.c", "/* int_util.c */\n")

    class Interrupted(Exception):
        pass

    def interrupt(member, obj):
        raise Interrupted()

    members = resolve(parse_sources(t.repo), "x86_64-pc-windows-msvc-msil", FileCache(t.repo))
    try:
        make_archive(t.ctx, "interrupted", win, "stamp\n", members, interrupt, 2)
        raised = False
    except Interrupted:
        raised = True
    t.check(
        "an interrupted build removes the archive and its stamp",
        raised and not os.path.exists(win) and not os.path.exists(win + ".stamp"),
    )

    # A1's per-target files beside the old layout's native compiler-rt.
    _self_test_resource_dir(t)

    # The replace's retries, and an archive held open during a rebuild.
    _self_test_replace_retries(t)
    if os.name == "nt":
        _self_test_held_archive(t)
    else:
        print("ok: held archive checks skipped (not Windows)")

    # The native oracle.
    lists = parse_cmake(t.repo)
    paths = native_oracle_sources(parse_sources(t.repo), lists)
    expected = (
        "absvsi2.c adddf3.c int_util.c muldc3.c truncsfbf2.c divti3.c truncxfbf2.c addtf3.c "
        "cpu_model/x86.c i386/fp_mode.c"
    ).split() + list(NATIVE_X86_64_SOURCES)
    t.check(
        "native oracle: sources.txt's compiler-rt lines, int128/x87 exclusions, TF/x87/x86 lists, "
        "x86_64 sources; architecture files supersede generic ones; never hosted, atomic or msil/",
        paths == [b + p for p in expected],
        "\n".join(paths),
    )
    oracle = os.path.join(t.root, "oracle")
    t.reset_calls()
    t.expect(
        "--native-oracle builds the native archive",
        ["--native-oracle", oracle],
        0,
        "native x86_64-unknown-linux-gnu: 16 members",
    )
    calls = t.clang_calls()
    t.check(
        "native oracle compiles for x86_64-unknown-linux-gnu with -fPIC",
        len(calls) == 16
        and all("--target=x86_64-unknown-linux-gnu " in c and " -fPIC " in c for c in calls),
        "\n".join(calls),
    )
    t.check(
        "native oracle archive and stamp",
        os.path.isfile(os.path.join(oracle, NATIVE_ARCHIVE + ".stamp")),
    )
    t.expect(
        "an up-to-date native oracle is not rebuilt", ["--native-oracle", oracle], 0, "up to date"
    )

    # Usage errors.
    for argv, needle in (
        (["--check", "--list"], "cannot be combined"),
        (
            ["--native-oracle", oracle, "--triple", "x86_64-unknown-linux-gnu-msil"],
            "--triple and --all",
        ),
        (["--check-list", "--out", out_a], "--out goes with"),
        (["--jobs", "0"], "--jobs must be at least 1"),
        (["--triple", "x86_64-unknown-linux-gnu"], "not an MSIL triple"),
        (["--triple", "x86_64-unknown-dotnet"], "not an MSIL triple"),
        (["--triple", "msil64"], "not an MSIL triple"),
        (["--no-such-option"], "unrecognized arguments"),
    ):
        t.expect("usage error: " + " ".join(argv), argv, 2, needle)
    code, out, err = t.run("--self-test", "--jobs", "2")
    t.check("usage error: --self-test with another option", code == 2, out + err)
    with _without_ctx(t):
        code, out, err = t.run("--build-dir", os.path.join(t.root, "no-build"))
    t.check(
        "usage error: a build directory without clang",
        code == 2 and "does not exist" in err,
        out + err,
    )

    # MSIL_BUILD_DIR, the default build directory.
    _self_test_build_dir_env(t, oracle)

    # msil-lock.ps1's lock on the build directory (Windows only).
    if os.name == "nt":
        _self_test_lock(t, out_a)
    else:
        print("ok: build lock checks skipped (not Windows)")

    # The repository's own list.
    real = parse_sources(REPO)
    cache = FileCache(REPO)
    resolved = resolve_all(real, list(ALL_TRIPLES) + list(PORTABLE_TRIPLES), cache)
    t.check(
        "the repository's sources.txt resolves for every --all triple without duplicates",
        all(resolved[x] for x in ALL_TRIPLES),
    )
    for x in PORTABLE_TRIPLES:
        names = [m.path for m in resolved[x]]
        t.check(
            "the repository's sources.txt for %s: int_util.c and the -ftrapv helpers, no "
            "msil/lib/builtins/ host family, no atomic or *ti* source (plan 5.18.8 PH3)" % x,
            BUILTINS + "/int_util.c" in names
            and all(
                any(
                    n == "%s/%s%s2.c" % (BUILTINS, f, w) or n == "%s/%s%s3.c" % (BUILTINS, f, w)
                    for n in names
                )
                for f in ("absv", "addv", "mulv", "negv", "subv")
                for w in ("si", "di")
            )
            and not any(n.startswith("msil/lib/builtins/") for n in names)
            and not any(posixpath.basename(n) == "atomic.c" for n in names)
            and not any(re.search(r"ti(\d|[a-z]f)?\.c$", posixpath.basename(n)) for n in names),
            "\n".join(names),
        )


def _self_test_lock(t, out_dir):
    global LOCK_WAIT_SECONDS
    lockdir = os.path.join(t.root, "lock-build")
    os.makedirs(lockdir)

    def blocked(name, exclusive):
        out = io.StringIO()
        try:
            with contextlib.redirect_stdout(out):
                release = enter_build_lock(lockdir, exclusive=exclusive, wait_seconds=0)
        except CheckError as e:
            t.check(name, "timed out" in str(e) and "waiting for" in out.getvalue(), str(e))
            return
        release()
        t.check(name, False, "the lock was granted")

    writer = enter_build_lock(lockdir, exclusive=True, wait_seconds=0)
    blocked("a reader waits while a writer (a build) holds the build lock", False)
    saved_wait, LOCK_WAIT_SECONDS = LOCK_WAIT_SECONDS, 0
    saved_dir, t.ctx.build_dir = t.ctx.build_dir, lockdir
    try:
        t.expect(
            "a build waits for the build lock",
            ["--out", out_dir],
            1,
            "timed out after 0 s waiting for",
        )
        t.expect(
            "--check waits for the build lock", ["--check", "--out", out_dir], 1, "waiting for"
        )
        writer()
        t.expect("a build runs once the build lock is free", ["--out", out_dir], 0)
        reader = enter_build_lock(lockdir, wait_seconds=0)
        t.expect("a build shares the build lock with another reader", ["--out", out_dir], 0)
        blocked("a writer cannot take the build lock while a reader holds it", True)
        reader()
        enter_build_lock(lockdir, exclusive=True, wait_seconds=0)()
        t.check("a run releases the build lock", True)
    finally:
        LOCK_WAIT_SECONDS = saved_wait
        t.ctx.build_dir = saved_dir


def _self_test_resource_dir(t):
    """Builds into the stand-in resource directory, which also holds the old
    layout's native compiler-rt files (plan 5.14.3 A1)."""
    lib = os.path.join(os.environ["BB_STANDIN_RES"], "lib")
    new_win = os.path.join(lib, "x86_64-pc-windows-msvc-msil", "clang_rt.builtins.lib")
    new_lin = os.path.join(lib, "x86_64-unknown-linux-gnu-msil", "libclang_rt.builtins.a")
    old_win = os.path.join(lib, "windows", "clang_rt.builtins-x86_64.lib")
    old_lin = os.path.join(lib, "linux", "libclang_rt.builtins-x86_64.a")
    native = {}
    for path in (old_win, old_lin):
        native[path] = ("native compiler-rt %s\n" % os.path.basename(path)).encode()
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(native[path])

    def untouched():
        """The native files are byte-identical, with nothing beside them."""
        for path, data in native.items():
            try:
                with open(path, "rb") as f:
                    if f.read() != data:
                        return False
            except OSError:
                return False
            if os.listdir(os.path.dirname(path)) != [os.path.basename(path)]:
                return False
        return True

    new_files = (new_win, new_lin, new_win + ".stamp", new_lin + ".stamp")
    code, out, err = run_tool(
        t.ctx.clang
        + ["--target=x86_64-pc-windows-msvc-msil", "-rtlib=compiler-rt", "-print-libgcc-file-name"]
    )
    t.check(
        "resource dir: while A1's file is absent, the stand-in clang names the old layout's",
        code == 0 and out.strip() == old_win,
        out + err,
    )
    t.expect(
        "resource dir: a build writes lib/<triple>/<A1 name>, not the old layout's file",
        [],
        0,
        "x86_64-pc-windows-msvc-msil: 5 members",
        "x86_64-unknown-linux-gnu-msil: 9 members",
        new_win,
        new_lin,
        absent=(old_win, old_lin, "shadows"),
    )
    t.check(
        "resource dir: archives and stamps at <resource dir>/lib/<triple>/<name>",
        all(os.path.isfile(p) for p in new_files),
    )
    t.check("resource dir: the old layout's native files stay byte-identical", untouched())
    t.expect(
        "resource dir: --check looks at A1's files",
        ["--check"],
        0,
        "x86_64-pc-windows-msvc-msil: up to date: " + new_win,
        "x86_64-unknown-linux-gnu-msil: up to date: " + new_lin,
        absent=(old_win, old_lin),
    )
    remove_quietly(new_win)
    t.expect(
        "resource dir: --check names a missing A1 file while clang names the old layout's",
        ["--check"],
        1,
        "x86_64-pc-windows-msvc-msil: missing: " + new_win,
        absent=(old_win,),
    )
    src = os.path.join(t.repo, BUILTINS, "adddf3.c")
    with open(src, "a", newline="\n") as f:
        f.write("/* STANDIN_FAIL */\n")
    t.expect(
        "resource dir: a failed compile removes A1's files",
        [],
        1,
        "adddf3.c did not compile",
        "removed " + new_win + " and its stamp",
        "removed " + new_lin + " and its stamp",
        absent=(old_win, old_lin),
    )
    t.check(
        "resource dir: a failed compile leaves the old layout's native files alone",
        untouched() and not any(os.path.exists(p) for p in new_files),
    )
    with open(src, "w", newline="\n") as f:
        f.write("/* adddf3.c */\n")
    shadows = [
        "error: %s: 'clang --target=%s -rtlib=compiler-rt -print-libgcc-file-name' names %s, "
        "which shadows the helper archive %s (plan 5.14.3 A1)" % (triple, triple, old, new)
        for triple, old, new in (
            ("x86_64-pc-windows-msvc-msil", old_win, new_win),
            ("x86_64-unknown-linux-gnu-msil", old_lin, new_lin),
        )
    ]
    os.environ["BB_STANDIN_SHADOW"] = "1"
    try:
        t.expect(
            "resource dir: a clang that names the old layout's file after the build is a "
            "usage error naming it",
            [],
            2,
            "x86_64-pc-windows-msvc-msil: 5 members",
            *shadows,
        )
        t.expect(
            "resource dir: the shadowing file stays a usage error while the archives are up "
            "to date",
            [],
            2,
            "x86_64-pc-windows-msvc-msil: up to date: " + new_win,
            *shadows,
        )
    finally:
        del os.environ["BB_STANDIN_SHADOW"]
    t.check(
        "resource dir: the shadowed native files stay byte-identical",
        untouched() and all(os.path.isfile(p) for p in new_files),
    )
    t.expect(
        "resource dir: an up-to-date build passes once clang names A1's files",
        [],
        0,
        "x86_64-unknown-linux-gnu-msil: up to date: " + new_lin,
        absent=("shadows",),
    )


def _self_test_replace_retries(t):
    calls = []

    def fails_twice(src, dst):
        calls.append(dst)
        if len(calls) < 3:
            raise PermissionError(13, "in use")

    def always(error):
        def replace(src, dst):
            calls.append(dst)
            raise error

        return replace

    try:
        replace_retrying("src", "dst", replace=fails_twice)
    except PermissionError:
        pass
    t.check("a replace that fails with PermissionError is retried", len(calls) == 3)
    del calls[:]
    try:
        replace_retrying("src", "dst", replace=always(PermissionError(13, "in use")))
        gave_up = False
    except PermissionError:
        gave_up = True
    t.check(
        "a replace that keeps failing gives up after a few hundred milliseconds of retries",
        gave_up
        and len(calls) == len(REPLACE_RETRY_DELAYS) + 1
        and 0.1 <= sum(REPLACE_RETRY_DELAYS) <= 1.0,
        "%d tries" % len(calls),
    )
    del calls[:]
    try:
        replace_retrying("src", "dst", replace=always(FileNotFoundError(2, "gone")))
        raised = False
    except FileNotFoundError:
        raised = True
    t.check("a replace's other errors are not retried", raised and len(calls) == 1)


def _self_test_held_archive(t):
    """A rebuild while a handle without delete sharing, as Python's open()
    makes, holds the old archive open (Windows)."""
    out = os.path.join(t.root, "out-held")
    win = os.path.join(out, "x86_64-pc-windows-msvc-msil", "clang_rt.builtins.lib")
    lin = os.path.join(out, "x86_64-unknown-linux-gnu-msil", "libclang_rt.builtins.a")
    t.expect("held archive: the first build", ["--out", out], 0)
    with open(win + ".stamp", "a", newline="\n") as f:
        f.write("stale\n")
    os.remove(lin)
    os.remove(lin + ".stamp")
    held = open(win, "rb")
    name = "held archive: a rebuild exits 1 and names the stale archive left without its stamp"
    try:
        try:
            text = t.expect(
                name,
                ["--out", out],
                1,
                "error: x86_64-pc-windows-msvc-msil: cannot write " + win + ": ",
                "; %s could not be removed (" % win,
                ") and stays, stale and without its stamp, so --check reports it: remove it "
                "once it is no longer in use",
                absent=("removed " + win,),
            )
        except Exception as e:  # the defect this case pins: a traceback
            t.check(name, False, "raised %r" % e)
            text = ""
        t.check(
            "held archive: no temporary directory remains",
            not [
                n
                for d in (os.path.dirname(win), os.path.dirname(lin))
                for n in os.listdir(d)
                if n.startswith(".build-builtins-")
            ],
        )
        t.check(
            "held archive: a later triple is still built",
            "x86_64-unknown-linux-gnu-msil: 9 members" in text
            and os.path.isfile(lin)
            and os.path.isfile(lin + ".stamp"),
            text,
        )
        t.check(
            "held archive: the archive stays without its stamp",
            os.path.isfile(win) and not os.path.exists(win + ".stamp"),
        )
        t.expect(
            "held archive: --check reports it",
            ["--check", "--out", out],
            1,
            "x86_64-pc-windows-msvc-msil: no stamp: " + win,
        )
    finally:
        held.close()
    t.expect(
        "held archive: a rebuild passes once the archive is closed",
        ["--out", out],
        0,
        "x86_64-pc-windows-msvc-msil: 5 members",
    )


@contextlib.contextmanager
def _without_ctx(t):
    """main() picks its build directory as on the command line, with REPO set
    to the stand-in repository, so that no default reaches a real build."""
    global REPO
    saved = t.ctx, REPO
    t.ctx, REPO = None, t.repo
    try:
        yield
    finally:
        t.ctx, REPO = saved


def _self_test_build_dir_env(t, oracle):
    """MSIL_BUILD_DIR as the default build directory (plan 5.14.3 A2). No
    build directory here has a clang, so every build mode is a usage error."""
    env_dir = os.path.join(t.root, "env-build")
    explicit = os.path.join(t.root, "explicit-build")
    saved_env = os.environ.get("MSIL_BUILD_DIR")
    os.environ["MSIL_BUILD_DIR"] = env_dir
    try:
        with _without_ctx(t):
            for argv, what in (
                ([], "a build"),
                (["--check"], "--check"),
                (["--native-oracle", oracle], "--native-oracle"),
            ):
                code, out, err = t.run(*argv)
                t.check(
                    "MSIL_BUILD_DIR is the default build directory of " + what,
                    code == 2 and "does not exist (MSIL_BUILD_DIR=%s)" % env_dir in err,
                    out + err,
                )
            code, out, err = t.run("--check", "--build-dir", explicit)
            t.check(
                "--build-dir wins over MSIL_BUILD_DIR",
                code == 2 and "does not exist (--build-dir %s)" % os.path.abspath(explicit) in err,
                out + err,
            )
            code, out, err = t.run("--list")
            t.check("--list ignores MSIL_BUILD_DIR", code == 0 and not err, err)
            try:
                parse_args(["--self-test"])
                bare = True
            except UsageError:
                bare = False
            t.check("a bare --self-test is no usage error while MSIL_BUILD_DIR is set", bare)
            os.environ["MSIL_BUILD_DIR"] = ""
            t.check(
                "an empty MSIL_BUILD_DIR counts as unset",
                build_dir_choice(None) == (os.path.join(t.repo, "build"), None),
            )
    finally:
        if saved_env is None:
            os.environ.pop("MSIL_BUILD_DIR", None)
        else:
            os.environ["MSIL_BUILD_DIR"] = saved_env


if __name__ == "__main__":
    sys.exit(main())

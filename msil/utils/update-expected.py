#!/usr/bin/env python3
# ===-- update-expected.py - Generate .expected files from native builds --===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Create or check <stem>.expected files for MSIL execution tests.

    update-expected.py [--clang build/bin/clang] msil/test/M1/02-fp/*.c
    update-expected.py --check msil/test/M1            (compare, do not write)
    update-expected.py --os linux [--check] msil/test/M1   (the Linux oracle)
    update-expected.py --check --levels O0,O2 msil/test/M3   (two levels)

The oracle is a native build of the same source with the just-built clang
(default <build>/bin/clang, where <build> is $MSIL_BUILD_DIR or <repo>/build),
at --opt (default -O0) or at each level of --levels (below), with the flags
of %{msil_cflags} (-std=gnu17
-ffreestanding -Werror=return-type -I msil/test/Inputs; -ffreestanding turns
off main's implicit 'return 0' except on MSVCRT triples, so falling off main
is a compile error on Linux triples). .ll tests are compiled with
-Wno-override-module instead of the C flags. These are the command lines of
the native-oracle legs of %{msil_c} and %{msil_ll} in msil/test/lit.cfg.py.
The program runs through 'msil-run.py --native', so the transcript format,
the private directory, the timeout and the memory cap are those of the lit
tests. The transcript newline rule of msil-run.py (every run of CRs before an
LF becomes one LF) applies to what is written and, with --check, to the
.expected file read, so a file that passes --check also passes the lit test.

--os selects the oracle and the file (default: the host's OS):

  windows  On a Windows host, clang for --target (default the host's MSVC
           triple) and its linker. As %{msil_native_ldflags} does, -lm
           follows the source when --target is not a Windows triple (glibc
           keeps the C math library, a Phase-1 import of plan D14, out of
           libc, and clang does not add -lm for C), and for an MSVC --target
           the compiler-rt builtins of --rtlib follow it as '-l<path>.lib'
           (the i128 helpers __udivti3, __fixdfti, ...; default 'auto', the
           newest clang_rt.builtins-<arch>.lib of the Visual Studio LLVM
           under %VCINSTALLDIR%Tools\\Llvm\\x64, else of the LLVM whose
           clang-cl is on PATH, as lit's msil_native_rtlib=auto; 'none'
           leaves it out). This oracle writes <stem>.expected; a
           <stem>.windows.expected override is reported and left alone.
  linux    On a Windows host, the E7 Linux oracle of lit's WSL legs (plan
           5.12.8): clang --target=x86_64-unknown-linux-gnu -fno-pic -c,
           linked by ld.lld (--ld-lld; default <clang's directory>, PATH, then
           the Visual Studio LLVM) as
             ld.lld -dynamic-linker /lib64/ld-linux-x86-64.so.2 --as-needed
               start.o <test>.o libm.so.6 libc.so.6 libgcc_s.so.1
           with msil/utils/linux-oracle/start.c (compiled once per run) and
           the libraries of --linux-sysroot (default $MSIL_LINUX_SYSROOT, else
           C:\\llvm-msil-scratch\\linux-sysroot; msil/utils/make-linux-sysroot.py
           makes it), run under WSL (--wsl-distro, default Ubuntu) by
           'msil-run.py --native --runner wsl'. On a Linux host, the host's
           clang build with -lm. Writing creates <stem>.linux.expected only
           for a transcript that differs from <stem>.expected (and removes one
           that no longer differs); <stem>.expected itself is written only
           when there is none.

--check compares with <stem>.<os>.expected if it exists, else with
<stem>.expected, which is the file msil-run.py --expect picks for that OS.

--levels L[,L...] (O0,O2 or -O0,-O2; plan 5.17.5, M24; it excludes --opt)
builds and runs every test at each level. --check then compares each level's
transcript with the file: 'ok: <test>' when every level matches, else one
'MISMATCH: <test>' line and a diff for each level that differs, whose second
file is 'native (<os>, -<L>)'. Writing needs the same transcript at every
level ('MISMATCH: <test> (the native transcripts at -<L> and -<L2> differ;
nothing written)' and their diff otherwise). A failure names its level, as
in 'FAIL (native compile at -O2): <test>'. With one level every line is as
without --levels.

Hosted mode (plan 5.17.3, M24) is decided per test, as lit decides it. A test
is hosted when the lit.local.cfg of its directory, or of a directory above
it, holds the line config.msil_enable_hosted(config) (msil/test/M3; lit
inherits a lit.local.cfg, and reads none in the suite's root, the directory
of lit.cfg.py, where the search stops), and --hosted makes every test named
on the command line hosted. A hosted test's C flags are --cflags without
-ffreestanding, the -I directory, and %{msil_hosted_cflags}:
--sysroot=<glibc sysroot> for a Linux target (--glibc-sysroot, default
$MSIL_GLIBC_SYSROOT, else C:\\llvm-msil-scratch\\glibc-sysroot on Windows and
none elsewhere, as lit's msil_glibc_sysroot), nothing for a Windows one. It
takes the hosted oracle, 'msil-prog.py --mode native --hosted' (also for a
%{msil_c} or %{msil_ll} test, built as a program of one source): the DLL C
runtime for an MSVC target, with --os linux on a Windows host the E7h
oracle (glibc's start files and exit path; it needs the glibc sysroot's
crt1.o, crti.o, crtn.o, libc_nonshared.a and libc.so script), run under WSL
as E7, and on a Linux host the host clang's ordinary link. A
'REDEFINE: %{msil_oracle_flags} = --hosted' gives a test the hosted oracle
alone, with its own C flags (the P3 rows of msil/test/M2/08-semantics), and
an empty one takes a hosted test back to the plain oracle; a value with any
other word fails the test (FAIL (test file)), and under --hosted a named
test ignores it. Every other test keeps the oracles above, so E7 stays for
msil/test/M1, msil/test/M2 and c-testsuite's libc-free.txt (whose directory
is skipped, below).

Hand-written expected files (plan 5.17.4): a test with 'REDEFINE:
%{msil_expected_rule} = <os>[,<os>]: <reason>' has a hand-written expected
file for each OS it names. For --os's OS nothing is built, checked or
written: the file (<stem>.<os>.expected when it exists, else <stem>.expected)
must exist, and 'rule: <file> (<reason>)' counts as a match ('MISSING: <file>
(hand-written under %{msil_expected_rule}: <reason>)' fails). For an OS it
does not name the oracle runs as usual, but never writes a <stem>.expected
that is the hand-written file of a named OS (one without a
<stem>.<os>.expected): it writes <stem>.<os>.expected instead ('wrote <file>
(<stem>.expected is the hand-written <os> expected file)'), or, when the
transcript equals <stem>.expected, removes that override or reports 'same
as'. A malformed rule fails the test (FAIL (test file)). A
%{msil_prog_loud_<L>} test (the loud-failure composite) has no native
oracle: without a rule that names --os's OS it fails (FAIL (test file)).

'REDEFINE: %{msil_limits} = --timeout S --mem-limit-mb N' (plan 5.17.9; either
option alone, also written --timeout=S) replaces --timeout and --mem-limit-mb
for the test: its native run gets S seconds, every step of its native build
ten times that (as lit's '%{msil_limits} --timeout-factor 10'), and both the
memory cap of N MiB. Any other word, or a value that is not positive, fails
the test (FAIL (test file)).

Exit statuses outside 0..255 are refused (plan D37): Linux truncates them, so
such a test would pass on one OS by accident. Nothing is written for them, and
--check fails on them whatever the .expected file says. Any other status that
is not '[exit N]' (a crash, a signal, a timeout) is refused as well: a native
run that does not finish normally is not an oracle.

Directories are searched for .c, .ll and .test files outside Inputs/ and
Output/ whose RUN lines use %{msil_c...} or %{msil_ll...} (the pipelines with
a native-oracle leg) or %{msil_prog} and %{msil_prog_<L>} (plan 5.12.4, M14);
tests with other RUN lines are skipped, among them the tests whose only
composites are %{msil_prog_fail_<L>} (a named diagnostic, no transcript) or
%{msil_det} (a determinism check). .c and .ll files named on the command line
are processed as %{msil_c} or %{msil_ll} tests unless they are %{msil_prog}
or loud-failure tests. msil/test/M3 (M24) is walked as any other directory,
with hosted mode detected (above). Nothing at or below msil/test/harness of
this source tree is built or checked (plan 5.17.5): the harness self-tests
there are built by their directories' lit.local.cfg with stand-in tools
(harness/prog/composite.test links with -Wl,--stand-in-native), so they have
no native oracle, and the harness leg runs them. Such a directory, named or
reached by the walk, is reported as 'skip (harness self-test: stand-in
tools): <directory>' and not walked, a file named below it gets the same
line, and both count as matches. Paths are compared absolute, with '..'
resolved and in os.path.normcase's case, so the self-tests' copies under the
build tree are processed and msil/test/harness/prog/../../Programs/support
is msil/test/Programs/support. Each test and each skipped directory is
processed and reported once, however the arguments nest or repeat: a
directory argument that the walk of another one reaches (it lies below that
one, under none of its Inputs/ and Output/ directories) is left out, as is a
repeated one, and a file that the command line names is processed once, as
a named file and where it is named, also when a walk finds it.

A %{msil_prog} test (a .test file in the format of plan 5.12.5) is built the
way the native-oracle leg of %{msil_prog} builds it: its split-file parts, if
it has any (a part separator line, or 'rm -rf %t && split-file %s %t' before
its composite), are written into a temporary directory (split-file, default
next to --clang, else on PATH; or --split-file, needed only by such a test),
its REDEFINE: lines give %{msil_srcs} (default:
the test file), %{msil_stock_srcs} and %{msil_archives} with %s, %S, %p, %t
and %{s:stem} replaced (%t is that directory), and 'msil-prog.py --mode
native' builds them at the level with --cflags, the --target triple's
-lm/compiler-rt rule and the archiver --ar (default llvm-ar next to --clang,
else on PATH); on a Windows host --os linux uses the E7 recipe (E7h for a
hosted test). %{msil_libs}
belongs to the MSIL link only and is not read. %{msil_native_libs} goes to
both builds, so the native build gets it (msil-prog.py --native-lib; plan
M15), and the files of %{msil_deps} are copied next to the native program
when it runs (msil-run.py --dep), as the native-oracle leg does.
%{msil_shlibs} (M22) builds the test's shared libraries natively before the
program (msil-prog.py --shlibs: DLLs and import libraries on Windows, the E7
recipe's ELF shared objects with --os linux), and the native program runs
with the build's shlib/ directory beside it (msil-run.py --dep-dir), as the
native-oracle leg of %{msil_prog} runs it. A REDEFINE: value may use lit's
%if and %else (decided as below), and a wildcard in %{msil_deps} is
expanded, as lit's shell expands it. RUN:
commands before the test's first composite, other than 'rm -rf %t &&
split-file %s %t', prepare its inputs (a test-built native library, plan
5.12.5): they run first, in lit's internal shell (llvm/utils/lit), with %if
decided by the features below and with the oracle's values of %clang_native
and %clang (--clang; %clang_native with --target), %ld_lld and %lld_link
(next to --clang, PATH, then the Visual Studio LLVM), %msil_ar (--ar),
%msil_triple, %python, %{msil_cflags}, %{msil_native_ldflags}, %s, %S, %p,
%t and %{s:stem}; a command that uses another substitution fails the test. A REQUIRES: line that is false,
or an UNSUPPORTED: line that is true, for the oracle skips a test of any kind
when the features the oracle knows decide it whatever the other features are:
msil-triple-windows, msil-triple-linux and msil-triple=<target>-msil follow
--target, system-<host os> the host and msil-wsl the E7 oracle.

M23 (plan 5.12.3, 5.12.4): REDEFINE: values may use %{msil_corpus}, the
exit corpus' cache (--corpus-dir: default $MSIL_CORPUS_CACHE, else
C:\\llvm-msil-scratch\\corpus-cache on Windows and ~/.cache/msil-corpus
elsewhere, as lit's msil_corpus_dir and msil/utils/corpus/fetch.py default
it), so the programs of msil/test/Programs build natively from the sources
that fetch.py verified; %{msil_copts} adds compile flags to the native build
(after the C flags, as msil-prog.py's --cflags gets them from lit); and
%{msil_expect} names the expected file's stem (default the test's own, as
lit's %S/%{s:stem}), so <stem>[.<os>].expected is written or checked there.
A directory whose lit.local.cfg, or that of a directory above it up to
msil/test (lit inherits it), sets config.suffixes to the one suffix ".test"
(in any quoting: [".test"], ['.test'], (".test",), ...) contributes only its
.test files (msil/test/Programs: the drivers, support files and shims are no
tests, also when only msil/test/Programs/support is named), the %{msil_det}
tests of msil/test/Programs/det are skipped as every determinism test is,
and a directory with a libc-free.txt (c-testsuite's lit test format, one
test per listed name, whose expected file msil_ctest_format.py writes from
<name>.c.expected plus [exit 0]; the name is the one
msil/test/Programs/c-testsuite/lit.local.cfg opens) is skipped as a whole.

--clang may also be a command line (a stand-in compiler of the harness
self-tests, written as msil-prog.py's --clang takes it).

Windows: run from a VS x64 developer prompt (the Windows native link needs
LIB; msil-env.ps1 sets it up). Exit status: 0 if everything was written or
matched (a rule counts as a match), 1 otherwise, 2 on a usage error. As a
module, main(argv) runs it with the argument list argv and returns that
status instead of exiting, also for argparse's own errors.
"""

import argparse
import ast
import concurrent.futures
import difflib
import functools
import glob
import importlib.util
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
TEST_ROOT = os.path.join(os.path.dirname(HERE), "test")
ORACLE_RUN_RE = re.compile(r"RUN:.*%\{msil_(c|ll)(_O[0-3sz])?\}")
# %{msil_prog} tests, and the composites that need no .expected file.
PROG_RUN_RE = re.compile(r"RUN:.*%\{msil_prog(_O[0-3sz])?\}")
FAIL_RUN_RE = re.compile(r"RUN:.*%\{msil_prog_fail_O[0-3sz]\}")
DET_RUN_RE = re.compile(r"RUN:.*%\{msil_det\}")
# The loud-failure composite (plan 5.17.4): no native oracle, hand-written
# expected files.
LOUD_RUN_RE = re.compile(r"RUN:.*%\{msil_prog_loud_O[0-3sz]\}")
# lit's directives (a keyword not inside a longer word).
DIRECTIVE_RE = re.compile(r"(?<![\w-])(REQUIRES|UNSUPPORTED|REDEFINE):(.*)$")
RUN_RE = re.compile(r"(?<![\w-])RUN:(.*)$")
# Any composite of msil/test/lit.cfg.py: the RUN: commands before the first
# one prepare the test's inputs (see Oracle.prepare).
COMPOSITE_RE = re.compile(r"%\{msil_(?:prog|det|c|ll)(?:_\w+)?\}")
# The preparation every %{msil_prog} test with split-file parts starts with,
# which build_prog does itself.
SPLIT_ONLY_RE = re.compile(r"^rm\s+-rf\s+%t\s+&&\s+split-file\s+%s\s+%t$")
# A split-file part separator, as split-file itself accepts it:
# '^(.|//)--- <part>' ('#--- ', '//--- ', ';--- ', ...).
SPLIT_PART_RE = re.compile(r"^(?://|.)--- ")
# A substitution of lit.cfg.py that the preparation does not know.
UNKNOWN_SUBST_RE = re.compile(r"%(?:\{[^}]*\}|[A-Za-z_][\w]*)")
REDEFINE_RE = re.compile(r"^\s*(%\{[_a-zA-Z][-_:0-9a-zA-Z]*\})\s*=(.*)$")
# The features whose truth the oracle knows (see the module documentation).
KNOWN_FEATURE_PREFIXES = ("msil-triple", "system-", "msil-wsl")
EXIT_RE = re.compile(r"^\[exit (\d+)\]$")
# The C flags of %{msil_cflags} (lit.cfg.py), without -I.
DEFAULT_CFLAGS = "-std=gnu17 -ffreestanding -Werror=return-type"
E7_TARGET = "x86_64-unknown-linux-gnu"
E7_LIBS = ("libm.so.6", "libc.so.6", "libgcc_s.so.1")
DEFAULT_LINUX_SYSROOT = r"C:\llvm-msil-scratch\linux-sysroot"
# The glibc sysroot of the hosted tests (make-glibc-sysroot.sh), as lit's
# msil_glibc_sysroot defaults it.
DEFAULT_GLIBC_SYSROOT = r"C:\llvm-msil-scratch\glibc-sysroot" if os.name == "nt" else ""
# The line of a hosted directory's lit.local.cfg (plan 5.17.3).
HOSTED_MARKER = "config.msil_enable_hosted(config)"
# lit reads no lit.local.cfg in the directory of a suite's configuration.
SUITE_CONFIGS = ("lit.cfg.py", "lit.site.cfg.py")
# The exit corpus' cache (plan 5.12.9), as fetch.py and lit default it.
DEFAULT_CORPUS_CACHE = (
    r"C:\llvm-msil-scratch\corpus-cache"
    if os.name == "nt"
    else os.path.join(os.path.expanduser("~"), ".cache", "msil-corpus")
)
# A lit.local.cfg that makes its directory a suite of .test files only: the
# fallback of test_only_dir for a file that does not parse as Python.
TEST_ONLY_SUFFIXES_RE = re.compile(
    r"^\s*config\.suffixes\s*=\s*[\[(]\s*[rRuU]?[\"']\.test[\"']\s*,?\s*[\])]", re.M
)
# The file c-testsuite's lit test format reads its test names from
# (msil/test/Programs/c-testsuite/lit.local.cfg opens the same name); a
# directory that holds it is skipped as a whole.
CTEST_LIST = "libc-free.txt"
# The harness self-tests (plan 5.17.5): their directories' lit.local.cfg
# build them with stand-in tools, so nothing at or below this directory of
# the source tree has a native oracle; collect skips it as a whole.
HARNESS_DIR = os.path.join(TEST_ROOT, "harness")
# Why collect skips a directory (or a named file) as a whole.
SKIP_CTEST = "c-testsuite format: one test per name of libc-free.txt"
SKIP_HARNESS = "harness self-test: stand-in tools"
# The directories that the walk of collect does not enter.
WALK_PRUNED = ("Inputs", "Output")
# lit's internal shell runs one preparation at a time (Oracle.prepare).
PREPARE_LOCK = threading.Lock()


def load_script(name, file):
    # No __pycache__ next to the scripts in the source tree.
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, file))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


msil_run = load_script("msil_run", "msil-run.py")
msil_prog = load_script("msil_prog", "msil-prog.py")


def host_arch():
    return {
        "amd64": "x86_64",
        "x86_64": "x86_64",
        "arm64": "aarch64",
        "aarch64": "aarch64",
    }.get(platform.machine().lower(), "x86_64")


def default_target():
    arch = host_arch()
    return {"Windows": arch + "-pc-windows-msvc", "Darwin": arch + "-apple-darwin"}.get(
        platform.system(), arch + "-unknown-linux-gnu"
    )


def is_windows_triple(triple):
    """The rule of lit.cfg.py's msil-triple-windows: MSVC and MinGW triples."""
    return "windows" in triple.split("-")


def is_msvc_triple(triple):
    """A Windows triple whose environment is MSVC (or missing), as lit.cfg.py
    decides where msil_native_rtlib applies."""
    parts = triple.split("-")
    return is_windows_triple(triple) and (len(parts) < 4 or parts[3].startswith("msvc"))


def default_clang():
    build = os.environ.get("MSIL_BUILD_DIR") or os.path.join(REPO, "build")
    clang = os.path.join(build, "bin", "clang" + (".exe" if os.name == "nt" else ""))
    return clang if os.path.isfile(clang) else None


def vs_llvm_dir():
    vc = os.environ.get("VCINSTALLDIR") if os.name == "nt" else None
    return os.path.join(vc, "Tools", "Llvm", "x64") if vc else None


def find_rtlib(arch):
    """lit.cfg.py's msil_native_rtlib=auto: the newest
    lib/clang/<version>/lib/windows/clang_rt.builtins-<arch>.lib of the Visual
    Studio LLVM, else of the LLVM whose clang-cl or lld-link is on PATH."""
    roots = [r for r in [vs_llvm_dir()] if r]
    for tool in ("clang-cl", "lld-link"):
        found = shutil.which(tool)
        if found:
            roots.append(os.path.dirname(os.path.dirname(found)))
    name = "clang_rt.builtins-%s.lib" % arch

    def key(path):
        ver = os.path.basename(os.path.dirname(os.path.dirname(os.path.dirname(path))))
        return tuple(int(x) for x in re.findall(r"\d+", ver))

    for root in roots:
        hits = glob.glob(
            os.path.join(root, "lib", "clang", "*", "lib", "windows", name)
        )
        if hits:
            return os.path.normpath(max(hits, key=key))
    return None


def find_ld_lld(clang, name="ld.lld"):
    """ld.lld (or lld-link) next to clang, on PATH, or in the Visual Studio
    LLVM, as lit.cfg.py finds %ld_lld and %lld_link."""
    exe = name + (".exe" if os.name == "nt" else "")
    candidates = [os.path.join(os.path.dirname(os.path.abspath(clang)), exe)]
    candidates.append(shutil.which(name))
    if vs_llvm_dir():
        candidates.append(os.path.join(vs_llvm_dir(), "bin", exe))
    for c in candidates:
        if c and os.path.isfile(c):
            return os.path.normpath(c)
    return None


def find_tool_next_to(command, name):
    """<name> in the directory of the first word of command (the clang
    command), else on PATH, else None."""
    exe = name + (".exe" if os.name == "nt" else "")
    first = command[0] if command else ""
    candidates = [os.path.join(os.path.dirname(os.path.abspath(first)), exe)]
    candidates.append(shutil.which(name))
    for c in candidates:
        if c and os.path.isfile(c):
            return os.path.normpath(c)
    return None


@functools.lru_cache(maxsize=None)
def has_hosted_marker(directory):
    """True if the lit.local.cfg of directory has the marker line of a
    hosted directory (blanks around it allowed)."""
    try:
        with open(
            os.path.join(directory, "lit.local.cfg"), encoding="utf-8", errors="replace"
        ) as f:
            return any(line.strip() == HOSTED_MARKER for line in f)
    except OSError:
        return False


def in_hosted_dir(src):
    """True if the test src lies in a hosted directory (plan 5.17.3): the
    lit.local.cfg of its directory or of one above it has the marker line, as
    lit inherits local configurations, up to the suite's root (the directory
    of lit.cfg.py), whose own lit.local.cfg lit does not read."""
    d = os.path.dirname(os.path.abspath(src))
    while True:
        if any(os.path.isfile(os.path.join(d, n)) for n in SUITE_CONFIGS):
            return False
        if has_hosted_marker(d):
            return True
        parent = os.path.dirname(d)
        if parent == d:
            return False
        d = parent


def parse_limits(text, timeout, mem_limit_mb):
    """The %{msil_limits} value text ('--timeout S --mem-limit-mb N', either
    alone, also as --timeout=S): (timeout, memory cap), each defaulting to
    the given one; raises ValueError."""
    words = msil_prog.split_words(text)
    i = 0
    while i < len(words):
        name, eq, value = words[i].partition("=")
        if name not in ("--timeout", "--mem-limit-mb"):
            raise ValueError("%r is not --timeout S or --mem-limit-mb N" % words[i])
        if not eq:
            i += 1
            if i == len(words):
                raise ValueError("%s has no value" % name)
            value = words[i]
        try:
            number = float(value) if name == "--timeout" else int(value)
        except ValueError:
            raise ValueError("%s %r is not a number" % (name, value))
        if not number > 0:
            raise ValueError("%s %s is not positive" % (name, value))
        if name == "--timeout":
            timeout = number
        else:
            mem_limit_mb = number
        i += 1
    return timeout, mem_limit_mb


class TestFileError(Exception):
    """A REDEFINE: value that the oracle cannot use."""


class Mode:
    """How one test is built and run (Oracle.mode): hosted (its C flags are
    hosted mode's), oracle_hosted (msil-prog.py --hosted), rule (None or
    (operating systems, reason) of %{msil_expected_rule}), timeout and
    mem_limit_mb (%{msil_limits})."""

    def __init__(self, hosted, oracle_hosted, rule, timeout, mem_limit_mb):
        self.hosted = hosted
        self.oracle_hosted = oracle_hosted
        self.rule = rule
        self.timeout = timeout
        self.mem_limit_mb = mem_limit_mb


class TestInfo:
    """What update-expected.py reads from a test file: the kind of test
    ('single' for %{msil_c}/%{msil_ll}, 'prog' for %{msil_prog}, 'loud' for
    %{msil_prog_loud_<L>}, 'fail' and 'det' for the composites without a
    transcript, None), its REQUIRES: and UNSUPPORTED: expressions and its
    REDEFINE: values."""

    def __init__(self, path):
        self.path = path
        self.kind = None
        self.requires = []
        self.unsupported = []
        self.redefines = {}
        # The RUN: commands before the first composite (continuation lines
        # joined as lit joins them): what a test runs to prepare its inputs.
        self.prep = []
        # Whether the test has split-file parts (a part separator line, or
        # the usual 'rm -rf %t && split-file %s %t' before its composite):
        # build_prog splits only such a test (M23: the programs of
        # msil/test/Programs have none; their sources are files in %S).
        self.split_parts = False
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                lines = f.read().splitlines()
        except OSError:
            return
        single = prog = loud = fail = det = False
        pending = None  # a REDEFINE: body continued with a trailing '\'
        runs, composite_seen = [], False
        for line in lines:
            self.split_parts = self.split_parts or bool(SPLIT_PART_RE.match(line))
            single = single or bool(ORACLE_RUN_RE.search(line))
            prog = prog or bool(PROG_RUN_RE.search(line))
            loud = loud or bool(LOUD_RUN_RE.search(line))
            fail = fail or bool(FAIL_RUN_RE.search(line))
            det = det or bool(DET_RUN_RE.search(line))
            r = RUN_RE.search(line)
            if r:
                text = r.group(1).rstrip()
                if runs and runs[-1].endswith("\\"):
                    runs[-1] = runs[-1][:-1] + text
                else:
                    runs.append(text)
                continue
            m = DIRECTIVE_RE.search(line)
            if not m:
                continue
            keyword, text = m.group(1), m.group(2)
            if keyword == "REDEFINE":
                # lit's continuation: the '\' and the blanks around it become
                # one blank.
                body = text
                if pending is not None:
                    body = pending.rstrip()[:-1].rstrip() + " " + text.lstrip()
                if body.rstrip().endswith("\\"):
                    pending = body
                    continue
                pending = None
                d = REDEFINE_RE.match(body)
                if d:
                    self.redefines[d.group(1)] = d.group(2).strip()
                continue
            exprs = [e.strip() for e in text.split(",") if e.strip()]
            (self.requires if keyword == "REQUIRES" else self.unsupported).extend(exprs)
        for cmd in runs:
            composite_seen = composite_seen or bool(COMPOSITE_RE.search(cmd))
            if not composite_seen:
                self.prep.append(cmd.strip())
        self.split_parts = self.split_parts or any(
            SPLIT_ONLY_RE.match(c) for c in self.prep
        )
        if prog:
            self.kind = "prog"
        elif single and path.endswith((".c", ".ll")):
            self.kind = "single"
        elif loud:
            self.kind = "loud"
        elif fail:
            self.kind = "fail"
        elif det:
            self.kind = "det"

    def skip_reason(self, features):
        """Why the oracle skips this test (REQUIRES:/UNSUPPORTED:), or None."""
        for expr in self.requires:
            if not evaluate(expr, features, unknown=True):
                return "REQUIRES: %s" % expr
        for expr in self.unsupported:
            if evaluate(expr, features, unknown=False):
                return "UNSUPPORTED: %s" % expr
        return None


def load_lit():
    """lit's ShUtil and TestRunner modules from llvm/utils/lit (the internal
    shell and the %if substitution of the RUN: lines); raises ImportError."""
    sys.path.insert(0, os.path.join(REPO, "llvm", "utils", "lit"))
    try:
        import lit.ShUtil
        import lit.TestRunner
    finally:
        sys.path.pop(0)
    return lit.ShUtil, lit.TestRunner


def evaluate(expr, features, unknown):
    """lit's boolean expression expr as far as the features the oracle knows
    (KNOWN_FEATURE_PREFIXES; true when they are in features) decide it:
    True or False when every value of the other features gives that result,
    else unknown. So is an expression with a regular expression, one with
    more than ten other features, or one that lit cannot parse."""
    try:
        sys.path.insert(0, os.path.join(REPO, "llvm", "utils", "lit"))
        from lit.BooleanExpression import BooleanExpression
    except ImportError:
        return unknown
    finally:
        sys.path.pop(0)
    try:
        tokens = [
            t
            for t in BooleanExpression.tokenize(expr)
            if t is not BooleanExpression.END and t not in ("(", ")", "&&", "||", "!")
        ]
        if any("{{" in t for t in tokens):
            return unknown
        known = {t for t in tokens if t.startswith(KNOWN_FEATURE_PREFIXES)}
        others = sorted({t for t in tokens if t not in known} - {"true"})
        if len(others) > 10:
            return unknown
        true = {t for t in known if t in features}
        results = {
            BooleanExpression.evaluate(
                expr, true | {t for i, t in enumerate(others) if mask >> i & 1}
            )
            for mask in range(1 << len(others))
        }
    except ValueError:
        return unknown
    return results.pop() if len(results) == 1 else unknown


def suffixes_test_only(text):
    """True if the lit.local.cfg text sets config.suffixes to the one suffix
    ".test", in any quoting ([".test"], ['.test'], (".test",), ...); the last
    such assignment at the top level decides, as lit runs the file."""
    try:
        tree = ast.parse(text)
    except SyntaxError:
        return bool(TEST_ONLY_SUFFIXES_RE.search(text))
    found = None
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        for target in node.targets:
            if (
                isinstance(target, ast.Attribute)
                and target.attr == "suffixes"
                and isinstance(target.value, ast.Name)
                and target.value.id == "config"
            ):
                try:
                    value = ast.literal_eval(node.value)
                except (ValueError, TypeError, SyntaxError):
                    found = False
                    continue
                found = isinstance(value, (list, tuple, set)) and list(value) == [".test"]
    return bool(found)


def test_only_dir(path):
    """True if the lit.local.cfg of the directory path makes it a suite of
    .test files only (config.suffixes = [".test"])."""
    try:
        with open(os.path.join(path, "lit.local.cfg"), encoding="utf-8") as f:
            return suffixes_test_only(f.read())
    except OSError:
        return False


def test_only_above(path):
    """True if a directory above path, up to msil/test, is a .test-only
    suite: lit's configuration is inherited, so a walk that starts below
    msil/test/Programs (msil/test/Programs/support) is one too."""
    root = os.path.normcase(os.path.abspath(TEST_ROOT))
    d = os.path.abspath(path)
    while os.path.normcase(d) != root:
        parent = os.path.dirname(d)
        if parent == d or not os.path.normcase(parent).startswith(root):
            return False
        d = parent
        if test_only_dir(d):
            return True
    return False


def norm(path):
    """path as collect compares paths: absolute, with '..' resolved, in the
    case and with the separators of os.path.normcase."""
    return os.path.normcase(os.path.abspath(path))


def is_within(path, top):
    """True if the norm() path path is the norm() path top or lies below
    it."""
    return path == top or path.startswith(os.path.join(top, ""))


def is_harness(path):
    """True if path is msil/test/harness of this source tree or lies below
    it: a harness self-test (plan 5.17.5). The self-tests' copies elsewhere
    (their %t directories under the build tree) are not."""
    return is_within(norm(path), norm(HARNESS_DIR))


def walk_reaches(top, path):
    """True if the walk of the directory top (collect) reaches the directory
    path, both norm() paths: path is top, or lies below it under none of the
    directories the walk does not enter (WALK_PRUNED)."""
    if not is_within(path, top):
        return False
    pruned = {os.path.normcase(d) for d in WALK_PRUNED}
    return not pruned.intersection(path[len(top) :].split(os.sep))


def collect(paths):
    """(path, explicit) for every test source to process, explicit when the
    command line names it, and (path, reason) for a directory skipped as a
    whole, or a named file skipped, for the reason SKIP_CTEST or
    SKIP_HARNESS. Each test and skipped directory comes once, however the
    arguments nest or repeat (paths compared as norm() paths): a directory
    argument that the walk of another one reaches is left out, as is a
    repeated one, and a file that the command line names comes once, where
    it names it, also when a walk finds it."""
    norms = [norm(p) for p in paths]
    isdirs = [os.path.isdir(p) for p in paths]
    tops = [(i, n) for i, (n, d) in enumerate(zip(norms, isdirs)) if d]
    named = {n for n, d in zip(norms, isdirs) if not d}
    seen = set()
    for i, p in enumerate(paths):
        if not isdirs[i]:
            if norms[i] not in seen:
                seen.add(norms[i])
                yield p, SKIP_HARNESS if is_harness(p) else True
            continue
        if any(
            j != i and walk_reaches(top, norms[i]) and (top != norms[i] or j < i)
            for j, top in tops
        ):
            continue
        # The .test-only directories (their lit.local.cfg); lit's
        # configuration is inherited, so their subdirectories are too, and so
        # is p when a directory above it is one.
        tests_only = {p} if test_only_above(p) else set()
        for root, dirs, files in os.walk(p):
            if is_harness(root):
                skip = SKIP_HARNESS
            elif CTEST_LIST in files:
                skip = SKIP_CTEST
            else:
                skip = None
            if skip:
                dirs[:] = []
                if norm(root) not in seen:
                    seen.add(norm(root))
                    yield root, skip
                continue
            dirs[:] = sorted(d for d in dirs if d not in WALK_PRUNED)
            if root in tests_only or test_only_dir(root):
                tests_only.update(os.path.join(root, d) for d in dirs)
                suffixes = (".test",)
            else:
                suffixes = (".c", ".ll", ".test")
            for f in sorted(files):
                if not f.endswith(suffixes):
                    continue
                path = os.path.join(root, f)
                n = norm(path)
                if n not in seen and n not in named:
                    seen.add(n)
                    yield path, False


def read_expected(path):
    with open(path, encoding="utf-8", newline="") as f:
        return msil_run.fold_newlines(f.read())


def write_expected(path, text):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


class Log:
    """What one test prints, kept until every test before it has printed
    (--jobs builds and runs several tests at once; the output stays in the
    order of the tests)."""

    def __init__(self):
        self.items = []

    def out(self, data):
        self.items.append((sys.stdout, data))

    def err(self, data):
        self.items.append((sys.stderr, data))

    def flush(self):
        for stream, data in self.items:
            msil_run.emit(stream, data)


class Oracle:
    """The settings every test shares: the parsed arguments and what main()
    derived from them."""

    def __init__(self, args, osname, e7, native_ldflags, ld_lld, libs, features):
        self.args = args
        self.osname = osname
        self.e7 = e7
        self.native_ldflags = native_ldflags
        self.ld_lld = ld_lld
        self.libs = libs
        self.features = features
        self.start_o = None
        self.env = dict(os.environ)
        # The linkers of a preparation (prepare).
        self.prep_ld_lld = ld_lld or find_ld_lld(args.clang_cmd[0])
        self.prep_lld_link = find_ld_lld(args.clang_cmd[0], "lld-link")

    def step(self, cmd, what, log, mode=None):
        """Run one compile or link step under the limits (the test's, of
        mode, else the run's): ten times the timeout and the memory cap;
        False (after logging why) when it failed."""
        args = self.args
        timeout = mode.timeout if mode else args.timeout
        mem_limit_mb = mode.mem_limit_mb if mode else args.mem_limit_mb
        try:
            res = msil_run.run_limited(cmd, None, self.env, timeout * 10, mem_limit_mb)
        except OSError as e:
            log.err("update-expected: cannot run %s: %s\n" % (cmd[0], e))
            return False
        log.out(res.stdout)
        log.err(res.stderr)
        if res.returncode != 0 or res.timed_out or res.memory_limit:
            why = (
                "timeout"
                if res.timed_out
                else "memory limit"
                if res.memory_limit
                else "exit status %s" % res.returncode
            )
            log.err("update-expected: %s failed (%s)\n" % (what, why))
            return False
        return True

    def c_flags(self, mode, include, quote=str):
        """The words of %{msil_cflags}, include being those of its -I
        <msil/test/Inputs>: --cflags and include, and for a test in hosted
        mode (plan 5.17.3) --cflags without -ffreestanding, include and
        %{msil_hosted_cflags} (its path written by quote)."""
        words = self.args.cflags.split()
        if not mode.hosted:
            return words + include
        words = [w for w in words if w != "-ffreestanding"]
        return words + include + self.hosted_cflags(quote)

    def hosted_cflags(self, quote=str):
        """%{msil_hosted_cflags}: --sysroot=<glibc sysroot> for a Linux
        triple (when there is one), nothing for a Windows triple."""
        target = E7_TARGET if self.e7 else self.args.target
        if "linux" not in target.split("-") or not self.args.glibc_sysroot:
            return []
        return ["--sysroot=" + quote(self.args.glibc_sysroot)]

    def build(self, src, work, log, level, mode):
        """The native program of src at level in the directory work and the
        msil-run.py options that run it, or (None, None). The command lines
        are those of the native-oracle legs of %{msil_c} and %{msil_ll} in
        msil/test/lit.cfg.py; the hosted oracle (mode) builds the source as
        a one-source program with msil-prog.py --mode native --hosted."""
        args = self.args
        name = os.path.splitext(os.path.basename(src))[0]
        inputs = os.path.join(TEST_ROOT, "Inputs")
        if src.endswith(".ll"):
            lang = flags = ["-Wno-override-module"]
        else:
            try:
                copts = self.copts(TestInfo(src), src)
            except (msil_prog.UsageError, ValueError) as e:
                # As build_prog reports it: no native build without the flags.
                log.err("update-expected: %s: %%{msil_copts}: %s\n" % (src, e))
                return None, None
            lang = self.c_flags(mode, ["-I", inputs]) + copts
            # msil-prog.py's --cflags: -I<dir> as one word, as lit writes it.
            flags = self.c_flags(mode, ["-I" + inputs]) + copts
        if mode.oracle_hosted:
            parts = ([src], [], "", "", "")
            exe = self.native_prog(src, parts, flags, level, mode, work, log)
            if not exe:
                return None, None
            run = self.run_mode() + ["--dep-dir", os.path.join(work, "prog", "shlib")]
            return exe, run
        if not self.e7:
            exe = os.path.join(work, name + ".exe")
            cmd = [*args.clang_cmd, "--target=" + args.target, level] + lang
            cmd += [src, *self.native_ldflags, "-o", exe]
            if not self.step(cmd, "the native compile", log, mode):
                return None, None
            return exe, ["--native"]
        obj = os.path.join(work, name + ".o")
        exe = os.path.join(work, name + ".elf")
        cmd = [*args.clang_cmd, "--target=" + E7_TARGET, "-fno-pic"] + lang
        cmd += [level, "-c", src, "-o", obj]
        if not self.step(cmd, "the E7 compile", log, mode):
            return None, None
        cmd = [self.ld_lld, "-dynamic-linker", "/lib64/ld-linux-x86-64.so.2"]
        cmd += ["--as-needed", self.start_o, obj, *self.libs, "-o", exe]
        if not self.step(cmd, "the E7 link", log, mode):
            return None, None
        return exe, self.run_mode()

    def run_mode(self):
        """The msil-run.py options that run a native program."""
        if not self.e7:
            return ["--native"]
        return [
            "--native",
            "--runner",
            "wsl",
            "--wsl-distro",
            self.args.wsl_distro,
            "--expect-os",
            "linux",
        ]

    def build_prog(self, info, work, log, level, mode):
        """The native program of the %{msil_prog} test info at level in the
        directory work, built as the native-oracle leg of %{msil_prog} builds
        it (msil-prog.py --mode native), and the msil-run.py options that run
        it, or (None, None)."""
        args, src = self.args, info.path
        t = os.path.join(work, "t")
        if not info.split_parts:
            # No parts to write (M23: msil/test/Programs names its sources
            # in %S and %{msil_corpus}), so no split-file is needed; %t is
            # an empty directory for the preparation commands.
            os.makedirs(t, exist_ok=True)
        elif not args.split_file_cmd:
            log.err("update-expected: no split-file found; pass --split-file PATH\n")
            return None, None
        elif not self.step([*args.split_file_cmd, src, t], "split-file", log, mode):
            return None, None
        test_dir = os.path.dirname(os.path.abspath(src))
        stem = os.path.splitext(os.path.basename(src))[0]

        def value(name, default, words):
            """The REDEFINE: value of name; for a list of words, a path with
            a blank (a temporary directory) is quoted for split_words. A
            value with lit's %if (and %else) is decided as prepare decides
            it."""
            text = info.redefines.get(name, default)
            if "%if " in text:
                text = self.decide_if(text)
            for key, sub in (
                ("%{msil_corpus}", args.corpus_dir.replace("\\", "/")),
                ("%{s:stem}", stem),
                ("%s", os.path.abspath(src)),
                ("%S", test_dir),
                ("%p", test_dir),
                ("%t", t),
            ):
                text = text.replace(key, msil_prog.quote_word(sub) if words else sub)
            return text

        prep = [c for c in info.prep if not SPLIT_ONLY_RE.match(c)]
        if prep and not self.prepare(prep, src, stem, t, work, log, mode):
            return None, None
        try:
            srcs = msil_prog.split_words(value("%{msil_srcs}", "%s", True))
            stock = msil_prog.split_words(value("%{msil_stock_srcs}", "", True))
            deps = msil_prog.split_words(value("%{msil_deps}", "", True))
            archives = value("%{msil_archives}", "", False)
            shlibs = value("%{msil_shlibs}", "", False)
            native_libs = value("%{msil_native_libs}", "", True)
            copts = msil_prog.split_words(value("%{msil_copts}", "", True))
        except (msil_prog.UsageError, ValueError) as e:
            log.err("update-expected: %s: %s\n" % (src, e))
            return None, None
        # lit's internal shell expands a wildcard in a run's --dep list.
        deps = [
            m
            for d in deps
            for m in (sorted(glob.glob(d)) if glob.has_magic(d) else [d]) or [d]
        ]
        # -I<dir> as one word, as %{msil_cflags} writes it, then the test's
        # %{msil_copts}, as lit's '%{msil_cflags} %{msil_copts}' gives them.
        cflags = self.c_flags(mode, ["-I" + os.path.join(TEST_ROOT, "Inputs")])
        cflags += copts
        parts = (srcs, stock, archives, shlibs, native_libs)
        exe = self.native_prog(src, parts, cflags, level, mode, work, log)
        if not exe:
            return None, None
        # %{msil_deps} and the libraries of %{msil_shlibs} (shlib/, which
        # msil-prog.py always makes): copied next to the native program, as
        # the composite's native-oracle leg does (msil-run.py --dep and
        # --dep-dir).
        run = self.run_mode() + ["--dep-dir", os.path.join(work, "prog", "shlib")]
        return exe, run + (["--dep", *deps] if deps else [])

    def native_prog(self, src, parts, cflags, level, mode, work, log):
        """'msil-prog.py --mode native' for the test src: the program of
        parts, (srcs, stock, archives, shlibs, native_libs) (the sources and
        stock sources as lists, then the values of %{msil_archives},
        %{msil_shlibs} and %{msil_native_libs}), with the compile flags
        cflags (words) at level, built in work/prog under the test's limits
        and, for mode's hosted oracle, with --hosted (plan 5.17.3); its path,
        or None (after logging why)."""
        args = self.args
        srcs, stock, archives, shlibs, native_libs = parts
        exe = os.path.join(work, "prog." + ("elf" if self.e7 else "exe"))
        cmd = [sys.executable, os.path.join(HERE, "msil-prog.py"), "--mode", "native"]
        cmd += ["--triple", args.target + "-msil", "--level", level[1:]]
        cmd += ["--clang=" + args.clang, "--cflags=" + msil_prog.join_words(cflags)]
        if args.ar:
            cmd.append("--ar=" + args.ar)
        if self.e7:
            cmd += ["--ld-lld=" + self.ld_lld, "--linux-sysroot=" + args.linux_sysroot]
            cmd.append("--start=" + self.start_o)
        else:
            cmd.append("--native-ldflags=" + msil_prog.join_words(self.native_ldflags))
        if mode.oracle_hosted:
            # E7h with the E7 options, the DLL C runtime for MSVC triples.
            cmd.append("--hosted")
            if self.e7:
                cmd.append("--glibc-sysroot=" + args.glibc_sysroot)
        # lit's %msil_prog_build: %{msil_limits} --timeout-factor 10.
        cmd += ["--timeout", repr(mode.timeout), "--timeout-factor", "10"]
        cmd += ["--mem-limit-mb", str(mode.mem_limit_mb)]
        cmd += ["--workdir", os.path.join(work, "prog"), "--out", exe]
        cmd += ["--src", *srcs, "--stock-src", *stock]
        cmd.append("--archives=" + archives)
        cmd.append("--shlibs=" + shlibs)
        cmd.append("--native-lib=" + native_libs)
        try:
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        except OSError as e:
            log.err("update-expected: cannot run msil-prog.py: %s\n" % e)
            return None
        if res.returncode != 0:
            # The commands msil-prog.py printed and the tools' messages.
            log.out(res.stdout)
            log.err(res.stderr)
            log.err(
                "update-expected: the native build (msil-prog.py) failed (exit "
                "status %s)\n" % res.returncode
            )
            return None
        return exe

    def placeholder(self, info, src, name, default):
        """The REDEFINE: value of the placeholder name of the test src (info
        is its TestInfo), with %if decided and %{msil_corpus}, %{s:stem},
        %s, %S and %p replaced (M23), or default."""
        text = info.redefines.get(name)
        if text is None:
            return default
        if "%if " in text:
            text = self.decide_if(text)
        test_dir = os.path.dirname(os.path.abspath(src))
        for key, sub in (
            ("%{msil_corpus}", self.args.corpus_dir.replace("\\", "/")),
            ("%{s:stem}", os.path.splitext(os.path.basename(src))[0]),
            ("%s", os.path.abspath(src)),
            ("%S", test_dir),
            ("%p", test_dir),
        ):
            text = text.replace(key, sub)
        return text

    def copts(self, info, src):
        """The words of the %{msil_copts} REDEFINE: of a single-source test;
        raises msil_prog.UsageError for a value that does not split into
        words (an unterminated quote) and ValueError for an %if that lit
        cannot decide."""
        text = self.placeholder(info, src, "%{msil_copts}", "")
        return msil_prog.split_words(text)

    def expect_stem(self, info, src):
        """The stem of the expected file: the %{msil_expect} REDEFINE: value
        (M23), else the test's own path without its suffix."""
        try:
            text = self.placeholder(info, src, "%{msil_expect}", None)
        except ValueError:
            text = None
        if not text or not text.strip():
            return os.path.splitext(src)[0]
        return os.path.normpath(text.strip())

    def mode(self, info, src, explicit):
        """The Mode of the test src (info is its TestInfo) as lit decides it
        (plan 5.17.3-5.17.5, 5.17.9): hosted when it lies in a hosted
        directory or, named on the command line (explicit), under --hosted,
        which also forces the hosted oracle; otherwise the oracle follows a
        REDEFINE: of %{msil_oracle_flags} (only --hosted is known), else the
        directory. The REDEFINE: values of %{msil_expected_rule} and
        %{msil_limits} give the rule and the limits. Raises TestFileError for
        a value it cannot use."""
        args = self.args
        forced = bool(explicit and args.hosted)
        hosted = forced or in_hosted_dir(src)

        def value(name):
            try:
                return self.placeholder(info, src, name, "")
            except ValueError as e:
                raise TestFileError("%s: %s" % (name, e))

        oracle_hosted = hosted
        if not forced and "%{msil_oracle_flags}" in info.redefines:
            text = value("%{msil_oracle_flags}")
            try:
                words = msil_prog.split_words(text)
            except msil_prog.UsageError as e:
                raise TestFileError("%%{msil_oracle_flags}: %s" % e)
            unknown = [w for w in words if w != "--hosted"]
            if unknown:
                raise TestFileError(
                    "%%{msil_oracle_flags}: %r: the oracle knows only --hosted"
                    % unknown[0]
                )
            oracle_hosted = "--hosted" in words
        try:
            rule = msil_prog.parse_expected_rule(value("%{msil_expected_rule}"))
        except msil_prog.UsageError as e:
            raise TestFileError("%%{msil_expected_rule}: %s" % e)
        try:
            timeout, mem_limit_mb = parse_limits(
                value("%{msil_limits}"), args.timeout, args.mem_limit_mb
            )
        except (ValueError, msil_prog.UsageError) as e:
            raise TestFileError("%%{msil_limits}: %s" % e)
        return Mode(hosted, oracle_hosted, rule, timeout, mem_limit_mb)

    def decide_if(self, text):
        """text with lit's %if/%else decided by the features the oracle knows
        (unknown ones are false), as prepare decides them; raises ValueError
        when lit cannot parse it or is missing."""
        try:
            _, lit_runner = load_lit()
        except ImportError as e:
            raise ValueError("cannot load lit to decide %%if: %s" % e)
        conditions = {f: True for f in self.features}
        return lit_runner.applySubstitutions([text], [], conditions)[0]

    def prepare(self, commands, src, stem, t, work, log, mode):
        """Run the RUN: commands that come before the composite of the
        %{msil_prog} test src (after the usual 'rm -rf %t && split-file %s
        %t', which build_prog does): the commands that build its test-built
        native libraries. They run in lit's internal shell, with %if decided
        by the features the oracle knows (unknown ones are false), and with
        the substitutions of msil/test/lit.cfg.py for the oracle's triple:
        %clang_native, %clang, %ld_lld, %lld_link, %msil_ar, %msil_triple,
        %python, %{msil_cflags} (hosted mode's for a hosted test, mode),
        %{msil_hosted_cflags}, %{msil_native_ldflags}, %s, %S, %p, %t and
        %{s:stem}; any other substitution fails the test. False (after
        logging why) when a command fails."""
        args = self.args
        try:
            lit_shutil, lit_runner = load_lit()
        except ImportError as e:
            log.err("update-expected: cannot load lit's shell: %s\n" % e)
            return False
        test_dir = os.path.dirname(os.path.abspath(src))

        def q(path):
            return '"%s"' % path.replace("\\", "/")

        clang = msil_prog.join_words([w.replace("\\", "/") for w in args.clang_cmd])
        # --ar may be a command line too (a stand-in of the self-tests).
        try:
            ar_cmd = msil_prog.program(args.ar, "--ar") if args.ar else None
        except msil_prog.UsageError as e:
            log.err("update-expected: %s\n" % e)
            return False
        ar = (
            msil_prog.join_words([w.replace("\\", "/") for w in ar_cmd])
            if ar_cmd
            else "llvm-ar-not-found"
        )
        target = E7_TARGET if self.e7 else args.target
        # The values are written as lit.cfg.py writes them: %t, %s, %S and %p
        # unquoted (with '/'), the -I directory of %{msil_cflags} and the
        # tools quoted.
        cflags = " ".join(
            self.c_flags(mode, ["-I" + q(os.path.join(TEST_ROOT, "Inputs"))], q)
        )

        def p(path):
            return path.replace("\\", "/")

        subs = [
            ("%{msil_hosted_cflags}", " ".join(self.hosted_cflags(q))),
            ("%{msil_cflags}", cflags),
            (
                "%{msil_native_ldflags}",
                " ".join(q(w) for w in self.native_ldflags),
            ),
            ("%{s:stem}", stem),
            (r"%clang_native(?!\w)", "%s --target=%s" % (clang, target)),
            (r"%clang(?!\w)", clang),
            (r"%ld_lld(?!\w)", q(self.prep_ld_lld or "ld.lld-not-found")),
            (r"%lld_link(?!\w)", q(self.prep_lld_link or "lld-link-not-found")),
            (r"%msil_ar(?!\w)", ar),
            (r"%msil_triple(?!\w)", target + "-msil"),
            (r"%python(?!\w)", q(sys.executable)),
            ("%s", p(os.path.abspath(src))),
            ("%S", p(test_dir)),
            ("%p", p(test_dir)),
            ("%t", p(t)),
        ]
        conditions = {f: True for f in self.features}
        env = dict(self.env)
        # split-file and the other tools of the build, as lit finds them.
        path_dirs = [os.path.dirname(os.path.abspath(args.clang_cmd[0]))]
        if args.split_file_cmd:
            path_dirs.insert(
                0, os.path.dirname(os.path.abspath(args.split_file_cmd[0]))
            )
        msil_run.set_env_var(
            env, "PATH", os.pathsep.join(path_dirs + [env.get("PATH", "")])
        )
        for command in commands:
            try:
                line = lit_runner.applySubstitutions(
                    [command], list(subs), conditions, recursion_limit=10
                )[0]
            except ValueError as e:
                log.err("update-expected: %s: RUN: %s: %s\n" % (src, command, e))
                return False
            unknown = UNKNOWN_SUBST_RE.findall(line)
            if unknown:
                log.err(
                    "update-expected: %s: cannot run 'RUN: %s': no oracle value "
                    "for %s\n" % (src, command, ", ".join(sorted(set(unknown))))
                )
                return False
            if not line:
                continue
            log.out("update-expected: prepare: %s\n" % line)
            try:
                parsed = lit_shutil.ShParser(line, os.name == "nt", True).parse()
            except Exception as e:  # lit's parser raises plain ValueErrors
                log.err("update-expected: %s: cannot parse '%s': %s\n" % (src, line, e))
                return False
            results = []
            with PREPARE_LOCK:
                shenv = lit_runner.ShellEnvironment(work, env)
                try:
                    rc, _ = lit_runner.executeShCmd(parsed, shenv, results)
                except Exception as e:  # InternalShellError and friends
                    log.err("update-expected: %s: '%s' failed: %s\n" % (src, line, e))
                    return False
            for r in results:
                log.out(r.stdout)
                log.err(r.stderr)
            if rc != 0:
                log.err(
                    "update-expected: %s: preparing failed (exit status %s): %s\n"
                    % (src, rc, line)
                )
                return False
        return True

    def process(self, src, explicit, work):
        """Build and run one test in the directory work at every level, then
        compare or write its expected file: (its Log, 1 if it failed else
        0)."""
        args, osname = self.args, self.osname
        log = Log()
        if isinstance(explicit, str):
            # A directory, or a named file, that collect skips as a whole:
            # explicit is the reason (SKIP_CTEST, SKIP_HARNESS).
            log.out("skip (%s): %s\n" % (explicit, src))
            return log, 0
        info = TestInfo(src)
        kind = info.kind
        if explicit and src.endswith((".c", ".ll")) and kind not in ("prog", "loud"):
            kind = "single"
        if kind == "fail":
            log.out("skip (fail-only: %%{msil_prog_fail_<L>}): %s\n" % src)
            return log, 0
        if kind == "det":
            log.out("skip (det: %%{msil_det}): %s\n" % src)
            return log, 0
        if kind is None:
            log.out(
                "skip (no %%{msil_c}/%%{msil_ll}/%%{msil_prog} RUN line): %s\n" % src
            )
            return log, 0
        why = info.skip_reason(self.features)
        if why:
            log.out("skip (%s, for %s): %s\n" % (why, args.target, src))
            return log, 0
        try:
            mode = self.mode(info, src, explicit)
        except TestFileError as e:
            log.err("update-expected: %s: %s\n" % (src, e))
            log.out("FAIL (test file): %s\n" % src)
            return log, 1
        stem = self.expect_stem(info, src)
        base = stem + ".expected"
        override = stem + "." + osname + ".expected"

        def rule_file(name):
            """The expected file of the OS name, as msil-run.py --expect
            picks it."""
            own = stem + "." + name + ".expected"
            return own if os.path.isfile(own) else base

        if mode.rule and osname in mode.rule[0]:
            # A hand-written expected file (plan 5.17.4): no build, no
            # comparison, no write.
            path = rule_file(osname)
            if not os.path.isfile(path):
                log.out(
                    "MISSING: %s (hand-written under %%{msil_expected_rule}: %s)\n"
                    % (path, mode.rule[1])
                )
                return log, 1
            log.out("rule: %s (%s)\n" % (path, mode.rule[1]))
            return log, 0
        if kind == "loud":
            log.err(
                "update-expected: %s: a %%{msil_prog_loud_<L>} test has no native "
                "oracle; its %s expected file is hand-written under a "
                "%%{msil_expected_rule} that names %s (plan 5.17.4)\n"
                % (src, osname, osname)
            )
            log.out("FAIL (test file): %s\n" % src)
            return log, 1
        if not args.check and osname == "windows" and os.path.exists(override):
            log.out("skip (has a %s override): %s\n" % (osname, src))
            return log, 0
        runs = []
        for level in args.levels:
            # One level keeps the directory layout of a run without --levels.
            lwork = work if len(args.levels) == 1 else os.path.join(work, level[1:])
            os.makedirs(lwork, exist_ok=True)
            out = self.run_level(info, src, kind, level, mode, lwork, log)
            if out is None:
                return log, 1
            runs.append((level, out))
        out = runs[0][1]

        def label(level):
            if len(args.levels) == 1:
                return "native (%s)" % osname
            return "native (%s, %s)" % (osname, level)

        if args.check:
            path = override if os.path.isfile(override) else base
            if not os.path.isfile(path):
                log.out("MISSING: %s (native transcript below)\n%s" % (path, out))
                return log, 1
            expected = read_expected(path)
            bad = [(level, text) for level, text in runs if text != expected]
            if not bad:
                log.out("ok: %s\n" % src)
                return log, 0
            log.out("MISMATCH: %s\n" % src)
            for level, text in bad:
                diff = difflib.unified_diff(
                    expected.splitlines(True), text.splitlines(True), path, label(level)
                )
                log.out("".join(diff))
            return log, 1
        for level, text in runs[1:]:
            if text != out:
                # The same output at every level (plan 5.17.13): no oracle.
                log.out(
                    "MISMATCH: %s (the native transcripts at %s and %s differ; "
                    "nothing written)\n" % (src, runs[0][0], level)
                )
                diff = difflib.unified_diff(
                    out.splitlines(True),
                    text.splitlines(True),
                    label(runs[0][0]),
                    label(level),
                )
                log.out("".join(diff))
                return log, 1
        # <stem>.expected may be the hand-written file of an OS that the
        # rule names (that OS has no file of its own): it is never written.
        named = mode.rule[0] if mode.rule else ()
        held = sorted(n for n in named if rule_file(n) == base)
        if held and not (os.path.isfile(base) and read_expected(base) == out):
            write_expected(override, out)
            log.out(
                "wrote %s (%s is the hand-written %s expected file)\n"
                % (override, os.path.basename(base), " and ".join(held))
            )
        elif (osname == "windows" and not held) or not os.path.isfile(base):
            write_expected(base, out)
            log.out("wrote %s\n" % base)
        elif read_expected(base) == out:
            if os.path.isfile(override):
                os.remove(override)
                log.out("removed %s (the transcript equals %s)\n" % (override, base))
            else:
                log.out("same as %s: %s\n" % (os.path.basename(base), src))
        else:
            write_expected(override, out)
            log.out("wrote %s (differs from %s)\n" % (override, os.path.basename(base)))
        return log, 0

    def run_level(self, info, src, kind, level, mode, work, log):
        """Build the test src (info is its TestInfo) at level in the
        directory work and run it: its transcript, or None after logging the
        failure ('FAIL (native compile)', 'FAIL (native run)', 'REFUSED:',
        each naming the level when the run has several)."""
        args = self.args
        at = "" if len(args.levels) == 1 else " at " + level
        if kind == "prog":
            exe, run_mode = self.build_prog(info, work, log, level, mode)
        else:
            exe, run_mode = self.build(src, work, log, level, mode)
        if not exe:
            log.out("FAIL (native compile%s): %s\n" % (at, src))
            return None

        run = [sys.executable, os.path.join(HERE, "msil-run.py"), *run_mode]
        run += [
            "--timeout",
            repr(mode.timeout),
            "--mem-limit-mb",
            str(mode.mem_limit_mb),
        ]
        run += ["--", exe]
        proc = subprocess.run(run, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out = msil_run.text_of(proc.stdout)
        status = out.rstrip("\n").split("\n")[-1]
        # msil-run.py exits with 1 (after printing the transcript) only for an
        # out-of-range status, which is refused below.
        if proc.returncode not in (0, 1) or not msil_run.STATUS_RE.match(status):
            log.err(msil_run.text_of(proc.stderr))
            log.out("FAIL (native run%s): %s\n" % (at, src))
            return None
        m = EXIT_RE.match(status)
        if not m or int(m.group(1)) > 255:
            why = (
                "its exit status is outside 0..255; Linux would truncate it (plan D37)"
                if msil_run.OUTSIDE_RE.match(status)
                else "a native run that does not exit normally is no oracle"
            )
            log.out(
                "REFUSED: %s: the native run%s ended with %s: %s\n"
                % (src, at, status, why)
            )
            return None
        return out


def main(argv=None):
    """Run the script with the argument list argv (default sys.argv[1:]);
    returns the exit status, also for argparse's own errors."""
    try:
        return run(sys.argv[1:] if argv is None else argv)
    except SystemExit as e:
        # argparse's usage errors (2) and --help (0), as a status.
        return e.code if isinstance(e.code, int) else (0 if e.code is None else 2)


def run(argv):
    ap = argparse.ArgumentParser(
        prog="update-expected.py",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=__doc__,
    )
    ap.add_argument(
        "--clang",
        default=default_clang(),
        help="clang for the native build (default: <build>/bin/clang)",
    )
    ap.add_argument(
        "--os",
        choices=("windows", "linux"),
        help="the oracle and the expected file: windows or linux (default: the "
        "host's; linux on a Windows host is the E7 Linux oracle)",
    )
    ap.add_argument(
        "--target",
        help="native triple (default: the host's, %s; with --os linux on Windows "
        "%s, the only one E7 builds)" % (default_target(), E7_TARGET),
    )
    ap.add_argument(
        "--cflags",
        default=DEFAULT_CFLAGS,
        help="must match %%{msil_cflags} of the tests' directory "
        "(without -I; default: %(default)s)",
    )
    ap.add_argument(
        "--opt",
        help="optimization level: O2 or --opt=-O2 (default: -O0)",
    )
    ap.add_argument(
        "--levels",
        help="several levels in one run instead of --opt: O0,O2 (each level's "
        "transcript must match)",
    )
    ap.add_argument(
        "--hosted",
        action="store_true",
        help="hosted mode for the files named on the command line (a hosted "
        "directory's C flags and the hosted oracle)",
    )
    ap.add_argument(
        "--glibc-sysroot",
        default=os.environ.get("MSIL_GLIBC_SYSROOT", DEFAULT_GLIBC_SYSROOT),
        help="hosted tests: make-glibc-sysroot.sh's directory (default: "
        "$MSIL_GLIBC_SYSROOT, else %s)"
        % (DEFAULT_GLIBC_SYSROOT.replace("%", "%%") or "none"),
    )
    ap.add_argument(
        "--rtlib",
        default="auto",
        help="compiler-rt builtins for MSVC targets: a .lib path, 'auto' (the "
        "default: lit's msil_native_rtlib=auto search) or 'none'",
    )
    ap.add_argument(
        "--linux-sysroot",
        default=os.environ.get("MSIL_LINUX_SYSROOT") or DEFAULT_LINUX_SYSROOT,
        help="E7: the directory of make-linux-sysroot.py (default: "
        "$MSIL_LINUX_SYSROOT, else %s)" % DEFAULT_LINUX_SYSROOT.replace("%", "%%"),
    )
    ap.add_argument(
        "--ld-lld",
        help="E7: ld.lld (default: next to --clang, PATH, then "
        "%%VCINSTALLDIR%%Tools\\Llvm\\x64\\bin)",
    )
    ap.add_argument(
        "--wsl-distro",
        default="Ubuntu",
        help="E7: the WSL distribution that runs the programs (default: %(default)s)",
    )
    ap.add_argument(
        "--ar",
        help="%%{msil_prog} tests: the archiver for %%{msil_archives} (default: "
        "llvm-ar next to --clang, else on PATH)",
    )
    ap.add_argument(
        "--split-file",
        help="%%{msil_prog} tests: split-file (default: next to --clang, else on "
        "PATH)",
    )
    ap.add_argument(
        "--corpus-dir",
        default=os.environ.get("MSIL_CORPUS_CACHE") or DEFAULT_CORPUS_CACHE,
        help="the exit corpus' cache, %%%%{msil_corpus} (default: "
        "$MSIL_CORPUS_CACHE, else %s)" % DEFAULT_CORPUS_CACHE.replace("%", "%%"),
    )
    ap.add_argument("--check", action="store_true", help="compare, do not write")
    ap.add_argument(
        "-j",
        "--jobs",
        type=int,
        default=os.cpu_count() or 1,
        help="tests built and run at once; the output keeps the order of the "
        "tests (default: the number of CPUs, %(default)s)",
    )
    ap.add_argument(
        "--timeout",
        type=float,
        default=msil_run.DEFAULT_TIMEOUT,
        help="limit for the native program in seconds (default %g); the "
        "compile gets ten times as long" % msil_run.DEFAULT_TIMEOUT,
    )
    ap.add_argument(
        "--mem-limit-mb",
        type=int,
        default=msil_run.DEFAULT_MEM_LIMIT_MB,
        help="memory cap in MiB (default %d)" % msil_run.DEFAULT_MEM_LIMIT_MB,
    )
    ap.add_argument("tests", nargs="+", help=".c/.ll/.test files or directories")
    args = ap.parse_args(argv)

    try:
        args.clang_cmd = (
            msil_prog.program(args.clang, "--clang") if args.clang else None
        )
    except msil_prog.UsageError as e:
        ap.error(str(e))
    if not args.clang_cmd or not (
        os.path.isfile(args.clang_cmd[0]) or shutil.which(args.clang_cmd[0])
    ):
        ap.error("no clang found; build clang or pass --clang PATH")
    if args.jobs < 1:
        ap.error("--jobs must be at least 1")
    args.corpus_dir = os.path.abspath(args.corpus_dir)
    if args.opt is not None and args.levels is not None:
        ap.error("--opt and --levels exclude each other (--levels O0 is one level)")
    if args.levels is not None:
        text, option = args.levels, "--levels"
        names = [x.strip() for x in text.split(",")]
    else:
        text, option = args.opt or "-O0", "--opt"
        names = [text]
    args.levels = []
    for name in names:
        level = name if name.startswith("-") else "-" + name
        if level[1:] not in msil_prog.LEVELS:
            levels = ", ".join(msil_prog.LEVELS)
            ap.error("%s %s: use one of %s" % (option, name, levels))
        if level in args.levels:
            ap.error("--levels %s names %s twice" % (text, level))
        args.levels.append(level)
    if args.glibc_sysroot:
        args.glibc_sysroot = os.path.abspath(args.glibc_sysroot)
    args.ar = args.ar or find_tool_next_to(args.clang_cmd, "llvm-ar")
    split_file = args.split_file or find_tool_next_to(args.clang_cmd, "split-file")
    try:
        args.split_file_cmd = (
            msil_prog.program(split_file, "--split-file") if split_file else None
        )
    except msil_prog.UsageError as e:
        ap.error(str(e))
    host = msil_run.host_os()
    osname = args.os or host
    if osname not in ("windows", "linux"):
        ap.error("no oracle for %s; pass --os windows or --os linux" % osname)
    if osname == "windows" and host != "windows":
        ap.error("--os windows needs a Windows host")
    e7 = osname == "linux" and host == "windows"
    if args.target is None:
        args.target = E7_TARGET if e7 else default_target()
    if e7 and args.target != E7_TARGET:
        ap.error(
            "the E7 Linux oracle builds %s only, not %s" % (E7_TARGET, args.target)
        )

    native_ldflags = []
    if not e7:
        if not is_windows_triple(args.target):
            native_ldflags = ["-lm"]
        elif is_msvc_triple(args.target) and host == "windows":
            if args.rtlib.lower() == "auto":
                rtlib = find_rtlib(args.target.split("-")[0])
                if not rtlib:
                    ap.error(
                        "--rtlib auto found no clang_rt.builtins library (Visual "
                        "Studio 'C++ Clang tools', VCINSTALLDIR=%s); pass --rtlib "
                        "PATH or --rtlib none"
                        % os.environ.get("VCINSTALLDIR", "<unset>")
                    )
            elif args.rtlib.lower() in ("", "none"):
                rtlib = None
            else:
                rtlib = os.path.abspath(args.rtlib)
                if not os.path.isfile(rtlib):
                    ap.error("--rtlib %s: no such file" % args.rtlib)
            if rtlib:
                native_ldflags = ["-l" + rtlib]
        if host == "windows" and not os.environ.get("LIB"):
            print(
                "update-expected: warning: LIB is not set; the native link needs a "
                "VS x64 developer prompt",
                file=sys.stderr,
            )

    ld_lld = libs = None
    if e7:
        ld_lld = args.ld_lld or find_ld_lld(args.clang_cmd[0])
        if not ld_lld or not os.path.isfile(ld_lld):
            ap.error("no ld.lld found for the E7 Linux oracle; pass --ld-lld PATH")
        sysroot = os.path.abspath(args.linux_sysroot)
        libdir = os.path.join(sysroot, "usr", "lib", "x86_64-linux-gnu")
        libs = [os.path.join(libdir, n) for n in E7_LIBS]
        lacking = [n for n, p in zip(E7_LIBS, libs) if not os.path.isfile(p)]
        if lacking:
            ap.error(
                "--linux-sysroot %s lacks %s; run 'python "
                "msil/utils/make-linux-sysroot.py --out %s'"
                % (args.linux_sysroot, ", ".join(lacking), args.linux_sysroot)
            )

    # The features whose truth the oracle knows (KNOWN_FEATURE_PREFIXES), as
    # lit.cfg.py sets them for a run of the triple <target>-msil.
    parts = args.target.split("-")
    features = {"msil-triple=%s-msil" % args.target, "system-" + host}
    if "windows" in parts:
        features.add("msil-triple-windows")
    if "linux" in parts:
        features.add("msil-triple-linux")
    if e7:
        features.add("msil-wsl")
    oracle = Oracle(args, osname, e7, native_ldflags, ld_lld, libs, features)
    failures = 0
    with tempfile.TemporaryDirectory(prefix="msil-update-expected-") as tmp:
        if e7:
            oracle.start_o = os.path.join(tmp, "start.o")
            cmd = [*args.clang_cmd, "--target=" + E7_TARGET, "-ffreestanding"]
            cmd += ["-fno-pic"]
            cmd += ["-O2", "-c", os.path.join(HERE, "linux-oracle", "start.c")]
            cmd += ["-o", oracle.start_o]
            log = Log()
            ok = oracle.step(cmd, "compiling msil/utils/linux-oracle/start.c", log)
            log.flush()
            if not ok:
                return 1
        items = list(collect(args.tests))

        def one(indexed):
            i, (src, explicit) = indexed
            work = os.path.join(tmp, "%05d" % i)
            os.makedirs(work)
            return oracle.process(src, explicit, work)

        jobs = max(1, min(args.jobs, len(items)))
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            for log, failed in pool.map(one, enumerate(items)):
                log.flush()
                failures += failed
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

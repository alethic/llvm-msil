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

The oracle is a native build of the same source with the just-built clang
(default <build>/bin/clang, where <build> is $MSIL_BUILD_DIR or <repo>/build),
at --opt (default -O0), with the flags of %{msil_cflags} (-std=gnu17
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
are always processed as %{msil_c} or %{msil_ll} tests.

A %{msil_prog} test (a .test file in the format of plan 5.12.5) is built the
way the native-oracle leg of %{msil_prog} builds it: its split-file parts are
written into a temporary directory (split-file, default next to --clang, else
on PATH; or --split-file), its REDEFINE: lines give %{msil_srcs} (default:
the test file), %{msil_stock_srcs} and %{msil_archives} with %s, %S, %p, %t
and %{s:stem} replaced (%t is that directory), and 'msil-prog.py --mode
native' builds them at --opt with --cflags, the --target triple's
-lm/compiler-rt rule and the archiver --ar (default llvm-ar next to --clang,
else on PATH); on a Windows host --os linux uses the E7 recipe. %{msil_libs}
belongs to the MSIL link only and is not read. A REQUIRES: line that is false,
or an UNSUPPORTED: line that is true, for the oracle skips a test of any kind
when the features the oracle knows decide it whatever the other features are:
msil-triple-windows, msil-triple-linux and msil-triple=<target>-msil follow
--target, system-<host os> the host and msil-wsl the E7 oracle.

--clang may also be a command line (a stand-in compiler of the harness
self-tests, written as msil-prog.py's --clang takes it).

Windows: run from a VS x64 developer prompt (the Windows native link needs
LIB; msil-env.ps1 sets it up). Exit status: 0 if everything was written or
matched, 1 otherwise.
"""

import argparse
import concurrent.futures
import difflib
import glob
import importlib.util
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
TEST_ROOT = os.path.join(os.path.dirname(HERE), "test")
ORACLE_RUN_RE = re.compile(r"RUN:.*%\{msil_(c|ll)(_O[0-3sz])?\}")
# %{msil_prog} tests, and the composites that need no .expected file.
PROG_RUN_RE = re.compile(r"RUN:.*%\{msil_prog(_O[0-3sz])?\}")
FAIL_RUN_RE = re.compile(r"RUN:.*%\{msil_prog_fail_O[0-3sz]\}")
DET_RUN_RE = re.compile(r"RUN:.*%\{msil_det\}")
# lit's directives (a keyword not inside a longer word).
DIRECTIVE_RE = re.compile(r"(?<![\w-])(REQUIRES|UNSUPPORTED|REDEFINE):(.*)$")
REDEFINE_RE = re.compile(r"^\s*(%\{[_a-zA-Z][-_:0-9a-zA-Z]*\})\s*=(.*)$")
# The features whose truth the oracle knows (see the module documentation).
KNOWN_FEATURE_PREFIXES = ("msil-triple", "system-", "msil-wsl")
EXIT_RE = re.compile(r"^\[exit (\d+)\]$")
# The C flags of %{msil_cflags} (lit.cfg.py), without -I.
DEFAULT_CFLAGS = "-std=gnu17 -ffreestanding -Werror=return-type"
E7_TARGET = "x86_64-unknown-linux-gnu"
E7_LIBS = ("libm.so.6", "libc.so.6", "libgcc_s.so.1")
DEFAULT_LINUX_SYSROOT = r"C:\llvm-msil-scratch\linux-sysroot"


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


def find_ld_lld(clang):
    exe = "ld.lld" + (".exe" if os.name == "nt" else "")
    candidates = [os.path.join(os.path.dirname(os.path.abspath(clang)), exe)]
    candidates.append(shutil.which("ld.lld"))
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


class TestInfo:
    """What update-expected.py reads from a test file: the kind of test
    ('single' for %{msil_c}/%{msil_ll}, 'prog' for %{msil_prog}, 'fail' and
    'det' for the composites without a transcript, None), its REQUIRES: and
    UNSUPPORTED: expressions and its REDEFINE: values."""

    def __init__(self, path):
        self.path = path
        self.kind = None
        self.requires = []
        self.unsupported = []
        self.redefines = {}
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                lines = f.read().splitlines()
        except OSError:
            return
        single = prog = fail = det = False
        pending = None  # a REDEFINE: body continued with a trailing '\'
        for line in lines:
            single = single or bool(ORACLE_RUN_RE.search(line))
            prog = prog or bool(PROG_RUN_RE.search(line))
            fail = fail or bool(FAIL_RUN_RE.search(line))
            det = det or bool(DET_RUN_RE.search(line))
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
        if prog:
            self.kind = "prog"
        elif single and path.endswith((".c", ".ll")):
            self.kind = "single"
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


def collect(paths):
    """(path, explicit) for every test source to process."""
    for p in paths:
        if os.path.isdir(p):
            for root, dirs, files in os.walk(p):
                dirs[:] = sorted(d for d in dirs if d not in ("Inputs", "Output"))
                for f in sorted(files):
                    if f.endswith((".c", ".ll", ".test")):
                        yield os.path.join(root, f), False
        else:
            yield p, True


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

    def step(self, cmd, what, log):
        """Run one compile or link step under the limits; False (after
        logging why) when it failed."""
        args = self.args
        try:
            res = msil_run.run_limited(
                cmd, None, self.env, args.timeout * 10, args.mem_limit_mb
            )
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

    def build(self, src, work, log):
        """The native program of src in the directory work and the
        msil-run.py options that run it, or (None, None). The command lines
        are those of the native-oracle legs of %{msil_c} and %{msil_ll} in
        msil/test/lit.cfg.py."""
        args = self.args
        name = os.path.splitext(os.path.basename(src))[0]
        if src.endswith(".ll"):
            lang = ["-Wno-override-module"]
        else:
            lang = args.cflags.split() + ["-I", os.path.join(TEST_ROOT, "Inputs")]
        if not self.e7:
            exe = os.path.join(work, name + ".exe")
            cmd = [*args.clang_cmd, "--target=" + args.target, args.opt] + lang
            cmd += [src, *self.native_ldflags, "-o", exe]
            if not self.step(cmd, "the native compile", log):
                return None, None
            return exe, ["--native"]
        obj = os.path.join(work, name + ".o")
        exe = os.path.join(work, name + ".elf")
        cmd = [*args.clang_cmd, "--target=" + E7_TARGET, "-fno-pic"] + lang
        cmd += [args.opt, "-c", src, "-o", obj]
        if not self.step(cmd, "the E7 compile", log):
            return None, None
        cmd = [self.ld_lld, "-dynamic-linker", "/lib64/ld-linux-x86-64.so.2"]
        cmd += ["--as-needed", self.start_o, obj, *self.libs, "-o", exe]
        if not self.step(cmd, "the E7 link", log):
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

    def build_prog(self, info, work, log):
        """The native program of the %{msil_prog} test info in the directory
        work, built as the native-oracle leg of %{msil_prog} builds it
        (msil-prog.py --mode native), and the msil-run.py options that run
        it, or (None, None)."""
        args, src = self.args, info.path
        t = os.path.join(work, "t")
        if not args.split_file_cmd:
            log.err("update-expected: no split-file found; pass --split-file PATH\n")
            return None, None
        if not self.step([*args.split_file_cmd, src, t], "split-file", log):
            return None, None
        test_dir = os.path.dirname(os.path.abspath(src))
        stem = os.path.splitext(os.path.basename(src))[0]

        def value(name, default, words):
            """The REDEFINE: value of name; for a list of words, a path with
            a blank (a temporary directory) is quoted for split_words."""
            text = info.redefines.get(name, default)
            for key, sub in (
                ("%{s:stem}", stem),
                ("%s", os.path.abspath(src)),
                ("%S", test_dir),
                ("%p", test_dir),
                ("%t", t),
            ):
                text = text.replace(key, msil_prog.quote_word(sub) if words else sub)
            return text

        try:
            srcs = msil_prog.split_words(value("%{msil_srcs}", "%s", True))
            stock = msil_prog.split_words(value("%{msil_stock_srcs}", "", True))
        except msil_prog.UsageError as e:
            log.err("update-expected: %s: %s\n" % (src, e))
            return None, None
        exe = os.path.join(work, "prog." + ("elf" if self.e7 else "exe"))
        # -I<dir> as one word, as %{msil_cflags} writes it.
        cflags = args.cflags.split() + ["-I" + os.path.join(TEST_ROOT, "Inputs")]
        cmd = [sys.executable, os.path.join(HERE, "msil-prog.py"), "--mode", "native"]
        cmd += ["--triple", args.target + "-msil", "--level", args.opt[1:]]
        cmd += ["--clang=" + args.clang, "--cflags=" + msil_prog.join_words(cflags)]
        if args.ar:
            cmd.append("--ar=" + args.ar)
        if self.e7:
            cmd += ["--ld-lld=" + self.ld_lld, "--linux-sysroot=" + args.linux_sysroot]
            cmd.append("--start=" + self.start_o)
        else:
            cmd.append("--native-ldflags=" + msil_prog.join_words(self.native_ldflags))
        cmd += ["--timeout", repr(args.timeout * 10)]
        cmd += ["--mem-limit-mb", str(args.mem_limit_mb)]
        cmd += ["--workdir", os.path.join(work, "prog"), "--out", exe]
        cmd += ["--src", *srcs, "--stock-src", *stock]
        cmd.append("--archives=" + value("%{msil_archives}", "", False))
        try:
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        except OSError as e:
            log.err("update-expected: cannot run msil-prog.py: %s\n" % e)
            return None, None
        if res.returncode != 0:
            # The commands msil-prog.py printed and the tools' messages.
            log.out(res.stdout)
            log.err(res.stderr)
            log.err(
                "update-expected: the native build (msil-prog.py) failed (exit "
                "status %s)\n" % res.returncode
            )
            return None, None
        return exe, self.run_mode()

    def process(self, src, explicit, work):
        """Build and run one test in the directory work, then compare or
        write its expected file: (its Log, 1 if it failed else 0)."""
        args, osname = self.args, self.osname
        log = Log()
        info = TestInfo(src)
        kind = info.kind
        if explicit and src.endswith((".c", ".ll")) and kind != "prog":
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
        stem = os.path.splitext(src)[0]
        base = stem + ".expected"
        override = stem + "." + osname + ".expected"
        if not args.check and osname == "windows" and os.path.exists(override):
            log.out("skip (has a %s override): %s\n" % (osname, src))
            return log, 0
        if kind == "prog":
            exe, run_mode = self.build_prog(info, work, log)
        else:
            exe, run_mode = self.build(src, work, log)
        if not exe:
            log.out("FAIL (native compile): %s\n" % src)
            return log, 1

        run = [sys.executable, os.path.join(HERE, "msil-run.py"), *run_mode]
        run += [
            "--timeout",
            repr(args.timeout),
            "--mem-limit-mb",
            str(args.mem_limit_mb),
        ]
        run += ["--", exe]
        proc = subprocess.run(run, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out = msil_run.text_of(proc.stdout)
        status = out.rstrip("\n").split("\n")[-1]
        # msil-run.py exits with 1 (after printing the transcript) only for an
        # out-of-range status, which is refused below.
        if proc.returncode not in (0, 1) or not msil_run.STATUS_RE.match(status):
            log.err(msil_run.text_of(proc.stderr))
            log.out("FAIL (native run): %s\n" % src)
            return log, 1
        m = EXIT_RE.match(status)
        if not m or int(m.group(1)) > 255:
            why = (
                "its exit status is outside 0..255; Linux would truncate it (plan D37)"
                if msil_run.OUTSIDE_RE.match(status)
                else "a native run that does not exit normally is no oracle"
            )
            log.out(
                "REFUSED: %s: the native run ended with %s: %s\n" % (src, status, why)
            )
            return log, 1

        if args.check:
            path = override if os.path.isfile(override) else base
            if not os.path.isfile(path):
                log.out("MISSING: %s (native transcript below)\n%s" % (path, out))
                return log, 1
            expected = read_expected(path)
            if expected == out:
                log.out("ok: %s\n" % src)
                return log, 0
            log.out("MISMATCH: %s\n" % src)
            diff = difflib.unified_diff(
                expected.splitlines(True),
                out.splitlines(True),
                path,
                "native (%s)" % osname,
            )
            log.out("".join(diff))
            return log, 1
        if osname == "windows" or not os.path.isfile(base):
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


def main():
    ap = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter, description=__doc__
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
        default="-O0",
        help="optimization level: O2 or --opt=-O2 (default: -O0)",
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
    args = ap.parse_args()

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
    if not args.opt.startswith("-"):
        args.opt = "-" + args.opt
    if args.opt[1:] not in msil_prog.LEVELS:
        ap.error("--opt %s: use one of %s" % (args.opt, ", ".join(msil_prog.LEVELS)))
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

#!/usr/bin/env python3
# ===-- il-snapshot.py - Snapshot the IL and diagnostics of MSIL inputs ---===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Compile every C and IR input of the MSIL tests and keep IL and diagnostics.

    python msil/utils/il-snapshot.py --build DIR --out DIR [--compare DIR]
        [--normalize forms] [--exclude PATH]... [--jobs N] [--timeout S]
        PATH...

Plan 5.12.4 (M15; M18 and M20 extend it): the proof that a change leaves the
text of 'clang -S' and llc byte-identical (5.10.13, D14 frozen). It runs from
the repository root (paths are taken relative to the current directory and
must lie in the repository) and compiles, with the tools of --build
(<build>/bin/clang and <build>/bin/llc):

  * every .c file under PATH... that 'git ls-tree -r HEAD' lists:
      clang --target=<triple> -std=gnu17 -ffreestanding -Werror=return-type
        -Imsil/test/Inputs -<L> -S <path> -o -
    (the flags of %{msil_cflags}, msil/test/lit.cfg.py), from the
    repository root under its repository-relative path;
  * every such .ll file: llc -mtriple=<triple> -<L> <path> -o -, the same
    way; a .ll file with split-file parts (a line that starts with ';--- ')
    is split by <build>/bin/split-file into a temporary directory instead,
    and each part whose name ends in .ll is compiled from its own directory
    under its relative name (llc ... <name>.ll -o -), so neither its
    source_filename nor a diagnostic names a temporary or --out directory;
    the other parts are not compiled;

minus every --exclude PATH (a file, or a directory and its subtree), at -O0
and -O2 for x86_64-pc-windows-msvc-msil and x86_64-unknown-linux-gnu-msil
and at -O2 for x86_64-w64-windows-gnu-msil and x86_64-unknown-linux-musl-msil.
The list of inputs comes from HEAD, their contents from the working tree.

For each input, triple and level it writes

    <out>/<triple>/<level>/<path>.il    what the compiler printed on stdout
                                        (left out when it printed nothing)
    <out>/<triple>/<level>/<path>.err   its stderr (CRLF folded to LF), then
                                        one line '[exit N]' ('[timeout]',
                                        '[memory limit]')

where <path> is the input's repository-relative path, or <file>/<part> for a
split-file part (for example llvm/test/CodeGen/MSIL/fp-expand.ll/libm32.ll).
In .err text the absolute path of the tool is replaced by its name ('llc',
'clang'), so the text depends neither on --out nor on --build, and a crash
report is cut at its first line ('PLEASE submit a bug report', 'Stack dump:',
...) and replaced by '[crash: stack dump omitted]': its addresses differ
between two runs of one build. A split-file failure is written as the .err
of the file itself in every configuration.

Every compile runs under the timeout (--timeout, default 600 s) and the
memory cap (4096 MiB) of msil-run.py, with the variables that add include or
library directories (INCLUDE, EXTERNAL_INCLUDE, LIB, CPATH, C_INCLUDE_PATH,
CPLUS_INCLUDE_PATH, LIBRARY_PATH) and CCC_OVERRIDE_OPTIONS removed from its
environment, so a snapshot taken in a Visual Studio developer prompt equals
one taken outside it, and with LLVM_DISABLE_SYMBOLIZATION=1 and
LLVM_DISABLE_CRASH_REPORT=1. --jobs N compiles N at once (default: the
number of CPUs).

--out is emptied first. It must not lie inside the repository, contain
--build, or overlap --compare, and a non-empty --out must hold the
il-snapshot.json that every snapshot writes (the options, HEAD and the
counts), so no other directory is ever emptied by accident.

--compare DIR compares the new snapshot with an earlier one, file by file
(il-snapshot.json aside): every file that differs, exists only in DIR
('missing') or only in --out ('extra') is listed, the first differences are
shown as unified diffs, and the exit status is 1. The usual use is a
baseline written with the build of HEAD before a milestone's build, and a
second snapshot with the new build compared against it.

--normalize forms (M18, plan 5.12.4; only with --compare) compares the .il
files of both snapshots after the encoding forms that plan Q26 and 5.10.11
changed are mapped back to Phase 1's, line by line (line endings and every
other byte are kept, and the .err files are compared exactly):

  * short branches become long ones: 'br.s', 'brfalse.s', 'brtrue.s',
    'beq.s', 'bge.s', 'bgt.s', 'ble.s', 'blt.s', 'bne.un.s', 'bge.un.s',
    'bgt.un.s', 'ble.un.s', 'blt.un.s' and 'leave.s' lose their '.s';
  * 'ldc.i4.m1' and 'ldc.i4.<n>' (n 0-8) become 'ldc.i4 -1' and
    'ldc.i4 <n>', and 'ldc.i4.s <n>' becomes 'ldc.i4 <n>';
  * 'nomangle cdecl' becomes 'cdecl'.

A mnemonic is rewritten only as a whole word (whitespace or the start or end
of the line on both sides). normalize_forms() is the same rewrite on any
text, and leaves FileCheck '{{...}}' regular expressions and '[[...]]'
variables as they are (IL text has neither); msil/utils/check-il-forms-diff.py
applies it to FileCheck lines and adds the same rewrite in escaped spelling
inside '{{...}}' (its docstring gives the rules). A difference that remains
is listed and diffed as above, with the diff taken between the normalized
texts.

Exit status: 0 on success (with --compare: no difference); 1 when --compare
finds a difference; 2 on a usage error or when the snapshot cannot be taken
(a missing tool, git failing, an input that cannot be split or read).
"""

import argparse
import concurrent.futures
import difflib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
IS_WINDOWS = os.name == "nt"
EXE = ".exe" if IS_WINDOWS else ""

# (triple, levels) in output order (plan 5.12.4).
CONFIGS = (
    ("x86_64-pc-windows-msvc-msil", ("O0", "O2")),
    ("x86_64-unknown-linux-gnu-msil", ("O0", "O2")),
    ("x86_64-w64-windows-gnu-msil", ("O2",)),
    ("x86_64-unknown-linux-musl-msil", ("O2",)),
)
# %{msil_cflags} of msil/test/lit.cfg.py, with -I relative to the repository.
CFLAGS = ("-std=gnu17", "-ffreestanding", "-Werror=return-type", "-Imsil/test/Inputs")
SPLIT_MARKER_RE = re.compile(r"^;--- \S", re.M)
# The start of LLVM's crash report (PrettyStackTrace, clang's crash
# diagnostics).
CRASH_RE = re.compile(
    r"^(?:PLEASE submit a bug report|Stack dump:|Exception Code: |"
    r"clang: error: clang frontend command failed due to signal)",
    re.M,
)
STAMP = "il-snapshot.json"
# Variables that add include or library directories, or options, to clang.
STRIPPED_ENV = (
    "INCLUDE",
    "EXTERNAL_INCLUDE",
    "LIB",
    "CPATH",
    "C_INCLUDE_PATH",
    "CPLUS_INCLUDE_PATH",
    "LIBRARY_PATH",
    "CCC_OVERRIDE_OPTIONS",
)
DIFF_FILES = 5  # unified diffs shown by --compare
DIFF_LINES = 40  # lines of each

# --normalize (M18): the kinds, and the rewrites of 'forms' (plan Q26 and
# 5.10.11; the module docstring lists them). A mnemonic is a whole word: no
# non-blank character right before or after it.
NORMALIZATIONS = ("forms",)
SHORT_BRANCH_RE = re.compile(
    r"(?<!\S)(br|brfalse|brtrue|beq|bge|bgt|ble|blt|bne\.un|bge\.un|bgt\.un|"
    r"ble\.un|blt\.un|leave)\.s(?!\S)"
)
LDC_I4_M1_RE = re.compile(r"(?<!\S)ldc\.i4\.[mM]1(?!\S)")
LDC_I4_N_RE = re.compile(r"(?<!\S)ldc\.i4\.([0-8])(?!\S)")
LDC_I4_S_RE = re.compile(r"(?<!\S)ldc\.i4\.s(?!\S)")
NOMANGLE_RE = re.compile(r"(?<!\S)nomangle[ \t]+(?=cdecl(?![\w.]))")
# FileCheck's '{{regex}}' and '[[variable]]' blocks, which normalize_forms()
# leaves alone.
FILECHECK_BLOCK_RE = re.compile(r"\{\{.*?\}\}|\[\[.*?\]\]")


def normalize_plain(text):
    text = SHORT_BRANCH_RE.sub(r"\1", text)
    text = LDC_I4_M1_RE.sub("ldc.i4 -1", text)
    text = LDC_I4_N_RE.sub(r"ldc.i4 \1", text)
    text = LDC_I4_S_RE.sub("ldc.i4", text)
    return NOMANGLE_RE.sub("", text)


def normalize_forms(text):
    """text with the Q26 forms mapped back to Phase 1's ('--normalize
    forms'): short branches long, 'ldc.i4.m1', 'ldc.i4.<n>' and 'ldc.i4.s'
    as 'ldc.i4 <n>', 'nomangle cdecl' as 'cdecl'. FileCheck '{{...}}' and
    '[[...]]' blocks are kept as they are; every other character, line
    endings included, is unchanged."""
    out, pos = [], 0
    for m in FILECHECK_BLOCK_RE.finditer(text):
        out.append(normalize_plain(text[pos : m.start()]))
        out.append(m.group(0))
        pos = m.end()
    out.append(normalize_plain(text[pos:]))
    return "".join(out)


def normalize_bytes(data, kinds):
    """The bytes of an .il file after the --normalize kinds."""
    if "forms" not in kinds:
        return data
    text = data.decode("utf-8", errors="surrogateescape")
    return normalize_forms(text).encode("utf-8", errors="surrogateescape")


def load_msil_run():
    # No __pycache__ next to the scripts in the source tree.
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location(
        "msil_run", os.path.join(HERE, "msil-run.py")
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


msil_run = load_msil_run()


class UsageError(Exception):
    pass


def say(text):
    sys.stdout.write("il-snapshot: %s\n" % text)
    sys.stdout.flush()


def norm(path):
    return os.path.normcase(os.path.realpath(os.path.abspath(path)))


def inside(path, directory):
    """Whether path is directory or lies below it."""
    path, directory = norm(path), norm(directory)
    try:
        return os.path.commonpath([path, directory]) == directory
    except ValueError:  # another drive
        return False


def repo_relative(path, what):
    """A path argument as a repository-relative path with '/'."""
    full = os.path.abspath(path)
    if not inside(full, REPO):
        raise UsageError("%s %s is not inside the repository %s" % (what, path, REPO))
    rel = os.path.relpath(full, REPO).replace(os.sep, "/")
    return "" if rel == "." else rel


def git(args):
    res = subprocess.run(
        ["git", "-C", REPO] + args,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if res.returncode != 0:
        raise UsageError(
            "git %s failed (status %d): %s"
            % (" ".join(args), res.returncode, res.stderr.decode("utf-8", "replace"))
        )
    return res.stdout


def tracked_inputs(paths, excludes):
    """The .c and .ll files under paths that HEAD tracks, minus excludes:
    (inputs, {exclude: number of inputs it removed})."""
    found, used = [], {e: 0 for e in excludes}
    seen = set()
    for p in paths:
        names = git(["ls-tree", "-r", "-z", "--name-only", "HEAD", "--", p or "."])
        names = [n.decode("utf-8") for n in names.split(b"\0") if n]
        if not names:
            raise UsageError("HEAD tracks no file under %s" % (p or "."))
        for n in names:
            if not n.endswith((".c", ".ll")) or n in seen:
                continue
            seen.add(n)
            hit = [e for e in excludes if n == e or n.startswith(e + "/")]
            for e in hit:
                used[e] += 1
            if not hit:
                found.append(n)
    return sorted(found), used


def child_env():
    env = dict(os.environ)
    for key in list(env):
        if (key.upper() if IS_WINDOWS else key) in STRIPPED_ENV:
            del env[key]
    # A crash prints a stack dump (cut below) and writes no reproducer.
    env["LLVM_DISABLE_SYMBOLIZATION"] = "1"
    env["LLVM_DISABLE_CRASH_REPORT"] = "1"
    return env


class Snapshot:
    def __init__(self, args):
        self.args = args
        self.clang = os.path.join(args.build, "bin", "clang" + EXE)
        self.llc = os.path.join(args.build, "bin", "llc" + EXE)
        self.split_file = os.path.join(args.build, "bin", "split-file" + EXE)
        self.env = child_env()

    def tool_paths(self):
        return (self.clang, self.llc, self.split_file)

    def clean_stderr(self, data, tool):
        """stderr as text, CRLF folded, the tool's absolute path replaced by
        its name."""
        text = data.decode("utf-8", errors="backslashreplace").replace("\r\n", "\n")
        name = os.path.splitext(os.path.basename(tool))[0]
        for spelling in {tool, tool.replace("\\", "/"), tool.replace("/", "\\")}:
            text = text.replace(spelling, name)
        # A crash's stack dump holds addresses and temporary file names that
        # differ between two runs of the same build: keep what comes before.
        m = CRASH_RE.search(text)
        if m:
            text = text[: m.start()] + "[crash: stack dump omitted]\n"
        return text

    def run(self, cmd, cwd):
        """(stdout bytes, .err text) of one compile."""
        try:
            res = msil_run.run_limited(
                cmd, cwd, self.env, self.args.timeout, msil_run.DEFAULT_MEM_LIMIT_MB
            )
        except OSError as e:
            return b"", "il-snapshot: cannot run %s: %s\n[not run]\n" % (
                os.path.basename(cmd[0]),
                e,
            )
        err = self.clean_stderr(res.stderr, cmd[0])
        if err and not err.endswith("\n"):
            err += "\n"
        if res.timed_out:
            status = "[timeout]"
        elif res.memory_limit:
            status = "[memory limit]"
        else:
            status = "[exit %s]" % msil_run.describe_exit(res.returncode)
        return res.stdout, err + status + "\n"

    def compile(self, job):
        """Compile one (kind, source, cwd, name, triple, level) job and write
        its .il and .err; returns the job's output path stem."""
        kind, cwd, name, rel_out, triple, level = job
        if kind == "c":
            cmd = [self.clang, "--target=" + triple, *CFLAGS, "-" + level, "-S"]
        else:
            cmd = [self.llc, "-mtriple=" + triple, "-" + level]
        cmd += [name, "-o", "-"]
        out, err = self.run(cmd, cwd)
        stem = os.path.join(self.args.out, triple, level, *rel_out.split("/"))
        os.makedirs(os.path.dirname(stem), exist_ok=True)
        if out:
            with open(stem + ".il", "wb") as f:
                f.write(out)
        with open(stem + ".err", "w", encoding="utf-8", newline="\n") as f:
            f.write(err)
        return stem

    def write_err_everywhere(self, rel_out, text):
        for triple, levels in CONFIGS:
            for level in levels:
                stem = os.path.join(self.args.out, triple, level, *rel_out.split("/"))
                os.makedirs(os.path.dirname(stem), exist_ok=True)
                with open(stem + ".err", "w", encoding="utf-8", newline="\n") as f:
                    f.write(text)

    def split(self, path, tmp):
        """The .ll parts of the split-file input path, split into a fresh
        directory under tmp: [(part directory, part file name, part path)],
        or None after writing the failure as the input's .err."""
        dest = tempfile.mkdtemp(prefix="split-", dir=tmp)
        try:
            res = msil_run.run_limited(
                [self.split_file, path, dest],
                REPO,
                self.env,
                self.args.timeout,
                msil_run.DEFAULT_MEM_LIMIT_MB,
            )
            rc = res.returncode if not res.timed_out else "timeout"
            err = self.clean_stderr(res.stderr, self.split_file)
        except OSError as e:
            rc, err = "not run", "il-snapshot: cannot run split-file: %s\n" % e
        if rc != 0:
            if err and not err.endswith("\n"):
                err += "\n"
            self.write_err_everywhere(
                path, "il-snapshot: split-file failed\n%s[exit %s]\n" % (err, rc)
            )
            return None
        parts = []
        for d, dirs, files in os.walk(dest):
            dirs.sort()
            for f in sorted(files):
                if f.endswith(".ll"):
                    rel = os.path.relpath(os.path.join(d, f), dest).replace(os.sep, "/")
                    parts.append((d, f, rel))
        return parts


def find_inputs(args):
    """Check the tools and list the inputs, before --out is emptied."""
    s = Snapshot(args)
    missing = [p for p in s.tool_paths() if not os.path.isfile(p)]
    if missing:
        raise UsageError(
            "--build %s lacks %s (build clang, llc and split-file)"
            % (args.build, ", ".join(missing))
        )
    inputs, used = tracked_inputs(args.paths, args.exclude)
    for e, n in used.items():
        if n == 0:
            say("note: --exclude %s matches no input" % e)
    if not inputs:
        raise UsageError("no .c or .ll input under %s" % " ".join(args.paths))
    return inputs


def snapshot(args, inputs):
    """Take the snapshot of inputs into args.out; returns the counts for the
    stamp."""
    s = Snapshot(args)
    started = time.time()
    tmp = tempfile.mkdtemp(prefix="il-snapshot-")
    try:
        jobs, n_files, n_parts = [], 0, 0
        for path in inputs:
            if not os.path.isfile(os.path.join(REPO, path)):
                s.write_err_everywhere(
                    path,
                    "il-snapshot: %s is tracked by HEAD but missing in the "
                    "working tree\n[not run]\n" % path,
                )
                continue
            n_files += 1
            units = []
            if path.endswith(".c"):
                units.append(("c", REPO, path, path))
            else:
                with open(
                    os.path.join(REPO, path), encoding="utf-8", errors="replace"
                ) as f:
                    text = f.read()
                if SPLIT_MARKER_RE.search(text):
                    parts = s.split(path, tmp)
                    for d, name, rel in parts or []:
                        units.append(("ll", d, name, path + "/" + rel))
                        n_parts += 1
                else:
                    units.append(("ll", REPO, path, path))
            for kind, cwd, name, rel_out in units:
                for triple, levels in CONFIGS:
                    for level in levels:
                        jobs.append((kind, cwd, name, rel_out, triple, level))
        say(
            "%d inputs (%d files, %d split-file parts) in %d configurations: "
            "%d compiles with %d jobs"
            % (
                len({j[3] for j in jobs}),
                n_files,
                n_parts,
                sum(len(l) for _, l in CONFIGS),
                len(jobs),
                args.jobs,
            )
        )
        done = 0
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            for _ in pool.map(s.compile, jobs):
                done += 1
                if done % 500 == 0:
                    say("%d of %d compiles done" % (done, len(jobs)))
    finally:
        msil_run.remove_tree(tmp)
    say("wrote %s in %.0f s" % (args.out.replace("\\", "/"), time.time() - started))
    return {
        "inputs": len({j[3] for j in jobs}),
        "files": n_files,
        "split-parts": n_parts,
        "compiles": len(jobs),
    }


def tree_files(root):
    files = {}
    for d, dirs, names in os.walk(root):
        dirs.sort()
        for n in sorted(names):
            p = os.path.join(d, n)
            rel = os.path.relpath(p, root).replace(os.sep, "/")
            if rel != STAMP:
                files[rel] = p
    return files


def read_stamp(directory):
    try:
        with open(os.path.join(directory, STAMP), encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def compare(new, old, normalize=()):
    """Compare snapshot new with snapshot old, the .il files after the
    --normalize kinds in normalize; returns the number of differences."""
    fa, fb = tree_files(new), tree_files(old)
    differs, missing, extra = [], [], []
    normalized = {}  # name -> (old text, new text) of a normalized .il file
    equal_after = 0  # .il files equal only after normalizing
    for name in sorted(set(fa) | set(fb)):
        if name not in fa:
            missing.append(name)
        elif name not in fb:
            extra.append(name)
        else:
            with open(fa[name], "rb") as f:
                a = f.read()
            with open(fb[name], "rb") as f:
                b = f.read()
            if a != b and normalize and name.endswith(".il"):
                a, b = normalize_bytes(a, normalize), normalize_bytes(b, normalize)
                if a == b:
                    equal_after += 1
                else:
                    normalized[name] = (b, a)
            if a != b:
                differs.append(name)
    for name in differs:
        print("differs: %s" % name)
    for name in missing:
        print("missing: %s (only in %s)" % (name, old.replace("\\", "/")))
    for name in extra:
        print("extra: %s (only in %s)" % (name, new.replace("\\", "/")))
    for name in differs[:DIFF_FILES]:
        if name in normalized:
            a, b = (
                t.decode("utf-8", errors="backslashreplace").splitlines(True)
                for t in normalized[name]
            )
        else:
            with open(fb[name], encoding="utf-8", errors="backslashreplace") as f:
                a = f.read().splitlines(True)
            with open(fa[name], encoding="utf-8", errors="backslashreplace") as f:
                b = f.read().splitlines(True)
        diff = list(difflib.unified_diff(a, b, "old/" + name, "new/" + name))
        for line in diff[:DIFF_LINES]:
            sys.stdout.write(line if line.endswith("\n") else line + "\n")
        if len(diff) > DIFF_LINES:
            print("... (%d more diff lines)" % (len(diff) - DIFF_LINES))
    total = len(differs) + len(missing) + len(extra)
    if normalize:
        say(
            "--normalize %s: the .il files of both snapshots were compared "
            "normalized; %d of them differ only in those forms"
            % (",".join(normalize), equal_after)
        )
    say(
        "compared %d files with %s: %d differ, %d missing, %d extra"
        % (
            len(set(fa) | set(fb)),
            old.replace("\\", "/"),
            len(differs),
            len(missing),
            len(extra),
        )
    )
    old_stamp, new_stamp = read_stamp(old), read_stamp(new)
    if old_stamp is None:
        say(
            "note: %s has no %s (not written by il-snapshot.py?)"
            % (old.replace("\\", "/"), STAMP)
        )
    elif new_stamp:
        for key in ("paths", "exclude", "configs", "cflags"):
            if old_stamp.get(key) != new_stamp.get(key):
                say(
                    "note: the snapshots differ in %s: %s here, %s in %s"
                    % (
                        key,
                        new_stamp.get(key),
                        old_stamp.get(key),
                        old.replace("\\", "/"),
                    )
                )
    return total


def empty_out(out):
    if os.path.lexists(out) and not os.path.isdir(out):
        raise UsageError("--out %s is not a directory" % out)
    if os.path.isdir(out) and os.listdir(out):
        if not os.path.isfile(os.path.join(out, STAMP)):
            raise UsageError(
                "--out %s is not empty and holds no %s; refusing to empty a "
                "directory that il-snapshot.py did not write" % (out, STAMP)
            )
        msil_run.remove_tree(out)
        if os.path.exists(out):
            raise UsageError("cannot empty --out %s" % out)
    os.makedirs(out, exist_ok=True)


def parse_args(argv):
    p = argparse.ArgumentParser(
        prog="il-snapshot.py",
        allow_abbrev=False,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=__doc__,
    )
    p.add_argument(
        "--build", required=True, help="build directory (bin/clang, bin/llc)"
    )
    p.add_argument("--out", required=True, help="snapshot directory (emptied first)")
    p.add_argument("--compare", metavar="DIR", help="compare with this snapshot")
    p.add_argument(
        "--normalize",
        action="append",
        default=[],
        choices=NORMALIZATIONS,
        help="with --compare: compare the .il files after this normalization "
        "('forms': short branches, short ldc.i4 forms, nomangle; repeatable)",
    )
    p.add_argument(
        "--exclude",
        action="append",
        default=[],
        metavar="PATH",
        help="leave out this file or directory (repeatable)",
    )
    p.add_argument(
        "--jobs", type=int, default=os.cpu_count() or 1, help="compiles at once"
    )
    p.add_argument(
        "--timeout", type=float, default=600.0, help="seconds per compile (600)"
    )
    p.add_argument("paths", nargs="+", metavar="PATH", help="directories or files")
    args = p.parse_args(argv)
    if args.jobs < 1:
        p.error("--jobs must be at least 1")
    if args.timeout <= 0:
        p.error("--timeout must be positive")
    return args


def check_args(args):
    args.build = os.path.abspath(args.build)
    args.out = os.path.abspath(args.out)
    args.normalize = [k for k in NORMALIZATIONS if k in args.normalize]
    if args.normalize and not args.compare:
        raise UsageError("--normalize applies to --compare, which is missing")
    if args.compare:
        args.compare = os.path.abspath(args.compare)
        if not os.path.isdir(args.compare):
            raise UsageError("--compare %s is not a directory" % args.compare)
        if inside(args.out, args.compare) or inside(args.compare, args.out):
            raise UsageError("--out and --compare overlap")
    if inside(args.out, REPO) or inside(REPO, args.out):
        raise UsageError("--out %s overlaps the repository %s" % (args.out, REPO))
    if inside(args.build, args.out):
        raise UsageError("--out %s holds the build directory" % args.out)
    if os.path.dirname(args.out) == args.out:
        raise UsageError("--out %s is a file-system root" % args.out)
    args.paths = [repo_relative(p, "path") for p in args.paths]
    args.exclude = [repo_relative(e, "--exclude").rstrip("/") for e in args.exclude]
    if "" in args.exclude:
        raise UsageError("--exclude names the whole repository")


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        check_args(args)
        inputs = find_inputs(args)
        empty_out(args.out)
        counts = snapshot(args, inputs)
        head = git(["rev-parse", "HEAD"]).decode("ascii").strip()
    except UsageError as e:
        sys.stderr.write("il-snapshot: error: %s\n" % e)
        return 2
    except OSError as e:
        sys.stderr.write("il-snapshot: error: %s\n" % e)
        return 2
    stamp = {
        "tool": "msil/utils/il-snapshot.py",
        "head": head,
        "paths": args.paths,
        "exclude": args.exclude,
        "configs": [[t, list(l)] for t, l in CONFIGS],
        "cflags": list(CFLAGS),
        **counts,
    }
    if args.normalize:
        stamp["normalize"] = args.normalize
    with open(os.path.join(args.out, STAMP), "w", encoding="utf-8", newline="\n") as f:
        json.dump(stamp, f, indent=2, sort_keys=True)
        f.write("\n")
    if args.compare:
        return 1 if compare(args.out, args.compare, args.normalize) else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())

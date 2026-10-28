#!/usr/bin/env python3
# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##
"""Cross-host determinism of CIL objects and archives (plan 5.7.12, 5.12.12).

  python msil/utils/ci/det-manifest.py build --clang PATH --ar PATH --out FILE
      [--work DIR] [--levels O0,O2] [--tests DIR] [--jobs N]
  python msil/utils/ci/det-manifest.py compare A B
  python msil/utils/ci/det-manifest.py --self-test

build compiles every C file of msil/test/M1 (--tests; Inputs/ and Output/
left out) with 'clang -c' for x86_64-pc-windows-msvc-msil and
x86_64-unknown-linux-gnu-msil at each level of --levels, with the flags of
lit's %{msil_cflags} (-std=gnu17 -ffreestanding -Werror=return-type
-I msil/test/Inputs) and -fdebug-compilation-dir=., from the repository root
and with relative paths (msil/test/M1/<dir>/<file>.c), so that nothing in an
object depends on where the checkout or the work directory is. The
x86_64-pc-windows-msvc-msil compiles (only those) also pass
-fms-compatibility-version=19.33, clang's default when it finds no MSVC:
clang writes the MSVC version into a windows-msvc triple, and the object
records that triple, so without the pin the same clang writes
x86_64-pc-windows-msvc19.33.0-msil on a host without Visual Studio (the CI's
Linux job) and, for example, x86_64-pc-windows-msvc19.51.36260-msil inside a
Visual Studio developer environment (the CI's Windows job, msil-env.ps1).
The x86_64-unknown-linux-gnu-msil compiles take no such flag. Each object is
written as <work>/<triple>/<level>/<dir>__<file>.o (one flat directory per
triple and level, so that the archive members' names are unique), and then
'llvm-ar rcs <work>/<triple>/<level>/libm1.a' archives that level's objects
in the order of their names. The compiles run on a thread pool (--jobs,
default the number of CPUs). --clang and --ar given as a path (with a
directory separator, for example build/bin/clang) are made absolute against
the current directory first, because the compiles run from the repository
root and the archiver from the work directory; a bare name is looked up on
PATH. Before anything in the work directory is touched, each tool must start
('<tool> --version' exits 0). The work directory (default a temporary
directory) must be outside the repository; build empties it first, but only
if build made it (it holds the marker file .det-manifest-work), it is empty,
or it holds nothing but <triple>/ directories (those of an earlier run), and
refuses any other directory. build then writes the marker into it; a
temporary work directory is removed afterwards. The manifest --out holds one
line per object and archive, sorted by path:

  <sha256>  <triple>/<level>/<name>

build exits 1 if a compile or the archiver fails (the manifest is not
written then) and 2 on a usage error (a work directory inside the
repository or holding files build did not write, a tool that does not
start, an unknown level, no C file).

compare reads two manifests, lists every entry that only one of them has
('only in <file>: <path>') and every entry whose hashes differ ('differs:
<path>'), and exits 1 when an entry differs or when the two have no entry in
common, else 0; it prints 'det-manifest: <n> common entries, <d> differ, <a>
only in <A>, <b> only in <B>'. A malformed line is a usage error (2).

The CI's determinism job compares the manifests that the Linux and the
Windows job build (msil-ci.yml; plan section 12, 'Cross-host determinism'),
and run-m1-gate.ps1's determinism row compares two builds in different work
directories of the development host. No PE is compared across hosts: each
host checks the determinism of its own links (the det legs).

--self-test (offline; a temporary directory outside the repository, removed
afterwards) builds a small tree of C files twice with stand-ins for clang
and llvm-ar that write the hash of their command line and input bytes, and
checks: two work directories give equal manifests; the stand-in sees
relative source paths, -fdebug-compilation-dir=. and both triples at every
level, with -fms-compatibility-version=19.33 on every windows-msvc compile
and no -fms-compatibility-version on a linux-gnu one; an edited source
differs in exactly its two objects per level and the archives; compare
reports one-sided entries without failing, fails on no common entry and on
a differing entry, and refuses a malformed line; a build
run from the root with relative, '/'-separated tool paths (the form
msil-ci.yml passed before M23's fix loop) gives the stand-ins absolute paths
(they refuse a relative one) and passes the archive step; build refuses a
work directory inside the repository, and one that holds a foreign file
(which survives); a directory of <triple>/ directories is emptied and
marked; a tool that does not start is a usage error before anything is
deleted; a failed compile writes no manifest. Python 3 standard library
only.
"""

import argparse
import concurrent.futures
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
TRIPLES = ("x86_64-pc-windows-msvc-msil", "x86_64-unknown-linux-gnu-msil")
LEVELS = ("O0", "O1", "O2", "O3", "Os", "Oz")
CFLAGS = ("-std=gnu17", "-ffreestanding", "-Werror=return-type")
# clang's default MSVC version when it finds no MSVC; pinned on the
# windows-msvc compiles so that the triple an object records does not depend
# on the host's MSVC (see the module docstring).
MSVC_PIN = "-fms-compatibility-version=19.33"
ARCHIVE = "libm1.a"
# Written into every work directory build makes; build empties a directory
# that holds it.
WORK_MARKER = ".det-manifest-work"
LINE_RE = re.compile(r"^([0-9a-f]{64})  (\S+)$")


class UsageError(Exception):
    pass


def triple_flags(triple):
    """The flags that only the compiles for triple take: the MSVC version
    pin for windows-msvc, nothing for linux-gnu."""
    return [MSVC_PIN] if "-windows-msvc" in triple else []


def tool(path):
    """The command of a tool: a .py file runs under this Python (the
    self-test's stand-ins)."""
    return [sys.executable, path] if path.endswith(".py") else [path]


def has_directory(path):
    """True if path names a directory part (a separator of this OS or '/')."""
    return any(sep and sep in path for sep in (os.sep, os.altsep, "/"))


def resolve_tool(path, option, root):
    """The command of the tool that --clang or --ar names, once it starts.

    A path (it has a directory part) and a .py stand-in are made absolute
    against the current directory: the compiles run from the root and the
    archiver from the work directory, and Windows cannot start a relative
    path written with '/'. A bare name stays a PATH lookup."""
    if has_directory(path) or path.endswith(".py"):
        path = os.path.abspath(path)
    cmd = tool(path)
    rc, out = run(cmd + ["--version"], root)
    if rc != 0:
        last = out.strip().splitlines()[-1] if out.strip() else "no output"
        raise UsageError("%s %s does not start ('--version' exited %d: %s)" % (option, path, rc, last))
    return cmd


def check_work(work):
    """Refuse an existing work directory that build did not make (no
    marker) and that holds anything but <triple>/ directories."""
    if not os.path.lexists(work):
        return
    if os.path.islink(work) or not os.path.isdir(work):
        raise UsageError("--work %s exists and is not a directory" % work)
    entries = sorted(os.listdir(work))
    if WORK_MARKER in entries:
        return

    def triple_dir(e):
        p = os.path.join(work, e)
        return e in TRIPLES and os.path.isdir(p) and not os.path.islink(p)

    foreign = [e for e in entries if not triple_dir(e)]
    if foreign:
        raise UsageError(
            "--work %s holds %s, which det-manifest did not write; build empties only a directory "
            "it made (marked by %s), an empty one or one that holds only <triple>/ directories"
            % (work, ", ".join(foreign[:5]) + (", ..." if len(foreign) > 5 else ""), WORK_MARKER)
        )


def inside(path, root):
    """True if path is root or below it (case-insensitive on Windows)."""
    p = os.path.normcase(os.path.realpath(path))
    r = os.path.normcase(os.path.realpath(root))
    return p == r or p.startswith(r.rstrip(os.sep) + os.sep)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def c_files(tests, root):
    """The C files under tests, as '/'-separated paths relative to root."""
    found = []
    for d, dirs, files in os.walk(tests):
        dirs[:] = sorted(x for x in dirs if x not in ("Inputs", "Output"))
        for f in sorted(files):
            if f.endswith(".c"):
                found.append(os.path.relpath(os.path.join(d, f), root).replace("\\", "/"))
    return sorted(found)


def object_name(rel, tests_rel):
    """<dir>__<file>.o for msil/test/M1/<dir>/<file>.c."""
    sub = rel[len(tests_rel) + 1 :] if rel.startswith(tests_rel + "/") else rel
    return sub[: -len(".c")].replace("/", "__") + ".o"


def run(cmd, cwd):
    """(returncode, output) of cmd."""
    try:
        res = subprocess.run(
            cmd,
            cwd=cwd,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
    except OSError as e:
        return 127, "cannot run %s: %s\n" % (cmd[0], e)
    return res.returncode, res.stdout.decode("utf-8", "replace")


def build(args):
    root = os.path.abspath(args.root or REPO)
    tests = os.path.abspath(args.tests or os.path.join(root, "msil", "test", "M1"))
    inputs = os.path.relpath(os.path.join(root, "msil", "test", "Inputs"), root)
    levels = [x.strip() for x in args.levels.split(",") if x.strip()]
    for level in levels:
        if level not in LEVELS:
            raise UsageError("--levels: unknown level %r (use %s)" % (level, ",".join(LEVELS)))
    if not levels:
        raise UsageError("--levels is empty")
    if not inside(tests, root):
        raise UsageError("--tests %s is not inside %s" % (tests, root))
    tests_rel = os.path.relpath(tests, root).replace("\\", "/")
    sources = c_files(tests, root)
    if not sources:
        raise UsageError("no C file under %s" % tests)
    temp = None
    work = os.path.abspath(args.work) if args.work else None
    if work and (inside(work, REPO) or inside(work, root)):
        raise UsageError("--work %s is inside the repository; use a directory outside it" % work)
    # Both tools must start before anything in the work directory is touched.
    clang = resolve_tool(args.clang, "--clang", root)
    ar = resolve_tool(args.ar, "--ar", root)
    if work:
        check_work(work)
        if os.path.isdir(work):
            shutil.rmtree(work)
        os.makedirs(work)
    else:
        work = temp = tempfile.mkdtemp(prefix="msil-det-manifest-")
    with open(os.path.join(work, WORK_MARKER), "w", encoding="utf-8", newline="\n") as f:
        f.write("a work directory of msil/utils/ci/det-manifest.py build, which empties it\n")
    try:
        jobs = []
        for triple in TRIPLES:
            for level in levels:
                os.makedirs(os.path.join(work, triple, level))
                for rel in sources:
                    out = os.path.join(work, triple, level, object_name(rel, tests_rel))
                    cmd = clang + ["--target=" + triple, *CFLAGS, *triple_flags(triple)]
                    cmd += ["-I" + inputs.replace("\\", "/")]
                    cmd += ["-fdebug-compilation-dir=.", "-" + level, "-c", rel, "-o", out]
                    jobs.append((rel, triple, level, cmd))
        failed = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = [pool.submit(run, cmd, root) for _, _, _, cmd in jobs]
            for (rel, triple, level, cmd), fut in zip(jobs, futures):
                rc, out = fut.result()
                if rc != 0:
                    failed.append("%s %s %s" % (triple, level, rel))
                    sys.stdout.write(
                        "det-manifest: FAILED (exit %d): %s\n%s" % (rc, " ".join(cmd), out)
                    )
        if failed:
            print("det-manifest: %d of %d compiles failed; no manifest written" % (len(failed), len(jobs)))
            return 1
        lines = []
        for triple in TRIPLES:
            for level in levels:
                d = os.path.join(work, triple, level)
                objects = sorted(f for f in os.listdir(d) if f.endswith(".o"))
                rc, out = run(ar + ["rcs", ARCHIVE] + objects, d)
                if rc != 0:
                    sys.stdout.write("det-manifest: FAILED (exit %d): llvm-ar rcs in %s\n%s" % (rc, d, out))
                    print("det-manifest: no manifest written")
                    return 1
                for f in objects + [ARCHIVE]:
                    lines.append("%s  %s/%s/%s" % (sha256_file(os.path.join(d, f)), triple, level, f))
        lines.sort(key=lambda x: x.split("  ", 1)[1])
        out_path = os.path.abspath(args.out)
        os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
        tmp = out_path + ".tmp"
        with open(tmp, "w", encoding="utf-8", newline="\n") as f:
            f.write("".join(x + "\n" for x in lines))
        os.replace(tmp, out_path)
        print(
            "det-manifest: wrote %s: %d objects and %d archives (%d C files, %s, %s)"
            % (
                out_path,
                len(lines) - len(TRIPLES) * len(levels),
                len(TRIPLES) * len(levels),
                len(sources),
                " and ".join(TRIPLES),
                ",".join(levels),
            )
        )
        return 0
    finally:
        if temp:
            shutil.rmtree(temp, ignore_errors=True)


def read_manifest(path):
    entries = {}
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
    except OSError as e:
        raise UsageError("cannot read %s: %s" % (path, e))
    for n, line in enumerate(text.splitlines(), 1):
        if not line.strip():
            continue
        m = LINE_RE.match(line)
        if not m:
            raise UsageError("%s:%d: not '<sha256>  <path>': %r" % (path, n, line))
        if m.group(2) in entries:
            raise UsageError("%s:%d: %s is listed twice" % (path, n, m.group(2)))
        entries[m.group(2)] = m.group(1)
    return entries


def compare(a_path, b_path):
    a = read_manifest(a_path)
    b = read_manifest(b_path)
    only_a = sorted(set(a) - set(b))
    only_b = sorted(set(b) - set(a))
    common = sorted(set(a) & set(b))
    differ = [p for p in common if a[p] != b[p]]
    for p in only_a:
        print("only in %s: %s" % (a_path, p))
    for p in only_b:
        print("only in %s: %s" % (b_path, p))
    for p in differ:
        print("differs: %s (%s and %s)" % (p, a[p][:12], b[p][:12]))
    print(
        "det-manifest: %d common entries, %d differ, %d only in %s, %d only in %s"
        % (len(common), len(differ), len(only_a), a_path, len(only_b), b_path)
    )
    if not common:
        print("det-manifest: error: the manifests have no entry in common")
        return 1
    return 1 if differ else 0


# ---------------------------------------------------------------- self-test
STAND_IN_CC = r'''import hashlib, os, sys
args = sys.argv[1:]
if not os.path.isabs(sys.argv[0]):
    sys.exit("stand-in clang: relative tool path " + sys.argv[0])
if args == ["--version"]:
    sys.exit(print("stand-in clang"))
log = os.environ.get("DET_SELFTEST_LOG")
if log:
    with open(log, "a", encoding="utf-8") as f:
        f.write(" ".join(args) + "\n")
src = args[args.index("-c") + 1]
out = args[args.index("-o") + 1]
if os.path.isabs(src):
    sys.exit("stand-in clang: absolute source path " + src)
with open(src, "rb") as f:
    data = f.read()
if b"#error" in data:
    sys.exit("stand-in clang: #error in " + src)
words = [a for a in args if a not in ("-o", out)]
with open(out, "wb") as f:
    f.write(hashlib.sha256(" ".join(words).encode() + b"\0" + data).digest())
'''

STAND_IN_AR = r'''import hashlib, os, sys
args = sys.argv[1:]
if not os.path.isabs(sys.argv[0]):
    sys.exit("stand-in llvm-ar: relative tool path " + sys.argv[0])
if args == ["--version"]:
    sys.exit(print("stand-in llvm-ar"))
assert args[0] == "rcs", args
h = hashlib.sha256()
for m in args[2:]:
    h.update(m.encode() + b"\0" + open(m, "rb").read())
open(args[1], "wb").write(h.digest())
'''


def self_test():
    failures = []

    def check(cond, what):
        print("%s: %s" % ("ok" if cond else "FAIL", what))
        if not cond:
            failures.append(what)

    tmp = tempfile.mkdtemp(prefix="msil-det-manifest-selftest-")
    if inside(tmp, REPO):
        print("det-manifest: the temporary directory %s is inside the repository" % tmp)
        return 1
    try:
        root = os.path.join(tmp, "repo")
        m1 = os.path.join(root, "msil", "test", "M1")
        for rel, text in (
            ("00-a/one.c", "int main(void) { return 1; }\n"),
            ("00-a/two.c", "int main(void) { return 2; }\n"),
            ("01-b/one.c", "int main(void) { return 3; }\n"),
            ("01-b/Inputs/skip.c", "#error not a test\n"),
        ):
            p = os.path.join(m1, *rel.split("/"))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
        os.makedirs(os.path.join(root, "msil", "test", "Inputs"))
        cc = os.path.join(tmp, "cc.py")
        ar = os.path.join(tmp, "ar.py")
        with open(cc, "w", encoding="utf-8") as f:
            f.write(STAND_IN_CC)
        with open(ar, "w", encoding="utf-8") as f:
            f.write(STAND_IN_AR)
        log = os.path.join(tmp, "cc.log")
        os.environ["DET_SELFTEST_LOG"] = log

        def do_build(work, out, levels="O0,O2", clang=cc, archiver=ar):
            ns = argparse.Namespace(
                clang=clang, ar=archiver, out=out, work=work, levels=levels, tests=m1, root=root, jobs=4
            )
            try:
                return build(ns)
            except UsageError as e:
                print("usage error: %s" % e)
                return 2

        a = os.path.join(tmp, "a.txt")
        b = os.path.join(tmp, "b.txt")
        check(do_build(os.path.join(tmp, "work-a"), a) == 0, "build in work-a")
        check(do_build(os.path.join(tmp, "work b"), b) == 0, "build in 'work b'")
        ea, eb = read_manifest(a), read_manifest(b)
        check(len(ea) == 2 * 2 * 4, "16 entries (3 objects and an archive, 2 triples, 2 levels)")
        check(ea == eb, "two work directories give equal manifests")
        check(compare(a, b) == 0, "compare of equal manifests exits 0")
        names = sorted(ea)
        check(
            "x86_64-pc-windows-msvc-msil/O0/00-a__one.o" in names
            and "x86_64-unknown-linux-gnu-msil/O2/libm1.a" in names
            and not any("skip" in n for n in names),
            "entry names <triple>/<level>/<dir>__<file>.o and libm1.a; Inputs/ left out",
        )
        with open(log, encoding="utf-8") as f:
            cmds = f.read().splitlines()
        # The MSVC version pin goes on every windows-msvc compile, and no
        # -fms-compatibility-version on a linux-gnu one.
        win = [c.split() for c in cmds if "--target=x86_64-pc-windows-msvc-msil" in c.split()]
        lin = [c.split() for c in cmds if "--target=x86_64-unknown-linux-gnu-msil" in c.split()]
        check(
            all("-fdebug-compilation-dir=." in c and " -c msil/test/M1/" in c for c in cmds)
            and any("--target=x86_64-pc-windows-msvc-msil" in c and " -O2 " in c for c in cmds)
            and any("--target=x86_64-unknown-linux-gnu-msil" in c and " -O0 " in c for c in cmds)
            and all("-Imsil/test/Inputs" in c for c in cmds)
            and win and lin and len(win) + len(lin) == len(cmds)
            and all(MSVC_PIN in w for w in win)
            and not any(x.startswith("-fms-compatibility-version") for w in lin for x in w),
            "relative sources and -I, -fdebug-compilation-dir=., both triples, both levels, "
            "%s on every windows-msvc compile and on no linux-gnu one" % MSVC_PIN,
        )
        with open(os.path.join(m1, "00-a", "two.c"), "a", encoding="utf-8") as f:
            f.write("/* edited */\n")
        c = os.path.join(tmp, "c.txt")
        check(do_build(os.path.join(tmp, "work-a"), c) == 0, "build after an edit")
        ec = read_manifest(c)
        diff = sorted(p for p in ea if ea[p] != ec.get(p))
        check(
            len(diff) == 8 and all(p.endswith(("00-a__two.o", ARCHIVE)) for p in diff),
            "the edited file differs in its 4 objects and the 4 archives",
        )
        check(compare(a, c) == 1, "compare fails on a differing entry")
        # msil-ci.yml passed build/bin/clang and build/bin/llvm-ar from the
        # checkout: relative, '/'-separated paths, which the archiver (run
        # from the work directory) and Windows' CreateProcess cannot start.
        # The stand-ins refuse a relative argv[0], so this covers both hosts.
        bindir = os.path.join(root, "build", "bin")
        os.makedirs(bindir)
        shutil.copy(cc, os.path.join(bindir, "cc.py"))
        shutil.copy(ar, os.path.join(bindir, "ar.py"))
        r = os.path.join(tmp, "r.txt")
        cwd = os.getcwd()
        os.chdir(root)
        try:
            rc = do_build(os.path.join(tmp, "work-rel"), r, clang="build/bin/cc.py", archiver="build/bin/ar.py")
        finally:
            os.chdir(cwd)
        check(
            rc == 0
            and os.path.isfile(os.path.join(tmp, "work-rel", TRIPLES[1], "O2", ARCHIVE))
            and read_manifest(r) == ec,
            "relative '/'-separated tool paths, run from the root, pass the archive step",
        )
        d = os.path.join(tmp, "d.txt")
        with open(d, "w", encoding="utf-8") as f:
            f.write("".join("%s  %s\n" % (h, p) for p, h in sorted(ea.items()) if "/O0/" in p))
        check(compare(a, d) == 0, "entries only in one manifest are listed, not fatal")
        e = os.path.join(tmp, "e.txt")
        with open(e, "w", encoding="utf-8") as f:
            f.write("%s  other/O9/x.o\n" % ("0" * 64))
        check(compare(a, e) == 1, "compare fails when no entry is common")
        bad = os.path.join(tmp, "bad.txt")
        with open(bad, "w", encoding="utf-8") as f:
            f.write("nonsense\n")
        try:
            compare(a, bad)
            refused = False
        except UsageError:
            refused = True
        check(refused, "a malformed line is a usage error")
        check(do_build(os.path.join(REPO, "build", "det-x"), os.path.join(tmp, "x.txt")) == 2,
              "a work directory inside the repository is refused")
        precious = os.path.join(tmp, "precious")
        os.makedirs(os.path.join(precious, TRIPLES[0]))
        keep = os.path.join(precious, "keep.txt")
        with open(keep, "w", encoding="utf-8") as f:
            f.write("not det-manifest's\n")
        check(do_build(precious, os.path.join(tmp, "p.txt")) == 2 and os.path.isfile(keep),
              "a work directory holding a foreign file is refused, and the file survives")
        old = os.path.join(tmp, "old")
        stale = os.path.join(old, TRIPLES[1], "O0", "stale.o")
        os.makedirs(os.path.dirname(stale))
        with open(stale, "wb") as f:
            f.write(b"stale")
        check(do_build(old, os.path.join(tmp, "o.txt"), "O0") == 0
              and not os.path.exists(stale) and os.path.isfile(os.path.join(old, WORK_MARKER)),
              "a work directory holding only <triple>/ directories is emptied and marked")
        mine = os.path.join(old, "mine.txt")
        with open(mine, "w", encoding="utf-8") as f:
            f.write("in a directory build made\n")
        obj = os.path.join(old, TRIPLES[1], "O0", "00-a__one.o")
        missing = os.path.join(tmp, "no-such-dir", "clang.exe")
        check(do_build(old, os.path.join(tmp, "m.txt"), clang=missing) == 2
              and os.path.isfile(mine) and os.path.isfile(obj),
              "a missing --clang is a usage error before anything is deleted")
        check(do_build(old, os.path.join(tmp, "m.txt"), archiver=missing) == 2
              and os.path.isfile(mine) and os.path.isfile(obj),
              "a missing --ar is a usage error before anything is deleted")
        check(do_build(old, os.path.join(tmp, "m.txt"), "O0") == 0 and not os.path.exists(mine),
              "a directory build made (marked) is emptied")
        with open(os.path.join(m1, "01-b", "one.c"), "w", encoding="utf-8") as f:
            f.write("#error broken\n")
        f_out = os.path.join(tmp, "f.txt")
        check(do_build(os.path.join(tmp, "work-f"), f_out) == 1 and not os.path.exists(f_out),
              "a failed compile writes no manifest")
        check(do_build(os.path.join(tmp, "work-g"), os.path.join(tmp, "g.txt"), "O7") == 2,
              "an unknown level is a usage error")
    finally:
        os.environ.pop("DET_SELFTEST_LOG", None)
        shutil.rmtree(tmp, ignore_errors=True)
    print("det-manifest: self-test %s (%d failure(s))" % ("passed" if not failures else "FAILED", len(failures)))
    return 1 if failures else 0


def main(argv):
    if argv == ["--self-test"]:
        return self_test()
    p = argparse.ArgumentParser(
        description=__doc__.splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument("--self-test", action="store_true", help="run the offline self-test")
    sub = p.add_subparsers(dest="command")
    b = sub.add_parser("build", help="compile msil/test/M1 and write a manifest")
    b.add_argument("--clang", required=True, help="the tree's clang (a path is made absolute; it must start)")
    b.add_argument("--ar", required=True, help="the tree's llvm-ar (a path is made absolute; it must start)")
    b.add_argument("--out", required=True, help="the manifest to write")
    b.add_argument(
        "--work",
        help="work directory outside the repository, emptied first: only one that build made "
        "(marked by %s), an empty one or one that holds only <triple>/ directories" % WORK_MARKER,
    )
    b.add_argument("--levels", default="O0,O2", help="comma list (default O0,O2)")
    b.add_argument("--tests", help="the C files' directory (default msil/test/M1)")
    b.add_argument("--root", help=argparse.SUPPRESS)
    b.add_argument("--jobs", type=int, default=os.cpu_count() or 1, help="parallel compiles")
    c = sub.add_parser("compare", help="compare two manifests")
    c.add_argument("a")
    c.add_argument("b")
    args = p.parse_args(argv)
    if args.self_test:
        p.error("--self-test takes no other argument")
    try:
        if args.command == "build":
            if args.jobs < 1:
                raise UsageError("--jobs must be at least 1")
            return build(args)
        if args.command == "compare":
            return compare(args.a, args.b)
    except UsageError as e:
        print("det-manifest: error: %s" % e, file=sys.stderr)
        return 2
    p.print_usage(sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

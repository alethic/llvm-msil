#!/usr/bin/env python3
# ===-- linux-syntax-check.py - Linux-target syntax check of fork C++ -----===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Check C++ files the way the Linux CI job's compiler sees them.

    python msil/utils/ci/linux-syntax-check.py [--changed-from REV] [FILE...]
    python msil/utils/ci/linux-syntax-check.py --setup-only
    python msil/utils/ci/linux-syntax-check.py --check-toolchain

The Linux job of .github/workflows/msil-ci.yml builds with Ubuntu 24.04's
clang 18 and libstdc++ 14; neither the Windows build (clang-cl and MSVC's
STL) nor the Windows CI job can show an error that only that pair reports
(run 41257850076 stopped at one: a nested class with default member
initializers inside std::optional). This script compiles each file with
-fsyntax-only for --target=x86_64-unknown-linux-gnu with that compiler and
that library, prints one line per file ('ok: <file>', or 'FAIL: <file>'
followed by the compiler's output, indented) and a summary line, and exits
1 when any file fails, 0 otherwise.

Files: the FILE arguments, else (and also with --changed-from REV, default
HEAD) every C++ file of the fork that differs from REV or is untracked:
'git diff --name-only REV' plus 'git ls-files --others --exclude-standard',
the '.cpp' files under llvm/, clang/, lld/ and msil/ that exist, outside
directories named 'test' or 'Inputs' (lit inputs, which the build never
compiles). With no such file it prints 'linux-syntax-check: no changed C++
file (against REV)' and exits 0.

Flags: those of the file's entry in <build>/compile_commands.json (the
Windows build's clang-cl command; --build, default $MSIL_BUILD_DIR, else
<repo>/build), translated to the Linux job's command line: its -D macros
(the Windows-only ones dropped: UNICODE, _UNICODE, _CRT_*, _SCL_*,
_HAS_EXCEPTIONS, the /D ones) after the Linux job's own macros, its -I
directories, preceded by an overlay holding a Linux copy of the build's
llvm/Config/llvm-config.h (host and default triple x86_64-unknown-linux-gnu,
LLVM_ON_UNIX, HAVE_SYSEXITS_H), and the Linux job's warning, language and
code-generation flags (no precompiled header). A repository file without an
entry takes the entry of a file in its directory, else in the nearest
directory above it with one (a file that a build has not configured yet); a
file outside the repository takes -I <repo>/llvm/include and -I
<build>/include. A repository file with neither is reported as 'skip:' and
is no failure.

The toolchain: the Ubuntu 24.04 (noble) packages below, the newest version of
each in noble-updates, else noble (main, then universe), fetched once from
the Ubuntu archive over HTTPS, checked against the SHA-256 of the archive's
package index and extracted without root (dpkg-deb -x) into
<toolchain>/root, which also serves as the sysroot (glibc 2.39 headers from
libc6-dev and linux-libc-dev; libstdc++ 14 through --gcc-install-dir). Its
compiler runs with LD_LIBRARY_PATH on the packages' own libraries, so it
needs no package of the host. <toolchain> is --toolchain, else
$MSIL_LINUX_CLANG18, else C:\\llvm-msil-scratch\\linux-clang18 on Windows and
$HOME/llvm-msil-scratch/linux-clang18 elsewhere; it lies outside the
repository. <toolchain>/linux-syntax-check.stamp (JSON) records the packages
and versions once the extracted compiler has run; a stamp with another
package list fetches again. --setup-only fetches and stops; --check-toolchain
fetches nothing and exits 0 when the stamp is current and the extracted
compiler is in <toolchain>/root, 1 otherwise (lit's feature for the
self-test). A run that finds the toolchain ready when it starts never
fetches.

On Windows the compiles run inside WSL (--distro, default Ubuntu, the
distribution of lit.cfg.py), in one 'wsl --exec python3' process that runs
this script with --worker; on Linux they run in this process. --jobs N
compiles N files at a time (default: the CPU count).
"""

import argparse
import concurrent.futures
import contextlib
import hashlib
import json
import lzma
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request

PROG = "linux-syntax-check"
ARCHIVE = "https://archive.ubuntu.com/ubuntu"
# Searched in this order for each package; the first suite that has it wins.
SUITES = ("noble-updates", "noble")
COMPONENTS = ("main", "universe")
# The compiler, its resource headers, libstdc++ 14 and the C library headers,
# and the libraries clang 18 loads (libxml2 and libicu74 because newer Ubuntu
# releases have other sonames; the rest so that nothing comes from the host).
PACKAGES = (
    "clang-18",
    "libclang-cpp18",
    "libllvm18",
    "libclang-common-18-dev",
    "libstdc++-14-dev",
    "libgcc-14-dev",
    "libc6-dev",
    "linux-libc-dev",
    "libxml2",
    "libicu74",
    "liblzma5",
    "libffi8",
    "libedit2",
    "libbsd0",
    "libmd0",
    "libtinfo6",
    "libzstd1",
    "zlib1g",
)
STAMP = "linux-syntax-check.stamp"
STAMP_VERSION = 1
TRIPLE = "x86_64-unknown-linux-gnu"
GCC_DIR = "usr/lib/gcc/x86_64-linux-gnu/14"
CLANG = "usr/lib/llvm-18/bin/clang"
LIB_DIRS = ("usr/lib/llvm-18/lib", "usr/lib/x86_64-linux-gnu", "lib/x86_64-linux-gnu")

# The Linux job's macros and flags (its compile command for an LLVM library
# file, Release with assertions, as run 41257850076's log shows them).
CI_DEFINES = [
    "-D_DEBUG",
    "-D_GLIBCXX_ASSERTIONS",
    "-D_GLIBCXX_USE_CXX11_ABI=1",
    "-D_GNU_SOURCE",
    "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE",
    "-D__STDC_CONSTANT_MACROS",
    "-D__STDC_FORMAT_MACROS",
    "-D__STDC_LIMIT_MACROS",
]
CI_FLAGS = [
    "-fPIC",
    "-fno-semantic-interposition",
    "-fvisibility-inlines-hidden",
    "-Werror=date-time",
    "-Werror=unguarded-availability-new",
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
    "-Wwrite-strings",
    "-Wcast-qual",
    "-Wmissing-field-initializers",
    "-pedantic",
    "-Wno-long-long",
    "-Wc++98-compat-extra-semi",
    "-Wimplicit-fallthrough",
    "-Wcovered-switch-default",
    "-Wno-noexcept-type",
    "-Wnon-virtual-dtor",
    "-Wdelete-non-virtual-dtor",
    "-Wsuggest-override",
    "-Wstring-conversion",
    "-Wno-pass-failed",
    "-Wmisleading-indentation",
    "-Wctad-maybe-unsupported",
    "-fdiagnostics-color=never",
    "-ffunction-sections",
    "-fdata-sections",
    "-O3",
    "-DNDEBUG",
    "-std=c++17",
    "-fvisibility=hidden",
    "-UNDEBUG",
    "-fno-exceptions",
    "-funwind-tables",
    "-fno-rtti",
]
# Macros of the Windows build that the Linux job does not define.
DROP_DEFINE_PREFIXES = (
    "-DUNICODE",
    "-D_UNICODE",
    "-D_CRT_",
    "-D_SCL_",
    "-D_HAS_EXCEPTIONS",
)
ROOTS = ("llvm", "clang", "lld", "msil")
SKIP_DIRS = ("test", "Inputs")
COMPILE_TIMEOUT = 900


def note(msg):
    print("%s: %s" % (PROG, msg), flush=True)


class Failure(Exception):
    pass


# --- the toolchain (runs where the compiles run: WSL or a Linux host) -----


def default_toolchain():
    if os.environ.get("MSIL_LINUX_CLANG18"):
        return os.environ["MSIL_LINUX_CLANG18"]
    if os.name == "nt":
        return "C:/llvm-msil-scratch/linux-clang18"
    return os.path.join(os.path.expanduser("~"), "llvm-msil-scratch", "linux-clang18")


def read_stamp(tc):
    try:
        with open(os.path.join(tc, STAMP), encoding="utf-8") as f:
            stamp = json.load(f)
    except (OSError, ValueError):
        return None
    if stamp.get("version") != STAMP_VERSION:
        return None
    if sorted(stamp.get("packages", {})) != sorted(PACKAGES):
        return None
    return stamp


def fetch(url, dest=None):
    req = urllib.request.Request(url, headers={"User-Agent": "llvm-msil " + PROG})
    for attempt in range(3):
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                if dest is None:
                    return r.read()
                with open(dest + ".part", "wb") as f:
                    shutil.copyfileobj(r, f, 1 << 20)
                os.replace(dest + ".part", dest)
                return None
        except OSError as e:
            if attempt == 2:
                raise Failure("cannot fetch %s: %s" % (url, e))
            time.sleep(2 * (attempt + 1))


def parse_index(text):
    """{package: {field: value}} of one Packages file."""
    out = {}
    for para in text.split("\n\n"):
        fields = {}
        key = None
        for line in para.split("\n"):
            if not line:
                continue
            if line[0].isspace():
                if key:
                    fields[key] += "\n" + line.strip()
                continue
            key, _, value = line.partition(":")
            fields[key] = value.strip()
        if "Package" in fields and fields.get("Architecture") in ("amd64", "all"):
            out.setdefault(fields["Package"], fields)
    return out


def resolve_packages():
    """{package: (suite, fields)} for PACKAGES, each from the first suite and
    component that has it."""
    found = {}
    for suite in SUITES:
        for comp in COMPONENTS:
            missing = [p for p in PACKAGES if p not in found]
            if not missing:
                return found
            url = "%s/dists/%s/%s/binary-amd64/Packages.xz" % (ARCHIVE, suite, comp)
            note("reading %s" % url)
            index = parse_index(lzma.decompress(fetch(url)).decode("utf-8", "replace"))
            for p in missing:
                if p in index:
                    found[p] = (suite, index[p])
    missing = [p for p in PACKAGES if p not in found]
    if missing:
        raise Failure(
            "the Ubuntu archive's %s indexes have no package %s"
            % ("/".join(SUITES), ", ".join(missing))
        )
    return found


def extract_deb(deb, root):
    if shutil.which("dpkg-deb"):
        r = subprocess.run(["dpkg-deb", "-x", deb, root], capture_output=True, text=True)
        if r.returncode != 0:
            raise Failure("dpkg-deb -x %s failed: %s" % (deb, r.stderr.strip()))
        return
    raise Failure(
        "dpkg-deb is not installed, so %s cannot be extracted (install dpkg)" % deb
    )


def clang_env(root):
    env = dict(os.environ)
    libs = [os.path.join(root, d) for d in LIB_DIRS]
    if env.get("LD_LIBRARY_PATH"):
        libs.append(env["LD_LIBRARY_PATH"])
    env["LD_LIBRARY_PATH"] = ":".join(libs)
    env["LC_ALL"] = "C"
    return env


def clang_version(root):
    clang = os.path.join(root, CLANG)
    try:
        r = subprocess.run(
            [clang, "--version"], capture_output=True, text=True, env=clang_env(root), timeout=120
        )
    except OSError as e:
        raise Failure("cannot run %s: %s" % (clang, e))
    if r.returncode != 0:
        raise Failure(
            "%s --version exited %d: %s" % (clang, r.returncode, (r.stderr or r.stdout).strip())
        )
    return r.stdout.splitlines()[0].strip() if r.stdout else "clang (no version line)"


@contextlib.contextmanager
def setup_lock(tc):
    os.makedirs(tc, exist_ok=True)
    path = os.path.join(tc, ".lock")
    with open(path, "a+") as f:
        try:
            import fcntl

            fcntl.flock(f, fcntl.LOCK_EX)
        except ImportError:
            pass
        yield


def ready_stamp(tc):
    """The stamp of tc when its toolchain is ready (a current stamp and the
    extracted compiler under root/), else None. --check-toolchain, main() and
    ensure_toolchain() share this one test."""
    stamp = read_stamp(tc)
    if stamp and os.path.isfile(os.path.join(tc, "root", CLANG)):
        return stamp
    return None


def ensure_toolchain(tc, allow_fetch=True):
    """The stamp of a ready toolchain in tc, fetched and extracted if needed."""
    stamp = ready_stamp(tc)
    root = os.path.join(tc, "root")
    if stamp:
        return stamp
    if not allow_fetch:
        raise Failure("no toolchain in %s (run with --setup-only)" % tc)
    with setup_lock(tc):
        stamp = ready_stamp(tc)
        if stamp:
            return stamp
        note("fetching Ubuntu 24.04's clang 18 and libstdc++ 14 into %s (once)" % tc)
        found = resolve_packages()
        debs = os.path.join(tc, "debs")
        os.makedirs(debs, exist_ok=True)
        new_root = root + ".new"
        shutil.rmtree(new_root, ignore_errors=True)
        os.makedirs(new_root)
        record = {}
        for p in PACKAGES:
            suite, fields = found[p]
            filename = fields["Filename"]
            sha = fields.get("SHA256", "").lower()
            if not re.fullmatch(r"[0-9a-f]{64}", sha):
                raise Failure("the index entry of %s has no SHA256" % p)
            deb = os.path.join(debs, os.path.basename(filename))
            have = None
            if os.path.isfile(deb):
                with open(deb, "rb") as f:
                    have = hashlib.sha256(f.read()).hexdigest()
            if have != sha:
                note("fetching %s %s (%s, %s bytes)" % (p, fields["Version"], suite, fields.get("Size", "?")))
                fetch("%s/%s" % (ARCHIVE, filename), deb)
                with open(deb, "rb") as f:
                    have = hashlib.sha256(f.read()).hexdigest()
                if have != sha:
                    os.remove(deb)
                    raise Failure(
                        "%s: SHA-256 %s, but the index says %s" % (os.path.basename(deb), have, sha)
                    )
            extract_deb(deb, new_root)
            record[p] = {"version": fields["Version"], "suite": suite, "sha256": sha,
                         "file": os.path.basename(filename)}
        if not os.path.isdir(os.path.join(new_root, GCC_DIR)):
            raise Failure("the extracted packages have no %s" % GCC_DIR)
        version = clang_version(new_root)
        shutil.rmtree(root, ignore_errors=True)
        os.replace(new_root, root)
        stamp = {"version": STAMP_VERSION, "clang": version, "packages": record}
        tmp = os.path.join(tc, STAMP + ".tmp")
        with open(tmp, "w", encoding="utf-8", newline="\n") as f:
            json.dump(stamp, f, indent=1, sort_keys=True)
            f.write("\n")
        os.replace(tmp, os.path.join(tc, STAMP))
        note("toolchain ready: %s" % version)
        return stamp


def compile_one(root, entry):
    args = (
        [os.path.join(root, CLANG), "--driver-mode=g++"]
        + entry["args"]
        + ["--target=" + TRIPLE, "--sysroot=" + root,
           "--gcc-install-dir=" + os.path.join(root, GCC_DIR), "-fsyntax-only", entry["path"]]
    )
    try:
        r = subprocess.run(
            args, capture_output=True, text=True, errors="replace", env=clang_env(root),
            timeout=COMPILE_TIMEOUT,
        )
    except subprocess.TimeoutExpired:
        return 1, "did not finish within %d s" % COMPILE_TIMEOUT
    except OSError as e:
        return 1, "cannot run clang: %s" % e
    return r.returncode, (r.stdout + r.stderr).rstrip()


def run_worker(job):
    """Compile job['files'] with the toolchain in job['toolchain']; 0, 1 or 2."""
    tc = job["toolchain"]
    try:
        stamp = ensure_toolchain(tc, allow_fetch=job.get("fetch", True))
    except Failure as e:
        note("error: %s" % e)
        return 2
    if job.get("setup_only"):
        return 0
    root = os.path.join(tc, "root")
    pkgs = stamp["packages"]
    note("%s; libstdc++-14-dev %s; glibc headers %s" % (
        stamp["clang"], pkgs["libstdc++-14-dev"]["version"], pkgs["libc6-dev"]["version"]))
    files = job["files"]
    failed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, job.get("jobs", 1))) as pool:
        futures = [(e, pool.submit(compile_one, root, e)) for e in files]
        for e, fut in futures:
            rc, out = fut.result()
            if rc == 0:
                print("ok: %s" % e["name"], flush=True)
            else:
                failed += 1
                print("FAIL: %s" % e["name"], flush=True)
                for line in out.splitlines() or ["(no output; exit %d)" % rc]:
                    print("    " + line, flush=True)
    note("%d file(s) checked, %d failed" % (len(files), failed))
    return 1 if failed else 0


# --- the driver (runs where the repository and the build are) -------------


def to_linux(path):
    """A path of this host as the compile host sees it (WSL's /mnt/<drive>)."""
    p = os.path.abspath(path).replace("\\", "/")
    if os.name == "nt":
        m = re.match(r"^([A-Za-z]):(/.*)?$", p)
        if not m:
            raise Failure("cannot name %s inside WSL" % path)
        return "/mnt/" + m.group(1).lower() + (m.group(2) or "")
    return p


def norm(path):
    p = os.path.abspath(path).replace("\\", "/")
    return p.lower() if os.name == "nt" else p


def git_lines(repo, *args):
    r = subprocess.run(["git", "-C", repo] + list(args), capture_output=True, text=True)
    if r.returncode != 0:
        raise Failure("git %s failed: %s" % (" ".join(args), r.stderr.strip()))
    return [l for l in r.stdout.splitlines() if l.strip()]


def changed_files(repo, rev):
    names = set(git_lines(repo, "diff", "--name-only", rev, "--"))
    names |= set(git_lines(repo, "ls-files", "--others", "--exclude-standard"))
    out = []
    for n in sorted(names):
        parts = n.split("/")
        if not n.endswith(".cpp") or parts[0] not in ROOTS:
            continue
        if any(p in SKIP_DIRS for p in parts[:-1]):
            continue
        if os.path.isfile(os.path.join(repo, n)):
            out.append(os.path.join(repo, n))
    return out


def load_compile_db(build):
    path = os.path.join(build, "compile_commands.json")
    try:
        with open(path, encoding="utf-8") as f:
            db = json.load(f)
    except (OSError, ValueError) as e:
        raise Failure("cannot read %s: %s" % (path, e))
    byfile = {}
    for e in db:
        f = e["file"]
        if not os.path.isabs(f):
            f = os.path.join(e.get("directory", build), f)
        byfile.setdefault(norm(f), e)
    return byfile


def entry_args(e):
    if e.get("arguments"):
        return list(e["arguments"])
    return shlex.split(e["command"], posix=False)


def unquote(a):
    if len(a) >= 2 and a[0] == a[-1] == '"':
        return a[1:-1]
    return a


def translate(e, directory):
    """(defines, include dirs) of a compile_commands entry, Linux-target."""
    defines, incs = [], []
    args = [unquote(a) for a in entry_args(e)[1:]]
    i = 0
    while i < len(args):
        a = args[i]
        i += 1
        if a in ("-D", "-I", "/D", "/I") and i < len(args):
            a, i = a + args[i], i + 1
        if a.startswith("-D"):
            if not a.startswith(DROP_DEFINE_PREFIXES):
                defines.append(a)
        elif a.startswith("-I") or a.startswith("/I"):
            d = a[2:]
            if not os.path.isabs(d):
                d = os.path.join(directory, d)
            incs.append("-I" + to_linux(d))
    return defines, incs


def find_entry(db, repo, path):
    """The compile_commands entry for path, else one of its directory or of
    the nearest directory above it inside its top directory; else None."""
    key = norm(path)
    if key in db:
        return db[key]
    repo_key = norm(repo).rstrip("/") + "/"
    if not key.startswith(repo_key):
        return None
    rel = key[len(repo_key):].split("/")
    for depth in range(len(rel) - 1, 1, -1):
        prefix = repo_key + "/".join(rel[:depth]) + "/"
        candidates = sorted(k for k in db if k.startswith(prefix) and "/" not in k[len(prefix):])
        if candidates:
            return db[candidates[0]]
    return None


def make_overlay(build, out_dir):
    src = os.path.join(build, "include", "llvm", "Config", "llvm-config.h")
    try:
        with open(src, encoding="utf-8") as f:
            text = f.read()
    except OSError as e:
        raise Failure("cannot read %s: %s" % (src, e))
    subs = [
        (r'#define LLVM_DEFAULT_TARGET_TRIPLE "[^"]*"', '#define LLVM_DEFAULT_TARGET_TRIPLE "%s"' % TRIPLE, True),
        (r'#define LLVM_HOST_TRIPLE "[^"]*"', '#define LLVM_HOST_TRIPLE "%s"' % TRIPLE, True),
        (r"/\* #undef LLVM_ON_UNIX \*/|#define LLVM_ON_UNIX 1", "#define LLVM_ON_UNIX 1", True),
        (r"/\* #undef HAVE_SYSEXITS_H \*/", "#define HAVE_SYSEXITS_H 1", False),
    ]
    for pattern, repl, required in subs:
        text, n = re.subn(pattern, repl, text)
        if n == 0 and required:
            raise Failure("%s has no line matching %r for the Linux overlay" % (src, pattern))
    dest = os.path.join(out_dir, "overlay", "llvm", "Config")
    os.makedirs(dest, exist_ok=True)
    with open(os.path.join(dest, "llvm-config.h"), "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return os.path.join(out_dir, "overlay")


def build_job(args, repo, files, run_dir):
    build = args.build
    db = load_compile_db(build)
    overlay = "-I" + to_linux(make_overlay(build, run_dir))
    generic = ["-I" + to_linux(os.path.join(repo, "llvm", "include")),
               "-I" + to_linux(os.path.join(build, "include"))]
    entries, skipped = [], []
    repo_key = norm(repo).rstrip("/") + "/"
    for f in files:
        f = os.path.abspath(f)
        if not os.path.isfile(f):
            raise Failure("no such file: %s" % f)
        e = find_entry(db, repo, f)
        name = os.path.relpath(f, repo).replace("\\", "/") if norm(f).startswith(repo_key) else f.replace("\\", "/")
        if e is not None:
            defines, incs = translate(e, e.get("directory", build))
        elif not norm(f).startswith(repo_key):
            defines, incs = [], generic
        else:
            skipped.append(name)
            continue
        seen, flags = set(), []
        for d in CI_DEFINES + defines:
            if d not in seen:
                seen.add(d)
                flags.append(d)
        flags.append(overlay)
        flags += incs + CI_FLAGS
        entries.append({"name": name, "path": to_linux(f), "args": flags})
    return entries, skipped


def main(argv=None):
    ap = argparse.ArgumentParser(prog=PROG, description=__doc__.split("\n\n")[0])
    ap.add_argument("files", nargs="*", help="files to check (default: the changed ones)")
    ap.add_argument("--changed-from", metavar="REV", help="also every C++ file changed since REV")
    ap.add_argument("--repo", default=os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..")))
    ap.add_argument("--build", default=None, help="the build directory (compile_commands.json)")
    ap.add_argument("--toolchain", default=None, help="where the Ubuntu packages go")
    ap.add_argument("--distro", default="Ubuntu", help="the WSL distribution (Windows only)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--setup-only", action="store_true", help="fetch the toolchain and stop")
    ap.add_argument("--check-toolchain", action="store_true",
                    help="exit 0 when the toolchain is ready (a current stamp and the extracted "
                    "compiler), 1 otherwise; fetch nothing")
    ap.add_argument("--worker", metavar="JOB", help=argparse.SUPPRESS)
    args = ap.parse_args(argv)

    if args.worker:
        with open(args.worker, encoding="utf-8") as f:
            return run_worker(json.load(f))

    tc = args.toolchain or default_toolchain()
    if args.check_toolchain:
        stamp = ready_stamp(tc)
        ok = stamp is not None
        print("%s: %s" % (PROG, ("toolchain ready in %s: %s" % (tc, stamp["clang"])) if ok
                          else ("no current toolchain in %s" % tc)))
        return 0 if ok else 1
    repo = os.path.abspath(args.repo)
    args.build = os.path.abspath(args.build or os.environ.get("MSIL_BUILD_DIR") or os.path.join(repo, "build"))

    try:
        files = []
        rev = None
        if not args.setup_only:
            if args.changed_from or not args.files:
                rev = args.changed_from or "HEAD"
                files += changed_files(repo, rev)
            files += [os.path.abspath(f) for f in args.files]
            seen = set()
            files = [f for f in files if not (norm(f) in seen or seen.add(norm(f)))]
            if not files:
                note("no changed C++ file (against %s)" % rev)
                return 0
        os.makedirs(tc, exist_ok=True)
        run_dir = tempfile.mkdtemp(prefix="run-", dir=tc)
        try:
            entries, skipped = ([], []) if args.setup_only else build_job(args, repo, files, run_dir)
            for s in skipped:
                print("skip: %s (no compile command for it or its directory in %s)"
                      % (s, os.path.join(args.build, "compile_commands.json")), flush=True)
            if not args.setup_only and not entries:
                note("%d file(s), none with a compile command" % len(skipped))
                return 0
            # A run that starts with a ready toolchain never fetches: lit's
            # self-test runs only after --check-toolchain said ready, and a
            # toolchain that breaks meanwhile fails the run instead.
            job = {"toolchain": to_linux(tc), "jobs": args.jobs, "files": entries,
                   "setup_only": args.setup_only, "fetch": ready_stamp(tc) is None}
            job_path = os.path.join(run_dir, "job.json")
            with open(job_path, "w", encoding="utf-8", newline="\n") as f:
                json.dump(job, f)
            if os.name != "nt":
                return run_worker(job)
            wsl = shutil.which("wsl.exe") or shutil.which("wsl")
            if not wsl:
                raise Failure("wsl.exe is not on PATH (the compiles run inside WSL)")
            cmd = [wsl, "-d", args.distro, "--exec", "python3", to_linux(os.path.abspath(__file__)),
                   "--worker", to_linux(job_path)]
            r = subprocess.run(cmd)
            if r.returncode not in (0, 1):
                note("error: the WSL worker (%s) exited %d" % (" ".join(cmd), r.returncode))
                return 2
            return r.returncode
        finally:
            shutil.rmtree(run_dir, ignore_errors=True)
    except Failure as e:
        note("error: %s" % e)
        return 2


if __name__ == "__main__":
    sys.exit(main())

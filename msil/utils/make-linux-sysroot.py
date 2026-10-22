#!/usr/bin/env python3
# ===-- make-linux-sysroot.py - Linux libraries for the E7 oracle ---------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Copy the Linux libraries of the E7 native oracle out of a WSL distribution.

    make-linux-sysroot.py [--out DIR] [--distro NAME]

The E7 oracle (plan 5.12.8, recon tests-p2 E7) builds the native twin of a
test for x86_64-unknown-linux-gnu on Windows, without a compiler or C headers
in WSL: msil/utils/linux-oracle/start.c and the test are compiled by clang,
then linked by ld.lld against the shared libraries this script copies, and the
program runs under WSL. The layout is

    DIR/usr/lib/x86_64-linux-gnu/libc.so.6, libm.so.6, libgcc_s.so.1,
                                 ld-linux-x86-64.so.2
    DIR/usr/lib/x86_64-linux-gnu/libc.so   'GROUP ( libc.so.6 )'
    DIR/usr/lib/x86_64-linux-gnu/libm.so   'GROUP ( libm.so.6 )'
    DIR/lib64/ld-linux-x86-64.so.2
    DIR/msil-linux-sysroot.stamp           distribution, glibc version, hashes

libgcc_s.so.1 is there for the i128 helpers of native builds (__udivti3,
__fixdfti, ...), which libc.so.6 does not export; the E7 link names it after
libc.so.6 with --as-needed, so programs that do not use it do not need it.
msil-ld uses the same directory as --sysroot from M15 on. Symbolic links are
resolved (every file is a regular copy), the files are read inside WSL
('wsl.exe -d NAME'; on a Linux host from the local file system, where NAME is
not used) and verified against their SHA-256, and only files whose contents
changed are written, so a second run changes nothing. DIR defaults to
$MSIL_LINUX_SYSROOT, else C:\\llvm-msil-scratch\\linux-sysroot on Windows (lit's
default --param msil_linux_sysroot); a directory inside the repository is
refused (nothing downloaded or generated is committed, plan 5.6.1). Callers in
Git Bash set MSYS_NO_PATHCONV=1 when they pass Linux paths to other tools.

Exit status: 0 on success, 1 when the libraries cannot be read or written,
2 on a usage error.
"""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
IS_WINDOWS = os.name == "nt"
DEFAULT_WINDOWS_DIR = r"C:\llvm-msil-scratch\linux-sysroot"
LIBDIR = "usr/lib/x86_64-linux-gnu"
LIBRARIES = ("libc.so.6", "libm.so.6", "libgcc_s.so.1", "ld-linux-x86-64.so.2")
LOADER = "ld-linux-x86-64.so.2"
# Where a library may live on the distribution (Ubuntu merges /lib into /usr/lib).
SEARCH = ("/usr/lib/x86_64-linux-gnu", "/lib/x86_64-linux-gnu", "/usr/lib64", "/lib64")
SCRIPTS = {"libc.so": "GROUP ( libc.so.6 )\n", "libm.so": "GROUP ( libm.so.6 )\n"}
STAMP = "msil-linux-sysroot.stamp"

# Read on stdin by 'sh -s' inside the distribution: one 'file NAME SHA256 PATH'
# line per library (the path with every link resolved), then the glibc version
# and the distribution's name.
PROBE = (
    "set -u\n"
    "for n in %s; do\n"
    "  p=\n"
    "  for d in %s; do\n"
    '    if [ -e "$d/$n" ]; then p=$(readlink -f "$d/$n"); break; fi\n'
    "  done\n"
    '  if [ -z "$p" ]; then echo "missing $n"; exit 3; fi\n'
    '  h=$(sha256sum "$p" | cut -d" " -f1) || exit 4\n'
    '  echo "file $n $h $p"\n'
    "done\n"
    'echo "glibc $(getconf GNU_LIBC_VERSION 2>/dev/null | sed "s/^glibc //")"\n'
    "if [ -r /etc/os-release ]; then . /etc/os-release; fi\n"
    'echo "os ${PRETTY_NAME:-unknown}"\n'
) % (" ".join(LIBRARIES), " ".join(SEARCH))


class Error(Exception):
    pass


def decode(data):
    # wsl.exe writes its own messages in UTF-16LE.
    if b"\0" in data:
        return data.decode("utf-16-le", "replace")
    return data.decode("utf-8", "replace")


def wsl_exe():
    return shutil.which("wsl.exe") or os.path.join(
        os.environ.get("SystemRoot", r"C:\Windows"), "System32", "wsl.exe"
    )


def run_in_linux(distro, argv, stdin=None, timeout=300):
    """stdout bytes of argv, run inside WSL on Windows and locally elsewhere."""
    cmd = ([wsl_exe(), "-d", distro, "--exec"] if IS_WINDOWS else []) + argv
    env = dict(os.environ)
    env["WSL_UTF8"] = "1"
    try:
        res = subprocess.run(
            cmd,
            input=stdin,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            env=env,
        )
    except (OSError, subprocess.SubprocessError) as e:
        raise Error("cannot run %s: %s" % (" ".join(cmd[:6]), e))
    if res.returncode != 0:
        raise Error(
            "%s exited with %d:\n%s%s"
            % (
                " ".join(cmd if len(cmd) < 8 else cmd[:8] + ["..."]),
                res.returncode,
                decode(res.stdout),
                decode(res.stderr),
            )
        )
    return res.stdout


def probe(distro):
    """({name: (sha256, path)}, glibc version, distribution name)."""
    out = decode(run_in_linux(distro, ["sh", "-s"], stdin=PROBE.encode("utf-8")))
    files, glibc, osname = {}, "", ""
    for line in out.replace("\r\n", "\n").split("\n"):
        kind, _, rest = line.partition(" ")
        if kind == "file":
            name, digest, path = rest.split(" ", 2)
            files[name] = (digest.lower(), path)
        elif kind == "glibc":
            glibc = rest.strip()
        elif kind == "os":
            osname = rest.strip()
    missing = [n for n in LIBRARIES if n not in files]
    if missing:
        raise Error("the probe found no %s:\n%s" % (", ".join(missing), out))
    return files, glibc or "unknown", osname or "unknown"


def sha256_of(path):
    try:
        with open(path, "rb") as f:
            return hashlib.sha256(f.read()).hexdigest()
    except OSError:
        return None


def write_if_changed(path, data):
    """Write bytes atomically unless the file already holds them; True if
    it was written."""
    try:
        with open(path, "rb") as f:
            if f.read() == data:
                return False
    except OSError:
        pass
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix=".tmp-", dir=os.path.dirname(path))
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise
    return True


def read_library(distro, path):
    if IS_WINDOWS:
        return run_in_linux(distro, ["cat", "--", path], timeout=600)
    with open(path, "rb") as f:
        return f.read()


def inside(path, root):
    path = os.path.normcase(os.path.realpath(path))
    root = os.path.normcase(os.path.realpath(root))
    try:
        return os.path.commonpath([path, root]) == root
    except ValueError:  # different drives
        return False


def make_sysroot(out, distro):
    files, glibc, osname = probe(distro)
    libdir = os.path.join(out, *LIBDIR.split("/"))
    changed = []
    for name in LIBRARIES:
        digest, src = files[name]
        targets = [os.path.join(libdir, name)]
        if name == LOADER:
            targets.append(os.path.join(out, "lib64", name))
        stale = [t for t in targets if sha256_of(t) != digest]
        if not stale:
            continue
        data = read_library(distro, src)
        if hashlib.sha256(data).hexdigest() != digest:
            raise Error("%s changed while it was copied (or the copy is damaged)" % src)
        for t in stale:
            if write_if_changed(t, data):
                changed.append(t)
    for name, text in SCRIPTS.items():
        t = os.path.join(libdir, name)
        if write_if_changed(t, text.encode("ascii")):
            changed.append(t)
    stamp = [
        "# make-linux-sysroot.py: the libraries of the E7 Linux oracle (plan 5.12.8)",
        "distro %s" % (distro if IS_WINDOWS else "(local file system)"),
        "os %s" % osname,
        "glibc %s" % glibc,
    ]
    for name in LIBRARIES:
        stamp.append("sha256 %s %s/%s" % (files[name][0], LIBDIR, name))
    t = os.path.join(out, STAMP)
    if write_if_changed(t, ("\n".join(stamp) + "\n").encode("utf-8")):
        changed.append(t)
    return glibc, osname, changed


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    default_out = os.environ.get("MSIL_LINUX_SYSROOT") or (
        DEFAULT_WINDOWS_DIR if IS_WINDOWS else None
    )
    ap.add_argument(
        "--out",
        default=default_out,
        help="the sysroot directory (default: $MSIL_LINUX_SYSROOT, else %s on "
        "Windows)" % DEFAULT_WINDOWS_DIR.replace("%", "%%"),
    )
    ap.add_argument(
        "--distro",
        default="Ubuntu",
        help="the WSL distribution to copy from (default: %(default)s)",
    )
    args = ap.parse_args(argv)
    if not args.out:
        ap.error("--out DIR is required ($MSIL_LINUX_SYSROOT is not set)")
    out = os.path.abspath(args.out)
    if inside(out, REPO):
        sys.stderr.write(
            "make-linux-sysroot: error: %s is inside the repository %s; put the "
            "sysroot outside it (for example %s)\n" % (out, REPO, DEFAULT_WINDOWS_DIR)
        )
        return 2
    try:
        glibc, osname, changed = make_sysroot(out, args.distro)
    except (Error, OSError) as e:
        sys.stderr.write("make-linux-sysroot: error: %s\n" % e)
        return 1
    for path in changed:
        print("make-linux-sysroot: wrote %s" % path)
    print(
        "make-linux-sysroot: %s: glibc %s (%s), %d file(s) updated"
        % (out, glibc, osname, len(changed))
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())

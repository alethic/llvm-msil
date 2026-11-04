#!/usr/bin/env python3
# ===-- build-libc.py - LLVM libc as the managed C library c.dll ----------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Build LLVM libc for msil64-unknown-dotnet as the shared assembly c.dll.

  python msil/utils/build/build-libc.py [--build-dir DIR] [--jobs N]
  python msil/utils/build/build-libc.py [--build-dir DIR] --check
  python msil/utils/build/build-libc.py [--build-dir DIR] --check-reproducible OUT
  python msil/utils/build/build-libc.py --check-exports | --write-exports
  python msil/utils/build/build-libc.py --check-append-only OLD NEW
  python msil/utils/build/build-libc.py --self-test

The managed C library of the portable triple (plan 5.19.3, 5.19.7): llvm-libc
built by its own CMake for msil64-unknown-dotnet, linked by msil-ld into the
shared assembly c.dll and installed, with the generated headers and an empty
libm.a, into the portable sysroot that the clang driver searches (plan 5.18.4
PV2, PV3).

--build-dir DIR names the build whose tools the script uses: bin/clang,
bin/clang++, bin/llvm-ar, bin/llvm-ranlib and bin/msil-ld (default:
$MSIL_BUILD_DIR when it is set and not empty, else <repository>/build, the
shared build). The libc build is its own CMake tree, DIR/runtimes/
msil64-unknown-dotnet, configured once from <repository>/runtimes with
LLVM_ENABLE_RUNTIMES=libc and these variables (never the shared tree's CMake
cache):

  LLVM_LIBC_FULL_BUILD=ON  LIBC_TARGET_TRIPLE=msil64-unknown-dotnet
  LLVM_DEFAULT_TARGET_TRIPLE=msil64-unknown-dotnet (so runtimes/CMakeLists.txt
  does not guess a host triple; never LLVM_RUNTIMES_TARGET, which
  LLVMLibCArchitectures.cmake refuses together with LIBC_TARGET_TRIPLE)
  CMAKE_{C,CXX,ASM}_COMPILER_TARGET=msil64-unknown-dotnet
  CMAKE_SYSTEM_NAME=Generic  CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
  CMAKE_BUILD_TYPE=Release (so NDEBUG: no shipped entry point reaches
  LIBC_ASSERT, write_to_stderr or internal::exit)  LLVM_INCLUDE_TESTS=OFF
  LLVM_INCLUDE_DOCS=OFF  LLVM_ENABLE_PER_TARGET_RUNTIME_DIR=OFF
  CMAKE_CXX_SCAN_FOR_MODULES=OFF  CMAKE_{C,CXX}_FLAGS=-fvisibility=hidden
  CMAKE_{C,CXX,ASM}_COMPILER, CMAKE_AR, CMAKE_RANLIB, CMAKE_LINKER (the
  build's clang, clang++, llvm-ar, llvm-ranlib and msil-ld),
  CMAKE_MAKE_PROGRAM (ninja) and Python3_EXECUTABLE (this Python)

No LIBC_CONFIG_PATH is passed, and no LIBC_CONF_* option: libc's CMake takes
config/dotnet/config.json and config/dotnet/msil64/{entrypoints,headers}.txt
from the triple (LIBC_TARGET_OS dotnet, LIBC_TARGET_ARCHITECTURE msil64), and
a LIBC_CONFIG_PATH would skip config.json (P-1b.a's entrypoints.txt then stops
the configure). On Windows cmake.exe and ninja.exe come from the Visual Studio
environment that msil/utils/build/msil-env.ps1 loads; elsewhere from PATH.

A build then runs ninja (all, and the errno provider's object library
libc.src.__support.OSUtil.dotnet.dotnet_util, which no entry point depends
on), adds that library's objects (members dotnet_util.<object>) to the
intermediate archive DIR/runtimes/msil64-unknown-dotnet/libc/lib/libc.a, so
that the archive defines the EXTERNAL errno provider __llvm_libc_errno (and
msil-ld's static C library rule T-P11 refuses it, plan 5.19.8), and links:

  msil-ld --target=msil64-unknown-dotnet --shared
      --export-list=<repository>/msil/lib/libc/msil64-unknown-dotnet.exports
      --exclude-libs=ALL --assembly-version=1.0.0.0
      libc.a <portable clang_rt.builtins> -o c.dll

with the helper archive that 'clang --target=msil64-unknown-dotnet
-rtlib=compiler-rt -print-libgcc-file-name' names (build it first with
'build-builtins.py --triple msil64-unknown-dotnet'). The forced exports
extract the members that define them and win over --exclude-libs (plan 5.18.6
PL7), and the --shared output named c records the C runtime identity c. It
installs into the sysroot DIR/lib/clang-runtimes/msil64-unknown-dotnet:
include/ (libc's install component libc-headers, replaced whole),
lib/msil64-unknown-dotnet/c.dll and lib/msil64-unknown-dotnet/libm.a (the
bare archive header '!<arch>\\n', an archive with no members, brief Q14 (a)).
libc.a is never installed (plan 5.19.8: no static C library before P-3).

The stamp lib/msil64-unknown-dotnet/c.dll.stamp records the SHA-256 of this
script, of the five tools, of the helper archive, of the export list and of
every file of libc/ (without test/, docs/, benchmarks/, fuzzing/ and
examples/), runtimes/, cmake/ and llvm/cmake/, a digest of the include/ tree
of clang's resource directory ('clang -print-resource-dir': the freestanding
stddef.h, stdarg.h and their helpers that the -ffreestanding -nostdlibinc
compiles include), the cmake version, the configure variables, the assembly
version, the reference set (none before P-2), the SHA-256 of the intermediate
libc.a after the errno provider is added, and the SHA-256 of every installed
file. A build whose stamp matches and whose libc.a and installed files are
unchanged does nothing; so a deleted libc.a, or one that a plain ninja run
rebuilt without the provider, makes a build rerun ninja and add the provider
again. Otherwise the stamp goes
first; a failure removes c.dll too, so a link fails loudly instead of taking a
stale c.dll. A build also fails, before it configures, when the checked-in
export list differs from the one entrypoints.txt gives (--write-exports).

A build writes under DIR/runtimes and the sysroot while holding the build
directory's exclusive lock (msil/utils/build/msil-lock.ps1's
DIR/.msil-build.lock, Windows only): it waits while msil-build.ps1 builds or a
lit run or gtest uses the tools, and they wait for it. --check and
--check-reproducible take the shared lock. So never run this script while
holding that directory's lock.

The export list msil/lib/libc/msil64-unknown-dotnet.exports (plan 5.19.7)
holds the C name (the last '.' component) of every libc.src.* entry of the
set(TARGET_LIBC_ENTRYPOINTS ...) block of
libc/config/dotnet/msil64/entrypoints.txt, without 'errno' (the entry point
libc.src.errno.errno defines no exported symbol), plus __llvm_libc_errno, one
per line after a comment header, sorted. It is append-only: a regeneration
keeps the names of the checked-in list in their order and appends the new ones
sorted; an entry point that is gone from entrypoints.txt while the list still
names it is an error (a name is never removed). Only that block counts:
TARGET_LLVMLIBC_ENTRYPOINTS adds names for the header generator only (setjmp
and friends, which nothing defines), and a list(APPEND TARGET_LIBC_ENTRYPOINTS
...) is an error.

Modes (at most one; without one the script builds):
  --check          builds nothing; exits 1 when c.dll, libm.a, the headers,
                   the stamp or the intermediate libc.a is missing, when the
                   stamp differs from the one a build would write, or when
                   libc.a or an installed file changed.
  --check-reproducible OUT
                   builds nothing; empties the directory OUT, relinks c.dll
                   into it from the intermediate libc.a and the helper
                   archive with the arguments of the installed link, and exits
                   1 unless the result is byte-identical to the installed
                   c.dll (printing both SHA-256 hashes) or when c.dll or
                   libc.a is missing. It writes the marker file
                   .build-libc-relink in OUT before the relink. OUT must be
                   new, empty or hold only what an earlier run wrote there:
                   the marker, c.dll, and the c.dll.tmp<seven lowercase hex
                   digits> that a killed msil-ld leaves, all regular files.
                   Anything else in it (another file, a directory, a link),
                   or a c.dll or c.dll.tmp without the marker, is a usage
                   error, and so is an OUT that is a file system root or a
                   mount point, is or holds the repository or the build
                   directory, lies in either, or really holds a file the
                   relink reads or compares with (the installed c.dll,
                   libc.a, the helper archive, the export list or a tool,
                   also through a file link), or, on Windows, has a name in
                   its path that ends in a dot or a space (which a Windows
                   path drops). Links and aliases are resolved (by path and
                   by file identity): symbolic links,
                   junctions, extended-length, 8.3, subst and UNC loopback
                   paths. OUT is checked before the wait for the build lock
                   and again just before it is emptied (against the helper
                   archive only then, once clang named it). A relinked c.dll
                   that is the installed one itself is a usage error too.
                   The compiles' determinism is not covered.
  --check-exports  exits 1 unless the checked-in export list equals the one
                   entrypoints.txt gives; needs no build.
  --write-exports  writes that export list; needs no build.
  --check-append-only OLD NEW
                   exits 1 unless the names of export list NEW (blank lines
                   and '#' lines aside; CRLF and a UTF-8 BOM accepted) start
                   with every name of OLD in its order and repeat none.
  --self-test      checks this script offline, in a temporary directory, with
                   stand-ins for the tools, cmake and ninja.

Exit status: 0 on success, 1 for a failed build or check, 2 for a usage error
(also a missing tool or helper archive for a build, which fails --check and
--check-reproducible with exit 1, or an unreadable OLD or NEW).
"""

import argparse
import contextlib
import hashlib
import io
import os
import queue
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
PROG = "build-libc.py"
EXE = ".exe" if os.name == "nt" else ""

TRIPLE = "msil64-unknown-dotnet"
# Plan 5.19.7: <abi-major>.<abi-minor>.0.0, the minor raised by each stage
# commit that appends to the export list.
ASSEMBLY_VERSION = "1.0.0.0"
ENTRYPOINTS = "libc/config/dotnet/msil64/entrypoints.txt"
EXPORTS = "msil/lib/libc/msil64-unknown-dotnet.exports"
ENTRYPOINT_BLOCK = "TARGET_LIBC_ENTRYPOINTS"
# Entry point target names that define no exported symbol.
NOT_EXPORTED = ("errno",)
# The named extras of plan 5.19.7 that are no entry point.
EXTRA_EXPORTS = ("__llvm_libc_errno",)
# The EXTERNAL errno provider (P-1b.b), an object library that no entry point
# depends on; its objects join libc.a as dotnet_util.<object>.
PROVIDER_TARGET = "libc.src.__support.OSUtil.dotnet.dotnet_util"
PROVIDER_DIR = "libc/src/__support/OSUtil/dotnet/CMakeFiles/%s.dir" % PROVIDER_TARGET
PROVIDER_PREFIX = "dotnet_util."
LIBC_ARCHIVE = "libc/lib/libc.a"
LIBC_TREE = "runtimes/" + TRIPLE
SYSROOT = "lib/clang-runtimes/" + TRIPLE
LIB_DIR = "lib/" + TRIPLE
HEADER_COMPONENT = "libc-headers"
EMPTY_ARCHIVE = b"!<arch>\n"
STAMP_VERSION = 2
# The source trees a build depends on, and the parts of them it does not.
SOURCE_DIRS = ("libc", "runtimes", "cmake", "llvm/cmake")
SOURCE_SKIP = ("libc/test", "libc/docs", "libc/benchmarks", "libc/fuzzing", "libc/examples")
TOOLS = ("clang", "clang++", "llvm-ar", "llvm-ranlib", "msil-ld")
CONFIGURE_TIMEOUT = 30 * 60
BUILD_TIMEOUT = 4 * 60 * 60
TOOL_TIMEOUT = 15 * 60
# run_streamed: how often it checks the deadline, how long it reads after the
# child exited, and how long it waits for the reader thread and a killed child.
STREAM_POLL = 0.25
STREAM_EOF_GRACE = 10
STREAM_JOIN = 10
REPLACE_RETRY_DELAYS = (0.025, 0.05, 0.1, 0.1, 0.1)
LOCK_FILE = ".msil-build.lock"
LOCK_WAIT_SECONDS = 240 * 60
ENTRY_RE = re.compile(r"^libc\.src\.[A-Za-z0-9_]+(?:\.[A-Za-z0-9_]+)+$")
NAME_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

EXPORTS_HEADER = """\
# msil64-unknown-dotnet.exports - the exports of c.dll, the managed C library
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Generated by 'python msil/utils/build/build-libc.py --write-exports' (checked
# by --check-exports) from the TARGET_LIBC_ENTRYPOINTS block of
# libc/config/dotnet/msil64/entrypoints.txt: the C name of each entry point but
# errno, plus __llvm_libc_errno (plan 5.19.7). msil-ld --export-list reads it
# when build-libc.py links c.dll. This is c.dll's C ABI and it is append-only:
# a later stage adds names at the end and never removes or reorders one
# (build-libc.py --check-append-only OLD NEW).
"""


class UsageError(Exception):
    """A usage error: exit status 2."""


class CheckError(Exception):
    """A failed build or check: exit status 1."""


def say(msg, file=None):
    print("%s: %s" % (PROG, msg), file=file or sys.stdout)
    (file or sys.stdout).flush()


def slashes(path):
    return path.replace("\\", "/")


def read_text(path):
    try:
        with open(path, encoding="utf-8") as f:
            return f.read()
    except (OSError, UnicodeDecodeError):
        return None


def file_digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def os_error_text(e):
    return e.strerror or str(e)


# --------------------------------------------------------------------------
# The export list
# --------------------------------------------------------------------------


def parse_entrypoints(text, where=ENTRYPOINTS):
    """The C names of the TARGET_LIBC_ENTRYPOINTS block, in file order."""
    if re.search(r"list\s*\(\s*APPEND\s+%s\b" % ENTRYPOINT_BLOCK, text):
        raise CheckError(
            "%s: list(APPEND %s ...) is not supported; list every entry point in the "
            "set(%s ...) block" % (where, ENTRYPOINT_BLOCK, ENTRYPOINT_BLOCK)
        )
    start = re.compile(r"^\s*set\s*\(\s*%s\s*(.*)$" % ENTRYPOINT_BLOCK)
    names, seen, inside, found = [], {}, False, 0
    for number, raw in enumerate(text.splitlines(), 1):
        if not inside:
            m = start.match(raw)
            if not m:
                continue
            found += 1
            if found > 1:
                raise CheckError("%s:%d: a second set(%s ...)" % (where, number, ENTRYPOINT_BLOCK))
            inside = True
            line = m.group(1)
        else:
            line = raw
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        end = line.endswith(")")
        if end:
            line = line[:-1].strip()
        for entry in line.split():
            if not ENTRY_RE.match(entry):
                raise CheckError(
                    "%s:%d: '%s' is not an entry point of the form libc.src.<area>.<name>"
                    % (where, number, entry)
                )
            name = entry.rsplit(".", 1)[1]
            if name in seen:
                raise CheckError(
                    "%s:%d: %s is listed twice (also on line %d)"
                    % (where, number, name, seen[name])
                )
            seen[name] = number
            names.append(name)
        if end:
            inside = False
    if not found:
        raise CheckError("%s: no set(%s ...) block" % (where, ENTRYPOINT_BLOCK))
    if inside:
        raise CheckError("%s: the set(%s ...) block has no ')'" % (where, ENTRYPOINT_BLOCK))
    if not names:
        raise CheckError("%s: the set(%s ...) block is empty" % (where, ENTRYPOINT_BLOCK))
    return names


def export_names(lines_text, where):
    """The names of an export list's text: blank lines and '#' lines aside,
    CRLF and a UTF-8 byte order mark accepted."""
    names, seen = [], {}
    for number, raw in enumerate(lines_text.lstrip("﻿").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if not NAME_RE.match(line):
            raise CheckError("%s:%d: '%s' is not a C name" % (where, number, line))
        if line in seen:
            raise CheckError(
                "%s:%d: %s is listed twice (also on line %d)" % (where, number, line, seen[line])
            )
        seen[line] = number
        names.append(line)
    return names


def read_list_file(path, what):
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        raise UsageError("cannot read %s %s: %s" % (what, path, os_error_text(e)))
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        raise CheckError("%s %s: not UTF-8" % (what, path))


def generated_exports(repo):
    """(the text of the export list, the names it adds to the checked-in one)."""
    text = read_text(os.path.join(repo, ENTRYPOINTS))
    if text is None:
        raise CheckError("cannot read %s" % ENTRYPOINTS)
    wanted = [n for n in parse_entrypoints(text) if n not in NOT_EXPORTED]
    wanted += [n for n in EXTRA_EXPORTS if n not in wanted]
    current = read_text(os.path.join(repo, EXPORTS))
    kept = export_names(current, EXPORTS) if current is not None else []
    gone = [n for n in kept if n not in wanted]
    if gone:
        raise CheckError(
            "%s names %s, which %s no longer lists; the export list is append-only "
            "(plan 5.19.7): a name is never removed" % (EXPORTS, ", ".join(gone), ENTRYPOINTS)
        )
    added = sorted(n for n in wanted if n not in kept)
    return EXPORTS_HEADER + "".join(n + "\n" for n in kept + added), added


def do_check_exports(repo):
    text, added = generated_exports(repo)
    current = read_text(os.path.join(repo, EXPORTS))
    if current == text:
        say("%s matches %s (%d names)" % (EXPORTS, ENTRYPOINTS, len(export_names(text, EXPORTS))))
        return 0
    if current is None:
        say("error: %s is missing; write it with --write-exports" % EXPORTS, sys.stderr)
    elif added:
        say(
            "error: %s lacks %d name(s) of %s: %s; append them with --write-exports"
            % (EXPORTS, len(added), ENTRYPOINTS, ", ".join(added)),
            sys.stderr,
        )
    else:
        say(
            "error: %s differs from the list that --write-exports writes (its header or "
            "layout)" % EXPORTS,
            sys.stderr,
        )
    return 1


def do_write_exports(repo):
    text, added = generated_exports(repo)
    path = os.path.join(repo, EXPORTS)
    if read_text(path) == text:
        say("%s is up to date" % EXPORTS)
        return 0
    os.makedirs(os.path.dirname(path), exist_ok=True)
    # Written beside it and renamed over it, so a link at that name is
    # replaced by the list, not written through.
    tmp = path + ".tmp"
    remove_quietly(tmp)
    try:
        with open(tmp, "x", encoding="utf-8", newline="\n") as f:
            f.write(text)
        replace_retrying(tmp, path)
    finally:
        remove_quietly(tmp)
    say("wrote %s (%d names appended)" % (EXPORTS, len(added)))
    return 0


def do_check_append_only(old_path, new_path):
    old = export_names(read_list_file(old_path, "OLD"), old_path)
    new = export_names(read_list_file(new_path, "NEW"), new_path)
    for i, name in enumerate(old):
        if i >= len(new):
            say(
                "error: %s drops %s and the %d name(s) after it (an export list is "
                "append-only, plan 5.19.7)" % (new_path, name, len(old) - i - 1),
                sys.stderr,
            )
            return 1
        if new[i] != name:
            where = "removes" if name not in new else "moves"
            say(
                "error: %s %s %s: name %d is %s there, not %s (an export list is "
                "append-only, plan 5.19.7)" % (new_path, where, name, i + 1, new[i], name),
                sys.stderr,
            )
            return 1
    say("%s keeps the %d names of %s and appends %d" % (new_path, len(old), old_path,
                                                        len(new) - len(old)))
    return 0


# --------------------------------------------------------------------------
# Tools, the lock and running commands
# --------------------------------------------------------------------------


class Context:
    """Where the repository, the build and the tools are (the self-test
    substitutes stand-ins)."""

    def __init__(self, repo, build_dir, tools, cmake, ninja, script_file, lock=True):
        self.repo = repo
        self.build_dir = build_dir
        self.tools = tools  # name -> (argv, file)
        self.cmake = cmake  # argv, or None until found
        self.ninja = ninja  # (argv, path for CMAKE_MAKE_PROGRAM), or None until found
        self.script_file = script_file
        self.lock = lock

    def argv(self, tool):
        return list(self.tools[tool][0])

    def path(self, tool):
        return self.tools[tool][1]

    @staticmethod
    def from_build_dir(build_dir, origin=None, missing=UsageError):
        build_dir = os.path.abspath(build_dir)
        tools = {}
        for name in TOOLS:
            path = os.path.join(build_dir, "bin", name + EXE)
            if not os.path.isfile(path):
                raise missing(
                    "%s does not exist (%s)" % (path, origin or "--build-dir " + build_dir)
                )
            tools[name] = ([path], path)
        return Context(REPO, build_dir, tools, None, None, os.path.abspath(__file__))


def build_dir_choice(build_dir):
    """--build-dir, else $MSIL_BUILD_DIR when it is set and not empty, else
    <repository>/build."""
    if build_dir:
        return build_dir, None
    env = os.environ.get("MSIL_BUILD_DIR")
    if env:
        return env, "MSIL_BUILD_DIR=" + env
    return os.path.join(REPO, "build"), None


def find_cmake_ninja(ctx):
    """Fills ctx.cmake and ctx.ninja: on Windows from the Visual Studio
    environment of msil-env.ps1, elsewhere from PATH."""
    if ctx.cmake is not None and ctx.ninja is not None:
        return
    if os.name != "nt":
        cmake, ninja = shutil.which("cmake"), shutil.which("ninja")
    else:
        env_ps1 = os.path.join(ctx.repo, "msil", "utils", "build", "msil-env.ps1")
        shell = shutil.which("pwsh") or shutil.which("powershell")
        if not shell:
            raise UsageError("neither pwsh nor powershell is on PATH, so msil-env.ps1 cannot run")
        command = (
            "$ErrorActionPreference = 'Stop'; . '%s' | Out-Null; "
            "foreach ($t in 'cmake', 'ninja') { $c = Get-Command \"$t.exe\" -CommandType "
            "Application -ErrorAction SilentlyContinue | Select-Object -First 1; "
            "if ($c) { \"$t=$($c.Source)\" } }" % env_ps1.replace("'", "''")
        )
        code, out, err = run_tool([shell, "-NoProfile", "-NonInteractive", "-Command", command],
                                  timeout=300)
        found = dict(
            line.strip().split("=", 1)
            for line in out.splitlines()
            if re.match(r"^(cmake|ninja)=", line.strip())
        )
        if code != 0 and not found:
            raise UsageError("msil-env.ps1 failed: %s" % (err or out).strip()[-500:])
        cmake, ninja = found.get("cmake"), found.get("ninja")
    if not cmake:
        raise UsageError("no cmake found (%s)" % ("Visual Studio environment of msil-env.ps1"
                                                   if os.name == "nt" else "PATH"))
    if not ninja:
        raise UsageError("no ninja found (%s)" % ("Visual Studio environment of msil-env.ps1"
                                                   if os.name == "nt" else "PATH"))
    ctx.cmake = [cmake]
    ctx.ninja = ([ninja], ninja)


def run_tool(argv, cwd=None, timeout=TOOL_TIMEOUT):
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
    return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")


def kill_tree(p):
    """Kills p and, on Windows, the processes it started (ninja's compiles
    would otherwise keep running and keep the output pipe open)."""
    if os.name == "nt" and p.poll() is None:
        try:
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(p.pid)],
                           stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, timeout=60)
        except (OSError, subprocess.TimeoutExpired):
            pass
    try:
        p.kill()
    except OSError:
        pass


def run_streamed(argv, cwd, timeout, log=None):
    """Runs argv with its output copied to stdout (and log); returns the exit
    status, or None when it did not start or timed out.

    A reader thread feeds the output lines to a queue that this thread polls,
    so the deadline holds while the child prints nothing: past it the child
    (and on Windows the processes it started) is killed. Once the child has
    exited, its output is read for STREAM_EOF_GRACE seconds more at most (a
    process it left behind may hold the pipe open)."""
    say("running: " + " ".join(argv))
    deadline = time.monotonic() + timeout
    try:
        p = subprocess.Popen(
            argv, cwd=cwd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
    except OSError as e:
        say("error: cannot run %s: %s" % (argv[0], os_error_text(e)), sys.stderr)
        return None
    lines = queue.Queue()

    def read():
        try:
            for raw in iter(p.stdout.readline, b""):
                lines.put(raw)
        except (OSError, ValueError):
            pass
        finally:
            lines.put(None)

    reader = threading.Thread(target=read, name="build-libc output", daemon=True)
    reader.start()

    def copy(raw):
        line = raw.decode("utf-8", "replace")
        sys.stdout.write(line)
        if log is not None:
            log.append(line)

    timed_out, eof, exited_at = False, False, None
    while True:
        now = time.monotonic()
        if exited_at is None and p.poll() is not None:
            exited_at = now
        if exited_at is not None and (now - exited_at > STREAM_EOF_GRACE or now >= deadline):
            break  # it exited; something it started holds the pipe
        if now >= deadline:
            timed_out = True
            break
        try:
            raw = lines.get(timeout=min(STREAM_POLL, deadline - now))
        except queue.Empty:
            continue
        if raw is None:
            eof = True
            break
        copy(raw)
    if timed_out:
        kill_tree(p)
    reader.join(timeout=STREAM_JOIN if timed_out else 1)
    # Keep what the reader read up to the end (or the kill).
    while not eof:
        try:
            raw = lines.get_nowait()
        except queue.Empty:
            break
        if raw is None:
            eof = True
        else:
            copy(raw)
    sys.stdout.flush()
    if not reader.is_alive():
        p.stdout.close()  # else the reader still holds the pipe's lock
    if not timed_out:
        try:
            return p.wait(timeout=max(1, deadline - time.monotonic()))
        except subprocess.TimeoutExpired:
            kill_tree(p)
    try:
        p.wait(timeout=STREAM_JOIN)
    except subprocess.TimeoutExpired:
        pass
    say("error: %s timed out after %d s" % (argv[0], timeout), sys.stderr)
    return None


def enter_build_lock(build_dir, exclusive=False, wait_seconds=None):
    """Takes msil-lock.ps1's lock on <build_dir>/.msil-build.lock and returns
    a function that releases it.

    Readers open the file for reading and share it for reading only; writers
    (msil-build.ps1, and this script while it builds) open it for reading and
    writing and record who they are, as msil-lock.ps1 does. The system's
    sharing rules allow many readers or one writer. A lock file that is a
    link, or for a writer a hard link to another file, is refused (a usage
    error), so a writer never rewrites another file. Elsewhere than on
    Windows a running executable can be replaced, and there is nothing to
    lock."""
    if os.name != "nt":
        return lambda: None
    if wait_seconds is None:
        wait_seconds = LOCK_WAIT_SECONDS
    import ctypes
    from ctypes import wintypes

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateFileW.restype = wintypes.HANDLE
    kernel32.CreateFileW.argtypes = (
        wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
        wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE,
    )
    kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
    kernel32.WriteFile.argtypes = (
        wintypes.HANDLE, wintypes.LPCVOID, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD),
        wintypes.LPVOID,
    )
    kernel32.SetEndOfFile.argtypes = (wintypes.HANDLE,)

    class FileInfo(ctypes.Structure):  # BY_HANDLE_FILE_INFORMATION
        _fields_ = [("attributes", wintypes.DWORD), ("times", wintypes.DWORD * 6),
                    ("volume", wintypes.DWORD), ("size", wintypes.DWORD * 2),
                    ("links", wintypes.DWORD), ("index", wintypes.DWORD * 2)]

    kernel32.GetFileInformationByHandle.argtypes = (wintypes.HANDLE, ctypes.POINTER(FileInfo))
    generic_read, generic_write, file_share_read = 0x80000000, 0x40000000, 0x1
    open_always, error_sharing_violation = 4, 32
    # FILE_FLAG_OPEN_REPARSE_POINT: a link planted after the check below is
    # opened itself, not followed, and refused by its attributes.
    flags = 0x80 | 0x00200000  # FILE_ATTRIBUTE_NORMAL, FILE_FLAG_OPEN_REPARSE_POINT
    invalid = ctypes.c_void_p(-1).value
    os.makedirs(build_dir, exist_ok=True)
    path = os.path.join(build_dir, LOCK_FILE)
    if is_link(path):
        # A writer truncates and rewrites the lock file, never another file.
        raise UsageError("%s is a link (a symbolic link or junction), not the lock file; "
                         "remove it" % path)
    access = generic_read | (generic_write if exclusive else 0)
    deadline = time.monotonic() + wait_seconds
    announced = False
    while True:
        handle = kernel32.CreateFileW(
            path, access, file_share_read, None, open_always, flags, None
        )
        if handle not in (None, invalid):
            break
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
    info = FileInfo()
    if not kernel32.GetFileInformationByHandle(handle, ctypes.byref(info)):
        error = ctypes.get_last_error()
        kernel32.CloseHandle(handle)
        raise UsageError("cannot read %s: %s" % (path, ctypes.FormatError(error).strip()))
    why = None
    if info.attributes & 0x400:  # FILE_ATTRIBUTE_REPARSE_POINT
        why = "is a link (a symbolic link or junction), not the lock file"
    elif exclusive and info.links > 1:
        # A writer would truncate and rewrite the file the other name reaches.
        why = "is a hard link to another file, not the lock file"
    if why:
        kernel32.CloseHandle(handle)
        raise UsageError("%s %s; remove it" % (path, why))
    if exclusive:
        record = ("pid=%d\nstarted=%s\nwhat=%s\n" % (
            os.getpid(), time.strftime("%Y-%m-%dT%H:%M:%S"), PROG)).encode("utf-8")
        written = wintypes.DWORD(0)
        kernel32.WriteFile(handle, record, len(record), ctypes.byref(written), None)
        kernel32.SetEndOfFile(handle)
    return lambda: kernel32.CloseHandle(handle)


# --------------------------------------------------------------------------
# The stamp
# --------------------------------------------------------------------------


def source_files(repo):
    """The repository-relative files a build depends on (SOURCE_DIRS without
    SOURCE_SKIP and Python caches), sorted."""
    out = []
    for top in SOURCE_DIRS:
        root = os.path.join(repo, top)
        for dirpath, dirnames, filenames in os.walk(root):
            rel = slashes(os.path.relpath(dirpath, repo))
            dirnames[:] = sorted(
                d for d in dirnames
                if d != "__pycache__" and (rel + "/" + d) not in SOURCE_SKIP
            )
            for name in filenames:
                if not name.endswith(".pyc"):
                    out.append(rel + "/" + name)
    return sorted(out)


def source_digest(repo):
    h = hashlib.sha256()
    for rel in source_files(repo):
        digest = file_digest(os.path.join(repo, rel))
        h.update(rel.encode("utf-8") + b"\0" + digest.encode() + b"\n")
    return h.hexdigest()


def tree_digest(root):
    """A digest of every file under root: its root-relative path ('/'-separated)
    and SHA-256, in sorted order."""
    files = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        rel = os.path.relpath(dirpath, root)
        for name in filenames:
            files.append(slashes(os.path.normpath(os.path.join(rel, name))))
    h = hashlib.sha256()
    for rel in sorted(files):
        h.update(rel.encode("utf-8") + b"\0" + file_digest(os.path.join(root, rel)).encode()
                 + b"\n")
    return h.hexdigest()


def resource_headers(ctx, missing):
    """The include/ directory of clang's resource directory: the freestanding
    headers (stddef.h, stdarg.h and their helpers) that llvm-libc's
    -ffreestanding -nostdlibinc compiles include and that clang's own digest
    does not cover."""
    code, out, err = run_tool(ctx.argv("clang") + ["--target=" + TRIPLE, "-print-resource-dir"])
    path = out.strip()
    if code != 0 or not path:
        raise missing("clang -print-resource-dir failed: %s" % err.strip()[:300])
    inc = os.path.join(path, "include")
    if not os.path.isdir(inc):
        raise missing("clang's resource directory %s has no include/ (build the clang target)"
                      % slashes(path))
    return inc


def helper_archive(ctx, missing=UsageError):
    code, out, err = run_tool(
        ctx.argv("clang") + ["--target=" + TRIPLE, "-rtlib=compiler-rt", "-print-libgcc-file-name"]
    )
    path = out.strip()
    if code != 0 or not path:
        raise missing("clang -print-libgcc-file-name failed: %s" % err.strip()[:300])
    if not os.path.isfile(path):
        raise missing(
            "the portable helper archive %s does not exist (build it with 'python "
            "msil/utils/build/build-builtins.py --triple %s', plan 5.18.8)"
            % (slashes(path), TRIPLE)
        )
    return path


def configure_variables(ctx):
    """The -D variables of the libc tree's configure, in order."""
    flags = "-fvisibility=hidden"
    p = lambda tool: slashes(ctx.path(tool))
    return [
        ("LLVM_ENABLE_RUNTIMES", "libc"),
        ("LLVM_LIBC_FULL_BUILD", "ON"),
        ("LIBC_TARGET_TRIPLE", TRIPLE),
        ("LLVM_DEFAULT_TARGET_TRIPLE", TRIPLE),
        ("CMAKE_BUILD_TYPE", "Release"),
        ("CMAKE_SYSTEM_NAME", "Generic"),
        ("CMAKE_TRY_COMPILE_TARGET_TYPE", "STATIC_LIBRARY"),
        ("CMAKE_C_COMPILER", p("clang")),
        ("CMAKE_CXX_COMPILER", p("clang++")),
        ("CMAKE_ASM_COMPILER", p("clang")),
        ("CMAKE_C_COMPILER_TARGET", TRIPLE),
        ("CMAKE_CXX_COMPILER_TARGET", TRIPLE),
        ("CMAKE_ASM_COMPILER_TARGET", TRIPLE),
        ("CMAKE_AR", p("llvm-ar")),
        ("CMAKE_RANLIB", p("llvm-ranlib")),
        ("CMAKE_LINKER", p("msil-ld")),
        ("CMAKE_MAKE_PROGRAM", slashes(ctx.ninja[1])),
        ("CMAKE_C_FLAGS", flags),
        ("CMAKE_CXX_FLAGS", flags),
        ("CMAKE_CXX_SCAN_FOR_MODULES", "OFF"),
        ("LLVM_INCLUDE_TESTS", "OFF"),
        ("LLVM_INCLUDE_DOCS", "OFF"),
        ("LLVM_ENABLE_PER_TARGET_RUNTIME_DIR", "OFF"),
        ("Python3_EXECUTABLE", slashes(sys.executable)),
    ]


def configure_argv(ctx, tree):
    argv = list(ctx.cmake) + ["-G", "Ninja", "-S", slashes(os.path.join(ctx.repo, "runtimes")),
                              "-B", slashes(tree)]
    return argv + ["-D%s=%s" % kv for kv in configure_variables(ctx)]


def cmake_version(ctx, missing):
    code, out, err = run_tool(list(ctx.cmake) + ["--version"])
    first = out.strip().splitlines()[0] if out.strip() else ""
    if code != 0 or not first:
        raise missing("'cmake --version' failed: %s" % (err or out).strip()[:300])
    return first


def build_relative(ctx, path):
    """path relative to the build directory when it is inside it, else
    absolute ('/'-separated)."""
    try:
        rel = os.path.relpath(path, ctx.build_dir)
    except ValueError:  # another drive (Windows)
        return slashes(os.path.abspath(path))
    return slashes(os.path.abspath(path) if rel.startswith("..") else rel)


def input_stamp(ctx, missing):
    """The stamp's input lines (everything but the installed files)."""
    find_cmake_ninja(ctx)
    builtins = helper_archive(ctx, missing)
    exports = os.path.join(ctx.repo, EXPORTS)
    if not os.path.isfile(exports):
        raise CheckError("%s is missing; write it with --write-exports" % EXPORTS)
    build = slashes(os.path.abspath(ctx.build_dir))
    lines = [
        "%s stamp %d" % (PROG, STAMP_VERSION),
        "triple %s" % TRIPLE,
        "assembly-version %s" % ASSEMBLY_VERSION,
        "references none",
        "script %s" % file_digest(ctx.script_file),
    ]
    for name in TOOLS:
        lines.append("tool %s %s" % (name, file_digest(ctx.path(name))))
    lines += [
        "cmake %s" % cmake_version(ctx, missing),
        "builtins %s %s" % (build_relative(ctx, builtins), file_digest(builtins)),
        "exports %s" % file_digest(exports),
        "sources %s" % source_digest(ctx.repo),
        "resource-headers %s" % tree_digest(resource_headers(ctx, missing)),
    ]
    for k, v in configure_variables(ctx):
        lines.append("configure %s=%s" % (k, v.replace(build, "<build>")))
    return lines, builtins


def installed_files(sysroot):
    """The sysroot-relative files a build installs, sorted (the headers, c.dll,
    libm.a); the stamp is not one."""
    out = []
    inc = os.path.join(sysroot, "include")
    for dirpath, dirnames, filenames in os.walk(inc):
        dirnames.sort()
        for name in filenames:
            out.append(slashes(os.path.relpath(os.path.join(dirpath, name), sysroot)))
    for name in ("c.dll", "libm.a"):
        if os.path.isfile(os.path.join(sysroot, LIB_DIR, name)):
            out.append(LIB_DIR + "/" + name)
    return sorted(out)


def output_lines(sysroot):
    return ["output %s %s" % (rel, file_digest(os.path.join(sysroot, rel)))
            for rel in installed_files(sysroot)]


def stamp_path(sysroot):
    return os.path.join(sysroot, LIB_DIR, "c.dll.stamp")


def archive_path(build_dir):
    """The intermediate libc.a (never installed before P-3)."""
    return os.path.join(build_dir, LIBC_TREE, LIBC_ARCHIVE)


def archive_line(build_dir):
    """The stamp's line for the intermediate libc.a, taken after
    add_provider."""
    return "archive %s" % file_digest(archive_path(build_dir))


def missing_outputs(build_dir):
    sysroot = os.path.join(build_dir, SYSROOT)
    lib = os.path.join(sysroot, LIB_DIR)
    out = [p for p in (os.path.join(lib, "c.dll"), os.path.join(lib, "libm.a"),
                       os.path.join(sysroot, "include", "stdio.h"), stamp_path(sysroot),
                       archive_path(build_dir))
           if not os.path.isfile(p)]
    return out


def stamp_state(build_dir, inputs):
    """None when libc.a and the installed files match the stamp a build would
    write, else what differs."""
    sysroot = os.path.join(build_dir, SYSROOT)
    gone = missing_outputs(build_dir)
    if gone:
        return "missing: " + ", ".join(slashes(p) for p in gone)
    recorded = (read_text(stamp_path(sysroot)) or "").splitlines()
    want_inputs = [l for l in recorded if not l.startswith(("output ", "archive "))]
    if want_inputs != inputs:
        changed = [l.split(" ", 1)[0] for l in inputs if l not in want_inputs]
        changed += [l.split(" ", 1)[0] for l in want_inputs if l not in inputs]
        return "stale (stamp differs: %s)" % ", ".join(sorted(set(changed)) or ["order"])
    if [l for l in recorded if l.startswith("archive ")] != [archive_line(build_dir)]:
        return "the intermediate archive changed: %s" % slashes(archive_path(build_dir))
    want_outputs = [l for l in recorded if l.startswith("output ")]
    have = output_lines(sysroot)
    if want_outputs != have:
        differ = sorted(set(l.split(" ")[1] for l in set(want_outputs) ^ set(have)))
        return "an installed file changed: " + ", ".join(differ)
    return None


# --------------------------------------------------------------------------
# The build
# --------------------------------------------------------------------------


def replace_retrying(src, dst, replace=os.replace):
    """os.replace, retried for a few hundred milliseconds on a
    PermissionError (Windows: another process has dst open)."""
    for delay in REPLACE_RETRY_DELAYS + (None,):
        try:
            replace(src, dst)
            return
        except PermissionError:
            if delay is None:
                raise
            time.sleep(delay)


def remove_quietly(path):
    try:
        os.remove(path)
    except FileNotFoundError:
        pass
    except OSError:
        return False
    return True


def configure(ctx, tree):
    """Configures the libc tree unless it is configured with the same
    command (recorded in <tree>/build-libc.configure)."""
    argv = configure_argv(ctx, tree)
    record = os.path.join(tree, "build-libc.configure")
    text = "\n".join(argv) + "\n"
    if os.path.isfile(os.path.join(tree, "CMakeCache.txt")) and read_text(record) == text:
        say("configure: %s is configured" % slashes(tree))
        return
    remove_quietly(record)
    os.makedirs(tree, exist_ok=True)
    code = run_streamed(argv, ctx.repo, CONFIGURE_TIMEOUT)
    if code != 0:
        raise CheckError("configuring llvm-libc for %s failed (exit %s)" % (TRIPLE, code))
    remove_quietly(record)
    with open(record, "x", encoding="utf-8", newline="\n") as f:
        f.write(text)


def provider_objects(tree):
    """The errno provider's objects, sorted."""
    d = os.path.join(tree, PROVIDER_DIR)
    out = []
    for dirpath, dirnames, filenames in os.walk(d):
        dirnames.sort()
        for name in filenames:
            if name.endswith((".obj", ".o")):
                out.append(os.path.join(dirpath, name))
    if not out:
        raise CheckError(
            "no object of %s under %s: the EXTERNAL errno provider __llvm_libc_errno "
            "(libc/src/__support/OSUtil/dotnet) was not built" % (PROVIDER_TARGET, slashes(d))
        )
    return sorted(out)


def add_provider(ctx, tree):
    """Adds the provider's objects to libc.a as dotnet_util.<object> (llvm-ar
    replaces a member of that name, so a rerun changes nothing)."""
    archive = os.path.join(tree, LIBC_ARCHIVE)
    if not os.path.isfile(archive):
        raise CheckError("the libc build wrote no %s" % slashes(archive))
    objs = provider_objects(tree)
    tmp = tempfile.mkdtemp(prefix=".build-libc-", dir=os.path.dirname(archive))
    try:
        names = []
        for o in objs:
            name = PROVIDER_PREFIX + os.path.basename(o)
            shutil.copyfile(o, os.path.join(tmp, name))
            names.append(name)
        code, out, err = run_tool(ctx.argv("llvm-ar") + ["rsD", archive] + names, cwd=tmp)
        if code != 0:
            raise CheckError("llvm-ar could not add %s to %s: %s"
                             % (", ".join(names), slashes(archive), (out + err).strip()[:500]))
    finally:
        remove_tree(tmp, ignore_errors=True)
    say("libc.a: added the errno provider (%s)" % ", ".join(PROVIDER_PREFIX + os.path.basename(o)
                                                            for o in objs))
    return archive


def link_cdll(ctx, archive, builtins, staging):
    """Links staging/c.dll; returns its path."""
    out = os.path.join(staging, "c.dll")
    argv = ctx.argv("msil-ld") + [
        "--target=" + TRIPLE,
        "--shared",
        "--export-list=" + os.path.join(ctx.repo, EXPORTS),
        "--exclude-libs=ALL",
        "--assembly-version=" + ASSEMBLY_VERSION,
        archive,
        builtins,
        "-o",
        out,
    ]
    say("running: " + " ".join(argv))
    code, sout, err = run_tool(argv, cwd=staging)
    text = (sout + err).strip()
    if text:
        print(text)
    if code != 0 or not os.path.isfile(out):
        raise CheckError("linking c.dll failed (exit %s)" % code)
    return out


def install_headers(ctx, tree, staging):
    """Installs the libc-headers component into staging; returns
    staging/include."""
    argv = list(ctx.cmake) + ["--install", slashes(tree), "--component", HEADER_COMPONENT,
                              "--prefix", slashes(staging)]
    code = run_streamed(argv, ctx.repo, TOOL_TIMEOUT)
    inc = os.path.join(staging, "include")
    if code != 0 or not os.path.isfile(os.path.join(inc, "stdio.h")):
        raise CheckError("installing the libc headers failed (exit %s)" % code)
    return inc


def install(sysroot, staged_include, staged_cdll):
    """Puts the headers, c.dll and libm.a into the sysroot."""
    lib = os.path.join(sysroot, LIB_DIR)
    os.makedirs(lib, exist_ok=True)
    inc = os.path.join(sysroot, "include")
    old = None
    if os.path.exists(inc):
        old = tempfile.mkdtemp(prefix=".build-libc-old-", dir=sysroot)
        replace_retrying(inc, os.path.join(old, "include"))
    replace_retrying(staged_include, inc)
    if old:
        remove_tree(old, ignore_errors=True)
    # Created anew ('x'), so a link left at that name is replaced, not written
    # through.
    tmp_m = os.path.join(lib, ".libm.a.tmp")
    remove_quietly(tmp_m)
    with open(tmp_m, "xb") as f:
        f.write(EMPTY_ARCHIVE)
    replace_retrying(tmp_m, os.path.join(lib, "libm.a"))
    replace_retrying(staged_cdll, os.path.join(lib, "c.dll"))


def do_build(ctx, jobs):
    sysroot = os.path.join(ctx.build_dir, SYSROOT)
    tree = os.path.join(ctx.build_dir, LIBC_TREE)
    text, added = generated_exports(ctx.repo)
    if read_text(os.path.join(ctx.repo, EXPORTS)) != text:
        raise CheckError(
            "%s differs from the list that %s gives%s; update it with --write-exports first"
            % (EXPORTS, ENTRYPOINTS, " (it lacks %s)" % ", ".join(added) if added else "")
        )
    for name in TOOLS:
        code, _, err = run_tool(ctx.argv(name) + ["--version"])
        if code != 0:
            raise UsageError("'%s --version' failed: %s" % (ctx.path(name), err.strip()[:300]))
    inputs, builtins = input_stamp(ctx, UsageError)
    if stamp_state(ctx.build_dir, inputs) is None:
        say("up to date: %s" % slashes(os.path.join(sysroot, LIB_DIR, "c.dll")))
        return 0
    start = time.monotonic()
    os.makedirs(os.path.join(sysroot, LIB_DIR), exist_ok=True)
    stamp = stamp_path(sysroot)
    if not remove_quietly(stamp):
        raise CheckError("cannot remove the old stamp %s" % slashes(stamp))
    staging = tempfile.mkdtemp(prefix=".build-libc-", dir=sysroot)
    ok = False
    try:
        configure(ctx, tree)
        code = run_streamed(ctx.ninja[0] + ["-C", slashes(tree), "-j", str(jobs), "all",
                                            PROVIDER_TARGET], ctx.repo, BUILD_TIMEOUT)
        if code != 0:
            raise CheckError("building llvm-libc for %s failed (exit %s)" % (TRIPLE, code))
        archive = add_provider(ctx, tree)
        archived = archive_line(ctx.build_dir)
        cdll = link_cdll(ctx, archive, builtins, staging)
        inc = install_headers(ctx, tree, os.path.join(staging, "headers"))
        install(sysroot, inc, cdll)
        tmp = os.path.join(staging, "stamp.tmp")
        with open(tmp, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(inputs + [archived] + output_lines(sysroot)) + "\n")
        replace_retrying(tmp, stamp)
        ok = True
    except OSError as e:
        raise CheckError("%s: %s" % (e.filename, os_error_text(e)) if e.filename else str(e))
    finally:
        remove_tree(staging, ignore_errors=True)
        if not ok:
            # A link must fail loudly rather than take a stale c.dll.
            remove_quietly(stamp)
            cdll_path = os.path.join(sysroot, LIB_DIR, "c.dll")
            if os.path.exists(cdll_path) and not remove_quietly(cdll_path):
                say("error: %s could not be removed and stays, stale and without its stamp, "
                    "so --check reports it: remove it once it is no longer in use"
                    % slashes(cdll_path), sys.stderr)
    cdll = os.path.join(sysroot, LIB_DIR, "c.dll")
    say("c.dll: %d bytes, %.1f s: %s" % (os.path.getsize(cdll), time.monotonic() - start,
                                         slashes(cdll)))
    return 0


def do_check(ctx):
    sysroot = os.path.join(ctx.build_dir, SYSROOT)
    gone = missing_outputs(ctx.build_dir)
    if gone:
        say("error: missing: %s (build them with build-libc.py)"
            % ", ".join(slashes(p) for p in gone), sys.stderr)
        return 1
    inputs, _ = input_stamp(ctx, CheckError)
    state = stamp_state(ctx.build_dir, inputs)
    if state is not None:
        say("error: %s: %s" % (slashes(os.path.join(sysroot, LIB_DIR, "c.dll")), state),
            sys.stderr)
        return 1
    say("up to date: %s" % slashes(os.path.join(sysroot, LIB_DIR, "c.dll")))
    return 0


def inside(path, top):
    """Whether path is top or lies under it (both absolute)."""
    path, top = os.path.normcase(path), os.path.normcase(top)
    try:
        return os.path.commonpath([path, top]) == top
    except ValueError:  # another drive (Windows)
        return False


# A Windows path with an extended-length or device prefix (\\?\, \\.\, \??\)
# before a drive (group 2) or UNC\server\share (group 1): the same file as
# the plain path, which os.path.abspath and normcase leave apart.
PREFIXED_RE = re.compile(r"^(?:\\\\[?.]\\|\\\?\?\\)(?:(UNC\\)|([A-Za-z]:(?:\\|$)))", re.I)


def plain_path(path):
    """path without an extended-length or device prefix (Windows); any other
    path unchanged."""
    if os.name != "nt":
        return path
    p = path.replace("/", "\\")
    m = PREFIXED_RE.match(p)
    if not m:
        return path
    return ("\\\\" + p[m.end(1):]) if m.group(1) else p[m.start(2):]


def path_forms(path):
    """The absolute, case-foldable forms of path that reproducible_dir
    compares: as written, without an extended-length prefix, and resolved by
    os.path.realpath (symbolic links, junctions, 8.3 names)."""
    forms = []
    for p in (path, plain_path(path)):
        resolved = []
        try:
            resolved.append(os.path.realpath(p))
        except (OSError, ValueError):
            pass
        for q in [p] + resolved:
            q = os.path.abspath(plain_path(q))
            if q not in forms:
                forms.append(q)
    return forms


def ancestors(path):
    """path and each directory above it, up to its root."""
    while True:
        yield path
        parent = os.path.dirname(path)
        if parent == path:
            return
        path = parent


def file_id(path):
    """(st_dev, st_ino) of the file that path reaches after the system follows
    every link, junction, mount point, subst drive and UNC share, or None
    (none there, or a file system that gives no file ID)."""
    try:
        st = os.stat(path)
    except (OSError, ValueError):
        return None
    return (st.st_dev, st.st_ino) if st.st_ino else None


# The names the relink writes in OUT (matched whole and case-sensitively):
# c.dll, and the temporary file c.dll.tmp<seven lowercase hex digits> that
# msil-ld's output buffer (llvm::FileOutputBuffer) creates beside it and
# renames to c.dll or removes, so only a killed msil-ld leaves one. msil-ld
# writes nothing else beside its -o output (a map file or saved temporaries
# only with -Map or --save-temps).
RELINK_NAME_RE = re.compile(r"c\.dll(?:\.tmp[0-9a-f]{7})?")
# The file --check-reproducible writes in OUT once it emptied it, before the
# relink. A later run replaces a c.dll only beside it, so a directory that
# holds a c.dll no relink wrote (a lit output directory of the build that a
# link moved elsewhere) is refused.
RELINK_MARK = ".build-libc-relink"


def relink_inputs(build_dir, repo, tool_files, builtins=None):
    """The files --check-reproducible reads or compares with: the installed
    c.dll, the intermediate libc.a, the export list, the tools (clang names
    the helper archive, msil-ld links, and a mismatch's stamp check reads them
    all) and, once clang named it, the helper archive."""
    return [os.path.join(build_dir, SYSROOT, LIB_DIR, "c.dll"), archive_path(build_dir),
            os.path.join(repo, EXPORTS)] + list(tool_files) + ([builtins] if builtins else [])


def reproducible_dir(out_dir, build_dir, repo, inputs):
    """The absolute OUT of --check-reproducible, refused (a usage error) where
    emptying it could remove anything but what an earlier run wrote there, or
    the relink would replace a file it compares with: a file system root or a
    mount point (a whole file system), a non-directory, a directory that is
    or holds the repository or the build directory or lies in either, the
    directory that really holds one of inputs (relink_inputs; a file link is
    resolved), a directory holding anything but RELINK_MARK and the files the
    relink writes (RELINK_NAME_RE), all regular files (so another file, a
    directory or a link is refused), and one holding such a file without
    RELINK_MARK. So a directory of the build or the repository that a link
    moved to another place is refused without looking for links: it holds
    other names, or a c.dll without the marker, or nothing to remove.

    OUT is emptied through its absolute path, so that path and OUT as written
    are checked, each in every form of path_forms (paths compared case-folded)
    and by file identity: OUT lies in the repository or the build directory
    when the file ID of any directory above one of its forms is theirs, holds
    one when its own file ID is that of a directory above one of their forms,
    and holds an input when one of its regular files has the input's file ID
    (os.stat follows a file link; a path form can lose a trailing dot or
    space or name a UNC share, a file ID cannot). The system resolves a file
    ID, so a junction, a symbolic link, an extended-length or 8.3 name, a
    subst drive or a UNC loopback share (\\\\localhost\\D$) is refused like
    the directory it reaches.

    On Windows an OUT with a name in its path that ends in a dot or a space
    is refused: a Windows path drops them, and os.path.abspath does even
    after a \\\\?\\ prefix, so the absolute path could be a sibling of the
    OUT named."""
    dotted = [c for c in re.split(r"[\\/]", plain_path(out_dir))
              if c not in (".", "..") and c.endswith((".", " "))] if os.name == "nt" else []
    out = os.path.abspath(out_dir)
    outs = path_forms(out_dir)
    outs += [f for f in path_forms(out) if f not in outs]
    out_id = file_id(out)
    above_out = {i for f in outs for i in map(file_id, ancestors(f)) if i}
    tops = path_forms(build_dir) + path_forms(repo)

    def holds_input(f):
        """Whether a path form of OUT is a form of the directory that holds f
        as written or after os.path.realpath (f a file link)."""
        dirs = path_forms(os.path.dirname(os.path.abspath(f)))
        dirs += path_forms(os.path.dirname(os.path.realpath(f)))
        return any(os.path.normcase(d) == os.path.normcase(o) for d in dirs for o in outs)

    mounts = mount_points()
    why = None
    if dotted:
        why = "has a name in its path that ends in a dot or a space ('%s'), which a " \
              "Windows path drops" % dotted[0]
    elif any(os.path.dirname(f) == f or is_mount_point(f, mounts) for f in [out_dir] + outs):
        why = "is a file system root"
    elif os.path.exists(out) and not os.path.isdir(out):
        why = "is not a directory"
    elif any(inside(t, f) for t in tops for f in outs) or (out_id and out_id in {
            i for t in tops for i in map(file_id, ancestors(t))}):
        why = "is or holds the repository or the build directory"
    elif any(inside(f, t) for t in tops for f in outs) or not above_out.isdisjoint(
            map(file_id, tops)):
        why = "lies in the repository or the build directory"
    else:
        held = [f for f in inputs if holds_input(f)]
        names = stray = []
        if not held and os.path.isdir(out):
            try:
                names = sorted(os.listdir(out))
            except OSError as e:
                raise UsageError("--check-reproducible %s: cannot list it: %s"
                                 % (out_dir, os_error_text(e)))
            stray = [n for n in names if not (
                (n == RELINK_MARK or RELINK_NAME_RE.fullmatch(n))
                and is_plain_file(os.path.join(out, n)))]
            # An input whose real file is in OUT is one of its regular files.
            ids = {i: f for i, f in ((file_id(f), f) for f in inputs) if i}
            held = [ids[i] for i in (file_id(os.path.join(out, n)) for n in names
                                     if n not in stray) if i in ids]
        if held:
            why = "really holds %s, a file the relink reads or compares with" % slashes(held[0])
        elif stray:
            why = ("holds %s, which is not a file the relink writes; give an empty or new "
                   "directory" % stray[0])
        elif names and RELINK_MARK not in names:
            why = ("holds %s but not %s, so no relink wrote it; remove it or give an empty "
                   "or new directory" % (names[0], RELINK_MARK))
    if why:
        raise UsageError("--check-reproducible %s: the directory to empty %s" % (out_dir, why))
    return out


def is_plain_file(path):
    """Whether path is a regular file itself, not a link or a directory."""
    try:
        return stat.S_ISREG(os.lstat(path).st_mode) and not is_link(path)
    except OSError:
        return False


def is_link(path):
    """Whether path is a symbolic link or, on Windows, a junction or another
    name-surrogate reparse point (one that redirects to another place)."""
    if os.path.islink(path):
        return True
    if os.name != "nt":
        return False
    try:
        tag = os.lstat(path).st_reparse_tag
    except (OSError, AttributeError):
        return False
    return bool(tag & 0x20000000)  # IsReparseTagNameSurrogate


def mount_points():
    """The mount points /proc/self/mountinfo lists (Linux; a bind mount of a
    directory on the same file system, which os.path.ismount misses), else
    none."""
    try:
        with open("/proc/self/mountinfo") as f:
            lines = f.read().splitlines()
    except OSError:
        return frozenset()
    octal = re.compile(r"\\([0-7]{3})")  # mountinfo writes a space as \040
    return frozenset(octal.sub(lambda m: chr(int(m.group(1), 8)), l.split()[4])
                     for l in lines if len(l.split()) > 4)


def is_mount_point(path, mounts=frozenset()):
    """Whether path is a mount point: on Windows a mount-point reparse point
    whose target is a volume (\\\\?\\Volume{...}\\, which mountvol makes; a
    junction has the same tag but a directory target), elsewhere a directory
    that os.path.ismount or mounts names."""
    if os.name == "nt":
        try:
            if os.lstat(path).st_reparse_tag != 0xA0000003:  # IO_REPARSE_TAG_MOUNT_POINT
                return False
            return os.readlink(path).lower().startswith("\\\\?\\volume{")
        except (OSError, ValueError, AttributeError):
            return False
    if os.path.islink(path):
        return False
    return os.path.ismount(path) or (bool(mounts) and os.path.realpath(path) in mounts)


def remove_tree(path, ignore_errors=False, mounts=None):
    """Removes path: a link itself (never what it reaches), a directory with
    everything in it, or a file. shutil.rmtree follows a junction on some
    Python versions; this never follows one. A mount point (a bind mount, a
    volume mounted on a folder) stops it with an OSError before anything
    below it goes."""
    if mounts is None:
        mounts = mount_points()
    try:
        if is_mount_point(path, mounts):
            raise OSError("%s is a mount point; unmount it" % path)
        if is_link(path):
            try:
                os.unlink(path)
            except OSError:
                os.rmdir(path)  # a directory junction
        elif os.path.isdir(path):
            for name in os.listdir(path):
                remove_tree(os.path.join(path, name), ignore_errors, mounts)
            os.rmdir(path)
        else:
            os.remove(path)
    except OSError:
        if not ignore_errors:
            raise


def empty_dir(path):
    """Creates path, or removes everything in it; a link in it goes, never
    what it reaches, and a mount point in it stops it (an OSError)."""
    os.makedirs(path, exist_ok=True)
    mounts = mount_points()
    for name in os.listdir(path):
        remove_tree(os.path.join(path, name), mounts=mounts)


def do_check_reproducible(ctx, out_arg):
    """Relinks c.dll into OUT (out_arg as given) as a build links it and
    compares it with the installed one."""
    sysroot = os.path.join(ctx.build_dir, SYSROOT)
    installed = os.path.join(sysroot, LIB_DIR, "c.dll")
    archive = archive_path(ctx.build_dir)
    gone = [p for p in (installed, archive) if not os.path.isfile(p)]
    if gone:
        say("error: missing: %s (build them with build-libc.py)"
            % ", ".join(slashes(p) for p in gone), sys.stderr)
        return 1
    builtins = helper_archive(ctx, CheckError)
    # main() checked OUT before it waited for the build lock, which can take
    # hours; OUT may have changed since, so check it again just before
    # emptying it, now also against the helper archive that clang named.
    out_dir = reproducible_dir(out_arg, ctx.build_dir, ctx.repo, relink_inputs(
        ctx.build_dir, ctx.repo, [ctx.path(n) for n in TOOLS], builtins))
    try:
        empty_dir(out_dir)
    except OSError as e:
        raise CheckError("cannot empty %s: %s" % (slashes(out_dir), os_error_text(e)))
    mark = os.path.join(out_dir, RELINK_MARK)
    try:
        with open(mark, "x") as f:
            f.write("build-libc.py --check-reproducible relinks c.dll here and empties this "
                    "directory before each relink\n")
    except OSError as e:
        raise CheckError("cannot write %s: %s" % (slashes(mark), os_error_text(e)))
    relinked = link_cdll(ctx, archive, builtins, out_dir)
    try:
        same = os.path.samefile(relinked, installed)
    except OSError:
        same = False
    if same:  # a relink compared with itself would pass
        raise UsageError("--check-reproducible %s: the relinked c.dll is the installed %s itself"
                         % (out_arg, slashes(installed)))
    want, got = file_digest(installed), file_digest(relinked)
    if want == got:
        say("reproducible: %s relinks byte-identically (SHA-256 %s)" % (slashes(installed), want))
        return 0
    say("error: the relinked %s differs from the installed %s"
        % (slashes(relinked), slashes(installed)), sys.stderr)
    say("installed SHA-256 %s" % want, sys.stderr)
    say("relinked  SHA-256 %s" % got, sys.stderr)
    try:
        state = stamp_state(ctx.build_dir, input_stamp(ctx, CheckError)[0])
    except (CheckError, UsageError, OSError):
        state = None
    if state is not None:
        say("note: the installed files are not current (%s); rebuild, then check again"
            % state, sys.stderr)
    return 1


# --------------------------------------------------------------------------
# Command line
# --------------------------------------------------------------------------


class Parser(argparse.ArgumentParser):
    def error(self, message):
        raise UsageError(message)


def parse_args(argv):
    ap = Parser(prog=PROG, description=__doc__.splitlines()[0])
    ap.add_argument("--build-dir", metavar="DIR",
                    help="the build whose tools a build and --check use (default: "
                    "$MSIL_BUILD_DIR when it is set and not empty, else <repository>/build)")
    ap.add_argument("--jobs", metavar="N", type=int,
                    help="ninja jobs (default: one per processor)")
    ap.add_argument("--check", action="store_true",
                    help="build nothing; exit 1 unless the installed files are current")
    ap.add_argument("--check-reproducible", metavar="OUT",
                    help="build nothing; relink c.dll into the emptied directory OUT (new, "
                    "empty or holding only what an earlier --check-reproducible wrote there: "
                    "%s, c.dll, c.dll.tmp<7 lowercase hex digits>) and exit 1 unless it is "
                    "byte-identical to the installed one" % RELINK_MARK)
    ap.add_argument("--check-exports", action="store_true",
                    help="exit 1 unless the checked-in export list equals the generated one")
    ap.add_argument("--write-exports", action="store_true",
                    help="write the export list from entrypoints.txt")
    ap.add_argument("--check-append-only", nargs=2, metavar=("OLD", "NEW"),
                    help="exit 1 unless export list NEW only appends to OLD")
    ap.add_argument("--self-test", action="store_true",
                    help="check this script offline, with stand-ins")
    args = ap.parse_args(argv)
    modes = [name for name, on in (
        ("--check", args.check),
        ("--check-reproducible", args.check_reproducible is not None),
        ("--check-exports", args.check_exports),
        ("--write-exports", args.write_exports),
        ("--check-append-only", args.check_append_only is not None),
        ("--self-test", args.self_test),
    ) if on]
    if len(modes) > 1:
        raise UsageError("%s cannot be combined" % " and ".join(modes))
    mode = modes[0] if modes else "build"
    if args.jobs is not None and (args.jobs < 1 or mode != "build"):
        raise UsageError("--jobs must be at least 1" if args.jobs < 1
                         else "--jobs goes with a build")
    if args.build_dir is not None and mode not in ("build", "--check", "--check-reproducible"):
        raise UsageError("--build-dir goes with a build, --check or --check-reproducible")
    if mode == "--check-reproducible" and not args.check_reproducible.strip():
        raise UsageError("--check-reproducible needs a directory")
    return args, mode


def main(argv=None, ctx=None):
    try:
        args, mode = parse_args(sys.argv[1:] if argv is None else argv)
        if mode == "--self-test":
            return self_test()
        repo = ctx.repo if ctx else REPO
        if mode == "--check-exports":
            return do_check_exports(repo)
        if mode == "--write-exports":
            return do_write_exports(repo)
        if mode == "--check-append-only":
            return do_check_append_only(*args.check_append_only)
        if ctx is None:
            build_dir, origin = build_dir_choice(args.build_dir)
            tools = [os.path.join(build_dir, "bin", n + EXE) for n in TOOLS]
        else:
            build_dir, tools = ctx.build_dir, [ctx.path(n) for n in TOOLS]
        if mode == "--check-reproducible":  # a usage error before the tools and the lock
            reproducible_dir(args.check_reproducible, build_dir, repo,
                             relink_inputs(build_dir, repo, tools))
        if ctx is None:
            if mode == "--check":
                gone = missing_outputs(os.path.abspath(build_dir))
                if gone:
                    say("error: missing: %s (build them with build-libc.py)"
                        % ", ".join(slashes(p) for p in gone), sys.stderr)
                    return 1
            ctx = Context.from_build_dir(
                build_dir, origin, UsageError if mode == "build" else CheckError)
        release = (enter_build_lock(ctx.build_dir, exclusive=(mode == "build"))
                   if ctx.lock else (lambda: None))
        try:
            if mode == "--check":
                return do_check(ctx)
            if mode == "--check-reproducible":
                return do_check_reproducible(ctx, args.check_reproducible)
            return do_build(ctx, args.jobs or os.cpu_count() or 4)
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

# Every stand-in appends 'tool: <args>' to $BL_STANDIN_LOG.
STANDIN_COMMON = r"""
import os, sys
args = sys.argv[1:]
def log(tool):
    with open(os.environ["BL_STANDIN_LOG"], "a") as f:
        f.write("%s: %s\n" % (tool, " ".join(args)))
def fail_if(tool):
    if os.environ.get("BL_STANDIN_FAIL") == tool:
        sys.stderr.write("stand-in %s: planted failure\n" % tool); sys.exit(1)
"""

STANDIN_CLANG = STANDIN_COMMON + r"""
if args == ["--version"]:
    print("stand-in clang"); sys.exit(0)
if "-print-libgcc-file-name" in args:
    print(os.environ["BL_STANDIN_BUILTINS"]); sys.exit(0)
if "-print-resource-dir" in args:
    print(os.environ["BL_STANDIN_RESOURCE_DIR"]); sys.exit(0)
sys.stderr.write("stand-in clang: unexpected %r\n" % args); sys.exit(3)
"""

STANDIN_AR = STANDIN_COMMON + r"""
if args == ["--version"]:
    print("stand-in llvm-ar"); sys.exit(0)
log("llvm-ar"); fail_if("llvm-ar")
if args[0] != "rsD":
    sys.stderr.write("stand-in llvm-ar: unexpected %r\n" % args); sys.exit(3)
archive = args[1]
lines = open(archive).read().splitlines() if os.path.exists(archive) else ["!<stand-in>"]
for m in args[2:]:
    if os.sep in m or "/" in m:
        sys.stderr.write("stand-in llvm-ar: not a bare member name %r\n" % m); sys.exit(3)
    entry = "member %s %s" % (m, open(m).read().strip())
    lines = [l for l in lines if not l.startswith("member %s " % m)] + [entry]
open(archive, "w", newline="\n").write("\n".join(lines) + "\n")
"""

STANDIN_SIMPLE = STANDIN_COMMON + r"""
if args == ["--version"]:
    print("stand-in %s" % os.path.basename(sys.argv[0])); sys.exit(0)
sys.stderr.write("stand-in: unexpected %r\n" % args); sys.exit(3)
"""

STANDIN_LD = STANDIN_COMMON + r"""
if args == ["--version"]:
    print("stand-in msil-ld"); sys.exit(0)
log("msil-ld"); fail_if("msil-ld")
out = args[args.index("-o") + 1]
lst = [a for a in args if a.startswith("--export-list=")][0].split("=", 1)[1]
names = [l.strip() for l in open(lst) if l.strip() and not l.startswith("#")]
inputs = [a for a in args[:args.index("-o")] if not a.startswith("-")]
body = "c.dll\nexports %s\n" % " ".join(names)
for i in inputs:
    body += "input %s\n%s" % (os.path.basename(i), open(i).read())
open(out, "w", newline="\n").write(body)
"""

STANDIN_CMAKE = STANDIN_COMMON + r"""
if args == ["--version"]:
    print("cmake version 9.9.9-stand-in"); sys.exit(0)
log("cmake"); fail_if("cmake")
if args[0] == "--install":
    prefix = args[args.index("--prefix") + 1]
    tree = args[1]
    inc = os.path.join(prefix, "include")
    os.makedirs(os.path.join(inc, "llvm-libc-types"), exist_ok=True)
    for name in ("stdio.h", "string.h", "llvm-libc-types/size_t.h"):
        open(os.path.join(inc, name), "w", newline="\n").write("/* %s */\n" % name)
    sys.exit(0)
tree = args[args.index("-B") + 1]
os.makedirs(tree, exist_ok=True)
open(os.path.join(tree, "CMakeCache.txt"), "w").write("\n".join(args) + "\n")
"""

STANDIN_NINJA = STANDIN_COMMON + r"""
if args == ["--version"]:
    print("1.0-stand-in"); sys.exit(0)
log("ninja"); fail_if("ninja")
tree = args[args.index("-C") + 1]
lib = os.path.join(tree, "libc", "lib")
os.makedirs(lib, exist_ok=True)
open(os.path.join(lib, "libc.a"), "w", newline="\n").write("!<stand-in>\nmember strlen.cpp.obj x\n")
if os.environ.get("BL_STANDIN_NO_PROVIDER") != "1":
    d = os.path.join(tree, os.environ["BL_STANDIN_PROVIDER_DIR"])
    os.makedirs(d, exist_ok=True)
    open(os.path.join(d, "errno.cpp.obj"), "w", newline="\n").write("defines __llvm_libc_errno\n")
"""


class SelfTest:
    def __init__(self):
        self.failures = 0
        self.root = tempfile.mkdtemp(prefix="build-libc-self-test-")
        self.repo = os.path.join(self.root, "repo")
        self.build = os.path.join(self.root, "build")
        tools_dir = os.path.join(self.root, "tools")
        os.makedirs(tools_dir)
        self.log = os.path.join(self.root, "calls.log")
        texts = {
            "clang": STANDIN_CLANG, "clang++": STANDIN_SIMPLE, "llvm-ar": STANDIN_AR,
            "llvm-ranlib": STANDIN_SIMPLE, "msil-ld": STANDIN_LD,
            "cmake": STANDIN_CMAKE, "ninja": STANDIN_NINJA,
        }
        files = {}
        for name, text in texts.items():
            path = os.path.join(tools_dir, name + ".py")
            with open(path, "w", newline="\n") as f:
                f.write(text)
            files[name] = path
        py = [sys.executable, "-I", "-S"]
        self.builtins = os.path.join(self.build, "lib", "clang", "99", "lib", TRIPLE,
                                     "libclang_rt.builtins.a")
        os.makedirs(os.path.dirname(self.builtins))
        with open(self.builtins, "w", newline="\n") as f:
            f.write("!<stand-in builtins>\n")
        self.resource_dir = os.path.join(self.build, "lib", "clang", "99")
        os.makedirs(os.path.join(self.resource_dir, "include"))
        for name in ("stddef.h", "stdarg.h", "__stddef_size_t.h"):
            with open(os.path.join(self.resource_dir, "include", name), "w", newline="\n") as f:
                f.write("/* stand-in %s */\n" % name)
        os.environ["BL_STANDIN_RESOURCE_DIR"] = self.resource_dir
        os.environ["BL_STANDIN_LOG"] = self.log
        os.environ["BL_STANDIN_BUILTINS"] = self.builtins
        os.environ["BL_STANDIN_PROVIDER_DIR"] = PROVIDER_DIR
        self.ctx = Context(
            self.repo,
            self.build,
            {n: (py + [files[n]], files[n]) for n in TOOLS},
            py + [files["cmake"]],
            (py + [files["ninja"]], files["ninja"]),
            os.path.abspath(__file__),
            lock=False,
        )

    def write(self, rel, text):
        path = os.path.join(self.repo, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", newline="\n") as f:
            f.write(text)

    def read(self, rel, base=None):
        return read_text(os.path.join(base or self.repo, rel))

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
        missing = [n for n in needles if not re.search(n, text, re.M)]
        present = [n for n in absent if re.search(n, text, re.M)]
        self.check(
            name,
            got == code and not missing and not present,
            "exit %s (expected %d), missing %r, unexpected %r; output:\n%s"
            % (got, code, missing, present, text),
        )
        return text

    def calls(self):
        text = read_text(self.log) or ""
        with open(self.log, "w"):
            pass
        return text.splitlines()


SELF_TEST_ENTRYPOINTS = """\
# A synthetic list.
if(NOT LIBC_CONF_ERRNO_MODE STREQUAL "LIBC_ERRNO_MODE_EXTERNAL")
  message(FATAL_ERROR "x")
endif()

set(TARGET_LIBC_ENTRYPOINTS
    # string.h entrypoints
    libc.src.string.strlen
    libc.src.string.memcpy   # a trailing comment

    libc.src.errno.errno
    libc.src.stdlib.abs
    libc.src.ctype.isalpha
)

set(TARGET_LLVMLIBC_ENTRYPOINTS
    ${TARGET_LIBC_ENTRYPOINTS}
    libc.src.setjmp.setjmp
)
# Not shipped:
#   libc.src.stdlib.malloc: needs the heap; P-2
"""


def self_test():
    keys = ("BL_STANDIN_LOG", "BL_STANDIN_BUILTINS", "BL_STANDIN_RESOURCE_DIR",
            "BL_STANDIN_PROVIDER_DIR",
            "BL_STANDIN_FAIL", "BL_STANDIN_NO_PROVIDER", "MSIL_BUILD_DIR")
    saved_env = {k: os.environ.get(k) for k in keys}
    t = SelfTest()
    try:
        _self_test_exports(t)
        _self_test_append_only(t)
        _self_test_usage(t)
        _self_test_build(t)
        _self_test_reproducible(t)
        _self_test_stream(t)
        if os.name == "nt":
            _self_test_lock(t)
    finally:
        for k, v in saved_env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        remove_tree(t.root, ignore_errors=True)  # it holds links
    if t.failures:
        print("%s --self-test: %d check(s) failed" % (PROG, t.failures))
        return 1
    print("%s --self-test: all checks passed" % PROG)
    return 0


def _self_test_exports(t):
    t.write(ENTRYPOINTS, SELF_TEST_ENTRYPOINTS)
    t.expect("--check-exports fails while the export list is missing", ["--check-exports"], 1,
             r"is missing; write it with --write-exports")
    t.expect("--write-exports writes the export list", ["--write-exports"], 0,
             r"wrote msil/lib/libc/msil64-unknown-dotnet\.exports \(5 names appended\)")
    names = export_names(t.read(EXPORTS), EXPORTS)
    t.check("the export list holds the block's C names without errno, plus __llvm_libc_errno, "
            "sorted", names == ["__llvm_libc_errno", "abs", "isalpha", "memcpy", "strlen"],
            repr(names))
    t.check("the export list starts with its comment header",
            t.read(EXPORTS).startswith(EXPORTS_HEADER))
    t.check("TARGET_LLVMLIBC_ENTRYPOINTS and comments give no name", "setjmp" not in names
            and "malloc" not in names)
    t.expect("--check-exports passes on the written list", ["--check-exports"], 0,
             r"matches .* \(5 names\)$")
    t.expect("--write-exports leaves a current list alone", ["--write-exports"], 0, "up to date")
    t.write(ENTRYPOINTS, SELF_TEST_ENTRYPOINTS.replace(
        "libc.src.stdlib.abs\n", "libc.src.stdlib.abs\n    libc.src.stdlib.atoi\n"
        "    libc.src.string.bcmp\n"))
    t.expect("--check-exports names the entry points the list lacks", ["--check-exports"], 1,
             r"lacks 2 name\(s\) of .*: atoi, bcmp; append them with --write-exports")
    t.expect("a build refuses a stale export list before it configures", [], 1,
             r"differs from the list that .* gives \(it lacks atoi, bcmp\); update it")
    t.check("the refused build ran no tool", t.calls() == [])
    t.expect("--write-exports appends new names at the end, sorted", ["--write-exports"], 0,
             r"\(2 names appended\)")
    t.check("the appended list keeps the old order",
            export_names(t.read(EXPORTS), EXPORTS)
            == ["__llvm_libc_errno", "abs", "isalpha", "memcpy", "strlen", "atoi", "bcmp"])
    t.write(ENTRYPOINTS, SELF_TEST_ENTRYPOINTS)
    t.expect("an entry point gone from entrypoints.txt is an error (append-only)",
             ["--check-exports"], 1, r"names atoi, bcmp, which .* no longer lists; the export "
             r"list is append-only")
    os.remove(os.path.join(t.repo, EXPORTS))
    t.expect("--write-exports again", ["--write-exports"], 0, "5 names appended")
    with open(os.path.join(t.repo, EXPORTS), "a", newline="\n") as f:
        f.write("# a comment\n")
    t.expect("--check-exports catches a changed layout", ["--check-exports"], 1,
             r"differs from the list that --write-exports writes")
    t.expect("--write-exports restores it", ["--write-exports"], 0, r"\(0 names appended\)")
    for name, text, needle in (
        ("no block", "set(OTHER a)\n", r"no set\(TARGET_LIBC_ENTRYPOINTS \.\.\.\) block"),
        ("an unterminated block", "set(TARGET_LIBC_ENTRYPOINTS\n libc.src.a.b\n",
         r"block has no '\)'"),
        ("two blocks", "set(TARGET_LIBC_ENTRYPOINTS libc.src.a.b)\nset(TARGET_LIBC_ENTRYPOINTS "
         "libc.src.a.c)\n", r":2: a second set\(TARGET_LIBC_ENTRYPOINTS"),
        ("an empty block", "set(TARGET_LIBC_ENTRYPOINTS\n)\n", "block is empty"),
        ("a duplicate", "set(TARGET_LIBC_ENTRYPOINTS\n libc.src.a.b\n libc.src.c.b\n)\n",
         r":3: b is listed twice \(also on line 2\)"),
        ("a variable", "set(TARGET_LIBC_ENTRYPOINTS\n ${X}\n)\n", r":2: '\$\{X\}' is not an entry"),
        ("list(APPEND)", "set(TARGET_LIBC_ENTRYPOINTS libc.src.a.b)\nlist(APPEND "
         "TARGET_LIBC_ENTRYPOINTS libc.src.a.c)\n", r"list\(APPEND TARGET_LIBC_ENTRYPOINTS"),
    ):
        t.write(ENTRYPOINTS, text)
        t.expect("entrypoints.txt with %s is an error" % name, ["--check-exports"], 1, needle)
    t.write(ENTRYPOINTS, "set(TARGET_LIBC_ENTRYPOINTS libc.src.string.strlen libc.src.errno.errno "
            "libc.src.stdlib.abs libc.src.ctype.isalpha libc.src.string.memcpy)\n")
    t.expect("a one-line block gives the same list", ["--check-exports"], 0, r"\(5 names\)")
    t.write(ENTRYPOINTS, SELF_TEST_ENTRYPOINTS)
    target = os.path.join(t.root, "exports-target.txt")
    with open(target, "w") as f:
        f.write("# keep\n")
    os.remove(os.path.join(t.repo, EXPORTS))
    why = _make_link("symbolic link", os.path.join(t.repo, EXPORTS), target)
    if why:
        print("skip: --write-exports over a symbolic link (%s)" % why)
    else:
        t.expect("--write-exports over a symbolic link", ["--write-exports"], 0, "5 names appended")
        t.check("--write-exports replaces a symbolic link instead of writing through it",
                read_text(target) == "# keep\n" and not is_link(os.path.join(t.repo, EXPORTS)))
    t.run("--write-exports")
    t.expect("--check-exports passes once the list is written again", ["--check-exports"], 0,
             r"\(5 names\)")


def _self_test_append_only(t):
    d = os.path.join(t.root, "lists")
    os.makedirs(d)

    def lst(name, text, encoding="utf-8"):
        path = os.path.join(d, name)
        with open(path, "w", encoding=encoding, newline="") as f:
            f.write(text)
        return path

    old = lst("old", "# header\na\nb\nc\n")
    cases = (
        ("appending passes", "# header\na\nb\nc\nd\ne\n", 0, r"keeps the 3 names .* appends 2"),
        ("an equal list passes", "a\nb\nc\n", 0, r"appends 0$"),
        ("a changed comment and blank lines pass", "# other\n\na\n\nb\nc\n", 0, "appends 0"),
        ("CRLF and a BOM pass", "﻿# header\r\na\r\nb\r\nc\r\nd\r\n", 0, "appends 1"),
        ("deleting a line fails", "a\nc\n", 1, r"removes b: name 2 is c there, not b"),
        ("deleting the last line fails", "a\nb\n", 1, r"drops c and the 0 name\(s\) after it"),
        ("reordering fails", "a\nc\nb\n", 1, r"moves b: name 2 is c there, not b"),
        ("inserting in the middle fails", "a\nz\nb\nc\n", 1, r"moves b: name 2 is z there"),
        ("a repeated name fails", "a\nb\nc\na\n", 1, r":4: a is listed twice"),
        ("a line that is no C name fails", "a\nb\nc\nd e\n", 1, r"'d e' is not a C name"),
    )
    for name, text, code, needle in cases:
        t.expect("--check-append-only: " + name,
                 ["--check-append-only", old, lst("new", text)], code, needle)
    t.expect("--check-append-only: an unreadable OLD is a usage error",
             ["--check-append-only", os.path.join(d, "none"), old], 2, r"cannot read OLD ")


def _self_test_usage(t):
    for name, argv, needle in (
        ("two modes", ["--check", "--check-exports"], "cannot be combined"),
        ("--jobs 0", ["--jobs", "0"], "at least 1"),
        ("--jobs with --check", ["--check", "--jobs", "2"], "goes with a build"),
        ("--build-dir with --check-exports", ["--check-exports", "--build-dir", "x"],
         "goes with a build, --check or --check-reproducible"),
        ("--check-reproducible with --check", ["--check", "--check-reproducible", "x"],
         "cannot be combined"),
        ("--check-reproducible with --jobs", ["--check-reproducible", "x", "--jobs", "2"],
         "goes with a build"),
        ("--check-reproducible without a directory", ["--check-reproducible"], "expected one"),
        ("--check-append-only with one file", ["--check-append-only", "x"], "expected 2"),
        ("an unknown option", ["--frobnicate"], "unrecognized"),
    ):
        t.expect("a usage error exits 2: " + name, argv, 2, needle)
    saved = os.environ.get("MSIL_BUILD_DIR")
    empty = os.path.join(t.root, "empty-build")
    os.makedirs(empty)
    try:
        os.environ.pop("MSIL_BUILD_DIR", None)
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = main(["--check", "--build-dir", empty])
        t.check("--check against an empty directory exits 1, naming the missing files",
                code == 1 and "missing: " in err.getvalue() and "c.dll" in err.getvalue(),
                "exit %s: %s" % (code, err.getvalue()))
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = main(["--build-dir", empty])
        t.check("a build with a directory that has no tools is a usage error",
                code == 2 and "clang" in err.getvalue() and "does not exist" in err.getvalue(),
                "exit %s: %s" % (code, err.getvalue()))
        os.environ["MSIL_BUILD_DIR"] = empty
        err = io.StringIO()
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(err):
            code = main([])
        t.check("$MSIL_BUILD_DIR names the default build directory",
                code == 2 and "MSIL_BUILD_DIR=" + empty in err.getvalue(), err.getvalue())
    finally:
        if saved is None:
            os.environ.pop("MSIL_BUILD_DIR", None)
        else:
            os.environ["MSIL_BUILD_DIR"] = saved


def _self_test_build(t):
    t.write(EXPORTS, "")
    os.remove(os.path.join(t.repo, EXPORTS))
    t.run("--write-exports")
    for rel in ("runtimes/CMakeLists.txt", "cmake/Modules/x.cmake", "llvm/cmake/config.cmake",
                "libc/src/string/strlen.cpp", "libc/test/x_test.cpp"):
        t.write(rel, "# %s\n" % rel)
    sysroot = os.path.join(t.build, SYSROOT)
    lib = os.path.join(sysroot, LIB_DIR)
    tree = os.path.join(t.build, LIBC_TREE)
    t.calls()
    t.expect("--check before a build exits 1", ["--check"], 1, r"missing: .*c\.dll")
    t.expect("a build configures, builds, links and installs", ["--jobs", "3"], 0,
             r"libc\.a: added the errno provider \(dotnet_util\.errno\.cpp\.obj\)",
             r"^build-libc\.py: c\.dll: \d+ bytes")
    calls = t.calls()
    conf = [c for c in calls if c.startswith("cmake: -G")]
    t.check("one configure of <repo>/runtimes into <build>/runtimes/msil64-unknown-dotnet",
            len(conf) == 1 and "-S %s " % slashes(os.path.join(t.repo, "runtimes")) in conf[0]
            and "-B %s " % slashes(tree) in conf[0], repr(calls))
    c = conf[0] if conf else ""
    for var in ("LLVM_ENABLE_RUNTIMES=libc", "LLVM_LIBC_FULL_BUILD=ON",
                "LIBC_TARGET_TRIPLE=" + TRIPLE, "LLVM_DEFAULT_TARGET_TRIPLE=" + TRIPLE,
                "CMAKE_C_COMPILER_TARGET=" + TRIPLE,
                "CMAKE_CXX_COMPILER_TARGET=" + TRIPLE,
                "CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
                "CMAKE_SYSTEM_NAME=Generic", "CMAKE_BUILD_TYPE=Release", "LLVM_INCLUDE_TESTS=OFF",
                "CMAKE_C_FLAGS=-fvisibility=hidden", "CMAKE_CXX_FLAGS=-fvisibility=hidden",
                "CMAKE_MAKE_PROGRAM=", "CMAKE_AR=", "CMAKE_LINKER="):
        t.check("the configure passes -D" + var, " -D" + var in c, c)
    t.check("the configure passes no LIBC_CONFIG_PATH and no LIBC_CONF_* option",
            "LIBC_CONFIG_PATH" not in c and "LIBC_CONF_" not in c, c)
    t.check("the configure passes no LLVM_RUNTIMES_TARGET (refused with LIBC_TARGET_TRIPLE)",
            "LLVM_RUNTIMES_TARGET" not in c, c)
    t.check("ninja builds all and the errno provider with -j 3",
            any(x.startswith("ninja: -C %s -j 3 all %s" % (slashes(tree), PROVIDER_TARGET))
                for x in calls), repr(calls))
    t.check("llvm-ar adds the provider to libc.a under its member name",
            any(x == "llvm-ar: rsD %s dotnet_util.errno.cpp.obj"
                % os.path.join(tree, LIBC_ARCHIVE) for x in calls), repr(calls))
    archive_text = t.read(LIBC_ARCHIVE, tree) or ""
    t.check("libc.a holds the provider", "member dotnet_util.errno.cpp.obj defines "
            "__llvm_libc_errno" in archive_text, archive_text)
    ld = [x for x in calls if x.startswith("msil-ld: ")]
    t.check("msil-ld links c.dll with the export list, --exclude-libs=ALL and the version",
            len(ld) == 1 and all(s in ld[0] for s in (
                "--target=" + TRIPLE, " --shared ",
                "--export-list=" + os.path.join(t.repo, EXPORTS),
                " --exclude-libs=ALL ", " --assembly-version=1.0.0.0 ",
                " %s %s -o " % (os.path.join(tree, LIBC_ARCHIVE), t.builtins))),
            repr(ld))
    t.check("msil-ld writes an output named c.dll", bool(ld) and ld[0].endswith("c.dll"), repr(ld))
    t.check("the headers are installed from the libc-headers component",
            any(x.startswith("cmake: --install %s --component libc-headers --prefix "
                             % slashes(tree)) for x in calls), repr(calls))
    cdll = read_text(os.path.join(lib, "c.dll")) or ""
    t.check("c.dll is installed in lib/msil64-unknown-dotnet and exports the list",
            cdll.startswith("c.dll\nexports __llvm_libc_errno abs isalpha memcpy strlen\n"), cdll)
    with open(os.path.join(lib, "libm.a"), "rb") as f:
        t.check("libm.a is the bare archive header (no members)", f.read() == EMPTY_ARCHIVE)
    t.check("the headers are installed in include/",
            os.path.isfile(os.path.join(sysroot, "include", "stdio.h"))
            and os.path.isfile(os.path.join(sysroot, "include", "llvm-libc-types", "size_t.h")))
    t.check("libc.a is not installed", not os.path.exists(os.path.join(lib, "libc.a")))
    left = [n for n in os.listdir(sysroot) if n.startswith(".build-libc")]
    t.check("no temporary directory is left in the sysroot", not left, repr(left))
    stamp = read_text(stamp_path(sysroot)) or ""
    for line in ("triple %s" % TRIPLE, "assembly-version 1.0.0.0", "references none",
                 "configure CMAKE_BUILD_TYPE=Release", "output lib/%s/c.dll " % TRIPLE,
                 "output include/stdio.h ", "configure LLVM_DEFAULT_TARGET_TRIPLE=" + TRIPLE,
                 "resource-headers " + tree_digest(os.path.join(t.resource_dir, "include")),
                 "archive " + file_digest(os.path.join(tree, LIBC_ARCHIVE))):
        t.check("the stamp records '%s'" % line, ("\n" + line) in stamp, stamp)
    t.expect("--check passes after the build", ["--check"], 0, "up to date")
    t.expect("a second build is up to date", [], 0, r"up to date: ")
    t.check("an up-to-date build runs no cmake, ninja, llvm-ar or msil-ld",
            [x for x in t.calls() if not x.startswith("clang")] == [])

    # The intermediate libc.a: deleted, then rebuilt by a plain ninja run
    # (without the provider); each is stale until a build reruns ninja and
    # add_provider.
    archive = os.path.join(tree, LIBC_ARCHIVE)
    os.remove(archive)
    t.expect("--check reports a deleted libc.a", ["--check"], 1, r"missing: .*libc/lib/libc\.a")
    t.expect("a build after libc.a is deleted is not up to date", [], 0, "c.dll: ",
             absent=(r"up to date",))
    calls = t.calls()
    t.check("the build reruns ninja and adds the provider again",
            any(x.startswith("ninja: ") for x in calls)
            and any(x.startswith("llvm-ar: ") for x in calls), repr(calls))
    t.check("the build recreates libc.a with the provider",
            "member dotnet_util.errno.cpp.obj" in (t.read(LIBC_ARCHIVE, tree) or ""))
    t.expect("--check passes once libc.a is rebuilt", ["--check"], 0, "up to date")
    with open(archive, "w", newline="\n") as f:
        f.write("!<stand-in>\nmember strlen.cpp.obj x\n")
    t.expect("--check reports libc.a rebuilt without the provider", ["--check"], 1,
             r"the intermediate archive changed: .*libc/lib/libc\.a")
    t.expect("a build after a plain ninja rebuild of libc.a", [], 0, "c.dll: ")
    t.check("the build adds the provider to the plainly rebuilt libc.a",
            "member dotnet_util.errno.cpp.obj" in (t.read(LIBC_ARCHIVE, tree) or ""))
    t.expect("--check passes once the provider is back", ["--check"], 0, "up to date")

    # Clang's resource headers (stddef.h, stdarg.h) are an input that the
    # digest of the clang binary does not cover.
    t.calls()
    resource_inc = os.path.join(t.resource_dir, "include")
    with open(os.path.join(resource_inc, "stddef.h"), "a") as f:
        f.write("/* changed */\n")
    t.expect("--check reports a changed clang resource header", ["--check"], 1,
             r"stale \(stamp differs: resource-headers\)")
    t.expect("a build after a resource header change", [], 0, "c.dll: ")
    t.check("the build reruns ninja after a resource header change",
            any(x.startswith("ninja: ") for x in t.calls()))
    with open(os.path.join(resource_inc, "__stddef_new.h"), "w") as f:
        f.write("/* new */\n")
    t.expect("--check reports an added clang resource header", ["--check"], 1,
             r"stamp differs: resource-headers")
    t.run()
    os.rename(resource_inc, resource_inc + ".away")
    t.expect("--check without clang's resource include/ exits 1", ["--check"], 1,
             r"resource directory .* has no include/")
    os.rename(resource_inc + ".away", resource_inc)
    t.expect("--check passes again once the resource headers are back", ["--check"], 0,
             "up to date")
    t.calls()

    # A changed input: a source file, then a test file (not an input).
    t.write("libc/test/x_test.cpp", "// changed\n")
    t.expect("a changed libc/test file leaves the build current", ["--check"], 0, "up to date")
    t.write("libc/src/string/strlen.cpp", "// changed\n")
    t.expect("--check reports a changed libc source", ["--check"], 1,
             r"stale \(stamp differs: sources\)")
    t.expect("a rebuild after a source change", [], 0, "c.dll: ")
    calls = t.calls()
    t.check("the rebuild reuses the configured tree (no configure, ninja again)",
            not any(x.startswith("cmake: -G") for x in calls)
            and any(x.startswith("ninja: ") for x in calls)
            and len([x for x in calls if x.startswith("llvm-ar: ")]) == 1, repr(calls))
    archive_text = t.read(LIBC_ARCHIVE, tree) or ""
    t.check("re-adding the provider keeps one member",
            archive_text.count("member dotnet_util.errno.cpp.obj") == 1, archive_text)

    with open(os.path.join(lib, "c.dll"), "a") as f:
        f.write("tampered\n")
    t.expect("--check reports a changed c.dll", ["--check"], 1,
             r"an installed file changed: lib/msil64-unknown-dotnet/c\.dll")
    t.expect("a build replaces a changed c.dll", [], 0, "c.dll: ")
    t.calls()
    os.remove(os.path.join(sysroot, "include", "string.h"))
    t.expect("--check reports a removed header", ["--check"], 1,
             r"an installed file changed: include/string\.h")
    t.run()
    with open(os.path.join(sysroot, "include", "stale.h"), "w") as f:
        f.write("stale\n")
    t.run("--write-exports")
    t.write(ENTRYPOINTS, SELF_TEST_ENTRYPOINTS.replace(
        "libc.src.stdlib.abs\n", "libc.src.stdlib.abs\n    libc.src.stdlib.labs\n"))
    t.run("--write-exports")
    t.expect("--check reports a changed export list", ["--check"], 1, r"stamp differs: exports")
    t.calls()
    t.expect("a rebuild after an export list change", [], 0, "c.dll: ")
    t.check("the rebuilt c.dll exports the appended name",
            "strlen labs\n" in (read_text(os.path.join(lib, "c.dll")) or ""))
    t.check("a rebuild replaces include/ whole (a stale header goes)",
            not os.path.exists(os.path.join(sysroot, "include", "stale.h")))

    # A changed configure record reconfigures; a changed tool is a stale stamp.
    os.remove(os.path.join(tree, "build-libc.configure"))
    with open(t.ctx.path("msil-ld"), "a") as f:
        f.write("# changed\n")
    t.calls()
    t.expect("--check reports a changed tool", ["--check"], 1, r"stamp differs: tool")
    t.expect("a rebuild after a tool change", [], 0, "c.dll: ")
    t.check("a missing configure record configures again",
            any(x.startswith("cmake: -G") for x in t.calls()))

    # Failures: c.dll and the stamp go, so a link fails loudly.
    with open(t.ctx.path("clang++"), "a") as f:
        f.write("# changed\n")
    for tool, needle in (("ninja", r"building llvm-libc for .* failed \(exit 1\)"),
                         ("msil-ld", r"linking c\.dll failed \(exit 1\)"),
                         ("llvm-ar", r"llvm-ar could not add dotnet_util\.errno\.cpp\.obj")):
        os.environ["BL_STANDIN_FAIL"] = tool
        t.expect("a failed %s fails the build" % tool, [], 1, needle)
        os.environ.pop("BL_STANDIN_FAIL")
        t.check("after a failed %s neither c.dll nor its stamp is left" % tool,
                not os.path.exists(os.path.join(lib, "c.dll"))
                and not os.path.exists(stamp_path(sysroot)))
        t.expect("--check after a failed %s exits 1" % tool, ["--check"], 1, "missing: ")
        t.expect("the next build recovers", [], 0, "c.dll: ")
        with open(t.ctx.path("clang++"), "a") as f:
            f.write("# changed\n")
    os.remove(os.path.join(tree, "build-libc.configure"))
    os.environ["BL_STANDIN_FAIL"] = "cmake"
    t.expect("a failed configure fails the build", [], 1,
             r"configuring llvm-libc for msil64-unknown-dotnet failed")
    os.environ.pop("BL_STANDIN_FAIL")
    t.check("a failed configure leaves no configure record",
            not os.path.exists(os.path.join(tree, "build-libc.configure")))
    os.environ["BL_STANDIN_NO_PROVIDER"] = "1"
    shutil.rmtree(os.path.join(tree, PROVIDER_DIR))
    t.expect("a build without the errno provider's object fails", [], 1,
             r"no object of libc\.src\.__support\.OSUtil\.dotnet\.dotnet_util")
    os.environ.pop("BL_STANDIN_NO_PROVIDER")
    t.expect("the build recovers once the provider is built", [], 0, "c.dll: ")
    saved = os.environ["BL_STANDIN_BUILTINS"]
    os.environ["BL_STANDIN_BUILTINS"] = os.path.join(t.root, "no-builtins.a")
    t.expect("--check with a missing helper archive exits 1", ["--check"], 1,
             r"helper archive .* does not exist")
    t.expect("a missing helper archive is a usage error naming build-builtins.py", [], 2,
             r"build-builtins\.py --triple msil64-unknown-dotnet")
    os.environ["BL_STANDIN_BUILTINS"] = saved
    t.expect("--check passes again once the helper archive is back", ["--check"], 0,
             "up to date")


def _self_test_reproducible(t):
    sysroot = os.path.join(t.build, SYSROOT)
    installed = os.path.join(sysroot, LIB_DIR, "c.dll")
    out = os.path.join(t.root, "repro")
    stale = [os.path.join(out, "old", "deeper", "stale.txt"), os.path.join(out, "stale.dll")]
    os.makedirs(os.path.dirname(stale[0]))
    for path in stale:
        with open(path, "w") as f:
            f.write("stale\n")
    t.run()  # current
    t.calls()
    t.expect("--check-reproducible refuses an OUT that holds anything but the files the relink "
             "writes (exit 2)", ["--check-reproducible", out], 2, r"the directory to empty holds "
             r"old, which is not a file the relink writes; give an empty or new directory$")
    t.check("a refused --check-reproducible leaves everything in OUT in place",
            all(read_text(p) == "stale\n" for p in stale) and not t.calls())
    remove_tree(os.path.join(out, "old"), ignore_errors=True)
    remove_quietly(stale[1])
    # A c.dll without the marker (a lit output directory of the build, say)
    # was not written by a relink; near-miss names are not the relink's even
    # beside the marker.
    with open(os.path.join(out, "c.dll"), "w") as f:
        f.write("stale\n")
    t.expect("--check-reproducible refuses an OUT that holds a c.dll but not the marker (exit 2)",
             ["--check-reproducible", out], 2, r"the directory to empty holds c\.dll but not "
             r"\.build-libc-relink, so no relink wrote it; remove it or give an empty or new "
             r"directory$")
    t.check("a refused --check-reproducible leaves a c.dll without the marker in place",
            read_text(os.path.join(out, "c.dll")) == "stale\n" and not t.calls())
    remove_quietly(os.path.join(out, "c.dll"))
    with open(os.path.join(out, RELINK_MARK), "w") as f:
        f.write("stale\n")
    near = ["c.dll.bak", "C.DLL", "c.dll.tmp0123ABC", "c.dll.tmp0123abcd", "c.dll.tmpzzzzzzz"]
    if os.name != "nt":
        near.append("c.dll\n")  # a pattern ending in '$' matches before the newline
    wrong = []
    for name in near:
        with open(os.path.join(out, name), "w") as f:
            f.write("stale\n")
        code, out_text, err_text = t.run("--check-reproducible", out)
        if (code != 2 or "holds %s, which is not a file the relink writes" % name not in err_text
                or read_text(os.path.join(out, name)) != "stale\n"):
            wrong.append((name, code, out_text + err_text))
        remove_quietly(os.path.join(out, name))
    t.check("--check-reproducible refuses names the relink does not write, such as c.dll.bak, "
            "C.DLL or c.dll.tmp without seven lowercase hex digits, and leaves them (exit 2)",
            not wrong, repr(wrong))
    # An earlier run's c.dll, and the temporary file a killed msil-ld leaves,
    # go beside the marker.
    for name in ("c.dll", "c.dll.tmp0123abc"):
        with open(os.path.join(out, name), "w") as f:
            f.write("stale\n")
    t.expect("--check-reproducible passes when the relink is byte-identical",
             ["--check-reproducible", out], 0, r"^build-libc\.py: reproducible: .*c\.dll relinks "
             r"byte-identically \(SHA-256 [0-9a-f]{64}\)$")
    t.check("--check-reproducible replaces an earlier run's c.dll in OUT and leaves only c.dll "
            "and a new marker", sorted(os.listdir(out)) == [RELINK_MARK, "c.dll"]
            and read_text(os.path.join(out, "c.dll")) != "stale\n"
            and read_text(os.path.join(out, RELINK_MARK)) != "stale\n", repr(os.listdir(out)))
    ld = [x for x in t.calls() if x.startswith("msil-ld: ")]
    tree = os.path.join(t.build, LIBC_TREE)
    want = ("msil-ld: --target=%s --shared --export-list=%s --exclude-libs=ALL "
            "--assembly-version=%s %s %s -o %s" % (
                TRIPLE, os.path.join(t.repo, EXPORTS), ASSEMBLY_VERSION,
                os.path.join(tree, LIBC_ARCHIVE), t.builtins, os.path.join(out, "c.dll")))
    t.check("--check-reproducible relinks with the build's msil-ld arguments into OUT",
            ld == [want], repr(ld))
    t.check("--check-reproducible runs no cmake, ninja or llvm-ar",
            not any(x.startswith(("cmake: ", "ninja: ", "llvm-ar: ")) for x in t.calls()))
    with open(installed, "rb") as f:
        good = f.read()
    with open(installed, "wb") as f:
        f.write(good[:-2] + bytes([good[-2] ^ 1]) + good[-1:])
    t.expect("--check-reproducible fails on a differing byte, printing both SHA-256 hashes",
             ["--check-reproducible", out], 1, r"differs from the installed .*c\.dll$",
             r"^build-libc\.py: installed SHA-256 [0-9a-f]{64}$",
             r"^build-libc\.py: relinked  SHA-256 [0-9a-f]{64}$",
             r"note: the installed files are not current \(an installed file changed")
    with open(installed, "wb") as f:
        f.write(good)
    t.expect("--check-reproducible passes again once c.dll is restored",
             ["--check-reproducible", out], 0, r"relinks byte-identically")
    for name, where, needle in (
        ("the build directory", t.build, r"is or holds the repository or the build directory"),
        ("a directory holding the build", t.root, r"is or holds the repository"),
        ("the sysroot", os.path.join(sysroot, "lib"),
         r"lies in the repository or the build directory"),
        ("the libc tree", os.path.join(tree, "libc"),
         r"lies in the repository or the build directory"),
        ("a repository directory", os.path.join(t.repo, "libc"),
         r"lies in the repository or the build directory"),
        ("a directory in the build directory", os.path.join(t.build, "tmp", "repro"),
         r"lies in the repository or the build directory"),
        ("a file", os.path.join(t.repo, EXPORTS), r"is not a directory"),
    ):
        t.expect("--check-reproducible refuses to empty %s (exit 2)" % name,
                 ["--check-reproducible", where], 2, needle)
    t.check("a refused --check-reproducible leaves the libc tree and the sysroot alone",
            os.path.isfile(os.path.join(tree, LIBC_ARCHIVE)) and os.path.isfile(installed))
    clang = os.path.join(t.build, "lib", "clang")
    os.makedirs(clang, exist_ok=True)
    sentinel = os.path.join(clang, "sentinel.txt")
    with open(sentinel, "w") as f:
        f.write("keep\n")
    t.expect("--check-reproducible refuses to empty a build-tree directory such as "
             "<build>/lib/clang (exit 2)",
             ["--check-reproducible", clang], 2,
             r"lies in the repository or the build directory")
    t.check("a refused --check-reproducible leaves <build>/lib/clang alone",
            os.path.isfile(sentinel))
    _self_test_aliases(t, clang, sentinel, out)
    _self_test_inputs(t, installed, out)
    archive = os.path.join(tree, LIBC_ARCHIVE)
    os.rename(archive, archive + ".away")
    t.expect("--check-reproducible without the intermediate libc.a exits 1",
             ["--check-reproducible", out], 1, r"missing: .*libc\.a")
    os.rename(archive + ".away", archive)


def _make_link(kind, link, target):
    """Creates a junction or a symbolic link at link; returns None, or why it
    could not (printed as a skip)."""
    try:
        if kind == "junction":
            if os.name != "nt":
                return "junctions exist on Windows only"
            import _winapi
            _winapi.CreateJunction(target, link)
        else:
            os.symlink(target, link, target_is_directory=os.path.isdir(target))
    except (OSError, ImportError, AttributeError) as e:
        return os_error_text(e) if isinstance(e, OSError) else str(e)
    return None


def _self_test_aliases(t, clang, sentinel, out):
    """--check-reproducible resolves links and aliases: each form that
    reaches <build>/lib/clang or a directory holding the build is refused
    like the directory itself, an OUT that holds a link is refused, and so
    is an extended-length OUT with a name in its path that ends in a dot or
    a space (Windows), and empty_dir removes a link but never what it
    reaches."""
    lies = r"lies in the repository or the build directory"
    holds = r"is or holds the repository or the build directory"
    aliases = os.path.join(t.root, "aliases")
    os.makedirs(aliases)

    def refused(name, where, needle):
        t.expect("--check-reproducible refuses %s (exit 2)" % name,
                 ["--check-reproducible", where], 2, needle)
        t.check("a refused --check-reproducible through %s leaves <build>/lib/clang alone"
                % name, os.path.isfile(sentinel)
                and not os.path.exists(os.path.join(clang, "new")))

    for kind in ("junction", "symbolic link"):
        link = os.path.join(aliases, kind.replace(" ", "-"))
        why = _make_link(kind, link, clang)
        if why:
            print("skip: --check-reproducible through a %s (%s)" % (kind, why))
            continue
        refused("a %s to <build>/lib/clang" % kind, link, lies)
        refused("a new directory below a %s to <build>/lib/clang" % kind,
                os.path.join(link, "new", "dir"), lies)
        root_link = link + "-root"
        if not _make_link(kind, root_link, t.root):
            refused("a %s to a directory holding the build" % kind, root_link, holds)

    # main() checks OUT before it waits for the build lock (up to
    # LOCK_WAIT_SECONDS); an OUT that becomes a link during the wait is
    # refused by the check just before emptying it.
    late = os.path.join(aliases, "late")
    kind = "junction" if os.name == "nt" else "symbolic link"
    made = []
    saved = enter_build_lock, t.ctx.lock

    def link_while_waiting(build_dir, exclusive=False, wait_seconds=None):
        made.append(_make_link(kind, late, clang))
        return lambda: None

    globals()["enter_build_lock"], t.ctx.lock = link_while_waiting, True
    try:
        code, out_text, err_text = t.run("--check-reproducible", late)
    finally:
        globals()["enter_build_lock"], t.ctx.lock = saved
    t.check("an OUT that becomes a link to <build>/lib/clang while --check-reproducible "
            "waits for the build lock is refused (exit 2)",
            made == [None] and code == 2 and re.search(lies, err_text) is not None,
            "link: %r; exit %s; output:\n%s" % (made, code, out_text + err_text))
    t.check("a refused --check-reproducible through a link made during the lock wait "
            "leaves <build>/lib/clang alone", os.path.isfile(sentinel)
            and os.path.isfile(t.builtins))

    # A mount point (a bind mount, a volume on a folder) that appears in OUT
    # after the last check stops emptying it (exit 1) before anything below
    # it goes. A real one needs privilege and could expose a whole volume, so
    # is_mount_point is replaced for one directory, made just after the check
    # before emptying.
    mount_out = os.path.join(t.root, "mount-out")
    mounted = os.path.join(mount_out, "m")
    below = os.path.join(mounted, "below.txt")
    saved_is_mount_point, saved_reproducible_dir = is_mount_point, reproducible_dir
    checks = []

    def mount_after_check(*args):
        checks.append(saved_reproducible_dir(*args))
        if len(checks) == 2:  # the check just before emptying
            os.makedirs(mounted)
            with open(below, "w") as f:
                f.write("keep\n")
        return checks[-1]

    globals()["is_mount_point"] = lambda path, mounts=frozenset(): (
        os.path.normcase(path) == os.path.normcase(mounted)
        or saved_is_mount_point(path, mounts))
    globals()["reproducible_dir"] = mount_after_check
    try:
        t.expect("--check-reproducible stops at a mount point that appears in OUT after its last "
                 "check (exit 1)", ["--check-reproducible", mount_out], 1,
                 r"error: cannot empty \S.*: \S.* is a mount point; unmount it$")
    finally:
        globals()["is_mount_point"] = saved_is_mount_point
        globals()["reproducible_dir"] = saved_reproducible_dir
    t.check("a stopped --check-reproducible keeps what is below the mount point",
            read_text(below) == "keep\n")

    # An OUT that is itself a mount point is a whole file system, refused like
    # a file system root (the same replacement of is_mount_point).
    mount_root = os.path.join(t.root, "mount-root")
    os.makedirs(mount_root)
    in_root = os.path.join(mount_root, "keep.txt")
    with open(in_root, "w") as f:
        f.write("keep\n")
    globals()["is_mount_point"] = lambda path, mounts=frozenset(): (
        os.path.normcase(os.path.abspath(path)) == os.path.normcase(mount_root)
        or saved_is_mount_point(path, mounts))
    try:
        t.expect("--check-reproducible refuses an OUT that is a mount point as a file system "
                 "root (exit 2)", ["--check-reproducible", mount_root], 2,
                 r"the directory to empty is a file system root$")
    finally:
        globals()["is_mount_point"] = saved_is_mount_point
    t.check("a refused mount-point OUT keeps what is in it", read_text(in_root) == "keep\n")

    # A directory of the build or the repository that a link moved to another
    # place (a junction that puts <build>/lib/clang on another drive) holds
    # other names, or a c.dll without the marker (<build>/other stands for a
    # lit output directory that holds only c.dll), so an OUT that names that
    # place, or a directory holding it, is refused without a search for
    # links, whether the script uses the directory (lib/clang) or not
    # (<build>/other, <repository>/clang).
    stray = r"the directory to empty holds \S+, which is not a file the relink writes; " \
            r"give an empty or new directory$"
    unmarked = r"the directory to empty holds c\.dll but not \.build-libc-relink, so no relink " \
               r"wrote it; remove it or give an empty or new directory$"
    for n, (label, part, name) in enumerate((
            ("<build>/lib/clang", clang, "keep.txt"),
            ("<build>/other", os.path.join(t.build, "other"), "c.dll"),
            ("<repository>/clang", os.path.join(t.repo, "clang"), "keep.txt"))):
        holder = os.path.join(t.root, "moved-%d" % n)
        moved = os.path.join(holder, "part")
        os.makedirs(holder)
        os.makedirs(part, exist_ok=True)
        with open(os.path.join(part, name), "w") as f:
            f.write("keep\n")
        os.rename(part, moved)
        try:
            why = _make_link(kind, part, moved)
            if why:
                print("skip: --check-reproducible of the place a %s moved %s to (%s)"
                      % (kind, label, why))
                continue
            for what, where, needle in (
                    ("the place a link moved %s to" % label, moved,
                     unmarked if name == "c.dll" else stray),
                    ("a directory holding the place a link moved %s to" % label, holder, stray)):
                t.expect("--check-reproducible refuses %s (exit 2)" % what,
                         ["--check-reproducible", where], 2, needle)
            t.check("a refused --check-reproducible of the place a link moved %s to leaves it "
                    "alone" % label, read_text(os.path.join(moved, name)) == "keep\n")
        finally:
            remove_tree(part, ignore_errors=True)  # the link, never what it reaches
            if os.path.isdir(moved):
                os.rename(moved, part)

    # Links beside the marker that reach a directory outside OUT, and a file
    # link named c.dll in OUT (which holds the marker): each OUT is refused
    # and the links stay, and empty_dir removes them but nothing they reach.
    keep = os.path.join(t.root, "keep")
    os.makedirs(keep)
    keep_files = [os.path.join(keep, "keep.txt"), os.path.join(t.root, "keep-file.txt")]
    for path in keep_files:
        with open(path, "w") as f:
            f.write("keep\n")
    link_out = os.path.join(t.root, "link-out")
    os.makedirs(link_out)
    with open(os.path.join(link_out, RELINK_MARK), "w") as f:
        f.write("keep\n")
    made = []
    for kind, name in (("junction", "inner-junction"), ("symbolic link", "inner-symlink")):
        why = _make_link(kind, os.path.join(link_out, name), keep)
        if why:
            print("skip: an OUT that holds a %s (%s)" % (kind, why))
        else:
            made.append(os.path.join(link_out, name))
    if made:
        t.expect("--check-reproducible refuses an OUT that holds links to directories (exit 2)",
                 ["--check-reproducible", link_out], 2, r"holds %s, which is not a file the "
                 r"relink writes" % re.escape(os.path.basename(min(made))))
    remove_quietly(os.path.join(out, "c.dll"))
    why = _make_link("symbolic link", os.path.join(out, "c.dll"), keep_files[1])
    if why:
        print("skip: --check-reproducible of an OUT that holds a file link named c.dll (no file "
              "link: %s)" % why)
    else:
        made.append(os.path.join(out, "c.dll"))
        t.expect("--check-reproducible refuses an OUT that holds a file link named c.dll (exit 2)",
                 ["--check-reproducible", out], 2,
                 r"holds c\.dll, which is not a file the relink writes")
    if made:
        t.check("a refused --check-reproducible leaves the links in OUT and what they reach",
                all(is_link(p) for p in made) and all(read_text(p) == "keep\n" for p in keep_files))
        empty_dir(link_out)
        empty_dir(out)
        t.check("empty_dir removes the links in a directory but nothing they reach",
                all(read_text(p) == "keep\n" for p in keep_files)
                and not os.listdir(out) and not os.listdir(link_out),
                "%r %r" % (made, os.listdir(out) + os.listdir(link_out)))
    else:
        empty_dir(out)

    if os.name != "nt":
        print("skip: --check-reproducible through extended-length, 8.3 and UNC loopback "
              "paths (Windows only)")
        return
    ext = "\\\\?\\"
    refused("an extended-length path of <build>/lib/clang", ext + clang, lies)
    refused("an extended-length path of a directory holding the build", ext + t.root, holds)
    refused("a device path (\\\\.\\) of <build>/lib/clang", "\\\\.\\" + clang, lies)
    # os.path.abspath drops a trailing dot or space of a name in an
    # extended-length OUT's path, so without the refusal the sibling without
    # it (which holds only the marker, so it passes) would be emptied and
    # relinked into.
    for end, what in ((".", "dot"), (" ", "space")):
        for where, tail in (("whose name", ""), ("below a directory whose name", "out")):
            base = os.path.join(t.root, "dotted")
            sibling = os.path.join(base, tail) if tail else base
            named = os.path.join(ext + base + end, tail) if tail else ext + base + end
            os.makedirs(sibling)
            os.makedirs(named)
            for path in (os.path.join(sibling, RELINK_MARK), os.path.join(named, "keep.txt")):
                with open(path, "w") as f:
                    f.write("keep\n")
            try:
                t.expect("--check-reproducible refuses an extended-length OUT %s ends in a %s "
                         "(exit 2)" % (where, what), ["--check-reproducible", named], 2,
                         r"the directory to empty has a name in its path that ends in a dot or "
                         r"a space \('dotted%s'\), which a Windows path drops$" % re.escape(end))
                t.check("a refused --check-reproducible of an OUT %s ends in a %s leaves it and "
                        "its sibling without the %s alone" % (where, what, what),
                        os.listdir(sibling) == [RELINK_MARK]
                        and os.listdir(named) == ["keep.txt"]
                        and read_text(os.path.join(sibling, RELINK_MARK)) == "keep\n"
                        and read_text(os.path.join(named, "keep.txt")) == "keep\n",
                        "%r %r" % (os.listdir(sibling), os.listdir(named)))
            finally:
                remove_tree(ext + base + end, ignore_errors=True)
                remove_tree(base, ignore_errors=True)
    import ctypes
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    names = []
    for convert, label in ((kernel32.GetShortPathNameW, "the 8.3 short name"),
                           (kernel32.GetLongPathNameW, "the long name")):
        buf = ctypes.create_unicode_buffer(32768)
        if convert(clang, buf, len(buf)) and buf.value not in [n for n, _ in names] + [clang]:
            names.append((buf.value, label))
    if names:
        for name, label in names:
            refused("%s of <build>/lib/clang" % label, name, lies)
    else:
        print("skip: --check-reproducible through an 8.3 name (the volume keeps none)")
    drive, rest = os.path.splitdrive(clang)
    unc = "\\\\localhost\\%s$%s" % (drive[:1], rest)
    if len(drive) == 2 and os.path.isdir(unc):
        refused("a UNC loopback path (\\\\localhost\\<drive>$) of <build>/lib/clang", unc, lies)
        refused("an extended-length UNC loopback path of <build>/lib/clang",
                ext + "UNC" + unc[1:], lies)
    else:
        print("skip: --check-reproducible through a UNC loopback path (the administrative "
              "share of the drive is not reachable)")


def unchanged(path, before):
    """Whether path has the text and the file ID recorded in before, a
    (read_text, file_id) pair; the ID is compared only where the file
    system gives one, so a file deleted and written again is seen there."""
    return before[0] is not None and read_text(path) == before[0]         and (before[1] is None or file_id(path) == before[1])


def _self_test_inputs(t, installed, out):
    """--check-reproducible refuses an OUT that really holds a file the relink
    reads or compares with, wherever a file link or clang puts it. Each such
    file is named c.dll there, beside the marker, so OUT holds only what an
    earlier run writes and only this check refuses it (an installed c.dll
    that is a file link to an earlier run's output, say); the installed c.dll
    would otherwise be replaced by the relink and compared with itself. The
    files main() knows before the build-lock wait are refused before it, also
    from the command line (without t.ctx), by path alone where the file
    system gives no file ID, and by file identity through a UNC loopback path
    and a directory name that ends in a dot (Windows)."""
    held = r"the directory to empty really holds .+, a file the relink reads or compares with$"
    waits = []
    saved = enter_build_lock, t.ctx.lock, file_id

    def record_wait(build_dir, exclusive=False, wait_seconds=None):
        waits.append(build_dir)
        return lambda: None

    globals()["enter_build_lock"], t.ctx.lock = record_wait, True
    try:
        for n, (label, path) in enumerate((
                ("the helper archive", t.builtins),
                ("msil-ld", t.ctx.path("msil-ld")),
                ("the installed c.dll", installed),
                ("libc.a", os.path.join(t.build, LIBC_TREE, LIBC_ARCHIVE)))):
            place = os.path.join(t.root, "input-%d" % n)
            real = os.path.join(place, "c.dll")
            os.makedirs(place)
            with open(os.path.join(place, RELINK_MARK), "w") as f:
                f.write("keep\n")
            if not os.path.isfile(path):  # an earlier broken case removed it
                t.check("%s is there to be moved" % label, False)
                continue
            os.rename(path, real)
            try:
                why = _make_link("symbolic link", path, real)
                if why:
                    print("skip: --check-reproducible of the directory a file link moved %s to "
                          "(no file link: %s)" % (label, why))
                    continue
                # A relink of the installed c.dll would write the same text.
                before = read_text(real), file_id(real)
                del waits[:]
                t.expect("--check-reproducible refuses the directory a file link moved %s to "
                         "(exit 2)" % label, ["--check-reproducible", place], 2, held)
                if n:  # not the helper archive, which clang names after the wait
                    t.check("--check-reproducible refuses the directory a file link moved %s to "
                            "before the build-lock wait" % label, not waits)
                    # main() as the command line runs it, without t.ctx, with
                    # the stand-in repository as REPO (wherever the temporary
                    # directory lies); its msil-ld is <build>/bin/msil-ld.
                    bin_dir = os.path.join(t.build, "bin")
                    cli_ld = os.path.join(bin_dir, "msil-ld" + EXE)
                    new_bin = not os.path.isdir(bin_dir)
                    if label == "msil-ld":
                        os.makedirs(bin_dir, exist_ok=True)
                        _make_link("symbolic link", cli_ld, real)
                    cli = io.StringIO()
                    saved_repo = REPO
                    globals()["REPO"] = t.repo
                    try:
                        with contextlib.redirect_stdout(cli), contextlib.redirect_stderr(cli):
                            code = main(["--build-dir", t.build, "--check-reproducible", place])
                    finally:
                        globals()["REPO"] = saved_repo
                        if is_link(cli_ld):
                            os.remove(cli_ld)
                        if new_bin and os.path.isdir(bin_dir):
                            os.rmdir(bin_dir)
                    t.check("--check-reproducible refuses the directory a file link moved %s to "
                            "from the command line, before it looks for the build's tools "
                            "(exit 2)" % label, code == 2 and re.search(
                                held, cli.getvalue(), re.M) is not None,
                            "exit %s; output:\n%s" % (code, cli.getvalue()))
                if label == "the installed c.dll":
                    globals()["file_id"] = lambda path: None
                    try:
                        t.expect("--check-reproducible refuses the directory a file link moved "
                                 "the installed c.dll to by path where there are no file IDs "
                                 "(exit 2)", ["--check-reproducible", place], 2, held)
                    finally:
                        globals()["file_id"] = saved[2]
                    drive, rest = os.path.splitdrive(place)
                    unc = "\\\\localhost\\%s$%s" % (drive[:1], rest)
                    if os.name == "nt" and len(drive) == 2 and os.path.isdir(unc):
                        t.expect("--check-reproducible refuses a UNC loopback path of the "
                                 "directory a file link moved the installed c.dll to (exit 2)",
                                 ["--check-reproducible", unc], 2, held)
                    else:
                        print("skip: --check-reproducible of a UNC loopback path of an input's "
                              "directory (Windows only, with the drive's administrative share)")
                t.check("a refused --check-reproducible leaves %s and its file link in place"
                        % label, unchanged(real, before) and is_link(path))
            finally:
                if is_link(path):
                    os.remove(path)
                if os.path.isfile(real):
                    os.rename(real, path)
    finally:
        globals()["enter_build_lock"], t.ctx.lock, globals()["file_id"] = saved
    # The installed c.dll moved into a directory whose name ends in a dot
    # (made through the extended-length prefix) and reached through a file
    # link on a UNC loopback path, OUT a local link to that directory: their
    # path forms differ and lose the dot, so only the file identity of OUT's
    # c.dll refuses it.
    drive, rest = os.path.splitdrive(t.root)
    dot = "\\\\?\\" + os.path.join(t.root, "input-dot.")
    why = None if os.name == "nt" and len(drive) == 2 else "Windows only"
    if not why and not os.path.isdir("\\\\localhost\\%s$%s" % (drive[:1], rest)):
        why = "the drive's administrative share is not reachable"
    if not why:
        real, link_out = os.path.join(dot, "c.dll"), os.path.join(t.root, "input-dot-out")
        os.makedirs(dot)
        with open(os.path.join(dot, RELINK_MARK), "w") as f:
            f.write("keep\n")
        os.rename(installed, real)
        try:
            why = _make_link("symbolic link", installed, "\\\\?\\UNC\\localhost\\%s$%s" % (
                drive[:1], os.path.join(rest, "input-dot.", "c.dll")))
            why = why or _make_link("symbolic link", link_out, dot)
            why = why and "no file link: " + why
            if not why:
                before = read_text(real), file_id(real)  # a relink would write the same text
                t.expect("--check-reproducible refuses a link to a directory named with a "
                         "trailing dot that really holds the installed c.dll, a file link to "
                         "it through a UNC loopback path (exit 2)",
                         ["--check-reproducible", link_out], 2, held)
                t.check("a refused --check-reproducible leaves the installed c.dll in a "
                        "directory named with a trailing dot in place",
                        unchanged(real, before) and is_link(installed))
        finally:
            for path in (link_out, installed):
                if is_link(path):
                    remove_tree(path)
            os.rename(real, installed)
            os.remove(os.path.join(dot, RELINK_MARK))
            os.rmdir(dot)
    if why:
        print("skip: --check-reproducible of a link to a directory named with a trailing dot "
              "that holds the installed c.dll (%s)" % why)
    # A helper archive outside the build, as a clang built with an absolute
    # CLANG_RESOURCE_DIR reports it; only the check before emptying sees it.
    place = os.path.join(t.root, "input-resource")
    real = os.path.join(place, "c.dll")
    os.makedirs(place)
    with open(os.path.join(place, RELINK_MARK), "w") as f:
        f.write("keep\n")
    with open(real, "w", newline="\n") as f:
        f.write("!<stand-in builtins>\n")
    saved = os.environ["BL_STANDIN_BUILTINS"]
    os.environ["BL_STANDIN_BUILTINS"] = real
    try:
        t.expect("--check-reproducible refuses the directory of a helper archive outside the "
                 "build (exit 2)", ["--check-reproducible", place], 2, held)
    finally:
        os.environ["BL_STANDIN_BUILTINS"] = saved
    t.check("a refused --check-reproducible leaves a helper archive outside the build in place",
            read_text(real) == "!<stand-in builtins>\n")
    # A relinked c.dll that is the installed one (a link swapped in after the
    # last check) would compare with itself; a relink that returns the
    # installed file stands in for it.
    saved_link = link_cdll
    globals()["link_cdll"] = lambda ctx, archive, builtins, staging: installed
    try:
        t.expect("--check-reproducible refuses a relinked c.dll that is the installed one "
                 "(exit 2)", ["--check-reproducible", out], 2,
                 r"the relinked c\.dll is the installed .*c\.dll itself$")
    finally:
        globals()["link_cdll"] = saved_link
    empty_dir(out)


def _self_test_stream(t):
    """run_streamed's deadline holds while the child prints nothing."""
    py = [sys.executable, "-I", "-S", "-c"]
    for name, code, line in (
        ("a silent child", "import time; time.sleep(60)", None),
        ("a child that prints a line and then goes silent",
         "import sys, time; print('started'); sys.stdout.flush(); time.sleep(60)", "started"),
    ):
        log, out, err = [], io.StringIO(), io.StringIO()
        start = time.monotonic()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            got = run_streamed(py + [code], None, 2, log)
        took = time.monotonic() - start
        t.check("run_streamed times out %s in about the timeout" % name,
                got is None and took < 8 and "timed out after 2 s" in err.getvalue(),
                "returned %r after %.1f s: %s" % (got, took, err.getvalue()))
        if line:
            t.check("run_streamed keeps the line before the silence in the log",
                    [l.strip() for l in log] == [line] and line in out.getvalue(), repr(log))
    log = []
    with contextlib.redirect_stdout(io.StringIO()):
        got = run_streamed(py + ["print('a'); print('b'); raise SystemExit(3)"], None, 60, log)
    t.check("run_streamed returns a finished child's exit status and copies its output",
            got == 3 and [l.strip() for l in log] == ["a", "b"], "%r %r" % (got, log))


def _self_test_lock(t):
    global LOCK_WAIT_SECONDS
    lockdir = os.path.join(t.root, "lock-build")

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

    saved_wait, LOCK_WAIT_SECONDS = LOCK_WAIT_SECONDS, 0
    saved = (t.ctx.build_dir, t.ctx.lock)
    t.ctx.lock = True
    try:
        os.makedirs(lockdir)
        reader = enter_build_lock(lockdir, wait_seconds=0)
        t.ctx.build_dir = lockdir
        t.expect("a build waits for the exclusive build lock while a reader holds it", [], 1,
                 r"timed out after 0 s waiting for")
        reader()
        writer = enter_build_lock(lockdir, exclusive=True, wait_seconds=0)
        t.check("a writer records itself in the lock file",
                "what=%s" % PROG in (read_text(os.path.join(lockdir, LOCK_FILE)) or ""))
        blocked("a reader waits while a writer holds the build lock", False)
        writer()
        enter_build_lock(lockdir, exclusive=True, wait_seconds=0)()
        t.check("a released lock can be taken again", True)
        linkdir = os.path.join(t.root, "lock-link")
        os.makedirs(linkdir)
        target = os.path.join(t.root, "lock-target.txt")
        with open(target, "w") as f:
            f.write("keep\n")
        why = _make_link("symbolic link", os.path.join(linkdir, LOCK_FILE), target)
        if why:
            print("skip: a lock file that is a symbolic link (%s)" % why)
        else:
            try:
                enter_build_lock(linkdir, exclusive=True, wait_seconds=0)()
                refused = ""
            except UsageError as e:
                refused = str(e)
            t.check("a lock file that is a symbolic link is refused and its target left alone",
                    "is a link" in refused and read_text(target) == "keep\n", refused)
            # The same link planted after the is_link check: the open handle
            # is the link itself (FILE_FLAG_OPEN_REPARSE_POINT).
            saved_is_link = is_link
            globals()["is_link"] = lambda path: False
            try:
                enter_build_lock(linkdir, exclusive=True, wait_seconds=0)()
                refused = ""
            except UsageError as e:
                refused = str(e)
            finally:
                globals()["is_link"] = saved_is_link
            t.check("a lock file that becomes a symbolic link after the check is refused on "
                    "the open handle and its target left alone",
                    "is a link" in refused and read_text(target) == "keep\n", refused)
        harddir = os.path.join(t.root, "lock-hard")
        os.makedirs(harddir)
        other = os.path.join(t.root, "lock-other.txt")
        with open(other, "w") as f:
            f.write("keep\n")
        try:
            os.link(other, os.path.join(harddir, LOCK_FILE))
            why = None
        except (OSError, AttributeError) as e:
            why = os_error_text(e) if isinstance(e, OSError) else str(e)
        if why:
            print("skip: a lock file that is a hard link (%s)" % why)
        else:
            try:
                enter_build_lock(harddir, exclusive=True, wait_seconds=0)()
                refused = ""
            except UsageError as e:
                refused = str(e)
            t.check("a writer refuses a lock file that is a hard link and leaves the other "
                    "file alone", "is a hard link" in refused and read_text(other) == "keep\n",
                    refused)
    finally:
        LOCK_WAIT_SECONDS = saved_wait
        t.ctx.build_dir, t.ctx.lock = saved


if __name__ == "__main__":
    sys.exit(main())

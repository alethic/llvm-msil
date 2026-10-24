#!/usr/bin/env python3
# ===-- build-mdtools.py - Build the C# metadata oracles offline ----------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Build msil/utils/mdtools/{mdump,mdcanon}.cs without a NuGet restore.

    python build-mdtools.py [--out DIR] [--build-dir DIR] [--dotnet PATH]
                            [--ref-major N] [--force]

The method is msil/utils/jitall/build-jitall.py's (plan 5.7.11): the newest
installed .NET SDK's Roslyn compiler (<sdk>/Roslyn/bincore/csc.dll) compiles
each tool against every reference assembly of the newest installed
Microsoft.NETCore.App.Ref <N>.x targeting pack (N = 8 by default), and the
script writes <tool>.runtimeconfig.json (tfm net<N>.0, Microsoft.NETCore.App
<N>.0.0, rollForward Major) next to each <tool>.dll. Nothing is downloaded.

Outputs, in --out (default <build>/tools/msil/mdtools, where <build> is
--build-dir, else $MSIL_BUILD_DIR, else <repo>/build):

    mdump.dll    mdump.runtimeconfig.json
    mdcanon.dll  mdcanon.runtimeconfig.json
    mdtools.stamp

Run a tool as 'dotnet exec --runtimeconfig <dir>/mdcanon.runtimeconfig.json
<dir>/mdcanon.dll ...' (or 'dotnet <dir>/mdcanon.dll ...').

The tools are rebuilt only when a source (mdump.cs, mdcanon.cs or this
script) changed since the last build, so lit can call this script on every
configuration. A build is compiled into a temporary sibling directory that is
renamed into place, so a concurrent run never sees a half-written tool.

Exit status: 0 when the tools are built or up to date, 1 on any error.
"""

import argparse
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
TOOLS = ("mdump", "mdcanon")
SOURCES = [os.path.join(HERE, t + ".cs") for t in TOOLS]
STAMP = "mdtools.stamp"
OUTPUTS = tuple(
    name for t in TOOLS for name in (t + ".dll", t + ".runtimeconfig.json")
) + (STAMP,)
IS_WINDOWS = os.name == "nt"
EXE = ".exe" if IS_WINDOWS else ""


class BuildError(Exception):
    pass


def log(msg):
    print("build-mdtools: " + msg, flush=True)


def version_key(text):
    """'10.0.401' -> (10, 0, 401, 1); a prerelease sorts below its release."""
    core, _, pre = text.partition("-")
    nums = [int(n) for n in re.findall(r"\d+", core)[:3]]
    nums += [0] * (3 - len(nums))
    return tuple(nums) + (0 if pre else 1,)


def default_build_dir():
    return os.environ.get("MSIL_BUILD_DIR") or os.path.join(REPO, "build")


def default_out_dir(build_dir):
    return os.path.join(build_dir, "tools", "msil", "mdtools")


def child_env():
    env = dict(os.environ)
    env.setdefault("DOTNET_CLI_TELEMETRY_OPTOUT", "1")
    env.setdefault("DOTNET_NOLOGO", "1")
    env.setdefault("DOTNET_SKIP_FIRST_TIME_EXPERIENCE", "1")
    return env


def find_dotnet(explicit):
    if explicit:
        if not os.path.isfile(explicit):
            raise BuildError("--dotnet %s does not exist" % explicit)
        return explicit
    candidates = [shutil.which("dotnet")]
    if os.environ.get("DOTNET_ROOT"):
        candidates.append(os.path.join(os.environ["DOTNET_ROOT"], "dotnet" + EXE))
    if IS_WINDOWS:
        candidates.append(r"C:\Program Files\dotnet\dotnet.exe")
    else:
        candidates += [
            "/usr/share/dotnet/dotnet",
            "/usr/lib/dotnet/dotnet",
            "/usr/local/share/dotnet/dotnet",
        ]
    candidates.append(os.path.join(os.path.expanduser("~"), ".dotnet", "dotnet" + EXE))
    for c in candidates:
        if c and os.path.isfile(c):
            return c
    raise BuildError("no dotnet host found (pass --dotnet or set DOTNET_ROOT)")


def find_csc(dotnet):
    """Newest SDK that has Roslyn/bincore/csc.dll, from 'dotnet --list-sdks'."""
    try:
        listing = subprocess.run(
            [dotnet, "--list-sdks"],
            capture_output=True,
            text=True,
            env=child_env(),
            timeout=120,
        ).stdout
    except (OSError, subprocess.SubprocessError) as e:
        raise BuildError("'%s --list-sdks' failed: %s" % (dotnet, e))
    sdks = []
    for m in re.finditer(r"^(\S+) \[(.+)\]\s*$", listing, re.M):
        sdks.append((version_key(m.group(1)), m.group(1), m.group(2)))
    for _, ver, base in sorted(sdks, reverse=True):
        csc = os.path.join(base, ver, "Roslyn", "bincore", "csc.dll")
        if os.path.isfile(csc):
            return ver, base, csc
    raise BuildError(
        "no .NET SDK with Roslyn/bincore/csc.dll found by '%s --list-sdks'" % dotnet
    )


def find_ref_pack(roots, major):
    """Newest Microsoft.NETCore.App.Ref <major>.x pack: (version, ref dir)."""
    best = None
    for root in roots:
        pattern = os.path.join(root, "packs", "Microsoft.NETCore.App.Ref", "*")
        for vdir in glob.glob(pattern):
            ver = os.path.basename(vdir)
            refdir = os.path.join(vdir, "ref", "net%d.0" % major)
            if (
                version_key(ver)[0] == major
                and os.path.isdir(refdir)
                and (best is None or version_key(ver) > version_key(best[0]))
            ):
                best = (ver, refdir)
    if best is None:
        raise BuildError(
            "no Microsoft.NETCore.App.Ref %d.x targeting pack under %s "
            "(install the .NET %d SDK)"
            % (major, " or ".join(os.path.join(r, "packs") for r in roots), major)
        )
    return best


def stamp_text(major):
    h = hashlib.sha256()
    for path in SOURCES + [os.path.abspath(__file__)]:
        with open(path, "rb") as f:
            h.update(f.read())
    return "mdump.cs+mdcanon.cs+build-mdtools.py sha256 %s ref-major %d\n" % (
        h.hexdigest(),
        major,
    )


def up_to_date(out, major):
    paths = [os.path.join(out, name) for name in OUTPUTS]
    if not all(os.path.isfile(p) for p in paths):
        return False
    built = min(os.path.getmtime(p) for p in paths)
    if any(os.path.getmtime(p) > built for p in SOURCES + [os.path.abspath(__file__)]):
        return False
    with open(os.path.join(out, STAMP), "r", encoding="utf-8") as f:
        return f.read() == stamp_text(major)


def compile_into(tmp, dotnet, csc, refdir, major):
    refs = sorted(glob.glob(os.path.join(refdir, "*.dll")))
    if not refs:
        raise BuildError("no reference assemblies in %s" % refdir)
    rsp = os.path.join(tmp, "mdtools.rsp")
    with open(rsp, "w", encoding="utf-8", newline="\n") as f:
        for r in refs:
            f.write('-r:"%s"\n' % r)
    config = {
        "runtimeOptions": {
            "tfm": "net%d.0" % major,
            "rollForward": "Major",
            "framework": {"name": "Microsoft.NETCore.App", "version": "%d.0.0" % major},
        }
    }
    for tool, source in zip(TOOLS, SOURCES):
        dll = os.path.join(tmp, tool + ".dll")
        cmd = [
            dotnet,
            csc,
            "-nologo",
            "-noconfig",
            "-nostdlib+",
            "-target:exe",
            "-nullable-",
            "-optimize+",
            "-deterministic+",
            "-out:" + dll,
            "@" + rsp,
            source,
        ]
        try:
            proc = subprocess.run(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                env=child_env(),
                timeout=600,
            )
        except (OSError, subprocess.SubprocessError) as e:
            raise BuildError("running csc failed: %s" % e)
        text = proc.stdout.decode("utf-8", errors="replace")
        if proc.returncode != 0 or not os.path.isfile(dll):
            raise BuildError(
                "csc failed on %s (exit %d):\n%s" % (source, proc.returncode, text)
            )
        if text.strip():
            sys.stdout.write(text)
        with open(
            os.path.join(tmp, tool + ".runtimeconfig.json"),
            "w",
            encoding="utf-8",
            newline="\n",
        ) as f:
            json.dump(config, f, indent=2)
            f.write("\n")
    os.remove(rsp)
    # Written last: its presence marks a complete build.
    with open(os.path.join(tmp, STAMP), "w", encoding="utf-8", newline="\n") as f:
        f.write(stamp_text(major))


def install(tmp, out, major, force):
    """Rename the finished build directory 'tmp' to 'out'.

    Renaming a directory onto an existing one fails on Windows and POSIX, so a
    stale 'out' is first moved aside. If another process installs a fresh build
    meanwhile, ours is discarded (not with --force, which always replaces
    'out'). On Windows a directory cannot be moved while a running tool has its
    .dll open, so that is retried for about 10 s.
    """
    error = None
    for _ in range(20):
        try:
            os.rename(tmp, out)
            return True
        except OSError:
            pass
        if not force and up_to_date(out, major):
            return False  # a concurrent build won the race
        aside = tempfile.mkdtemp(
            prefix=os.path.basename(out) + ".old-", dir=os.path.dirname(out)
        )
        try:
            os.rename(out, os.path.join(aside, "mdtools"))
        except FileNotFoundError:
            pass  # someone else moved it; retry
        except OSError as e:
            error = e
            time.sleep(0.5)
        finally:
            shutil.rmtree(aside, ignore_errors=True)
    raise BuildError(
        "cannot replace %s (is mdump or mdcanon running?): %s" % (out, error)
    )


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument(
        "--out", help="output directory (default <build>/tools/msil/mdtools)"
    )
    p.add_argument(
        "--build-dir",
        help="LLVM build directory (default $MSIL_BUILD_DIR, else <repo>/build)",
    )
    p.add_argument(
        "--dotnet",
        help="dotnet host (default: from PATH, DOTNET_ROOT or the "
        "standard install locations)",
    )
    p.add_argument(
        "--ref-major",
        type=int,
        default=8,
        help="major version of the targeting pack and runtimeconfig (default 8)",
    )
    p.add_argument("--force", action="store_true", help="rebuild even if up to date")
    args = p.parse_args()

    out = os.path.abspath(
        args.out or default_out_dir(args.build_dir or default_build_dir())
    )
    if not args.force and up_to_date(out, args.ref_major):
        log("%s is up to date" % out)
        return 0

    try:
        dotnet = find_dotnet(args.dotnet)
        sdk_ver, sdk_base, csc = find_csc(dotnet)
        roots = []
        for r in (
            os.path.dirname(os.path.abspath(sdk_base)),
            os.path.dirname(os.path.realpath(dotnet)),
        ):
            if r not in roots:
                roots.append(r)
        pack_ver, refdir = find_ref_pack(roots, args.ref_major)

        parent = os.path.dirname(out)
        os.makedirs(parent, exist_ok=True)
        tmp = tempfile.mkdtemp(prefix=os.path.basename(out) + ".tmp-", dir=parent)
        try:
            compile_into(tmp, dotnet, csc, refdir, args.ref_major)
            installed = install(tmp, out, args.ref_major, args.force)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    except BuildError as e:
        sys.stderr.write("build-mdtools: error: %s\n" % e)
        return 1

    if installed:
        log(
            "built %s (%s; SDK %s, Microsoft.NETCore.App.Ref %s)"
            % (out, ", ".join(t + ".dll" for t in TOOLS), sdk_ver, pack_ver)
        )
    else:
        log("%s was built concurrently; using it" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())

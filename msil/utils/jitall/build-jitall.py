#!/usr/bin/env python3
# ===-- build-jitall.py - Build the jitall force-JIT tool offline ---------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Build msil/utils/jitall/jitall.cs into jitall.dll without a NuGet restore.

    python build-jitall.py [--out DIR] [--build-dir DIR] [--dotnet PATH]
                           [--ref-major N] [--force]
    python build-jitall.py --csharp SRC --out EXE [--reference DLL]...
                           [--refs net8|net48] [--unsafe] [--dotnet PATH]

The newest installed .NET SDK's Roslyn compiler (<sdk>/Roslyn/bincore/csc.dll)
compiles jitall.cs against every reference assembly of the newest installed
Microsoft.NETCore.App.Ref <N>.x targeting pack (N = 8 by default), and the
script writes jitall.runtimeconfig.json (tfm net<N>.0, Microsoft.NETCore.App
<N>.0.0, rollForward Major) next to jitall.dll. Nothing is downloaded.

--out defaults to <build>/tools/msil/jitall, where <build> is --build-dir,
else $MSIL_BUILD_DIR, else <repo>/build. That is the directory msil-jitall.py
and msil/test/lit.cfg.py use.

The tool is rebuilt only when jitall.cs (or this script) changed since the
last build, so lit can call this script on every configuration. A build is
compiled into a temporary sibling directory that is renamed into place, so a
concurrent run never sees a half-written jitall.dll.

--csharp SRC (plan 5.12.4, M22; lit's %msil_csc) compiles one other C#
file, a program, with the same csc into --out EXE (-target:exe
-langversion:latest -deterministic+, -unsafe+ with --unsafe, a -r: per
--reference DLL, such as a library assembly built by this toolchain):
  net8   (the default) against every reference assembly of the newest
         Microsoft.NETCore.App.Ref 8.x pack ($MSIL_REF_NET8, the pack's
         ref/net8.0 directory, names another), and writes
         <stem>.runtimeconfig.json (tfm net8.0, Microsoft.NETCore.App 8.0.0,
         rollForward Major) next to EXE, so 'dotnet exec EXE' runs it;
  net48  against the .NET Framework 4.8 reference assemblies and their
         Facades: $MSIL_REF_NET48, else the Windows reference directory
         (%ProgramFiles(x86)%/Reference Assemblies/Microsoft/Framework/
         .NETFramework/v4.8), else build/.NETFramework/v4.8 of the NuGet
         package microsoft.netframework.referenceassemblies.net48 at the
         version msil/utils/tools/msil-tools.proj pins
         (MsilNet48RefAssembliesVersion) in a NuGet global-packages folder;
         the EXE runs on the .NET Framework.
It is compiled every time (no up-to-date check) and the jitall build is not
touched. Nothing is downloaded either way.

Exit status: 0 when jitall.dll is built or up to date (with --csharp: when
EXE is compiled), 1 on any error, 2 on a usage error.
"""

import argparse
import glob
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
SOURCE = os.path.join(HERE, "jitall.cs")
IS_WINDOWS = os.name == "nt"
EXE = ".exe" if IS_WINDOWS else ""

OUTPUTS = ("jitall.dll", "jitall.runtimeconfig.json", "jitall.stamp")


class BuildError(Exception):
    pass


def log(msg):
    print("build-jitall: " + msg, flush=True)


def version_key(text):
    """'10.0.401' -> (10, 0, 401, 1); a prerelease sorts below its release."""
    core, _, pre = text.partition("-")
    nums = [int(n) for n in re.findall(r"\d+", core)[:3]]
    nums += [0] * (3 - len(nums))
    return tuple(nums) + (0 if pre else 1,)


def default_build_dir():
    return os.environ.get("MSIL_BUILD_DIR") or os.path.join(REPO, "build")


def default_out_dir(build_dir):
    return os.path.join(build_dir, "tools", "msil", "jitall")


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
    for path in (SOURCE, os.path.abspath(__file__)):
        with open(path, "rb") as f:
            h.update(f.read())
    return "jitall.cs+build-jitall.py sha256 %s ref-major %d\n" % (h.hexdigest(), major)


def up_to_date(out, major):
    paths = [os.path.join(out, name) for name in OUTPUTS]
    if not all(os.path.isfile(p) for p in paths):
        return False
    dll_time = os.path.getmtime(paths[0])
    if any(os.path.getmtime(p) > dll_time for p in (SOURCE, os.path.abspath(__file__))):
        return False
    with open(paths[2], "r", encoding="utf-8") as f:
        return f.read() == stamp_text(major)


def compile_into(tmp, dotnet, csc, refdir, major):
    refs = sorted(glob.glob(os.path.join(refdir, "*.dll")))
    if not refs:
        raise BuildError("no reference assemblies in %s" % refdir)
    rsp = os.path.join(tmp, "jitall.rsp")
    with open(rsp, "w", encoding="utf-8", newline="\n") as f:
        for r in refs:
            f.write('-r:"%s"\n' % r)
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
        "-out:" + os.path.join(tmp, "jitall.dll"),
        "@" + rsp,
        SOURCE,
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
    if proc.returncode != 0 or not os.path.isfile(os.path.join(tmp, "jitall.dll")):
        raise BuildError("csc failed (exit %d):\n%s" % (proc.returncode, text))
    if text.strip():
        sys.stdout.write(text)
    os.remove(rsp)

    config = {
        "runtimeOptions": {
            "tfm": "net%d.0" % major,
            "rollForward": "Major",
            "framework": {"name": "Microsoft.NETCore.App", "version": "%d.0.0" % major},
        }
    }
    with open(
        os.path.join(tmp, "jitall.runtimeconfig.json"),
        "w",
        encoding="utf-8",
        newline="\n",
    ) as f:
        json.dump(config, f, indent=2)
        f.write("\n")
    # Written last: its presence marks a complete build.
    with open(
        os.path.join(tmp, "jitall.stamp"), "w", encoding="utf-8", newline="\n"
    ) as f:
        f.write(stamp_text(major))


def is_assembly(path):
    """Whether path is an assembly: a PE file with a CLI header whose
    metadata has an Assembly row (the .NET Framework's v4.8 directory also
    holds native DLLs and a module, System.EnterpriseServices.Thunk.dll and
    .Wrapper.dll, which csc refuses as references)."""
    try:
        with open(path, "rb") as f:
            data = f.read()
        u16 = lambda o: struct.unpack_from("<H", data, o)[0]
        u32 = lambda o: struct.unpack_from("<I", data, o)[0]
        if data[:2] != b"MZ":
            return False
        pe = u32(0x3C)
        if data[pe : pe + 4] != b"PE" + bytes(2):
            return False
        nsections, opt_size, opt = u16(pe + 6), u16(pe + 20), pe + 24
        count_at, dirs_at = {0x10B: (92, 96), 0x20B: (108, 112)}.get(
            u16(opt), (None, None)
        )
        if count_at is None or u32(opt + count_at) <= 14:
            return False
        sections = []
        for i in range(nsections):
            at = opt + opt_size + 40 * i
            vsize, va, rawsize, raw = struct.unpack_from("<IIII", data, at + 8)
            sections.append((va, max(vsize, rawsize), raw))

        def offset(rva):
            for va, size, raw in sections:
                if va <= rva < va + size:
                    return raw + rva - va
            raise ValueError("RVA 0x%X lies in no section" % rva)

        cli_rva = u32(opt + dirs_at + 14 * 8)
        if not cli_rva:
            return False
        root = offset(u32(offset(cli_rva) + 8))
        if u32(root) != 0x424A5342:
            return False
        at = root + 16 + u32(root + 12) + 2
        count = u16(at)
        at += 2
        for _ in range(count):
            off, end = u32(at), data.index(b"\0", at + 8)
            if data[at + 8 : end] in (b"#~", b"#-"):
                valid = struct.unpack_from("<Q", data, root + off + 8)[0]
                return bool(valid >> 0x20 & 1)
            at = at + 8 + ((end - (at + 8)) // 4 + 1) * 4
        return False
    except (OSError, ValueError, struct.error):
        return False


def net48_ref_dir(dotnet):
    """The .NET Framework 4.8 reference directory for --csharp --refs net48."""
    explicit = os.environ.get("MSIL_REF_NET48")
    if explicit:
        if not os.path.isfile(os.path.join(explicit, "mscorlib.dll")):
            raise BuildError("$MSIL_REF_NET48=%s has no mscorlib.dll" % explicit)
        return explicit
    candidates = []
    if IS_WINDOWS:
        candidates.append(
            os.path.join(
                os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                "Reference Assemblies",
                "Microsoft",
                "Framework",
                ".NETFramework",
                "v4.8",
            )
        )
    proj = os.path.join(REPO, "msil", "utils", "tools", "msil-tools.proj")
    try:
        with open(proj, encoding="utf-8") as f:
            m = re.search(
                r"<MsilNet48RefAssembliesVersion[^>]*>\s*([0-9.]+)\s*<", f.read()
            )
    except OSError:
        m = None
    if m:
        roots = [
            os.environ.get("NUGET_PACKAGES"),
            os.path.join(os.path.expanduser("~"), ".nuget", "packages"),
        ]
        try:
            listing = subprocess.run(
                [dotnet, "nuget", "locals", "global-packages", "--list"],
                capture_output=True,
                text=True,
                env=child_env(),
                timeout=120,
            ).stdout
            found = re.search(r"^global-packages:\s*(.+?)\s*$", listing, re.M)
            if found:
                roots.append(found.group(1))
        except (OSError, subprocess.SubprocessError):
            pass
        candidates += [
            os.path.join(
                r,
                "microsoft.netframework.referenceassemblies.net48",
                m.group(1),
                "build",
                ".NETFramework",
                "v4.8",
            )
            for r in roots
            if r
        ]
    for c in candidates:
        if os.path.isfile(os.path.join(c, "mscorlib.dll")):
            return c
    raise BuildError(
        "no .NET Framework 4.8 reference assemblies (searched %s; set "
        "$MSIL_REF_NET48)" % (", ".join(candidates) or "nothing")
    )


def compile_csharp(args, dotnet, sdk_ver, sdk_base, csc):
    """--csharp: compile args.csharp into args.out."""
    src, out = os.path.abspath(args.csharp), os.path.abspath(args.out)
    if not os.path.isfile(src):
        raise BuildError("--csharp %s: no such file" % args.csharp)
    for r in args.reference:
        if not os.path.isfile(r):
            raise BuildError("--reference %s: no such file" % r)
    if args.refs == "net8":
        explicit = os.environ.get("MSIL_REF_NET8")
        if explicit:
            if not os.path.isfile(os.path.join(explicit, "System.Runtime.dll")):
                raise BuildError(
                    "$MSIL_REF_NET8=%s has no System.Runtime.dll" % explicit
                )
            pack_ver, refdirs = "from $MSIL_REF_NET8", [explicit]
        else:
            roots = []
            for r in (
                os.path.dirname(os.path.abspath(sdk_base)),
                os.path.dirname(os.path.realpath(dotnet)),
            ):
                if r not in roots:
                    roots.append(r)
            pack_ver, refdir = find_ref_pack(roots, 8)
            refdirs = [refdir]
    else:
        refdir = net48_ref_dir(dotnet)
        pack_ver, refdirs = "v4.8", [refdir, os.path.join(refdir, "Facades")]
    refs = []
    for d in refdirs:
        # v4.8 also holds native DLLs (System.EnterpriseServices.Thunk.dll).
        refs += [
            r for r in sorted(glob.glob(os.path.join(d, "*.dll"))) if is_assembly(r)
        ]
    if not refs:
        raise BuildError("no reference assemblies in %s" % refdirs[0])
    os.makedirs(os.path.dirname(out), exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="build-jitall-csharp-")
    try:
        rsp = os.path.join(tmp, "csc.rsp")
        with open(rsp, "w", encoding="utf-8", newline="\n") as f:
            for r in refs + [os.path.abspath(x) for x in args.reference]:
                f.write('-r:"%s"\n' % r)
        cmd = [dotnet, csc, "-nologo", "-noconfig", "-nostdlib+", "-target:exe"]
        cmd += ["-langversion:latest", "-deterministic+"]
        if args.unsafe:
            cmd.append("-unsafe+")
        cmd += ["-out:" + out, "@" + rsp, src]
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
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    text = proc.stdout.decode("utf-8", errors="replace")
    if proc.returncode != 0 or not os.path.isfile(out):
        raise BuildError("csc failed (exit %d):\n%s" % (proc.returncode, text))
    if text.strip():
        sys.stdout.write(text)
    if args.refs == "net8":
        config = {
            "runtimeOptions": {
                "tfm": "net8.0",
                "rollForward": "Major",
                "framework": {"name": "Microsoft.NETCore.App", "version": "8.0.0"},
            }
        }
        stem = os.path.splitext(out)[0]
        with open(
            stem + ".runtimeconfig.json", "w", encoding="utf-8", newline="\n"
        ) as f:
            json.dump(config, f, indent=2)
            f.write("\n")
    log("compiled %s (SDK %s, %s references %s)" % (out, sdk_ver, args.refs, pack_ver))


def install(tmp, out, major):
    """Rename the finished build directory 'tmp' to 'out'.

    Renaming a directory onto an existing one fails on Windows and POSIX, so a
    stale 'out' is first moved aside. If another process installs a fresh build
    meanwhile, ours is discarded. On Windows a directory cannot be moved while
    a running jitall has jitall.dll open, so that is retried for about 10 s.
    """
    error = None
    for _ in range(20):
        try:
            os.rename(tmp, out)
            return True
        except OSError:
            pass
        if up_to_date(out, major):
            return False  # a concurrent build won the race
        aside = tempfile.mkdtemp(
            prefix=os.path.basename(out) + ".old-", dir=os.path.dirname(out)
        )
        try:
            os.rename(out, os.path.join(aside, "jitall"))
        except FileNotFoundError:
            pass  # someone else moved it; retry
        except OSError as e:
            error = e
            time.sleep(0.5)
        finally:
            shutil.rmtree(aside, ignore_errors=True)
    raise BuildError("cannot replace %s (is jitall running?): %s" % (out, error))


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--out", help="output directory (default <build>/tools/msil/jitall)")
    p.add_argument(
        "--build-dir",
        help="LLVM build directory (default $MSIL_BUILD_DIR, " "else <repo>/build)",
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
    p.add_argument(
        "--csharp",
        metavar="SRC",
        help="compile this C# program into --out EXE instead of building jitall",
    )
    p.add_argument(
        "--reference",
        action="append",
        default=[],
        metavar="DLL",
        help="with --csharp: an assembly the program references (repeatable)",
    )
    p.add_argument(
        "--refs",
        choices=("net8", "net48"),
        help="with --csharp: the reference assemblies (default net8)",
    )
    p.add_argument("--unsafe", action="store_true", help="with --csharp: -unsafe+")
    args = p.parse_args()
    if args.csharp is None:
        if args.reference or args.refs or args.unsafe:
            p.error("--reference, --refs and --unsafe go with --csharp")
    else:
        if not args.out:
            p.error("--csharp needs --out EXE")
        if args.force or args.build_dir:
            p.error("--force and --build-dir build jitall, not a --csharp program")
        args.refs = args.refs or "net8"
        try:
            dotnet = find_dotnet(args.dotnet)
            sdk_ver, sdk_base, csc = find_csc(dotnet)
            compile_csharp(args, dotnet, sdk_ver, sdk_base, csc)
        except BuildError as e:
            sys.stderr.write("build-jitall: error: %s\n" % e)
            return 1
        return 0

    out = os.path.abspath(
        args.out or default_out_dir(args.build_dir or default_build_dir())
    )
    if not args.force and up_to_date(out, args.ref_major):
        log("%s is up to date" % os.path.join(out, "jitall.dll"))
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
            installed = install(tmp, out, args.ref_major)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    except BuildError as e:
        sys.stderr.write("build-jitall: error: %s\n" % e)
        return 1

    if installed:
        log(
            "built %s (SDK %s, Microsoft.NETCore.App.Ref %s)"
            % (os.path.join(out, "jitall.dll"), sdk_ver, pack_ver)
        )
    else:
        log("%s was built concurrently; using it" % os.path.join(out, "jitall.dll"))
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# ===-- cli-corpus-check.py - Metadata oracles over the C program corpus --===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Run the ECMA-335 library's oracles over C programs of the test corpus.

    python cli-corpus-check.py --mode <m>[,<m>...] [--clang PATH] [--triple T]
        [--levels O0,O2] [--work DIR] [--mdtools DIR] [--dotnet PATH]
        [--ilasm-framework PATH] [--ilasm-nuget PATH] [--cil-dump PATH]
        [--cil-copy PATH] [--ilverify PATH] [--peverify PATH]
        [--ildasm-framework PATH] [--ildasm-nuget PATH] [--jitall DIR]
        [--wsl-distro NAME] [--wsl-dotnet PATH] <dir-or-.c>...

Inputs are C programs: a .c file named on the command line, or every .c file
under a directory (recursively) that has a <stem>.expected beside it, as in
msil/test/M1. Each program is compiled once per level with

    clang --target=<triple> -std=gnu17 -ffreestanding -Werror=return-type
          -I msil/test/Inputs -O<level> -S <program>.c -o <work>/il/<name>.<level>.il

(%{msil_cflags} of msil/test/lit.cfg.py), where <name> is the program's path
below the directory argument with '.' for the path separators (the stem for a
.c argument). The defaults are the triple x86_64-pc-windows-msvc-msil, the
levels O0,O2, clang <build>/bin/clang[.exe], mdcanon and mdump in
<build>/tools/msil/mdtools (build-mdtools.py builds them; this script does
not), and the work directory C:\\llvm-msil-scratch\\cli-corpus on Windows
($HOME/llvm-msil-scratch/cli-corpus elsewhere), which must be outside the
repository. <build> is $MSIL_BUILD_DIR, else <repo>/build.

Modes (plan 5.7.11; a mode framework that later milestones extend):

  ilasm   Assemble every 'clang -S' output with the .NET Framework ilasm
          (-nologo -quiet -dll -pe64 -x64) and with the NuGet ilasm at the
          MsilILAsmVersion of msil/utils/tools/msil-tools.proj, dump each set
          with one 'mdcanon --out-dir' run, and require byte-equal listings
          (tests-p2 E3): canonical listing v1 must not depend on the
          assembler. The first differing lines are printed.

The modes of plan M17 (5.7.13) share one compile and one assembly of the
inputs: every 'clang -S' output is assembled once, with -exe when it has an
.entrypoint (else -dll), by the .NET Framework ilasm on Windows and by the
NuGet ilasm elsewhere, into <work>/images/<name>.<level>.exe (or .dll), and
copied once with 'llvm-cil-copy <image> -o <work>/copies/run1/<file>'
(--cil-copy, default <build>/bin/llvm-cil-copy[.exe]).

  dumpers 'llvm-cil-dump --canonical' (--cil-dump, default
          <build>/bin/llvm-cil-dump[.exe]) and mdcanon print byte-equal
          listings of every image and of its copy, once with the default
          options and once with the six normalizations of plan 5.12.6 and
          --scopes=strip (--assembly-hash=ignore --params=drop
          --initlocals=ignore-without-locals --snan=quiet
          --pinvoke-module=strip --pinvoke-access=ignore --scopes=strip). The
          two dumpers implement one specification (5.7.10), so any
          difference fails; the first differing lines are printed.

  copy    Every copy is canonically equal to its image (mdcanon listings
          with the default options), and a second copy made from another
          directory (<work>/copies/run2, a relative -o) is byte-identical to
          the first. The copies of the programs under an '11-programs'
          directory (msil/test/M1/11-programs) at -O2 are then written as
          <stem>.exe and
            - verified as %msil_verify does (msil-verify.py with ILVerify,
              ilverify-allowed.txt, PEVerify and the neutralized pass through
              the Framework ildasm and the image's ilasm; runtime references
              from the newest installed Microsoft.NETCore.App 8.x);
            - force-JIT-compiled as %msil_jitall does, which completes D21
              (msil-jitall.py with --dotnet and the .NET 8 runtimeconfig,
              under lit's limits of 60 s and 4096 MB; jitall.dll from
              --jitall, default <build>/tools/msil/jitall, built or refreshed
              with msil/utils/jitall/build-jitall.py as lit does);
            - run with 'msil-run.py --expect <stem>' on .NET 8 (the newest
              installed 8.x, through a runtimeconfig.json in
              <work>/runtimeconfig);
            - checked with PEVerify /md (Windows): the copy's '[MD]' and
              '[IL]' messages are a subset of the image's (oracle O6);
            - round-tripped with 'msil-verify.py --roundtrip' through the
              NuGet ildasm and ilasm at MsilILAsmVersion, and the
              reassembled image is run on .NET 8 like the copy;
          and crc32, dispatch-vm, quicksort, string-table and struct-sim
          also run on .NET 10 and, on Windows, on .NET Framework 4.8
          (msil-run.py --netfx) and on WSL .NET 10 (--wsl-distro, default
          Ubuntu; --wsl-dotnet, default the private runtime
          $HOME/.dotnet-msil/dotnet of msil/utils/msil-wsl-dotnet.sh, else
          the distribution's dotnet). The WSL programs are compiled again
          for the Linux triple of the same architecture
          (<arch>-unknown-linux-gnu-msil), assembled and copied like the
          others, verified with msil-verify.py as the Windows copies are,
          and run with --expect-os linux. The programs run where the
          triple's OS runs: on Windows the triple must be a Windows triple,
          on Linux a Linux one (usage error otherwise).

  stats   'llvm-cil-dump --stats-only' and 'mdcanon --stats-only' print
          byte-equal lines for the installed .NET Framework 4.8
          mscorlib.dll (Windows) and the System.Runtime.dll and
          System.Private.CoreLib.dll of the newest installed
          Microsoft.NETCore.App 8.x, and their '--canonical' listings with
          the default options are byte-equal on the same files and on the
          System.Runtime.dll of the newest installed
          Microsoft.NETCore.App.Ref 8.x reference pack
          (packs/Microsoft.NETCore.App.Ref/8.*/ref/net8.0, next to the
          runtime's shared/ directory; skipped with a note when no pack is
          installed), so every row, signature and body of these files is
          decoded (host only; inputs are not compiled). mdcanon lists them
          with '--out-dir' batches, and each llvm-cil-dump run has a time
          bound of 120 s (a lookup that is quadratic in the row count shows
          as a timeout).

Every mode ends with a summary line '<script>: <mode>: <n> of <m> ...'. Exit
status: 0 when every requested mode passes; 2 on a usage error (an unknown
option or mode, a bad --levels value, a --work inside the repository, a
triple whose OS does not run on this host with --mode copy, or inputs that
are not directories or .c files or hold no C programs), which is reported
before anything runs; 1 when a mode fails or a tool is missing or cannot be
set up (dotnet, clang, mdtools, either ilasm, llvm-cil-dump, llvm-cil-copy,
the verifiers, jitall, the runtimes or WSL that a mode needs). dotnet is
found as build-mdtools.py finds it.
"""

import argparse
import concurrent.futures
import difflib
import glob
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
UTILS = os.path.dirname(HERE)


def _load_build_mdtools():
    """build-mdtools.py as a module, for its dotnet lookup and child_env
    (without writing a __pycache__ into the source tree)."""
    spec = importlib.util.spec_from_file_location(
        "build_mdtools", os.path.join(HERE, "build-mdtools.py")
    )
    module = importlib.util.module_from_spec(spec)
    saved, sys.dont_write_bytecode = sys.dont_write_bytecode, True
    try:
        spec.loader.exec_module(module)
    finally:
        sys.dont_write_bytecode = saved
    return module


BUILD_MDTOOLS = _load_build_mdtools()
child_env = BUILD_MDTOOLS.child_env
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
SCRIPT = "cli-corpus-check.py"
IS_WINDOWS = os.name == "nt"
EXE = ".exe" if IS_WINDOWS else ""
DEFAULT_TRIPLE = "x86_64-pc-windows-msvc-msil"
CFLAGS = ["-std=gnu17", "-ffreestanding", "-Werror=return-type"]
ILASM_ARCH_FLAGS = {"x86_64": ["-pe64", "-x64"], "aarch64": ["-pe64", "-arm64"]}
# Each mdcanon --out-dir run gets at most this many characters of file names,
# well below the 32,767 of a Windows command line.
MAX_BATCH_CHARS = 24000
# --mode copy: the programs of 11-programs that also run on .NET 10, .NET
# Framework 4.8 and WSL .NET 10 (plan M17.f); the others run on .NET 8 only.
RUN_EVERYWHERE = ("crc32", "dispatch-vm", "quicksort", "string-table", "struct-sim")
# --mode copy: jitall runs under the limits of lit's %msil_jitall (the
# defaults of msil_timeout and msil_mem_limit_mb in msil/test/lit.cfg.py).
JITALL_LIMITS = ["--timeout", "60", "--mem-limit-mb", "4096"]
# --mode stats: the time bound of each 'llvm-cil-dump --canonical' run on a
# BCL file (about 1 s each when lookups are indexed, plan 5.7.8).
CANONICAL_TIMEOUT = 120
# --mode dumpers: the second comparison turns on every normalization of plan
# 5.12.6 and --scopes=strip (5.7.11).
NORMALIZE_ALL = [
    "--assembly-hash=ignore",
    "--params=drop",
    "--initlocals=ignore-without-locals",
    "--snan=quiet",
    "--pinvoke-module=strip",
    "--pinvoke-access=ignore",
    "--scopes=strip",
]


class CheckError(Exception):
    """A failure of one step for one program; the run goes on."""


class UsageError(Exception):
    """A bad option value or input; the run stops with exit status 2, as for
    argparse's own errors."""


class SetupError(Exception):
    """A missing tool or another setup failure that is not a usage error; the
    run stops with exit status 1."""


def note(msg):
    print("%s: %s" % (SCRIPT, msg), flush=True)


def run(cmd, timeout, cwd=None):
    """Run cmd; return (exit status, combined output)."""
    try:
        proc = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env=child_env(),
            timeout=timeout,
            cwd=cwd,
        )
    except subprocess.TimeoutExpired:
        return None, "timed out after %d s" % timeout
    except OSError as e:
        return None, str(e)
    return proc.returncode, proc.stdout.decode("utf-8", errors="replace")


def run_split(cmd, timeout, cwd=None):
    """Run cmd; return (exit status, stdout bytes, stderr text). A listing is
    compared byte for byte, so stdout is kept apart from stderr and raw."""
    try:
        proc = subprocess.run(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=child_env(),
            timeout=timeout,
            cwd=cwd,
        )
    except subprocess.TimeoutExpired:
        return None, b"", "timed out after %d s" % timeout
    except OSError as e:
        return None, b"", str(e)
    return proc.returncode, proc.stdout, proc.stderr.decode("utf-8", errors="replace")


def script(name):
    """The command prefix that runs msil/utils/<name> with this Python."""
    return [sys.executable, os.path.join(UTILS, name)]


def triple_os(triple):
    """'windows', 'linux' or another OS word of an MSIL triple."""
    parts = triple.split("-")
    for os_name in ("windows", "linux", "darwin"):
        if any(p.startswith(os_name) for p in parts[1:]):
            return os_name
    return parts[2] if len(parts) > 2 else ""


def version_key(text):
    return tuple(int(x) if x.isdigit() else 0 for x in re.split(r"[.-]", text))


def tail(text, lines=15):
    rows = text.rstrip().splitlines()
    return "\n".join(rows[-lines:])


def same_or_inside(path, root):
    path = os.path.normcase(os.path.realpath(path))
    root = os.path.normcase(os.path.realpath(root))
    return path == root or path.startswith(root.rstrip(os.sep) + os.sep)


class Program:
    def __init__(self, source, name):
        self.source = source
        self.name = name


class Context:
    """Options, tools and the steps that several modes share. The constructor
    validates options and inputs (UsageError) and runs no tool."""

    def __init__(self, args):
        self.args = args
        self.build = os.environ.get("MSIL_BUILD_DIR") or os.path.join(REPO, "build")
        self.triple = args.triple
        self.arch = self.triple.split("-", 1)[0]
        self.levels = self._levels(args.levels)
        self.work = os.path.abspath(args.work or self._default_work())
        if same_or_inside(self.work, REPO):
            raise UsageError(
                "--work %s is inside the repository %s" % (self.work, REPO)
            )
        self.programs = self._programs(args.inputs)
        self.jobs = max(1, min(4, os.cpu_count() or 1))
        self._dotnet = None
        self._clang = None
        self._il = {}
        # The M17 modes' shared steps (assemble_all, copy_all) and tools.
        self._images = None
        self._copies = None
        self._tools = {}
        self._runtimes = None

    @staticmethod
    def _default_work():
        if IS_WINDOWS:
            return r"C:\llvm-msil-scratch\cli-corpus"
        return os.path.join(os.path.expanduser("~"), "llvm-msil-scratch", "cli-corpus")

    @staticmethod
    def _levels(text):
        levels = []
        for item in text.split(","):
            item = item.strip().lstrip("-")
            if not re.fullmatch(r"O[0-3sz]", item):
                raise UsageError("--levels: %r is not O0, O1, O2, O3, Os or Oz" % item)
            if item not in levels:
                levels.append(item)
        if not levels:
            raise UsageError("--levels is empty")
        return levels

    @staticmethod
    def _programs(inputs):
        found = []
        for item in inputs:
            if os.path.isdir(item):
                root = os.path.abspath(item)
                for dirpath, dirnames, filenames in os.walk(root):
                    dirnames.sort()
                    for f in sorted(filenames):
                        stem, ext = os.path.splitext(f)
                        if ext == ".c" and os.path.isfile(
                            os.path.join(dirpath, stem + ".expected")
                        ):
                            rel = os.path.relpath(os.path.join(dirpath, stem), root)
                            found.append(
                                Program(
                                    os.path.join(dirpath, f), rel.replace(os.sep, ".")
                                )
                            )
            elif os.path.isfile(item) and item.endswith(".c"):
                path = os.path.abspath(item)
                found.append(Program(path, os.path.splitext(os.path.basename(path))[0]))
            else:
                raise UsageError("%s is neither a directory nor a .c file" % item)
        if not found:
            raise UsageError(
                "no C programs (a .c file with a <stem>.expected) in %s"
                % " ".join(inputs)
            )
        names = {}
        for p in found:
            if p.name in names:
                raise UsageError(
                    "%s and %s have the same work name %s"
                    % (names[p.name], p.source, p.name)
                )
            names[p.name] = p.source
        return found

    # ------------------------------------------------------------ tools

    def dotnet(self):
        """The dotnet host, found exactly as build-mdtools.py finds it."""
        if not self._dotnet:
            try:
                found = BUILD_MDTOOLS.find_dotnet(self.args.dotnet)
            except BUILD_MDTOOLS.BuildError as e:
                raise SetupError(str(e))
            self._dotnet = os.path.abspath(found)
        return self._dotnet

    def clang(self):
        if self._clang:
            return self._clang
        path = self.args.clang or os.path.join(self.build, "bin", "clang" + EXE)
        if not os.path.isfile(path):
            raise SetupError("clang %s not found (pass --clang PATH)" % path)
        self._clang = os.path.abspath(path)
        return self._clang

    def mdtool(self, tool):
        """The command prefix that runs mdtools' <tool>.dll."""
        mdtools = os.path.abspath(
            self.args.mdtools or os.path.join(self.build, "tools", "msil", "mdtools")
        )
        dll = os.path.join(mdtools, tool + ".dll")
        config = os.path.join(mdtools, tool + ".runtimeconfig.json")
        if not os.path.isfile(dll) or not os.path.isfile(config):
            raise SetupError(
                "%s not found: build it with 'python %s' (or pass --mdtools DIR)"
                % (dll, os.path.join(HERE, "build-mdtools.py"))
            )
        return [self.dotnet(), "exec", "--runtimeconfig", config, dll]

    def ilasm_framework(self):
        path = self.args.ilasm_framework
        if not path:
            if not IS_WINDOWS:
                raise SetupError(
                    "the .NET Framework ilasm exists only on Windows (pass --ilasm-framework PATH)"
                )
            windir = (
                os.environ.get("WINDIR")
                or os.environ.get("SystemRoot")
                or r"C:\Windows"
            )
            path = os.path.join(
                windir, "Microsoft.NET", "Framework64", "v4.0.30319", "ilasm.exe"
            )
        if not os.path.isfile(path):
            raise SetupError(
                "Framework ilasm %s not found (pass --ilasm-framework PATH)" % path
            )
        return os.path.abspath(path)

    def ilasm_nuget_version(self):
        proj = os.path.join(REPO, "msil", "utils", "tools", "msil-tools.proj")
        try:
            with open(proj, encoding="utf-8") as f:
                m = re.search(
                    r"<MsilILAsmVersion[^>]*>\s*([0-9.]+)\s*</MsilILAsmVersion>",
                    f.read(),
                )
        except OSError as e:
            raise SetupError("cannot read %s: %s" % (proj, e))
        if not m:
            raise SetupError("cannot read MsilILAsmVersion from %s" % proj)
        return m.group(1)

    def nuget_roots(self):
        roots = [os.environ.get("NUGET_PACKAGES")]
        try:
            rc, out = run(
                [self.dotnet(), "nuget", "locals", "global-packages", "--list"], 120
            )
        except SetupError:
            rc, out = None, ""
        m = (
            re.search(r"^global-packages:\s*(.+?)\s*$", out or "", re.M)
            if rc == 0
            else None
        )
        if m:
            roots.append(m.group(1))
        roots.append(os.path.join(os.path.expanduser("~"), ".nuget", "packages"))
        unique = []
        for r in roots:
            if r and os.path.normcase(os.path.normpath(r)) not in [
                os.path.normcase(os.path.normpath(u)) for u in unique
            ]:
                unique.append(r)
        return unique

    def ilasm_nuget(self):
        return self.nuget_tool("ilasm", self.args.ilasm_nuget)

    def ildasm_nuget(self):
        """The NuGet ildasm at MsilILAsmVersion (the round trip of --mode
        copy uses it with the NuGet ilasm, as the lit round-trip leg does)."""
        return self.nuget_tool("ildasm", self.args.ildasm_nuget)

    def nuget_tool(self, name, explicit):
        """ilasm or ildasm from runtime.<rid>.Microsoft.NETCore.<ILAsm|ILDAsm>
        at MsilILAsmVersion in the NuGet global-packages folder, or the path
        given with --<name>-nuget."""
        if explicit:
            if not os.path.isfile(explicit):
                raise SetupError("--%s-nuget %s does not exist" % (name, explicit))
            return os.path.abspath(explicit)
        version = self.ilasm_nuget_version()
        rid = ("win" if IS_WINDOWS else "linux") + "-x64"
        tool = name + (".exe" if IS_WINDOWS else "")
        tried = []
        for root in self.nuget_roots():
            path = os.path.join(
                root,
                "runtime.%s.microsoft.netcore.%s" % (rid, name),
                version,
                "runtimes",
                rid,
                "native",
                tool,
            )
            tried.append(path)
            if os.path.isfile(path):
                return path
        raise SetupError(
            "NuGet %s %s not found (tried %s): run 'dotnet restore msil/utils/tools/msil-tools.proj "
            "-p:MsilToolsRid=%s' or pass --%s-nuget PATH"
            % (name, version, ", ".join(tried), rid, name)
        )

    # ------------------------------------------------------------ shared steps

    def prepare(self, subdir):
        """An empty directory <work>/<subdir>."""
        path = os.path.join(self.work, subdir)
        if os.path.isdir(path):
            shutil.rmtree(path)
        os.makedirs(path)
        return path

    def items(self):
        return [(p, level) for p in self.programs for level in self.levels]

    def compile_all(self):
        """clang -S of every program at every level, once per run:
        {(program name, level): .il path or CheckError}."""
        todo = [(p, lv) for p, lv in self.items() if (p.name, lv) not in self._il]
        if not todo:
            return self._il
        il_dir = os.path.join(self.work, "il")
        os.makedirs(il_dir, exist_ok=True)
        inputs = os.path.join(REPO, "msil", "test", "Inputs")

        def one(item):
            p, level = item
            out = os.path.join(il_dir, "%s.%s.il" % (p.name, level))
            if os.path.exists(out):
                os.remove(out)
            cmd = [self.clang(), "--target=" + self.triple] + CFLAGS
            cmd += ["-I", inputs, "-" + level, "-S", p.source, "-o", out]
            rc, text = run(cmd, 600)
            if rc != 0 or not os.path.isfile(out):
                return CheckError(
                    "clang -%s -S %s failed (%s):\n%s"
                    % (level, p.source, rc, tail(text))
                )
            return out

        with concurrent.futures.ThreadPoolExecutor(self.jobs) as pool:
            for item, result in zip(todo, pool.map(one, todo)):
                self._il[(item[0].name, item[1])] = result
        return self._il

    def ilasm(self, ilasm, il, out):
        """Assemble il into the DLL out (outputs keep their file name in
        every set, because ilasm names the module after the output file)."""
        cmd = [ilasm, "-nologo", "-quiet", "-dll"] + ILASM_ARCH_FLAGS.get(self.arch, [])
        cmd += ["-output=" + out, il]
        rc, text = run(cmd, 600)
        if rc != 0 or not os.path.isfile(out):
            raise CheckError("%s %s failed (%s):\n%s" % (ilasm, il, rc, tail(text)))

    def mdcanon_batch(self, files, out_dir, options=()):
        """One 'mdcanon --out-dir' process per batch of files; {file: .canon
        path or CheckError}."""
        os.makedirs(out_dir, exist_ok=True)
        prefix = self.mdtool("mdcanon") + list(options) + ["--out-dir", out_dir]
        batches, batch, size = [], [], 0
        for f in files:
            if batch and size + len(f) + 1 > MAX_BATCH_CHARS:
                batches.append(batch)
                batch, size = [], 0
            batch.append(f)
            size += len(f) + 1
        if batch:
            batches.append(batch)
        results = {}
        for b in batches:
            rc, text = run(prefix + b, 1800)
            errors = {}
            for line in text.splitlines():
                m = re.match(r"mdcanon: error: '(.+?)': (.*)$", line)
                if m:
                    errors[os.path.normcase(os.path.abspath(m.group(1)))] = m.group(2)
            for f in b:
                canon = os.path.join(out_dir, os.path.basename(f) + ".canon")
                err = errors.get(os.path.normcase(os.path.abspath(f)))
                if err is not None:
                    results[f] = CheckError("mdcanon %s: %s" % (f, err))
                elif rc not in (0, 1) or not os.path.isfile(canon):
                    results[f] = CheckError(
                        "mdcanon failed on %s (%s):\n%s" % (f, rc, tail(text))
                    )
                else:
                    results[f] = canon
        return results

    # ------------------------------------------------------------ M17 tools

    def built_tool(self, option, name):
        """llvm-cil-dump or llvm-cil-copy: --<option> or <build>/bin."""
        if name not in self._tools:
            path = getattr(self.args, option) or os.path.join(
                self.build, "bin", name + EXE
            )
            if not os.path.isfile(path):
                raise SetupError(
                    "%s %s not found (build it, or pass --%s PATH)"
                    % (name, path, option.replace("_", "-"))
                )
            self._tools[name] = os.path.abspath(path)
        return self._tools[name]

    def cil_dump(self):
        return self.built_tool("cil_dump", "llvm-cil-dump")

    def cil_copy(self):
        return self.built_tool("cil_copy", "llvm-cil-copy")

    def jitall(self):
        """The directory of jitall.dll: --jitall, else
        <build>/tools/msil/jitall. As msil/test/lit.cfg.py does, it is built
        or refreshed with msil/utils/jitall/build-jitall.py first, which does
        nothing when jitall.dll is up to date."""
        if "jitall" not in self._tools:
            out = os.path.abspath(
                self.args.jitall or os.path.join(self.build, "tools", "msil", "jitall")
            )
            cmd = script(os.path.join("jitall", "build-jitall.py"))
            rc, text = run(cmd + ["--out", out, "--dotnet", self.dotnet()], 600)
            if rc != 0 or not os.path.isfile(os.path.join(out, "jitall.dll")):
                raise SetupError(
                    "jitall could not be built in %s (build-jitall.py exited %s): %s"
                    % (out, rc, tail(text))
                )
            self._tools["jitall"] = out
        return self._tools["jitall"]

    def image_ilasm(self):
        """The assembler of the M17 modes' images: the .NET Framework ilasm on
        Windows (plan D31), the NuGet ilasm elsewhere."""
        return self.ilasm_framework() if IS_WINDOWS else self.ilasm_nuget()

    @staticmethod
    def netfx_tool(name):
        """<name> from the Windows SDK's 'NETFX 4.x Tools' x64 directory (as
        msil/test/lit.cfg.py finds PEVerify and ildasm), newest first."""
        sdk = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
        hits = glob.glob(
            os.path.join(
                sdk,
                "Microsoft SDKs",
                "Windows",
                "v*",
                "bin",
                "NETFX 4.* Tools",
                "x64",
                name,
            )
        )

        def key(path):
            netfx = re.search(r"NETFX (\d+(?:\.\d+)*)", path)
            sdk = re.search(r"[\\/]v(\d+(?:\.\d+)*)[A-Za-z]*[\\/]", path)
            return (
                version_key(netfx.group(1)) if netfx else (),
                version_key(sdk.group(1)) if sdk else (),
            )

        hits.sort(key=key, reverse=True)
        return hits[0] if hits else None

    def ilverify(self):
        if "ilverify" not in self._tools:
            path = self.args.ilverify or shutil.which("ilverify")
            if not path:
                candidate = os.path.join(
                    os.path.expanduser("~"), ".dotnet", "tools", "ilverify" + EXE
                )
                path = candidate if os.path.isfile(candidate) else None
            if not path or not os.path.isfile(path):
                raise SetupError(
                    "ILVerify not found: install the dotnet tool dotnet-ilverify or pass --ilverify PATH"
                )
            rc, out = run([path, "--version"], 120)
            if rc != 0:
                raise SetupError("ilverify %s does not run: %s" % (path, tail(out)))
            self._tools["ilverify"] = os.path.abspath(path)
        return self._tools["ilverify"]

    def peverify(self):
        """PEVerify (Windows only)."""
        if "peverify" not in self._tools:
            path = (
                self.args.peverify
                or shutil.which("peverify")
                or self.netfx_tool("PEVerify.exe")
            )
            if not path or not os.path.isfile(path):
                raise SetupError(
                    "PEVerify not found (Windows SDK, NETFX 4.8 Tools): pass --peverify PATH"
                )
            self._tools["peverify"] = os.path.abspath(path)
        return self._tools["peverify"]

    def ildasm_framework(self):
        """The ildasm of msil-verify.py's neutralized pass: the .NET Framework
        ildasm next to PEVerify on Windows, the NuGet ildasm elsewhere."""
        if "ildasm" not in self._tools:
            if self.args.ildasm_framework:
                path = self.args.ildasm_framework
            elif IS_WINDOWS:
                path = self.netfx_tool("ildasm.exe")
            else:
                path = self.ildasm_nuget()
            if not path or not os.path.isfile(path):
                raise SetupError(
                    "the .NET Framework ildasm was not found (Windows SDK, NETFX 4.8 Tools): "
                    "pass --ildasm-framework PATH"
                )
            self._tools["ildasm"] = os.path.abspath(path)
        return self._tools["ildasm"]

    def runtimes(self):
        """{major: (version, directory)} of the host's Microsoft.NETCore.App
        runtimes, newest version per major."""
        if self._runtimes is None:
            rc, out = run([self.dotnet(), "--list-runtimes"], 120)
            if rc != 0:
                raise SetupError("'dotnet --list-runtimes' failed: %s" % tail(out))
            found = {}
            for m in re.finditer(
                r"^Microsoft\.NETCore\.App (\S+) \[(.+)\]\s*$", out, re.M
            ):
                ver, base = m.group(1), m.group(2)
                major = version_key(ver)[0]
                if major not in found or version_key(ver) > version_key(
                    found[major][0]
                ):
                    found[major] = (ver, os.path.join(base, ver))
            self._runtimes = found
        return self._runtimes

    def runtime(self, major):
        rts = self.runtimes()
        if major not in rts:
            raise SetupError(
                "no Microsoft.NETCore.App %d.x runtime is installed for %s"
                % (major, self.dotnet())
            )
        return rts[major]

    def runtimeconfig(self, major):
        """<work>/runtimeconfig/net<major>.runtimeconfig.json, as lit writes
        it for the programs' runs."""
        self.runtime(major)
        d = os.path.join(self.work, "runtimeconfig")
        os.makedirs(d, exist_ok=True)
        path = os.path.join(d, "net%d.runtimeconfig.json" % major)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            json.dump(
                {
                    "runtimeOptions": {
                        "tfm": "net%d.0" % major,
                        "rollForward": "LatestPatch",
                        "framework": {
                            "name": "Microsoft.NETCore.App",
                            "version": "%d.0.0" % major,
                        },
                    }
                },
                f,
                indent=2,
            )
            f.write("\n")
        return path

    def wsl_exe(self):
        windir = (
            os.environ.get("WINDIR") or os.environ.get("SystemRoot") or r"C:\Windows"
        )
        path = shutil.which("wsl.exe") or os.path.join(windir, "System32", "wsl.exe")
        if not os.path.isfile(path):
            raise SetupError("wsl.exe not found: the WSL runs of --mode copy need WSL")
        return path

    def wsl_dotnet(self):
        """The dotnet host inside WSL: --wsl-dotnet, else the private runtime
        of msil/utils/msil-wsl-dotnet.sh ($HOME/.dotnet-msil/dotnet), else
        the distribution's dotnet."""
        if "wsl-dotnet" not in self._tools:
            if self.args.wsl_dotnet:
                self._tools["wsl-dotnet"] = self.args.wsl_dotnet
            else:
                probe = (
                    'p="$HOME/.dotnet-msil/dotnet"; '
                    'if [ -x "$p" ]; then echo "$p"; else command -v dotnet; fi'
                )
                rc, out = run(
                    [
                        self.wsl_exe(),
                        "-d",
                        self.args.wsl_distro,
                        "--exec",
                        "sh",
                        "-c",
                        probe,
                    ],
                    120,
                )
                found = [
                    l.strip() for l in out.splitlines() if l.strip().startswith("/")
                ]
                if rc != 0 or not found:
                    raise SetupError(
                        "no dotnet host in WSL %s (run 'wsl -d %s --cd <repository> --exec bash "
                        "msil/utils/msil-wsl-dotnet.sh' or pass --wsl-dotnet PATH): %s"
                        % (self.args.wsl_distro, self.args.wsl_distro, tail(out))
                    )
                self._tools["wsl-dotnet"] = found[-1]
        return self._tools["wsl-dotnet"]

    # ------------------------------------------------------------ M17 shared steps

    def assemble(self, il_path, out_dir, file_stem):
        """ilasm il_path into out_dir/<file_stem>.exe when it has an
        .entrypoint, else .dll; returns the image path (CheckError on
        failure)."""
        try:
            with open(il_path, encoding="utf-8-sig", errors="replace") as f:
                exe = re.search(r"^\s*\.entrypoint\b", f.read(), re.M) is not None
        except OSError as e:
            raise CheckError("cannot read %s: %s" % (il_path, e))
        out = os.path.join(out_dir, file_stem + (".exe" if exe else ".dll"))
        if os.path.exists(out):
            os.remove(out)
        cmd = [self.image_ilasm(), "-nologo", "-quiet", "-exe" if exe else "-dll"]
        cmd += ILASM_ARCH_FLAGS.get(self.arch, []) + ["-output=" + out, il_path]
        rc, text = run(cmd, 600)
        if rc != 0 or not os.path.isfile(out):
            raise CheckError("ilasm %s failed (%s):\n%s" % (il_path, rc, tail(text)))
        return out

    def copy_image(self, image, out, cwd=None):
        """llvm-cil-copy image -o out (out relative to cwd when given)."""
        target = os.path.join(cwd, out) if cwd else out
        if os.path.exists(target):
            os.remove(target)
        rc, text = run([self.cil_copy(), image, "-o", out], 600, cwd=cwd)
        if rc != 0 or not os.path.isfile(target):
            raise CheckError(
                "llvm-cil-copy %s -o %s failed (%s):\n%s" % (image, out, rc, tail(text))
            )
        return target

    def assemble_all(self):
        """Every compiled program assembled once into <work>/images:
        {(program name, level): image path or CheckError}."""
        if self._images is not None:
            return self._images
        self.image_ilasm()
        il = self.compile_all()
        out_dir = self.prepare("images")
        items = self.items()

        def one(item):
            p, level = item
            src = il[(p.name, level)]
            if isinstance(src, CheckError):
                return src
            try:
                return self.assemble(src, out_dir, "%s.%s" % (p.name, level))
            except CheckError as e:
                return e

        with concurrent.futures.ThreadPoolExecutor(self.jobs) as pool:
            self._images = {
                (p.name, lv): r for (p, lv), r in zip(items, pool.map(one, items))
            }
        return self._images

    def copy_all(self):
        """Every image copied once with llvm-cil-copy into
        <work>/copies/run1/<image file name>: {(program name, level): copy
        path or CheckError}."""
        if self._copies is not None:
            return self._copies
        self.cil_copy()
        images = self.assemble_all()
        self.prepare("copies")
        run1 = os.path.join(self.work, "copies", "run1")
        os.makedirs(run1)
        keys = sorted(images)

        def one(key):
            image = images[key]
            if isinstance(image, CheckError):
                return CheckError("no image to copy: %s" % image)
            try:
                return self.copy_image(
                    image, os.path.join(run1, os.path.basename(image))
                )
            except CheckError as e:
                return e

        with concurrent.futures.ThreadPoolExecutor(self.jobs) as pool:
            self._copies = dict(zip(keys, pool.map(one, keys)))
        return self._copies

    def cil_dump_listing(self, path, options, out):
        """'llvm-cil-dump --canonical <options> path' into out; returns out
        (CheckError on failure)."""
        rc, data, err = run_split(
            [self.cil_dump(), "--canonical"] + list(options) + [path], 600
        )
        if rc != 0:
            raise CheckError(
                "llvm-cil-dump %s failed (%s):\n%s" % (path, rc, tail(err))
            )
        with open(out, "wb") as f:
            f.write(data)
        return out


def same_bytes(a, b):
    with open(a, "rb") as f1, open(b, "rb") as f2:
        return f1.read() == f2.read()


def first_difference(a_path, b_path, a_label, b_label, lines=20):
    with open(a_path, "rb") as f:
        a = f.read().decode("utf-8", errors="replace").splitlines()
    with open(b_path, "rb") as f:
        b = f.read().decode("utf-8", errors="replace").splitlines()
    diff = list(difflib.unified_diff(a, b, a_label, b_label, lineterm="", n=2))
    return (
        "\n".join(diff[:lines])
        if diff
        else "(the files differ only in line endings or encoding)"
    )


def first_line_difference(a_path, b_path, a_label, b_label, context=3):
    """The first differing line of two listings and the lines before it, for
    listings too large for difflib (Framework mscorlib.dll's has 750,000)."""
    with open(a_path, "rb") as f:
        a = f.read().split(b"\n")
    with open(b_path, "rb") as f:
        b = f.read().split(b"\n")
    i = 0
    while i < min(len(a), len(b)) and a[i] == b[i]:
        i += 1
    rows = ["first difference at line %d:" % (i + 1)]
    rows += ["  " + x.decode("utf-8", "replace") for x in a[max(0, i - context) : i]]
    for label, lines in ((a_label, a), (b_label, b)):
        text = (
            lines[i].decode("utf-8", "replace") if i < len(lines) else "(end of file)"
        )
        rows.append("%s: %s" % (label, text))
    return "\n".join(rows)


# ---------------------------------------------------------------- modes


class Mode:
    """A comparison over the corpus. Later milestones add subclasses to MODES
    (and their options through add_arguments)."""

    name = ""
    help = ""

    @staticmethod
    def add_arguments(parser):
        pass

    def check_usage(self, ctx):
        """Raise UsageError for options this mode cannot use; runs before any
        mode runs anything."""

    def run(self, ctx):
        """Check every program; print the summary line; return True on success."""
        raise NotImplementedError


class IlasmMode(Mode):
    name = "ilasm"
    help = "listings of Framework ilasm and NuGet ilasm images are byte-equal"

    def run(self, ctx):
        start = time.time()
        fw = ctx.ilasm_framework()
        # Before the NuGet ilasm, whose lookup ignores a missing dotnet.
        ctx.mdtool("mdcanon")
        nuget = ctx.ilasm_nuget()
        version = ctx.ilasm_nuget_version()
        base = ctx.prepare("ilasm")
        dirs = {
            k: os.path.join(base, k) for k in ("fw", "nuget", "fw-canon", "nuget-canon")
        }
        for d in dirs.values():
            os.makedirs(d)
        items = ctx.items()
        note(
            "ilasm: %d programs at %s (%d images per assembler) in %s"
            % (len(ctx.programs), ",".join(ctx.levels), len(items), base)
        )
        il = ctx.compile_all()
        failures = {}
        dlls = {}

        def assemble(item):
            p, level = item
            key = (p.name, level)
            src = il[key]
            if isinstance(src, CheckError):
                return key, src
            name = "%s.%s.dll" % (p.name, level)
            try:
                ctx.ilasm(fw, src, os.path.join(dirs["fw"], name))
                ctx.ilasm(nuget, src, os.path.join(dirs["nuget"], name))
            except CheckError as e:
                return key, e
            return key, name

        with concurrent.futures.ThreadPoolExecutor(ctx.jobs) as pool:
            for key, result in pool.map(assemble, items):
                if isinstance(result, CheckError):
                    failures[key] = str(result)
                else:
                    dlls[key] = result

        names = [dlls[k] for k in sorted(dlls)]
        fw_canon = ctx.mdcanon_batch(
            [os.path.join(dirs["fw"], n) for n in names], dirs["fw-canon"]
        )
        nuget_canon = ctx.mdcanon_batch(
            [os.path.join(dirs["nuget"], n) for n in names], dirs["nuget-canon"]
        )
        equal = 0
        raw_differ = 0
        for key in sorted(dlls):
            n = dlls[key]
            a = fw_canon[os.path.join(dirs["fw"], n)]
            b = nuget_canon[os.path.join(dirs["nuget"], n)]
            if isinstance(a, CheckError) or isinstance(b, CheckError):
                failures[key] = "\n".join(
                    str(x) for x in (a, b) if isinstance(x, CheckError)
                )
                continue
            with open(os.path.join(dirs["fw"], n), "rb") as f1, open(
                os.path.join(dirs["nuget"], n), "rb"
            ) as f2:
                raw_differ += f1.read() != f2.read()
            with open(a, "rb") as f1, open(b, "rb") as f2:
                if f1.read() == f2.read():
                    equal += 1
                    continue
            failures[key] = "listings differ:\n" + first_difference(
                a, b, "Framework ilasm: " + a, "NuGet ilasm %s: %s" % (version, b)
            )
        for (name, level), text in sorted(failures.items()):
            note("ilasm: FAIL %s -%s: %s" % (name, level, text))
        note(
            "ilasm: %d of %d listings equal across Framework ilasm and NuGet ilasm %s "
            "(raw images differ in %d) in %.0f s"
            % (equal, len(items), version, raw_differ, time.time() - start)
        )
        return equal == len(items)


class DumpersMode(Mode):
    name = "dumpers"
    help = "llvm-cil-dump --canonical equals mdcanon on every image and copy"

    def run(self, ctx):
        start = time.time()
        ctx.mdtool("mdcanon")
        ctx.cil_dump()
        images = ctx.assemble_all()
        copies = ctx.copy_all()
        base = ctx.prepare("dumpers")
        note(
            "dumpers: %d images at %s and their copies, with the default and the "
            "normalized options, in %s" % (len(images), ",".join(ctx.levels), base)
        )
        failures = []
        total = equal = 0
        for label, options in (("default", []), ("normalized", NORMALIZE_ALL)):
            for kind, files in (("image", images), ("copy", copies)):
                keys = sorted(files)
                good = [k for k in keys if not isinstance(files[k], CheckError)]
                for k in keys:
                    if isinstance(files[k], CheckError):
                        total += 1
                        failures.append(
                            "%s of %s -%s (%s options): %s"
                            % (kind, k[0], k[1], label, files[k])
                        )
                out_dir = os.path.join(base, label, kind)
                cd_dir = os.path.join(out_dir, "llvm-cil-dump")
                os.makedirs(cd_dir)
                reference = ctx.mdcanon_batch(
                    [files[k] for k in good], os.path.join(out_dir, "mdcanon"), options
                )

                def one(
                    k, files=files, cd_dir=cd_dir, reference=reference, options=options
                ):
                    path = files[k]
                    out = os.path.join(cd_dir, os.path.basename(path) + ".canon")
                    try:
                        ctx.cil_dump_listing(path, options, out)
                    except CheckError as e:
                        return str(e)
                    ref = reference[path]
                    if isinstance(ref, CheckError):
                        return str(ref)
                    if same_bytes(ref, out):
                        return None
                    return "listings differ:\n" + first_difference(
                        ref, out, "mdcanon: " + ref, "llvm-cil-dump: " + out
                    )

                with concurrent.futures.ThreadPoolExecutor(ctx.jobs) as pool:
                    for k, err in zip(good, pool.map(one, good)):
                        total += 1
                        if err is None:
                            equal += 1
                        else:
                            failures.append(
                                "%s of %s -%s (%s options): %s"
                                % (kind, k[0], k[1], label, err)
                            )
        for text in failures:
            note("dumpers: FAIL " + text)
        note(
            "dumpers: %d of %d listings equal between llvm-cil-dump --canonical and mdcanon "
            "(images and copies; default options, and %s) in %.0f s"
            % (equal, total, " ".join(NORMALIZE_ALL), time.time() - start)
        )
        return total > 0 and equal == total


def check_command(cmd, timeout=1800, cwd=None):
    """None when cmd exits 0, else the reason with the end of its output."""
    rc, text = run(cmd, timeout, cwd=cwd)
    if rc == 0:
        return None
    return "%s failed (%s):\n%s" % (
        " ".join(os.path.basename(c) for c in cmd[:2]),
        rc,
        tail(text, 25),
    )


def peverify_md(peverify, path):
    """The '[MD]' and '[IL]' messages of 'PEVerify /md path', with the file's
    path replaced by '<file>'."""
    rc, text = run([peverify, "/nologo", "/md", path], 600)
    if rc is None or not re.search(r"Verified|Error\(s\) Verifying", text):
        raise CheckError(
            "PEVerify /md %s did not run (%s):\n%s" % (path, rc, tail(text))
        )
    messages = set()
    for line in text.splitlines():
        line = line.strip()
        if line.startswith(("[MD]", "[IL]")):
            messages.add(
                line.replace(path, "<file>").replace(os.path.basename(path), "<file>")
            )
    return messages


class CopyMode(Mode):
    name = "copy"
    help = (
        "llvm-cil-copy: equal, deterministic copies; 11-programs copies verify and run"
    )

    def check_usage(self, ctx):
        host = "windows" if IS_WINDOWS else "linux"
        if triple_os(ctx.triple) != host:
            raise UsageError(
                "--mode copy runs the programs on this host, but --triple %s is not a %s triple"
                % (ctx.triple, host)
            )

    def run(self, ctx):
        start = time.time()
        ctx.mdtool("mdcanon")
        ctx.cil_copy()
        images = ctx.assemble_all()
        copies = ctx.copy_all()
        base = ctx.prepare("copy")
        keys = sorted(images)
        note(
            "copy: %d images at %s and their copies in %s"
            % (len(keys), ",".join(ctx.levels), base)
        )
        failures = {}

        # A second copy, from another directory with a relative -o, writes
        # the same bytes.
        run2 = os.path.join(ctx.work, "copies", "run2")
        os.makedirs(run2, exist_ok=True)

        def second(k):
            first = copies[k]
            if isinstance(first, CheckError):
                return str(first)
            try:
                other = ctx.copy_image(images[k], os.path.basename(first), cwd=run2)
            except CheckError as e:
                return str(e)
            if same_bytes(first, other):
                return None
            return "the copies %s and %s differ" % (first, other)

        with concurrent.futures.ThreadPoolExecutor(ctx.jobs) as pool:
            for k, err in zip(keys, pool.map(second, keys)):
                if err:
                    failures.setdefault(k, []).append(err)

        # Every copy is canonically equal to its image.
        good = [k for k in keys if not isinstance(copies[k], CheckError)]
        canon_in = ctx.mdcanon_batch(
            [images[k] for k in good], os.path.join(base, "canon-images")
        )
        canon_out = ctx.mdcanon_batch(
            [copies[k] for k in good], os.path.join(base, "canon-copies")
        )
        for k in good:
            a, b = canon_in[images[k]], canon_out[copies[k]]
            if isinstance(a, CheckError) or isinstance(b, CheckError):
                failures.setdefault(k, []).append(
                    "\n".join(str(x) for x in (a, b) if isinstance(x, CheckError))
                )
            elif not same_bytes(a, b):
                failures.setdefault(k, []).append(
                    "the copy is not canonically equal to its image:\n"
                    + first_difference(a, b, "image: " + a, "copy: " + b)
                )
        for (name, level), texts in sorted(failures.items()):
            note("copy: FAIL %s -%s: %s" % (name, level, "\n".join(texts)))
        note(
            "copy: %d of %d copies canonically equal to their images and byte-identical "
            "across two directories" % (len(keys) - len(failures), len(keys))
        )
        runs_ok = self.run_programs(ctx, base, images, copies)
        note("copy: done in %.0f s" % (time.time() - start))
        return not failures and runs_ok

    @staticmethod
    def linux_copies(ctx, base, programs):
        """The WSL leg's programs: clang -S for the Linux triple of the same
        architecture, assembled and copied like the others."""
        triple = ctx.arch + "-unknown-linux-gnu-msil"
        il_dir, image_dir, copy_dir = (
            os.path.join(base, "linux", d) for d in ("il", "images", "copies")
        )
        for d in (il_dir, image_dir, copy_dir):
            os.makedirs(d, exist_ok=True)
        inputs = os.path.join(REPO, "msil", "test", "Inputs")

        def one(p):
            stem = os.path.splitext(os.path.basename(p.source))[0]
            il = os.path.join(il_dir, "%s.O2.il" % p.name)
            cmd = [ctx.clang(), "--target=" + triple] + CFLAGS
            cmd += ["-I", inputs, "-O2", "-S", p.source, "-o", il]
            rc, text = run(cmd, 600)
            if rc != 0 or not os.path.isfile(il):
                return CheckError(
                    "clang --target=%s -O2 -S %s failed (%s):\n%s"
                    % (triple, p.source, rc, tail(text))
                )
            try:
                image = ctx.assemble(il, image_dir, "%s.O2" % p.name)
                d = os.path.join(copy_dir, p.name)
                os.makedirs(d, exist_ok=True)
                return ctx.copy_image(image, os.path.join(d, stem + ".exe"))
            except CheckError as e:
                return e

        with concurrent.futures.ThreadPoolExecutor(ctx.jobs) as pool:
            return dict(zip([p.name for p in programs], pool.map(one, programs)))

    def run_programs(self, ctx, base, images, copies):
        """Verify and run the -O2 copies of the 11-programs programs."""
        if "O2" not in ctx.levels:
            note("copy: -O2 is not among --levels, so no copy is run")
            return True
        programs = [
            p
            for p in ctx.programs
            if os.path.basename(os.path.dirname(p.source)) == "11-programs"
        ]
        if not programs:
            note("copy: no program under an '11-programs' directory, so no copy is run")
            return True
        # Every tool first, so that a missing one is a setup error.
        expect_os = triple_os(ctx.triple)
        dotnet = ctx.dotnet()
        rc8, rc10 = ctx.runtimeconfig(8), ctx.runtimeconfig(10)
        runtime_dir = ctx.runtime(8)[1]
        ilverify = ctx.ilverify()
        peverify = ctx.peverify() if IS_WINDOWS else None
        ildasm = ctx.ildasm_framework()
        ilasm = ctx.image_ilasm()
        rt_ildasm, rt_ilasm = ctx.ildasm_nuget(), ctx.ilasm_nuget()
        jitall_dir = ctx.jitall()
        everywhere = [
            p
            for p in programs
            if os.path.splitext(os.path.basename(p.source))[0] in RUN_EVERYWHERE
        ]
        linux = {}
        wsl_dotnet = None
        if IS_WINDOWS and everywhere:
            wsl_dotnet = ctx.wsl_dotnet()
            linux = self.linux_copies(ctx, base, everywhere)
        run_dir = os.path.join(base, "run")
        allow = os.path.join(UTILS, "ilverify-allowed.txt")

        def run_on(expect, program, *extra, os_name=expect_os):
            cmd = script("msil-run.py") + ["--expect-os", os_name, "--expect", expect]
            return check_command(cmd + list(extra) + ["--", program])

        def verify(image):
            """%msil_verify's part of D21 on image (None when it passes)."""
            cmd = script("msil-verify.py") + [
                "--ilverify",
                ilverify,
                "--runtime-dir",
                runtime_dir,
                "--allow",
                allow,
            ]
            if peverify:
                cmd += ["--peverify", peverify]
            return check_command(cmd + ["--ildasm", ildasm, "--ilasm", ilasm, image])

        def jitall(image):
            """%msil_jitall's part of D21 on image (None when it passes)."""
            cmd = script("msil-jitall.py") + ["--jitall", jitall_dir]
            cmd += ["--dotnet", dotnet, "--runtimeconfig", rc8] + JITALL_LIMITS
            return check_command(cmd + ["--", image])

        def legs(p):
            key = (p.name, "O2")
            stem = os.path.splitext(os.path.basename(p.source))[0]
            expect = os.path.splitext(p.source)[0]
            copy = copies.get(key)
            if copy is None or isinstance(copy, CheckError):
                return [("copy", "no copy: %s" % copy)]
            d = os.path.join(run_dir, p.name)
            os.makedirs(d)
            exe = os.path.join(d, stem + ".exe")
            shutil.copyfile(copy, exe)
            results = []
            results.append(("D21 verification", verify(exe)))
            results.append(("D21 jitall", jitall(exe)))
            results.append(
                (
                    ".NET 8",
                    run_on(expect, exe, "--dotnet", dotnet, "--runtimeconfig", rc8),
                )
            )
            if peverify:
                try:
                    extra = peverify_md(peverify, exe) - peverify_md(
                        peverify, images[key]
                    )
                    err = (
                        "PEVerify /md reports for the copy what it does not report for its "
                        "image:\n  " + "\n  ".join(sorted(extra))
                        if extra
                        else None
                    )
                except CheckError as e:
                    err = str(e)
                results.append(("PEVerify /md subset", err))
            reassembled = os.path.join(d, "roundtrip", stem + ".exe")
            err = check_command(
                script("msil-verify.py")
                + [
                    "--roundtrip",
                    "--output",
                    reassembled,
                    "--ildasm",
                    rt_ildasm,
                    "--ilasm",
                    rt_ilasm,
                    exe,
                ]
            )
            results.append(("ildasm -> ilasm round trip", err))
            if err is None:
                results.append(
                    (
                        "round trip on .NET 8",
                        run_on(
                            expect,
                            reassembled,
                            "--dotnet",
                            dotnet,
                            "--runtimeconfig",
                            rc8,
                        ),
                    )
                )
            if stem in RUN_EVERYWHERE:
                results.append(
                    (
                        ".NET 10",
                        run_on(
                            expect, exe, "--dotnet", dotnet, "--runtimeconfig", rc10
                        ),
                    )
                )
                if IS_WINDOWS:
                    results.append(
                        (".NET Framework 4.8", run_on(expect, exe, "--netfx"))
                    )
                    twin = linux.get(p.name)
                    if isinstance(twin, CheckError) or twin is None:
                        err = "no Linux copy: %s" % twin
                        results.append(("Linux twin D21 verification", err))
                        results.append(("WSL .NET 10", err))
                    else:
                        results.append(("Linux twin D21 verification", verify(twin)))
                        results.append(
                            (
                                "WSL .NET 10",
                                run_on(
                                    expect,
                                    twin,
                                    "--runner",
                                    "wsl",
                                    "--wsl-distro",
                                    ctx.args.wsl_distro,
                                    "--dotnet",
                                    wsl_dotnet,
                                    "--runtimeconfig",
                                    rc10,
                                    os_name="linux",
                                ),
                            )
                        )
            return results

        passed = total = 0
        with concurrent.futures.ThreadPoolExecutor(ctx.jobs) as pool:
            for p, results in zip(programs, pool.map(legs, programs)):
                for leg, err in results:
                    total += 1
                    if err is None:
                        passed += 1
                    else:
                        note("copy: FAIL %s -O2 (%s): %s" % (p.name, leg, err))
        note(
            "copy: %d of %d checks of the -O2 copies of %d programs passed (D21 verification "
            "and jitall, .NET 8, PEVerify /md, round trip; %d also on .NET 10, .NET Framework "
            "4.8 and, with their Linux-triple twins verified, WSL .NET 10)"
            % (passed, total, len(programs), len(everywhere))
        )
        return passed == total


class StatsMode(Mode):
    name = "stats"
    help = "llvm-cil-dump equals mdcanon on BCL assemblies (--stats-only, --canonical)"

    def run(self, ctx):
        start = time.time()
        mdcanon = ctx.mdtool("mdcanon")
        cil_dump = ctx.cil_dump()
        files = []
        if IS_WINDOWS:
            windir = (
                os.environ.get("WINDIR")
                or os.environ.get("SystemRoot")
                or r"C:\Windows"
            )
            files.append(
                os.path.join(
                    windir, "Microsoft.NET", "Framework64", "v4.0.30319", "mscorlib.dll"
                )
            )
        version, directory = ctx.runtime(8)
        files += [
            os.path.join(directory, "System.Runtime.dll"),
            os.path.join(directory, "System.Private.CoreLib.dll"),
        ]
        missing = [f for f in files if not os.path.isfile(f)]
        if missing:
            raise SetupError("not found: %s" % ", ".join(missing))
        ref_version, ref = self.reference_pack_runtime(directory)
        base = ctx.prepare("stats")
        equal = 0
        for f in files:
            name = os.path.basename(f)
            a_path = os.path.join(base, name + ".llvm-cil-dump.stats")
            b_path = os.path.join(base, name + ".mdcanon.stats")
            rc1, a, err1 = run_split([cil_dump, "--stats-only", f], 600)
            rc2, b, err2 = run_split(mdcanon + ["--stats-only", f], 600)
            with open(a_path, "wb") as out:
                out.write(a)
            with open(b_path, "wb") as out:
                out.write(b)
            if rc1 != 0 or rc2 != 0:
                note(
                    "stats: FAIL %s: llvm-cil-dump exited %s, mdcanon %s:\n%s%s"
                    % (f, rc1, rc2, tail(err1), tail(err2))
                )
            elif a == b:
                equal += 1
            else:
                note(
                    "stats: FAIL %s: listings differ:\n%s"
                    % (f, first_difference(b_path, a_path, "mdcanon", "llvm-cil-dump"))
                )
        canon_files = files + ([ref] if ref else [])
        if not ref:
            note(
                "stats: no Microsoft.NETCore.App.Ref 8.x pack is installed next to %s, "
                "so no reference-pack System.Runtime.dll is listed" % directory
            )
        canon_equal = self.compare_canonical(
            ctx, os.path.join(base, "canonical"), cil_dump, canon_files
        )
        note(
            "stats: %d of %d --stats-only and %d of %d --canonical listings equal "
            "(Framework mscorlib.dll on Windows, Microsoft.NETCore.App %s "
            "System.Runtime.dll and System.Private.CoreLib.dll; --canonical also on %s) "
            "in %.0f s"
            % (
                equal,
                len(files),
                canon_equal,
                len(canon_files),
                version,
                "Microsoft.NETCore.App.Ref %s System.Runtime.dll" % ref_version
                if ref
                else "no reference pack",
                time.time() - start,
            )
        )
        return equal == len(files) and canon_equal == len(canon_files)

    @staticmethod
    def reference_pack_runtime(runtime_dir):
        """(version, path) of System.Runtime.dll in the newest
        Microsoft.NETCore.App.Ref 8.x reference pack of the dotnet root that
        holds runtime_dir (<root>/shared/Microsoft.NETCore.App/<version>),
        or (None, None) when no such pack is installed."""
        root = os.path.dirname(os.path.dirname(os.path.dirname(runtime_dir)))
        packs = os.path.join(root, "packs", "Microsoft.NETCore.App.Ref")
        found = []
        if os.path.isdir(packs):
            for ver in os.listdir(packs):
                path = os.path.join(packs, ver, "ref", "net8.0", "System.Runtime.dll")
                if version_key(ver)[0] == 8 and os.path.isfile(path):
                    found.append((version_key(ver), ver, path))
        if not found:
            return None, None
        return max(found)[1:]

    @staticmethod
    def compare_canonical(ctx, out_dir, cil_dump, files):
        """'llvm-cil-dump --canonical' (one run per file, CANONICAL_TIMEOUT
        each) against mdcanon's listings with the default options; returns
        the number of files whose listings are byte-equal."""
        # mdcanon --out-dir names each listing after its file, so files of one
        # name (the runtime's and the pack's System.Runtime.dll) go to
        # separate batches.
        batches = []
        for f in files:
            name = os.path.normcase(os.path.basename(f))
            for batch in batches:
                if name not in batch:
                    batch[name] = f
                    break
            else:
                batches.append({name: f})
        start = time.time()
        expected = {}
        for i, batch in enumerate(batches, 1):
            expected.update(
                ctx.mdcanon_batch(
                    list(batch.values()), os.path.join(out_dir, "mdcanon-%d" % i)
                )
            )
        note(
            "stats: mdcanon listed %d files in %d batch(es) in %.1f s"
            % (len(files), len(batches), time.time() - start)
        )
        equal = 0
        for i, f in enumerate(files, 1):
            out = os.path.join(
                out_dir, "llvm-cil-dump-%d.%s.canon" % (i, os.path.basename(f))
            )
            start = time.time()
            rc, data, err = run_split([cil_dump, "--canonical", f], CANONICAL_TIMEOUT)
            took = time.time() - start
            with open(out, "wb") as fh:
                fh.write(data)
            want = expected[f]
            if rc != 0:
                note(
                    "stats: FAIL %s: llvm-cil-dump --canonical exited %s after %.1f s:\n%s"
                    % (f, rc, took, tail(err))
                )
            elif isinstance(want, CheckError):
                note("stats: FAIL %s: %s" % (f, want))
            elif not same_bytes(want, out):
                note(
                    "stats: FAIL %s: --canonical listings differ:\n%s"
                    % (f, first_line_difference(want, out, "mdcanon", "llvm-cil-dump"))
                )
            else:
                equal += 1
                note(
                    "stats: %s: --canonical listings equal (%d lines; llvm-cil-dump %.1f s)"
                    % (f, data.count(b"\n"), took)
                )
        return equal


MODES = {m.name: m for m in (IlasmMode, DumpersMode, CopyMode, StatsMode)}


def main():
    p = argparse.ArgumentParser(
        prog=SCRIPT,
        description=__doc__.splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="modes:\n"
        + "\n".join("  %-8s %s" % (m.name, m.help) for m in MODES.values()),
    )
    p.add_argument(
        "--mode", required=True, help="comma-separated modes: " + ", ".join(MODES)
    )
    p.add_argument(
        "--clang", help="clang with the MSIL target (default <build>/bin/clang%s)" % EXE
    )
    p.add_argument(
        "--triple",
        default=DEFAULT_TRIPLE,
        help="target triple (default %s)" % DEFAULT_TRIPLE,
    )
    p.add_argument(
        "--levels", default="O0,O2", help="optimization levels (default O0,O2)"
    )
    p.add_argument("--work", help="work directory outside the repository")
    p.add_argument(
        "--mdtools",
        help="directory of mdcanon.dll and mdump.dll (default <build>/tools/msil/mdtools)",
    )
    p.add_argument(
        "--dotnet",
        help="dotnet host (default: from PATH, DOTNET_ROOT or the standard "
        "install locations, as build-mdtools.py finds it)",
    )
    p.add_argument("--ilasm-framework", help="the .NET Framework ilasm.exe")
    p.add_argument(
        "--ilasm-nuget",
        help="the NuGet ilasm (default: MsilILAsmVersion from the NuGet cache)",
    )
    p.add_argument(
        "--cil-dump", help="llvm-cil-dump (default <build>/bin/llvm-cil-dump%s)" % EXE
    )
    p.add_argument(
        "--cil-copy", help="llvm-cil-copy (default <build>/bin/llvm-cil-copy%s)" % EXE
    )
    p.add_argument(
        "--ilverify",
        help="ILVerify for --mode copy (default: ilverify on PATH or in ~/.dotnet/tools)",
    )
    p.add_argument(
        "--peverify",
        help="PEVerify for --mode copy (default: on PATH or in the Windows SDK's "
        "NETFX 4.x Tools)",
    )
    p.add_argument(
        "--ildasm-framework",
        help="the ildasm of msil-verify.py's neutralized pass (default: the Windows "
        "SDK's NETFX 4.x Tools ildasm; the NuGet ildasm elsewhere)",
    )
    p.add_argument(
        "--ildasm-nuget",
        help="the NuGet ildasm of the round trip (default: MsilILAsmVersion from the "
        "NuGet cache)",
    )
    p.add_argument(
        "--jitall",
        metavar="DIR",
        help="directory of jitall.dll for --mode copy, built or refreshed with "
        "build-jitall.py (default <build>/tools/msil/jitall)",
    )
    p.add_argument(
        "--wsl-distro",
        default="Ubuntu",
        help="WSL distribution of --mode copy's WSL runs (default Ubuntu)",
    )
    p.add_argument(
        "--wsl-dotnet",
        help="dotnet inside WSL (default $HOME/.dotnet-msil/dotnet when installed, "
        "else the distribution's dotnet)",
    )
    for m in MODES.values():
        m.add_arguments(p)
    p.add_argument(
        "inputs",
        nargs="+",
        metavar="dir-or-.c",
        help="C programs or directories of them",
    )
    args = p.parse_args()

    modes = []
    for name in args.mode.split(","):
        name = name.strip()
        if name not in MODES:
            p.error("unknown mode %r (known: %s)" % (name, ", ".join(MODES)))
        if name not in [m.name for m in modes]:
            modes.append(MODES[name]())
    try:
        ctx = Context(args)
        for mode in modes:
            mode.check_usage(ctx)
    except UsageError as e:
        p.error(str(e))  # exits 2
    try:
        ok = True
        for mode in modes:
            ok = mode.run(ctx) and ok
    except SetupError as e:
        sys.stderr.write("%s: error: %s\n" % (SCRIPT, e))
        return 1
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

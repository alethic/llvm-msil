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
        [--ilasm-framework PATH] [--ilasm-nuget PATH] <dir-or-.c>...

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

Every mode ends with a summary line '<script>: <mode>: <n> of <m> ...'. Exit
status: 0 when every requested mode passes; 2 on a usage error (an unknown
option or mode, a bad --levels value, a --work inside the repository, or
inputs that are not directories or .c files or hold no C programs), which is
reported before anything runs; 1 when a mode fails or a tool is missing or
cannot be set up (dotnet, clang, mdtools, either ilasm). dotnet is found as
build-mdtools.py finds it.
"""

import argparse
import concurrent.futures
import difflib
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))


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
        if self.args.ilasm_nuget:
            if not os.path.isfile(self.args.ilasm_nuget):
                raise SetupError(
                    "--ilasm-nuget %s does not exist" % self.args.ilasm_nuget
                )
            return os.path.abspath(self.args.ilasm_nuget)
        version = self.ilasm_nuget_version()
        rid = ("win" if IS_WINDOWS else "linux") + "-x64"
        tool = "ilasm" + (".exe" if IS_WINDOWS else "")
        tried = []
        for root in self.nuget_roots():
            path = os.path.join(
                root,
                "runtime.%s.microsoft.netcore.ilasm" % rid,
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
            "NuGet ilasm %s not found (tried %s): run 'dotnet restore msil/utils/tools/msil-tools.proj "
            "-p:MsilToolsRid=%s' or pass --ilasm-nuget PATH"
            % (version, ", ".join(tried), rid)
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


# ---------------------------------------------------------------- modes


class Mode:
    """A comparison over the corpus. Later milestones add subclasses to MODES
    (and their options through add_arguments)."""

    name = ""
    help = ""

    @staticmethod
    def add_arguments(parser):
        pass

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


MODES = {m.name: m for m in (IlasmMode,)}


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

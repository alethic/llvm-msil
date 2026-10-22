#!/usr/bin/env python3
# ===-- msil-prog.py - Build a multi-TU program of the MSIL test suite ----===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Build a multi-translation-unit program for the MSIL execution tests.

    msil-prog.py --clang CLANG --triple <triple>-msil --level <L> --workdir DIR
        [--mode msil|native] [--pipeline lto] [--link-output il]
        [--ar AR] [--ld MSIL-LD] [--ilasm=CMD] [--out FILE] [--also-il FILE]
        [--cflags=FLAGS] [--ldflags=FLAGS] [--native-ldflags=FLAGS]
        [--src FILE...]... [--stock-src FILE...]... [--archives=SPEC]
        [--lib=ARGS]... [--ld-lld LD.LLD --linux-sysroot DIR --start START]
        [--det] [--timeout S] [--mem-limit-mb N]

This is the build step of the %{msil_prog} composites of msil/test/lit.cfg.py
(plan 5.12.3, 5.12.4); the composites verify, force-JIT and run what it
builds. update-expected.py runs its native mode for the .expected files of
%{msil_prog} tests. It prints every command it runs ('msil-prog: <command>')
and passes each tool's output through.

--mode msil (the default) builds the program for the MSIL triple --triple,
with the pipeline --pipeline lto and the link output --link-output il (plan
5.12.1; 'obj' and 'pe' are M20's and are usage errors until then):

  1. each --src: CLANG --target=<triple> CFLAGS -<L> -flto -c SRC -o obj/<stem>.o
  2. each --stock-src (stock-clang bitcode, plan Q12): the tree's clang for
     the native triple, CLANG --target=<native> CFLAGS -<L> -flto
     [-fno-ms-volatile] -c SRC -o stock/<stem>.o; -fno-ms-volatile on
     windows-msvc only, where clang's default /volatile:ms differs from MSIL's
  3. each archive NAME of --archives: its members compiled as in 1 (or 2) into
     ar/NAME/<stem>.o, then AR rcs ar/NAME/libNAME.a <members in order>
  4. the link through the clang driver, which runs msil-ld (--ld MSIL-LD
     becomes --ld-path=MSIL-LD):
       CLANG --target=<triple> [--ld-path=MSIL-LD] -<L> LDFLAGS obj/... stock/...
         -Lar/NAME -lNAME ... LIB-ARGS -Wl,--lto-emit-asm -o prog.il
  5. ILASM-CMD -exe -output=<out> <workdir>/prog.il (with --ilasm): the
     program is always an EXE image, which runs under 'dotnet exec' and on the
     .NET Framework alike (plan 5.12.1, Q19); --also-il FILE also copies
     prog.il to FILE.

--mode native builds the same program for the native triple, the oracle of
plan 5.12.6 (O1): every --src, --stock-src and archive member is an ordinary
source (no -flto), and the link takes no LDFLAGS and no LIB-ARGS (they belong
to the MSIL link; %{msil_ldflags} has -nodefaultlibs in M14):

  - with --ld-lld, --linux-sysroot and --start (all three): the E7 Linux oracle
    of plan 5.12.8 for x86_64-unknown-linux-gnu, which builds on Windows and
    runs under WSL: CLANG --target=x86_64-unknown-linux-gnu -fno-pic CFLAGS -<L>
    -c, archives libNAME.a, and
      LD.LLD -dynamic-linker /lib64/ld-linux-x86-64.so.2 --as-needed START.o
        obj/... stock/... -Lar/NAME -lNAME ... <sysroot>/usr/lib/x86_64-linux-gnu/
        {libm.so.6,libc.so.6,libgcc_s.so.1} -o <out>
    START is msil/utils/linux-oracle/start.c (compiled into the build
    directory) or its object; NATIVE-LDFLAGS are not used;
  - otherwise the clang driver with lld, so archives and their cycles resolve
    as msil-ld resolves them (lld's rules, not GNU ld's): CLANG
    --target=<native> CFLAGS -<L> -c, archives NAME.lib for windows-msvc (the
    MSVC driver turns -lNAME into NAME.lib) and libNAME.a otherwise, and
      CLANG --target=<native> -fuse-ld=lld obj/... stock/... -Lar/NAME -lNAME
        ... NATIVE-LDFLAGS -o <out>
    NATIVE-LDFLAGS is %{msil_native_ldflags}: the compiler-rt builtins of
    msil_native_rtlib for MSVC triples, -lm for Linux triples.

Each mode ignores the options of the other (--ld, --ilasm, --ldflags and --lib
in native mode; --native-ldflags, --ld-lld, --linux-sysroot and --start in
MSIL mode), so one command prefix, lit's %msil_prog_build, serves both. An
empty --ld-lld=, --linux-sysroot= or --start= cancels an earlier one (the
last of a repeated option counts), which selects the driver recipe after a
prefix that names the E7 oracle.

--archives SPEC is NAME=SRC[,SRC...][;NAME=SRC[,SRC...]]... (%{msil_archives}):
one archive per NAME, linked in the order of SPEC after the objects; a SRC
written stock:SRC is compiled as a --stock-src (in MSIL mode). NAME is
letters, digits and '_', '.', '+', '-'; sources may not contain ',' or ';'.

--src and --stock-src take one or more files each and may be repeated; files
keep their order, and the stock objects follow the --src objects on the link
line. --lib ARGS (repeatable; %{msil_libs}) are further link arguments of the
MSIL link, after the objects and archives. --cflags, --ldflags,
--native-ldflags, --lib and --ilasm take one string that is split into words
at blanks, where "..." and '...' quote (with no escape characters, so Windows
paths keep their backslashes) and a quoted part joins the text next to it;
write them as --cflags=VALUE (VALUE may start with '-'). --clang, --ar and
--ld-lld take a program, or a command line split the same way (the harness
self-tests pass stand-ins written in Python).

Every command runs in the build directory, with the sources as paths relative
to it where possible, under --timeout seconds (default 600) and a memory cap of
--mem-limit-mb MiB (default 4096) each (msil-run.py's limits). The build
directory is --workdir, emptied first; --workdir may not contain a source.
--out is the program (default <workdir>/prog.exe; prog.elf for the E7 oracle).

--det (MSIL mode; %{msil_det}) builds twice, in <workdir>/a and <workdir>/b,
with the same relative command lines and -fdebug-compilation-dir=. added to
every compile, and compares every file of the two build directories byte for
byte: the objects, the archives and the --lto-emit-asm text, but not ilasm's
image prog.exe, which ilasm stamps (plan 5.12.4, 3.7 of recon tests-p2). It
prints each file that differs or exists in one build only. --out and --also-il
then receive build a's image and text.

Exit status: 0 on success; 1 when a step fails (its output is shown) or --det
finds a difference; 2 on a usage or configuration error.
"""

import argparse
import importlib.util
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LEVELS = ("O0", "O1", "O2", "O3", "Os", "Oz")
E7_TARGET = "x86_64-unknown-linux-gnu"
E7_LIBDIR = ("usr", "lib", "x86_64-linux-gnu")
E7_LIBS = ("libm.so.6", "libc.so.6", "libgcc_s.so.1")
E7_DYNAMIC_LINKER = "/lib64/ld-linux-x86-64.so.2"
NAME_RE = re.compile(r"^[A-Za-z0-9_][A-Za-z0-9_.+-]*$")


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


class StepFailed(Exception):
    pass


def split_words(text):
    """Split text into words at blanks. "..." and '...' quote without escape
    characters (lit's internal shell on Windows keeps backslashes the same
    way), and a quoted part joins the text next to it: -I"a b" is one word."""
    words, cur, quote, started = [], [], None, False
    for c in text:
        if quote:
            if c == quote:
                quote = None
            else:
                cur.append(c)
        elif c in "\"'":
            quote, started = c, True
        elif c.isspace():
            if started:
                words.append("".join(cur))
                cur, started = [], False
        else:
            cur.append(c)
            started = True
    if quote:
        raise UsageError("unterminated %s quote in %r" % (quote, text))
    if started:
        words.append("".join(cur))
    return words


def quote_word(word):
    """The inverse of split_words for one word (for printed commands)."""
    if word and not re.search(r"[\s\"']", word):
        return word
    if '"' not in word:
        return '"%s"' % word
    if "'" not in word:
        return "'%s'" % word
    raise UsageError("cannot quote %r (it has both kinds of quote)" % word)


def join_words(words):
    return " ".join(quote_word(w) for w in words)


def program(value, option):
    """A program path, or a command line (a stand-in): a list of words. The
    steps run in the build directory, so a program that is a file gets an
    absolute path."""
    if value is None:
        return None
    words = [value] if os.path.isfile(value) else split_words(value)
    if not words:
        raise UsageError("%s is empty" % option)
    if os.path.isfile(words[0]):
        words[0] = os.path.abspath(words[0])
    return words


def native_of(triple):
    if not triple.endswith("-msil"):
        raise UsageError("--triple %s does not end in -msil" % triple)
    return triple[: -len("-msil")]


def is_msvc(native):
    """lit.cfg.py's rule: a Windows triple whose environment is MSVC (or
    missing)."""
    parts = native.split("-")
    return "windows" in parts and (len(parts) < 4 or parts[3].startswith("msvc"))


def parse_archives(spec):
    """NAME=SRC[,SRC...][;...] -> [(NAME, [(SRC, stock)])]."""
    archives, seen = [], set()
    for part in spec.split(";"):
        if not part.strip():
            continue
        name, sep, srcs = part.partition("=")
        name = name.strip()
        if not sep:
            raise UsageError("--archives: %r is not NAME=SRC[,SRC...]" % part.strip())
        if not NAME_RE.match(name):
            raise UsageError("--archives: bad archive name %r" % name)
        if name in seen:
            raise UsageError("--archives: archive %s is named twice" % name)
        seen.add(name)
        members = []
        for src in srcs.split(","):
            src = src.strip()
            stock = src.startswith("stock:")
            if stock:
                src = src[len("stock:") :]
            if not src:
                raise UsageError("--archives: archive %s has an empty source" % name)
            members.append((src, stock))
        archives.append((name, members))
    return archives


def rel(path, base):
    """path relative to base with '/', or absolute when that is impossible
    (another drive on Windows)."""
    path = os.path.abspath(path)
    try:
        r = os.path.relpath(path, base)
    except ValueError:
        return path
    return r.replace(os.sep, "/")


def inside(path, directory):
    path = os.path.normcase(os.path.abspath(path))
    directory = os.path.normcase(os.path.abspath(directory))
    try:
        return os.path.commonpath([path, directory]) == directory
    except ValueError:
        return False


class Builder:
    def __init__(self, args):
        self.args = args
        self.env = dict(os.environ)

    def say(self, text):
        msil_run.emit(sys.stdout, "msil-prog: %s\n" % text)

    def run(self, cmd, cwd, what, produces=None):
        """Run one step under the limits, passing its output through; it
        fails unless the file produces (relative to cwd) exists afterwards."""
        self.say(join_words(cmd))
        args = self.args
        try:
            res = msil_run.run_limited(
                cmd, cwd, self.env, args.timeout, args.mem_limit_mb
            )
        except OSError as e:
            raise StepFailed("%s: cannot run %s: %s" % (what, cmd[0], e))
        msil_run.emit(sys.stdout, res.stdout)
        msil_run.emit(sys.stderr, res.stderr)
        if res.timed_out:
            raise StepFailed("%s failed (timeout after %g s)" % (what, args.timeout))
        if res.memory_limit:
            raise StepFailed(
                "%s failed (memory limit of %d MiB)" % (what, args.mem_limit_mb)
            )
        if res.returncode != 0:
            raise StepFailed(
                "%s failed (exit status %s)"
                % (what, msil_run.describe_exit(res.returncode))
            )
        if produces and not os.path.isfile(os.path.join(cwd, produces)):
            raise StepFailed("%s wrote no %s" % (what, produces))


def unique_name(used, directory, stem, ext):
    """<directory>/<stem><ext>, or <stem>.<n><ext> when that is taken."""
    name, n = "%s/%s%s" % (directory, stem, ext), 2
    while name in used:
        name = "%s/%s.%d%s" % (directory, stem, n, ext)
        n += 1
    used.add(name)
    return name


def build_msil(b, bdir, det):
    """Steps 1-5 of MSIL mode in bdir; returns (il path, image path or None)."""
    args = b.args
    clang, native = args.clang_cmd, args.native
    level = "-" + args.level
    det_flags = ["-fdebug-compilation-dir=."] if det else []
    used = set()
    os.makedirs(os.path.join(bdir, "obj"))

    def compile_one(src, stock, outdir):
        out = unique_name(
            used, outdir, os.path.splitext(os.path.basename(src))[0], ".o"
        )
        if stock:
            cmd = [*clang, "--target=" + native, *args.cflags_words, level, "-flto"]
            if is_msvc(native):
                cmd.append("-fno-ms-volatile")
            what = "the stock-clang compile of %s" % src
        else:
            cmd = [
                *clang,
                "--target=" + args.triple,
                *args.cflags_words,
                level,
                "-flto",
            ]
            what = "the compile of %s" % src
        cmd += det_flags + ["-c", rel(src, bdir), "-o", out]
        b.run(cmd, bdir, what, out)
        return out

    objects = [compile_one(s, False, "obj") for s in args.srcs]
    if args.stock_srcs:
        os.makedirs(os.path.join(bdir, "stock"))
    objects += [compile_one(s, True, "stock") for s in args.stock_srcs]
    lib_args = build_archives(b, bdir, compile_one, "lib%s.a")

    link = [*clang, "--target=" + args.triple]
    if args.ld:
        link.append("--ld-path=" + args.ld)
    link += [level, *args.ldflags_words, *objects, *lib_args, *args.lib_words]
    link += ["-Wl,--lto-emit-asm", "-o", "prog.il"]
    b.run(link, bdir, "the link", "prog.il")
    il = os.path.join(bdir, "prog.il")
    if not args.ilasm_cmd:
        return il, None
    image = os.path.join(bdir, "prog.exe") if det or not args.out else args.out
    image = os.path.abspath(image)
    if os.path.exists(image):
        os.remove(image)
    b.run(
        [*args.ilasm_cmd, "-exe", "-output=" + image, os.path.abspath(il)],
        bdir,
        "ilasm",
        image,
    )
    return il, image


def build_archives(b, bdir, compile_one, pattern):
    """Compile and archive every --archives entry; the link arguments."""
    args = b.args
    lib_args = []
    for name, members in args.archive_list:
        adir = "ar/" + name
        os.makedirs(os.path.join(bdir, "ar", name))
        objs = [compile_one(src, stock, adir) for src, stock in members]
        archive = "%s/%s" % (adir, pattern % name)
        b.run(
            [*args.ar_cmd, "rcs", archive, *objs],
            bdir,
            "archiving %s" % archive,
            archive,
        )
        lib_args += ["-L" + adir, "-l" + name]
    return lib_args


def build_native(b, bdir):
    """Native mode in bdir; returns the program's path."""
    args = b.args
    clang, native = args.clang_cmd, args.native
    level = "-" + args.level
    used = set()
    os.makedirs(os.path.join(bdir, "obj"))
    e7 = bool(args.ld_lld_cmd)
    target = ["--target=" + (E7_TARGET if e7 else native)] + (
        ["-fno-pic"] if e7 else []
    )

    def compile_one(src, stock, outdir):
        out = unique_name(
            used, outdir, os.path.splitext(os.path.basename(src))[0], ".o"
        )
        cmd = [*clang, *target, *args.cflags_words, level, "-c", rel(src, bdir)]
        b.run(cmd + ["-o", out], bdir, "the native compile of %s" % src, out)
        return out

    objects = [compile_one(s, False, "obj") for s in args.srcs]
    if args.stock_srcs:
        os.makedirs(os.path.join(bdir, "stock"))
    objects += [compile_one(s, False, "stock") for s in args.stock_srcs]
    if e7:
        pattern = "lib%s.a"
    else:
        pattern = "%s.lib" if is_msvc(native) else "lib%s.a"
    lib_args = build_archives(b, bdir, compile_one, pattern)
    out = os.path.abspath(
        args.out or os.path.join(bdir, "prog.elf" if e7 else "prog.exe")
    )
    if os.path.exists(out):
        os.remove(out)
    if e7:
        start = args.start
        if start.endswith(".c"):
            start_o = "start.o"
            cmd = [*clang, "--target=" + E7_TARGET, "-ffreestanding", "-fno-pic"]
            cmd += ["-O2", "-c", start, "-o", start_o]
            b.run(cmd, bdir, "the compile of %s" % start, start_o)
            start = start_o
        libdir = os.path.join(args.linux_sysroot, *E7_LIBDIR)
        cmd = [*args.ld_lld_cmd, "-dynamic-linker", E7_DYNAMIC_LINKER, "--as-needed"]
        cmd += [start, *objects, *lib_args]
        cmd += [os.path.join(libdir, n) for n in E7_LIBS] + ["-o", out]
        b.run(cmd, bdir, "the E7 link", out)
    else:
        cmd = [*clang, "--target=" + native, "-fuse-ld=lld", *objects, *lib_args]
        cmd += args.native_ldflags_words + ["-o", out]
        b.run(cmd, bdir, "the native link", out)
    return out


def tree_files(root):
    files = {}
    for d, dirs, names in os.walk(root):
        dirs.sort()
        for n in sorted(names):
            p = os.path.join(d, n)
            files[os.path.relpath(p, root).replace(os.sep, "/")] = p
    return files


def compare_builds(b, a_dir, b_dir, skip):
    """The --det comparison; returns the number of problems."""
    fa, fb = tree_files(a_dir), tree_files(b_dir)
    problems = 0
    for name in sorted(set(fa) | set(fb)):
        if name in skip:
            continue
        if name not in fb:
            b.say("--det: %s exists in build a only" % name)
            problems += 1
        elif name not in fa:
            b.say("--det: %s exists in build b only" % name)
            problems += 1
        else:
            with open(fa[name], "rb") as f:
                da = f.read()
            with open(fb[name], "rb") as f:
                db = f.read()
            if da != db:
                at = 0
                while at < min(len(da), len(db)) and da[at] == db[at]:
                    at += 1
                b.say(
                    "--det: %s differs between a and b (%d and %d bytes, first "
                    "difference at offset %d)" % (name, len(da), len(db), at)
                )
                problems += 1
    compared = len((set(fa) & set(fb)) - set(skip))
    if not problems:
        b.say(
            "--det: the %d files of builds a and b are identical%s"
            % (compared, " (ilasm's image prog.exe not compared)" if skip else "")
        )
    return problems


def parse_args(argv):
    p = argparse.ArgumentParser(
        prog="msil-prog.py",
        allow_abbrev=False,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=__doc__,
    )
    p.add_argument("--mode", choices=("msil", "native"), default="msil")
    p.add_argument("--pipeline", choices=("lto", "obj"), default="lto")
    p.add_argument("--link-output", choices=("il", "pe"), default="il")
    p.add_argument("--level", required=True, help="O0, O1, O2, O3, Os or Oz")
    p.add_argument("--triple", required=True, help="the MSIL triple (<native>-msil)")
    p.add_argument("--clang", required=True, help="clang (a program or a command)")
    p.add_argument("--ar", help="llvm-ar (a program or a command)")
    p.add_argument("--ld", help="msil-ld, passed to clang as --ld-path=")
    p.add_argument("--ilasm", help="the ilasm command (MSIL mode, link output il)")
    p.add_argument("--ld-lld", help="E7: ld.lld (a program or a command)")
    p.add_argument("--linux-sysroot", help="E7: make-linux-sysroot.py's directory")
    p.add_argument("--start", help="E7: msil/utils/linux-oracle/start.c or its object")
    p.add_argument("--cflags", default="", help="compile flags (%%{msil_cflags})")
    p.add_argument("--ldflags", default="", help="MSIL link flags (%%{msil_ldflags})")
    p.add_argument(
        "--native-ldflags",
        default="",
        help="native link flags (%%{msil_native_ldflags}; not with E7)",
    )
    p.add_argument("--src", nargs="*", action="extend", default=[], dest="srcs")
    p.add_argument(
        "--stock-src", nargs="*", action="extend", default=[], dest="stock_srcs"
    )
    p.add_argument("--archives", default="", help="NAME=SRC[,SRC...][;...]")
    p.add_argument("--lib", action="append", default=[], help="MSIL link arguments")
    p.add_argument("--workdir", required=True)
    p.add_argument("--out", help="the program (default <workdir>/prog.exe)")
    p.add_argument("--also-il", help="also copy the --lto-emit-asm text here")
    p.add_argument("--det", action="store_true", help="build twice and compare")
    p.add_argument(
        "--timeout", type=float, default=600.0, help="seconds per step (600)"
    )
    p.add_argument(
        "--mem-limit-mb",
        type=int,
        default=msil_run.DEFAULT_MEM_LIMIT_MB,
        help="memory cap per step in MiB (%d)" % msil_run.DEFAULT_MEM_LIMIT_MB,
    )
    args = p.parse_args(argv)
    if args.level not in LEVELS:
        p.error("--level %s: use one of %s" % (args.level, ", ".join(LEVELS)))
    if args.timeout <= 0 or args.mem_limit_mb <= 0:
        p.error("--timeout and --mem-limit-mb must be positive")
    return p, args


def check_args(args):
    """Everything parse_args cannot check; raises UsageError."""
    args.native = native_of(args.triple)
    if args.mode == "msil":
        if args.pipeline == "obj":
            raise UsageError(
                "--pipeline obj needs CIL objects and msil-ld's PE writer (plan M20); "
                "use --pipeline lto"
            )
        if args.link_output == "pe":
            raise UsageError(
                "--link-output pe needs msil-ld's PE writer (plan M20); use "
                "--link-output il"
            )
    else:
        if args.det:
            raise UsageError("--det compares MSIL builds; it has no native mode")
        if args.also_il:
            raise UsageError("--also-il is an MSIL-mode option")
    e7 = [args.ld_lld, args.linux_sysroot, args.start]
    if any(e7) and not all(e7):
        raise UsageError(
            "the E7 oracle needs all of --ld-lld, --linux-sysroot and --start"
        )
    if args.mode == "native" and all(e7) and args.native != E7_TARGET:
        raise UsageError(
            "the E7 Linux oracle builds %s only, not %s" % (E7_TARGET, args.native)
        )
    if not args.srcs:
        raise UsageError("no --src")
    args.clang_cmd = program(args.clang, "--clang")
    args.ar_cmd = program(args.ar, "--ar")
    # E7 is a native-mode recipe: MSIL mode ignores its options.
    args.ld_lld_cmd = (
        program(args.ld_lld, "--ld-lld")
        if args.mode == "native" and args.ld_lld
        else None
    )
    if args.ld:
        args.ld = os.path.abspath(args.ld) if os.path.isfile(args.ld) else args.ld
    args.ilasm_cmd = split_words(args.ilasm) if args.ilasm else None
    if args.ilasm is not None and not args.ilasm_cmd:
        raise UsageError("--ilasm is empty")
    args.cflags_words = split_words(args.cflags)
    args.ldflags_words = split_words(args.ldflags)
    args.native_ldflags_words = split_words(args.native_ldflags)
    args.lib_words = [w for lib in args.lib for w in split_words(lib)]
    args.archive_list = parse_archives(args.archives)
    if args.archive_list and not args.ar_cmd:
        raise UsageError("--archives needs --ar")
    if args.mode == "msil" and not args.det and not args.ilasm_cmd:
        raise UsageError("MSIL mode with --link-output il needs --ilasm")
    if args.linux_sysroot:
        libdir = os.path.join(args.linux_sysroot, *E7_LIBDIR)
        lacking = [n for n in E7_LIBS if not os.path.isfile(os.path.join(libdir, n))]
        if lacking:
            raise UsageError(
                "--linux-sysroot %s has no %s in usr/lib/x86_64-linux-gnu (run "
                "msil/utils/make-linux-sysroot.py)"
                % (args.linux_sysroot, ", ".join(lacking))
            )
        args.linux_sysroot = os.path.abspath(args.linux_sysroot)
    if args.start:
        if not os.path.isfile(args.start):
            raise UsageError("--start %s: no such file" % args.start)
        args.start = os.path.abspath(args.start)
    sources = list(args.srcs) + list(args.stock_srcs)
    sources += [s for _, members in args.archive_list for s, _ in members]
    for src in sources:
        if not os.path.isfile(src):
            raise UsageError("source %s: no such file" % src)
        if inside(src, args.workdir):
            raise UsageError(
                "--workdir %s holds the source %s; it is emptied before the build"
                % (args.workdir, src)
            )
    args.srcs = [os.path.abspath(s) for s in args.srcs]
    args.stock_srcs = [os.path.abspath(s) for s in args.stock_srcs]
    args.archive_list = [
        (name, [(os.path.abspath(s), stock) for s, stock in members])
        for name, members in args.archive_list
    ]
    if args.out:
        args.out = os.path.abspath(args.out)
    args.workdir = os.path.abspath(args.workdir)
    if os.path.dirname(args.workdir) == args.workdir:
        raise UsageError("--workdir %s is a file-system root" % args.workdir)


def main(argv=None):
    p, args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        check_args(args)
    except UsageError as e:
        sys.stderr.write("msil-prog: error: %s\n" % e)
        return 2
    b = Builder(args)
    msil_run.remove_tree(args.workdir)
    if os.path.exists(args.workdir):
        sys.stderr.write("msil-prog: error: cannot empty --workdir %s\n" % args.workdir)
        return 2
    try:
        if args.mode == "native":
            os.makedirs(args.workdir)
            build_native(b, args.workdir)
        elif not args.det:
            os.makedirs(args.workdir)
            il, _ = build_msil(b, args.workdir, False)
            if args.also_il:
                shutil.copyfile(il, args.also_il)
        else:
            dirs = [os.path.join(args.workdir, x) for x in ("a", "b")]
            built = []
            for d in dirs:
                os.makedirs(d)
                built.append(build_msil(b, d, True))
            skip = {"prog.exe"} if built[0][1] else set()
            if compare_builds(b, dirs[0], dirs[1], skip):
                sys.stderr.write(
                    "msil-prog: error: --det: builds a and b differ (see above)\n"
                )
                return 1
            il, image = built[0]
            if args.out and image:
                shutil.copyfile(image, args.out)
            if args.also_il:
                shutil.copyfile(il, args.also_il)
    except StepFailed as e:
        sys.stderr.write("msil-prog: error: %s\n" % e)
        return 1
    except OSError as e:
        sys.stderr.write("msil-prog: error: %s\n" % e)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())

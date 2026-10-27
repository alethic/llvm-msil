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
        [--mode msil|native] [--pipeline obj|lto] [--link-output pe|il]
        [--ar AR] [--ld MSIL-LD] [--llc LLC] [--ilasm=CMD] [--out FILE]
        [--also-il FILE] [--equiv-out FILE] [--equiv-skip FILE --test PATH]
        [--cflags=FLAGS] [--ldflags=FLAGS] [--native-ldflags=FLAGS]
        [--src FILE...]... [--stock-src FILE...]... [--archives=SPEC]
        [--shlibs=SPEC] [--lib=ARGS]... [--native-lib=ARGS]...
        [--ld-lld LD.LLD --linux-sysroot DIR --start START]
        [--det] [--timeout S] [--mem-limit-mb N]

This is the build step of the %{msil_prog} composites of msil/test/lit.cfg.py
(plan 5.12.3, 5.12.4), and from M20 of %{msil_c} and %{msil_ll} under
msil_pipeline=obj; the composites verify, force-JIT and run what it builds.
update-expected.py runs its native mode for the .expected files of
%{msil_prog} tests. It prints every command it runs ('msil-prog: <command>')
and passes each tool's output through.

--mode msil (the default) builds the program for the MSIL triple --triple,
with the pipeline --pipeline (plan 5.12.1: 'obj', the default from M20,
compiles CIL objects with clang -c; 'lto' compiles bitcode with clang -flto
-c) and the link output --link-output ('pe', the default from M20: msil-ld
writes the program image; 'il': msil-ld writes --lto-emit-asm text that
ilasm assembles, lto only):

  1. each --src: CLANG --target=<triple> CFLAGS -<L> [-flto] -c SRC
     -o obj/<stem>.o, -flto under lto; under obj a SRC that ends in '.ll' is
     compiled by --llc when it is given (%{msil_ll} under obj, plan 5.12.1:
     'llc -filetype=obj' plus the link):
       LLC -mtriple=<triple> -<L> -filetype=obj SRC -o obj/<stem>.o
     (llc takes -O0 to -O3 only)
  2. each --stock-src (stock-clang bitcode, plan Q12): the tree's clang for
     the native triple, CLANG --target=<native> CFLAGS -<L> -flto
     [-fno-ms-volatile] -c SRC -o stock/<stem>.o; -fno-ms-volatile on
     windows-msvc only, where clang's default /volatile:ms differs from MSIL's
     (also under obj: msil-ld links bitcode beside the objects)
  3. each archive NAME of --archives: its members compiled as in 1 (or 2) into
     ar/NAME/<stem>.o, then AR rcs ar/NAME/libNAME.a <members in order>
  4. the link through the clang driver, which runs msil-ld (--ld MSIL-LD
     becomes --ld-path=MSIL-LD); LINK below is
       CLANG --target=<triple> [--ld-path=MSIL-LD] -<L> LDFLAGS obj/... stock/...
         -Lar/NAME -lNAME ... LIB-ARGS NATIVE-LIB-ARGS
     and the program is always an EXE image, which runs under 'dotnet exec'
     and on the .NET Framework alike (plan 5.12.1, Q19):
       pe: LINK -o prog.exe (msil-ld writes the image; copied to --out);
       il: LINK -Wl,--lto-emit-asm -o prog.il, then
           ILASM-CMD -exe -output=<out> <workdir>/prog.il (--ilasm), and
           --also-il FILE copies prog.il to FILE.
  5. the equivalence outputs (plan 5.12.6), pe only, after the main link:
       --equiv-out FILE: LINK -Wl,--no-gc-sections -o equiv/prog.exe, copied
           to FILE: the image that O3 (obj) and O4 (lto) compare, never the
           one that runs, which keeps msil-ld's default --gc-sections;
       --also-il FILE (lto): LINK -Wl,--lto-emit-asm -o equiv/prog.il, copied
           to FILE: the same link's text, the text side of O4.
     Both keep the names of the main link ('prog' assembly, 'prog.exe'
     module), which O3's and O4's ilasm builds give theirs too.
     --equiv-skip FILE --test PATH (both or neither): when FILE
     (msil/utils/equiv-allowed.txt) has a
     'skip <oracle>[@<triple>][:<level>] <path> <reason>' line for the
     oracle the build feeds (O3 under obj, O4 under lto) that covers PATH
     (lit's %s, made repository-relative as msil-equiv.py makes it) for
     this build's --triple and --level (a line that names a triple or a
     level covers only that triple or level, one that names neither covers
     them all; plan section 12, 'Plan 5.12.6 amended (skip lines)'),
     neither output is built, and it prints
     'msil-prog: <oracle> skipped: <reason>'; msil-equiv.py --test with the
     same --triple and --level then skips the comparison the same way. A
     'skip O6' line (plan section 12, 'M21.c extended') is msil-verify.py's:
     O6 compares the image that runs with the composite's ilasm build, which
     this script builds neither way, so it changes nothing here, and an O3
     line does not skip O6.

--mode native builds the same program for the native triple, the oracle of
plan 5.12.6 (O1): every --src, --stock-src and archive member is an ordinary
source (no -flto), and the link takes no LDFLAGS and no LIB-ARGS (they belong
to the MSIL link; %{msil_ldflags} names msil-ld's --sysroot on Linux triples),
but it takes NATIVE-LIB-ARGS:

  - with --ld-lld, --linux-sysroot and --start (all three): the E7 Linux oracle
    of plan 5.12.8 for x86_64-unknown-linux-gnu, which builds on Windows and
    runs under WSL: CLANG --target=x86_64-unknown-linux-gnu -fno-pic CFLAGS -<L>
    -c, archives libNAME.a, and
      LD.LLD -dynamic-linker /lib64/ld-linux-x86-64.so.2 --as-needed
        -rpath $ORIGIN START.o obj/... stock/... -Lar/NAME -lNAME ...
        NATIVE-LIB-ARGS <sysroot>/usr/lib/x86_64-linux-gnu/
        {libm.so.6,libc.so.6,libgcc_s.so.1} -o <out>
    START is msil/utils/linux-oracle/start.c (compiled into the build
    directory) or its object; NATIVE-LDFLAGS are not used, and a
    NATIVE-LIB-ARGS word -Wl,A,B... (written for the clang driver) is passed
    as A B...; -rpath $ORIGIN (plan M15.c) lets the dynamic loader find a
    test-built shared library that msil-run.py --dep copied next to the
    program;
  - otherwise the clang driver with lld, so archives and their cycles resolve
    as msil-ld resolves them (lld's rules, not GNU ld's): CLANG
    --target=<native> CFLAGS -<L> -c, archives NAME.lib for windows-msvc (the
    MSVC driver turns -lNAME into NAME.lib) and libNAME.a otherwise, and
      CLANG --target=<native> -fuse-ld=lld obj/... stock/... -Lar/NAME -lNAME
        ... NATIVE-LIB-ARGS NATIVE-LDFLAGS -o <out>
    NATIVE-LDFLAGS is %{msil_native_ldflags}: the compiler-rt builtins of
    msil_native_rtlib for MSVC triples, -lm for Linux triples.

Each mode ignores the options of the other (--pipeline, --link-output, --ld,
--llc, --ilasm, --ldflags and --lib in native mode; --native-ldflags,
--ld-lld, --linux-sysroot and --start in MSIL mode), so one command prefix,
lit's %msil_prog_build, serves both. An
empty --ld-lld=, --linux-sysroot= or --start= cancels an earlier one (the
last of a repeated option counts), which selects the driver recipe after a
prefix that names the E7 oracle.

--archives SPEC is NAME=SRC[,SRC...][;NAME=SRC[,SRC...]]... (%{msil_archives}):
one archive per NAME, linked in the order of SPEC after the objects; a SRC
written stock:SRC is compiled as a --stock-src (in MSIL mode). NAME is
letters, digits and '_', '.', '+', '-'; sources may not contain ',' or ';'.

--shlibs SPEC is [?]NAME=SRC[,SRC...][;[?]NAME=SRC[,SRC...]]...
(%{msil_shlibs}, plan 5.12.3, 5.12.4, M22): shared libraries, built in the
order of SPEC before anything else (the program's sources, stock sources and
archives come after them), each linked against the libraries built before it
(the latest build of every other NAME) with the program's --cflags and
--ldflags (in MSIL mode %{msil_ldflags}, with %{msil_ref_flags}; no --lib or
--native-lib arguments). The program links every library (not the
interfaces) after its archives, in the order of SPEC. ?NAME builds only the
interface of NAME, for a cycle, which is written ?b=b.c;a=a.c;b=b.c: an
interface needs the library of its NAME later in SPEC, and each NAME has at
most one library and one interface. The names follow --archives' rule, a SRC
written stock:SRC is a stock source (MSIL mode), and each library's objects
go into shlib-obj/NAME/ (an interface's into shlib-iface-obj/NAME/):
  MSIL mode:   LINK-PREFIX -shared <objects> <earlier libraries> -o
               shlib/NAME.dll, where LINK-PREFIX is the program link's
               CLANG --target=<triple> [--ld-path=MSIL-LD] -<L> LDFLAGS; an
               interface adds -Wl,--interface-only and writes
               shlib-iface/NAME.dll; the program link gets shlib/NAME.dll
               after its archives;
  native, Windows (the driver recipe): CLANG --target=<native> -fuse-ld=lld
               -shared <objects> <earlier import libraries> NATIVE-LDFLAGS
               -o shlib/NAME.dll, which also writes shlib/NAME.lib; an
               interface is the same link with -Wl,/force:unresolved into
               shlib-iface/NAME.dll, of which only shlib-iface/NAME.lib is
               kept (MinGW: -Wl,--out-implib=<dir>/libNAME.dll.a, and for an
               interface -Wl,-Xlink=-force:unresolved, unverified); on MSVC
               triples every native compile of the build adds
               -fms-runtime-lib=dll, so the program and its libraries share
               one C runtime (heap, streams, errno) as MSIL assemblies share
               the process's ucrtbase (recon exports-multiasm 7.1), where
               the default static CRT gives each DLL its own; the program
               link gets shlib/NAME.lib (MinGW shlib/libNAME.dll.a);
  native, E7:  LD.LLD -shared -Bsymbolic --as-needed -soname libNAME.so
               -rpath $ORIGIN <objects> <earlier libraries> <the sysroot's
               libm.so.6, libc.so.6 and libgcc_s.so.1> -o shlib/libNAME.so,
               and the program link gets shlib/libNAME.so after its archives;
               each -rpath $ORIGIN finds that module's libraries next to it
               (ld.lld writes DT_RUNPATH, which covers only the DT_NEEDED of
               its own module, and the --as-needed program link leaves out a
               library that only another library needs); there are no
               interfaces (ld.lld -shared leaves undefined symbols to the
               dynamic loader), so ?NAME builds nothing; every compile of
               the build is -fPIC instead of -fno-pic, because a -fno-pic
               program would get copy relocations and canonical PLT entries
               that a -Bsymbolic library never sees (a second copy of
               exported data, two addresses for a function);
  native, Linux with the driver recipe: CLANG --target=<native>
               -fuse-ld=lld -shared <objects> <earlier libraries>
               -Wl,-Bsymbolic -Wl,-soname,libNAME.so -Wl,-rpath,$ORIGIN
               NATIVE-LDFLAGS -o shlib/libNAME.so, -fPIC compiles, no
               interfaces, and -Wl,-rpath,$ORIGIN on the program link too.
Without --shlibs every command is unchanged. The build directory always has
a shlib/ directory (empty without libraries), which the composites give
msil-verify.py, msil-jitall.py and msil-run.py as --dep-dir.

--src and --stock-src take one or more files each and may be repeated; files
keep their order, and the stock objects follow the --src objects on the link
line. --lib ARGS (repeatable; %{msil_libs}) are further link arguments of the
MSIL link, after the objects and archives. --native-lib ARGS (repeatable;
%{msil_native_libs}, plan 5.12.3, M15) are link arguments of both modes: the
native libraries of the program (for example -L<dir> -lnative for a
test-built native.dll or libnative.so), after the --lib arguments in MSIL
mode and after the archives in native mode. --cflags, --ldflags,
--native-ldflags, --lib, --native-lib and --ilasm take one string that is
split into words
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
every clang compile, and compares every file of the two build directories
byte for byte (plan 5.12.4, 3.7 of recon tests-p2): the objects, the
archives, the libraries and interfaces of --shlibs and their objects, and
msil-ld's image prog.exe (pe; from M20) or its --lto-emit-asm text (il), but
not ilasm's image prog.exe, which ilasm stamps; the equivalence outputs of
step 5 are built and compared in both builds too. It
prints each file that differs or exists in one build only. --out, --also-il
and --equiv-out then receive build a's files.

Usage errors: --pipeline obj with --link-output il (obj writes PE images
only); --also-il under obj; --equiv-out with --link-output il; --equiv-skip
without --test (or the reverse) or with a malformed FILE; --llc under lto;
--llc with a .ll source at -Os or -Oz; --also-il or --equiv-out in native
mode; il without --ilasm (unless --det); --shlibs with --link-output il (a
link against a .NET library has no --lto-emit-asm text) or with a malformed
SPEC (a bad name, a library or an interface named twice, an interface after
its library or without one).

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


def load_script(module_name, file_name):
    # No __pycache__ next to the scripts in the source tree.
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location(
        module_name, os.path.join(HERE, file_name)
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


msil_run = load_script("msil_run", "msil-run.py")
# The allow-list parser and the test-path rule of the equivalence oracles,
# shared so that --equiv-skip and msil-equiv.py --test skip the same tests.
msil_equiv = load_script("msil_equiv", "msil-equiv.py")


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


def parse_shlibs(spec):
    """[?]NAME=SRC[,SRC...][;...] -> [(NAME, interface, [(SRC, stock)])],
    checked: each NAME has at most one library and one interface, and an
    interface comes before its library."""
    shlibs, real, iface = [], set(), set()
    for part in spec.split(";"):
        if not part.strip():
            continue
        name, sep, srcs = part.partition("=")
        name = name.strip()
        interface = name.startswith("?")
        if interface:
            name = name[1:]
        if not sep:
            raise UsageError("--shlibs: %r is not [?]NAME=SRC[,SRC...]" % part.strip())
        if not NAME_RE.match(name):
            raise UsageError("--shlibs: bad library name %r" % name)
        what = "the interface ?%s" % name if interface else "library %s" % name
        if name in (iface if interface else real):
            raise UsageError("--shlibs: %s is named twice" % what)
        if interface and name in real:
            raise UsageError(
                "--shlibs: the interface ?%s comes after library %s; it is for "
                "the libraries built before %s" % (name, name, name)
            )
        (iface if interface else real).add(name)
        members = []
        for src in srcs.split(","):
            src = src.strip()
            stock = src.startswith("stock:")
            if stock:
                src = src[len("stock:") :]
            if not src:
                raise UsageError("--shlibs: %s has an empty source" % what)
            members.append((src, stock))
        shlibs.append((name, interface, members))
    lacking = sorted(iface - real)
    if lacking:
        raise UsageError(
            "--shlibs: the interface ?%s has no library %s after it (the program "
            "links the libraries, never an interface)" % (lacking[0], lacking[0])
        )
    return shlibs


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
    """Steps 1-5 of MSIL mode in bdir. Returns {'image': the program image or
    None (il without --ilasm), 'il': the --lto-emit-asm text or None,
    'equiv': the --no-gc-sections image or None}."""
    args = b.args
    clang, native = args.clang_cmd, args.native
    lto = args.pipeline == "lto"
    level = "-" + args.level
    det_flags = ["-fdebug-compilation-dir=."] if det else []
    used = set()
    os.makedirs(os.path.join(bdir, "obj"))
    os.makedirs(os.path.join(bdir, "shlib"))

    def compile_one(src, stock, outdir):
        out = unique_name(
            used, outdir, os.path.splitext(os.path.basename(src))[0], ".o"
        )
        os.makedirs(os.path.join(bdir, outdir), exist_ok=True)
        if stock:
            cmd = [*clang, "--target=" + native, *args.cflags_words, level, "-flto"]
            if is_msvc(native):
                cmd.append("-fno-ms-volatile")
            cmd += det_flags + ["-c"]
            what = "the stock-clang compile of %s" % src
        elif not lto and args.llc_cmd and src.endswith(".ll"):
            # %{msil_ll} under obj: llc writes the CIL object (plan 5.12.1).
            cmd = [*args.llc_cmd, "-mtriple=" + args.triple, level, "-filetype=obj"]
            what = "the llc compile of %s" % src
        else:
            cmd = [*clang, "--target=" + args.triple, *args.cflags_words, level]
            cmd += (["-flto"] if lto else []) + det_flags + ["-c"]
            what = "the compile of %s" % src
        cmd += [rel(src, bdir), "-o", out]
        b.run(cmd, bdir, what, out)
        return out

    prefix = [*clang, "--target=" + args.triple]
    if args.ld:
        prefix.append("--ld-path=" + args.ld)
    prefix += [level, *args.ldflags_words]
    shlibs = build_shlibs_msil(b, bdir, compile_one, prefix)

    objects = [compile_one(s, False, "obj") for s in args.srcs]
    if args.stock_srcs:
        os.makedirs(os.path.join(bdir, "stock"))
    objects += [compile_one(s, True, "stock") for s in args.stock_srcs]
    lib_args = build_archives(b, bdir, compile_one, "lib%s.a")

    link = prefix + [*objects, *lib_args, *shlibs, *args.lib_words]
    link += args.native_lib_words
    out = {"image": None, "il": None, "equiv": None}
    if args.link_output == "il":
        b.run(
            link + ["-Wl,--lto-emit-asm", "-o", "prog.il"], bdir, "the link", "prog.il"
        )
        out["il"] = os.path.join(bdir, "prog.il")
        if not args.ilasm_cmd:
            return out
        image = os.path.join(bdir, "prog.exe") if det or not args.out else args.out
        image = os.path.abspath(image)
        if os.path.exists(image):
            os.remove(image)
        b.run(
            [*args.ilasm_cmd, "-exe", "-output=" + image, os.path.abspath(out["il"])],
            bdir,
            "ilasm",
            image,
        )
        out["image"] = image
        return out
    b.run(link + ["-o", "prog.exe"], bdir, "the link", "prog.exe")
    out["image"] = os.path.join(bdir, "prog.exe")
    if args.also_il or args.equiv_out:
        os.makedirs(os.path.join(bdir, "equiv"))
    if args.also_il:
        text = "equiv/prog.il"
        b.run(link + ["-Wl,--lto-emit-asm", "-o", text], bdir, "the text link", text)
        out["il"] = os.path.join(bdir, text)
    if args.equiv_out:
        image = "equiv/prog.exe"
        b.run(
            link + ["-Wl,--no-gc-sections", "-o", image],
            bdir,
            "the equivalence link",
            image,
        )
        out["equiv"] = os.path.join(bdir, image)
    return out


def deliver(src, dest):
    """Copy the built file src to dest unless they are the same file."""
    if not src or not dest:
        return
    if os.path.normcase(os.path.abspath(src)) == os.path.normcase(dest):
        return
    os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)
    shutil.copyfile(src, dest)


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


def build_shlibs(b, link_one):
    """Build every --shlibs entry in order: link_one(name, interface,
    objdir, members, earlier) compiles the entry's members into objdir
    (shlib-obj/NAME, an interface's shlib-iface-obj/NAME), links it, and
    returns the file that later links name through (None: the recipe has no
    use for that interface, and nothing was built). earlier is the latest
    build of every other name, in the order of the builds. Returns what the
    program links: the libraries, in the order of the specification."""
    built, real = {}, []
    for name, interface, members in b.args.shlib_list:
        objdir = ("shlib-iface-obj/" if interface else "shlib-obj/") + name
        earlier = [path for n, path in built.items() if n != name]
        result = link_one(name, interface, objdir, members, earlier)
        if result is None:
            continue
        built.pop(name, None)
        built[name] = result
        if not interface:
            real.append(result)
    return real


def build_shlibs_msil(b, bdir, compile_one, prefix):
    """--shlibs in MSIL mode: 'LINK-PREFIX -shared' into shlib/NAME.dll, an
    interface with -Wl,--interface-only into shlib-iface/NAME.dll."""

    def link_one(name, interface, objdir, members, earlier):
        objs = [compile_one(src, stock, objdir) for src, stock in members]
        outdir = "shlib-iface" if interface else "shlib"
        os.makedirs(os.path.join(bdir, outdir), exist_ok=True)
        out = "%s/%s.dll" % (outdir, name)
        cmd = prefix + ["-shared", *objs, *earlier]
        if interface:
            cmd.append("-Wl,--interface-only")
        what = "the interface link of ?%s" if interface else "the link of library %s"
        b.run(cmd + ["-o", out], bdir, what % name, out)
        return out

    return build_shlibs(b, link_one)


def build_shlibs_native(b, bdir, compile_one, flavor, libdir):
    """--shlibs in native mode (flavor 'e7', 'msvc', 'mingw' or 'linux'; see
    the module documentation); libdir: the sysroot's library directory of
    the E7 recipe."""
    args = b.args

    def link_one(name, interface, objdir, members, earlier):
        if interface and flavor in ("e7", "linux"):
            # ld.lld -shared leaves undefined symbols to the dynamic loader.
            return None
        objs = [compile_one(src, stock, objdir) for src, stock in members]
        outdir = "shlib-iface" if interface else "shlib"
        os.makedirs(os.path.join(bdir, outdir), exist_ok=True)
        what = "the interface link of ?%s" if interface else "the link of library %s"
        what %= name
        if flavor == "e7":
            out = "shlib/lib%s.so" % name
            # -rpath $ORIGIN: the library finds its own libraries next to
            # it (the program's DT_RUNPATH does not apply to them).
            cmd = [*args.ld_lld_cmd, "-shared", "-Bsymbolic", "--as-needed"]
            cmd += ["-soname", "lib%s.so" % name, "-rpath", "$ORIGIN"]
            cmd += [*objs, *earlier]
            cmd += [os.path.join(libdir, n) for n in E7_LIBS] + ["-o", out]
            b.run(cmd, bdir, what, out)
            return out
        cmd = [*args.clang_cmd, "--target=" + args.native, "-fuse-ld=lld", "-shared"]
        cmd += [*objs, *earlier]
        if flavor == "linux":
            out = "shlib/lib%s.so" % name
            cmd += ["-Wl,-Bsymbolic", "-Wl,-soname,lib%s.so" % name]
            cmd += ["-Wl,-rpath,$ORIGIN"]
            cmd += args.native_ldflags_words + ["-o", out]
            b.run(cmd, bdir, what, out)
            return out
        dll = "%s/%s.dll" % (outdir, name)
        if flavor == "msvc":
            # The MSVC driver writes the import library next to the DLL.
            implib = "%s/%s.lib" % (outdir, name)
        else:
            implib = "%s/lib%s.dll.a" % (outdir, name)
            cmd.append("-Wl,--out-implib=" + implib)
        cmd += args.native_ldflags_words
        if interface:
            # Unresolved symbols are left to the library built later.
            cmd.append(
                "-Wl,/force:unresolved"
                if flavor == "msvc"
                else "-Wl,-Xlink=-force:unresolved"
            )
        b.run(cmd + ["-o", dll], bdir, what, implib)
        if interface:
            # Only the import library of an interface is used.
            os.remove(os.path.join(bdir, dll))
        return implib

    return build_shlibs(b, link_one)


def build_native(b, bdir):
    """Native mode in bdir; returns the program's path."""
    args = b.args
    clang, native = args.clang_cmd, args.native
    level = "-" + args.level
    used = set()
    os.makedirs(os.path.join(bdir, "obj"))
    os.makedirs(os.path.join(bdir, "shlib"))
    e7 = bool(args.ld_lld_cmd)
    windows = "windows" in native.split("-")
    if e7:
        flavor = "e7"
    elif windows:
        flavor = "msvc" if is_msvc(native) else "mingw"
    else:
        flavor = "linux"
    target = ["--target=" + (E7_TARGET if e7 else native)]
    if args.shlib_list:
        # One C runtime for the program and its libraries (MSVC), and code
        # that reaches another module's symbols through the GOT (ELF).
        target += {"msvc": ["-fms-runtime-lib=dll"], "mingw": []}.get(flavor, ["-fPIC"])
    elif e7:
        target.append("-fno-pic")
    libdir = os.path.join(args.linux_sysroot, *E7_LIBDIR) if e7 else None

    def compile_one(src, stock, outdir):
        out = unique_name(
            used, outdir, os.path.splitext(os.path.basename(src))[0], ".o"
        )
        os.makedirs(os.path.join(bdir, outdir), exist_ok=True)
        cmd = [*clang, *target, *args.cflags_words, level, "-c", rel(src, bdir)]
        b.run(cmd + ["-o", out], bdir, "the native compile of %s" % src, out)
        return out

    shlibs = build_shlibs_native(b, bdir, compile_one, flavor, libdir)
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
        cmd = [*args.ld_lld_cmd, "-dynamic-linker", E7_DYNAMIC_LINKER, "--as-needed"]
        cmd += ["-rpath", "$ORIGIN", start, *objects, *lib_args, *shlibs]
        cmd += linker_words(args.native_lib_words)
        cmd += [os.path.join(libdir, n) for n in E7_LIBS] + ["-o", out]
        b.run(cmd, bdir, "the E7 link", out)
    else:
        cmd = [*clang, "--target=" + native, "-fuse-ld=lld", *objects, *lib_args]
        cmd += shlibs + args.native_lib_words + args.native_ldflags_words
        if shlibs and flavor == "linux":
            # The dynamic loader finds the libraries next to the program.
            cmd.append("-Wl,-rpath,$ORIGIN")
        b.run(cmd + ["-o", out], bdir, "the native link", out)
    return out


def linker_words(words):
    """Driver link arguments for ld.lld itself: -Wl,A,B becomes A B."""
    out = []
    for w in words:
        if w.startswith("-Wl,"):
            out += [x for x in w[len("-Wl,") :].split(",") if x]
        else:
            out.append(w)
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
    p.add_argument("--pipeline", choices=("lto", "obj"), default="obj")
    p.add_argument("--link-output", choices=("il", "pe"), default="pe")
    p.add_argument("--level", required=True, help="O0, O1, O2, O3, Os or Oz")
    p.add_argument("--triple", required=True, help="the MSIL triple (<native>-msil)")
    p.add_argument("--clang", required=True, help="clang (a program or a command)")
    p.add_argument("--ar", help="llvm-ar (a program or a command)")
    p.add_argument("--ld", help="msil-ld, passed to clang as --ld-path=")
    p.add_argument(
        "--llc", help="llc for .ll sources under obj (a program or a command)"
    )
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
    p.add_argument(
        "--shlibs",
        default="",
        help="[?]NAME=SRC[,SRC...][;...]: shared libraries (%%{msil_shlibs})",
    )
    p.add_argument("--lib", action="append", default=[], help="MSIL link arguments")
    p.add_argument(
        "--native-lib",
        action="append",
        default=[],
        help="link arguments of both modes (%%{msil_native_libs})",
    )
    p.add_argument("--workdir", required=True)
    p.add_argument("--out", help="the program (default <workdir>/prog.exe)")
    p.add_argument(
        "--also-il",
        help="also write the --lto-emit-asm text here (lto; pe: a second link)",
    )
    p.add_argument(
        "--equiv-out", help="also write the -Wl,--no-gc-sections image here (pe)"
    )
    p.add_argument(
        "--equiv-skip",
        help="equiv-allowed.txt: build no equivalence output for a skipped --test",
    )
    p.add_argument("--test", help="the test's path, for --equiv-skip (lit's %%s)")
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
    args.skip_reason = None
    if args.mode == "msil":
        if args.pipeline == "obj" and args.link_output == "il":
            raise UsageError(
                "--pipeline obj writes PE images only (plan 5.12.1); use "
                "--link-output pe"
            )
        if args.pipeline == "obj" and args.also_il:
            raise UsageError(
                "--also-il is the --lto-emit-asm text of an LTO link (O4); it needs "
                "--pipeline lto"
            )
        if args.equiv_out and args.link_output == "il":
            raise UsageError(
                "--equiv-out is a second msil-ld image; it needs --link-output pe"
            )
        if args.llc and args.pipeline == "lto":
            raise UsageError(
                "--llc compiles IR sources to CIL objects; it needs --pipeline obj"
            )
        if bool(args.equiv_skip) != bool(args.test):
            raise UsageError("--equiv-skip and --test go together")
        if args.equiv_skip:
            try:
                allowed = msil_equiv.Allowed(args.equiv_skip)
            except msil_equiv.UsageError as e:
                raise UsageError("--equiv-skip: %s" % e)
            # The oracle this build feeds (plan 5.12.6).
            args.oracle = "O3" if args.pipeline == "obj" else "O4"
            args.skip_reason = allowed.skip_reason(
                args.oracle, msil_equiv.test_path(args.test), args.triple, args.level
            )
    else:
        if args.det:
            raise UsageError("--det compares MSIL builds; it has no native mode")
        if args.also_il:
            raise UsageError("--also-il is an MSIL-mode option")
        if args.equiv_out:
            raise UsageError("--equiv-out is an MSIL-mode option")
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
    args.llc_cmd = (
        program(args.llc, "--llc")
        if args.mode == "msil" and args.pipeline == "obj" and args.llc
        else None
    )
    if args.llc_cmd and args.level in ("Os", "Oz"):
        if any(s.endswith(".ll") for s in args.srcs):
            raise UsageError("--llc takes -O0 to -O3, not --level %s" % args.level)
    args.ilasm_cmd = split_words(args.ilasm) if args.ilasm else None
    if args.ilasm is not None and not args.ilasm_cmd:
        raise UsageError("--ilasm is empty")
    args.cflags_words = split_words(args.cflags)
    args.ldflags_words = split_words(args.ldflags)
    args.native_ldflags_words = split_words(args.native_ldflags)
    args.lib_words = [w for lib in args.lib for w in split_words(lib)]
    args.native_lib_words = [w for lib in args.native_lib for w in split_words(lib)]
    args.archive_list = parse_archives(args.archives)
    if args.archive_list and not args.ar_cmd:
        raise UsageError("--archives needs --ar")
    args.shlib_list = parse_shlibs(args.shlibs)
    if args.shlib_list and args.mode == "msil" and args.link_output == "il":
        raise UsageError(
            "--shlibs links the program against .NET libraries, which "
            "--lto-emit-asm cannot import from; use --link-output pe"
        )
    if (
        args.mode == "msil"
        and args.link_output == "il"
        and not args.det
        and not args.ilasm_cmd
    ):
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
    sources += [s for _, _, members in args.shlib_list for s, _ in members]
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
    args.shlib_list = [
        (name, interface, [(os.path.abspath(s), stock) for s, stock in members])
        for name, interface, members in args.shlib_list
    ]
    if args.out:
        args.out = os.path.abspath(args.out)
    for opt in ("also_il", "equiv_out"):
        if getattr(args, opt):
            setattr(args, opt, os.path.abspath(getattr(args, opt)))
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
    if args.skip_reason is not None:
        # The test's equivalence comparison is skipped (equiv-allowed.txt):
        # no equivalence link, and in pe mode no --lto-emit-asm text either.
        b.say("%s skipped: %s" % (args.oracle, args.skip_reason))
        args.equiv_out = None
        if args.link_output == "pe":
            args.also_il = None
    try:
        if args.mode == "native":
            os.makedirs(args.workdir)
            build_native(b, args.workdir)
        elif not args.det:
            os.makedirs(args.workdir)
            built = build_msil(b, args.workdir, False)
            if args.link_output == "pe":
                deliver(built["image"], args.out)
            deliver(built["il"], args.also_il)
            deliver(built["equiv"], args.equiv_out)
        else:
            dirs = [os.path.join(args.workdir, x) for x in ("a", "b")]
            built = []
            for d in dirs:
                os.makedirs(d)
                built.append(build_msil(b, d, True))
            # ilasm stamps its image; msil-ld's must be deterministic.
            skip = (
                {"prog.exe"}
                if args.link_output == "il" and built[0]["image"]
                else set()
            )
            if compare_builds(b, dirs[0], dirs[1], skip):
                sys.stderr.write(
                    "msil-prog: error: --det: builds a and b differ (see above)\n"
                )
                return 1
            deliver(built[0]["image"], args.out)
            deliver(built[0]["il"], args.also_il)
            deliver(built[0]["equiv"], args.equiv_out)
    except StepFailed as e:
        sys.stderr.write("msil-prog: error: %s\n" % e)
        return 1
    except OSError as e:
        sys.stderr.write("msil-prog: error: %s\n" % e)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())

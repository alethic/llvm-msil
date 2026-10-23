#!/usr/bin/env python3
# ===-- prog-cc.py - Stand-in tools for the msil-prog.py self-tests -------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Stand-ins for clang (with msil-ld behind it), llvm-ar and ld.lld in the
self-tests of %{msil_prog} and msil/utils/msil-prog.py (harness/prog).

    prog-cc.py --as cc [--ilasm ILASM] ARGS...   compile (-c) or link
    prog-cc.py --as ar rcs ARCHIVE MEMBER...
    prog-cc.py --as ld-lld ARGS...                  the E7 oracle's ld.lld
                                                    (-rpath $ORIGIN from M15)

harness/prog/lit.local.cfg puts them into %msil_prog_build, so every other
step of the composites (ilasm, msil-verify, jitall, msil-run and the
transcript comparison) is the real one. A "source" is any text file; the
stand-ins read these markers from it:

    prog-value: N          the source adds N to the program's exit status
    prog-plant: object     its object holds the build directory (for --det)
    prog-plant: il         the linked text holds the build directory (--det)
    prog-error: compile M  compiling it fails with the message M
    prog-error: link M     linking it fails with 'msil-ld: error: M'

A file named start.c is the E7 oracle's msil/utils/linux-oracle/start.c: it
must be compiled with exactly the flags of plan 5.12.8 and adds nothing.

An "object" is JSON (kind msil, stock or native, target, level, source,
value); an archive is JSON with its members. The program's exit status is the
sum of the values of every object and every archive member that the link sees
(no lazy extraction: these are plumbing tests, not linker tests), modulo 256.
An MSIL link (an MSIL --target, -Wl,--lto-emit-asm, -o X.il; no
-nodefaultlibs from M15, and --sysroot= only for a Linux triple) writes ILAsm
text of a program 'X' that returns that status, plus X.il.inputs, one line
per input in link order: 'object <path> <source> <value>', 'member <archive>
<member> <value>', 'arg <argument>' and 'ld-path <path>' (-L and --sysroot=
are not recorded). A native link (clang
with -fuse-ld=lld, or ld-lld) writes a program with the same status: for a
Windows target an EXE that ILASM assembles from the same text (it runs
directly on Windows, as a native program would), else a shell script
'exit <status>' (it runs on Linux and under WSL).

Every invocation checks the arguments that msil-prog.py must pass (plan 5.12.4)
and fails with 'prog-cc: error: <why>' (exit status 1) when one is wrong.
"""

import json
import os
import re
import subprocess
import sys

LEVELS = ("0", "1", "2", "3", "s", "z")
MARKER_RE = re.compile(r"prog-(value|plant|error):\s*(\S+)(?:[ \t]+(.*?))?\s*$", re.M)


class Fail(Exception):
    pass


def read_json(path, what):
    try:
        with open(path, encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, ValueError) as e:
        raise Fail("%s %s: %s" % (what, path, e))
    return data


def write_text(path, text):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def is_msvc(triple):
    parts = triple.split("-")
    return "windows" in parts and (len(parts) < 4 or parts[3].startswith("msvc"))


def parse(argv):
    """(options, inputs): -o, --target=, -O<L>, -c, -L, -l, the rest."""
    o = {"out": None, "target": None, "levels": [], "flags": [], "inputs": []}
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "-o":
            if i + 1 >= len(argv):
                raise Fail("-o needs a value")
            o["out"] = argv[i + 1]
            i += 2
            continue
        if a.startswith("--target="):
            o["target"] = a[len("--target=") :]
        elif re.match(r"^-O.$", a):
            o["levels"].append(a[2:])
        elif a.startswith("-"):
            o["flags"].append(a)
        else:
            o["inputs"].append(a)
        i += 1
    return o


def compile_source(o, src):
    flags, target = o["flags"], o["target"]
    if len(o["levels"]) != 1 or o["levels"][0] not in LEVELS:
        raise Fail("expected exactly one -O<level>, got %r" % o["levels"])
    if not target:
        raise Fail("no --target")
    if os.path.basename(src) == "start.c":
        # The E7 oracle's msil/utils/linux-oracle/start.c, with exactly the
        # flags of plan 5.12.8.
        if (
            target != "x86_64-unknown-linux-gnu"
            or o["levels"] != ["2"]
            or sorted(flags) != ["-c", "-ffreestanding", "-fno-pic"]
        ):
            raise Fail("start.c is not compiled as the E7 oracle compiles it")
        obj = {"stand-in": "prog-cc", "kind": "native", "target": target}
        obj.update({"level": "2", "source": "start.c", "value": 0, "plant": None})
        write_text(o["out"], json.dumps(obj, sort_keys=True) + "\n")
        return 0
    for need in ("-std=gnu17", "-ffreestanding", "-Werror=return-type"):
        if need not in flags:
            raise Fail("compile without %s (%%{msil_cflags})" % need)
    includes = [f[2:] for f in flags if f.startswith("-I")]
    if not any(os.path.isfile(os.path.join(d, "msil_test.h")) for d in includes):
        raise Fail("no -I directory with msil_test.h in %r" % includes)
    if target.endswith("-msil"):
        kind = "msil"
        if "-flto" not in flags:
            raise Fail("an MSIL compile without -flto")
        if "-fno-ms-volatile" in flags:
            raise Fail("-fno-ms-volatile in an MSIL compile")
    elif "-flto" in flags:
        kind = "stock"
        if ("-fno-ms-volatile" in flags) != is_msvc(target):
            raise Fail(
                "a stock-clang compile for %s %s -fno-ms-volatile"
                % (target, "without" if is_msvc(target) else "with")
            )
    else:
        kind = "native"
        if "-fno-ms-volatile" in flags:
            raise Fail("-fno-ms-volatile in a native compile")
    with open(src, encoding="utf-8", errors="replace") as f:
        text = f.read()
    obj = {"stand-in": "prog-cc", "kind": kind, "target": target}
    obj["level"] = o["levels"][0]
    obj["source"] = os.path.basename(src)
    obj["value"], obj["plant"], obj["link-error"] = 0, None, None
    for m in MARKER_RE.finditer(text):
        what, arg, rest = m.group(1), m.group(2), m.group(3) or ""
        if what == "value":
            obj["value"] += int(arg)
        elif what == "plant" and arg == "object":
            obj["plant"] = os.getcwd()
        elif what == "plant" and arg == "il":
            obj["plant-il"] = True
        elif what == "error" and arg == "compile":
            sys.stderr.write("%s:1:1: error: %s\n" % (src, rest))
            return 1
        elif what == "error" and arg == "link":
            obj["link-error"] = rest
    if "-fdebug-compilation-dir=." in flags:
        obj["debug-dir"] = "."
    write_text(o["out"], json.dumps(obj, sort_keys=True) + "\n")
    return 0


def resolve(o, names, extra_ok):
    """The link inputs in order: (kind, path, object or archive)."""
    dirs = [f[2:] for f in o["flags"] if f.startswith("-L")]
    seq = []
    for a in o["raw"]:
        if a.startswith("-l"):
            name = a[2:]
            if os.path.isfile(name) or name in extra_ok:
                seq.append(("arg", a, None))
                continue
            if "-fuse-ld=lld" in o["flags"] and name.endswith(".lib"):
                # The MSVC driver passes -l<path>.lib through (the
                # compiler-rt builtins of %{msil_native_ldflags}).
                seq.append(("arg", a, None))
                continue
            hits = [
                os.path.join(d, n).replace("\\", "/")
                for d in dirs
                for n in names(name)
                if os.path.isfile(os.path.join(d, n))
            ]
            if not hits:
                raise Fail("unable to find library -l%s in %r" % (name, dirs))
            seq.append(("archive", hits[0], read_json(hits[0], "archive")))
        elif a.startswith("-L") or a.startswith("--target=") or re.match(r"^-O.$", a):
            continue
        elif a.startswith("-"):
            seq.append(("arg", a, None))
        else:
            seq.append(("object", a, read_json(a, "object")))
    return seq


def total(seq, kinds, target):
    value, lines, plant, errors = 0, [], None, []
    for kind, path, data in seq:
        if kind == "arg":
            lines.append("arg %s" % path)
            continue
        objs = (
            [(None, data)]
            if kind == "object"
            else [(m["name"], m["object"]) for m in data["members"]]
        )
        for member, obj in objs:
            if obj.get("kind") not in kinds:
                raise Fail(
                    "%s: a %s object in a link of %s" % (path, obj.get("kind"), target)
                )
            value += obj["value"]
            if obj.get("plant-il"):
                plant = os.getcwd()
            if obj.get("link-error"):
                errors.append(obj["link-error"])
            if member is None:
                lines.append("object %s %s %d" % (path, obj["source"], obj["value"]))
            else:
                lines.append("member %s %s %d" % (path, member, obj["value"]))
    return value % 256, lines, plant, errors


def il_text(name, value, plant):
    text = "// The stand-in program of harness/prog/Inputs/prog-cc.py.\n"
    if plant:
        text += "// planted: %s\n" % plant
    return text + (
        ".assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) "
        ".ver 4:0:0:0 }\n"
        ".assembly '%s' { .ver 0:0:0:0 }\n\n"
        ".method public static int32 'main'() cil managed\n{\n"
        "  .maxstack 1\n  ldc.i4 %d\n  ret\n} // end of method 'main'\n\n"
        ".method private hidebysig static int32 '<msil>Main'(string[] args) cil managed\n"
        "{\n  .entrypoint\n  .maxstack 1\n  call int32 'main'()\n  ret\n"
        "} // end of method '<msil>Main'\n" % (name, value)
    )


def native_program(out, target, value, ilasm):
    """A program with the exit status value: an EXE through ilasm for a
    Windows target, else a shell script."""
    if "windows" in target.split("-"):
        il = out + ".il"
        write_text(il, il_text(os.path.splitext(os.path.basename(out))[0], value, None))
        if not ilasm:
            raise Fail("a Windows native link needs --ilasm")
        res = subprocess.run(
            [ilasm, "-nologo", "-quiet", "-exe", "-pe64", "-x64", "-output=" + out, il],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=120,
        )
        if res.returncode != 0:
            sys.stderr.write(res.stdout.decode("utf-8", "replace"))
            raise Fail("ilasm -exe failed with status %d" % res.returncode)
        return
    write_text(out, "#!/bin/sh\nexit %d\n" % value)
    os.chmod(out, 0o755)


def clang(argv, ilasm):
    o = parse(argv)
    o["raw"] = argv
    if not o["out"]:
        raise Fail("no -o")
    if "-c" in o["flags"]:
        if len(o["inputs"]) != 1 or not os.path.isfile(o["inputs"][0]):
            raise Fail("expected one existing source, got %r" % o["inputs"])
        return compile_source(o, o["inputs"][0])
    target = o["target"]
    if not target:
        raise Fail("a link without --target")
    raw = [
        a
        for i, a in enumerate(argv)
        if a != "-o"
        and (i == 0 or argv[i - 1] != "-o")
        and not a.startswith("--ld-path=")
    ]
    o["raw"] = raw
    if target.endswith("-msil"):
        if len(o["levels"]) != 1:
            raise Fail("an MSIL link without exactly one -O<level> (--lto-O<level>)")
        if "-Wl,--lto-emit-asm" not in o["flags"] or not o["out"].endswith(".il"):
            raise Fail("an MSIL link without -Wl,--lto-emit-asm -o <name>.il")
        if "-nodefaultlibs" in o["flags"]:
            raise Fail(
                "an MSIL link with -nodefaultlibs (%{msil_ldflags} of M14; from M15 "
                "the link reads the default libraries)"
            )
        sysroots = [f for f in o["flags"] if f.startswith("--sysroot=")]
        if sysroots and not target.split("-")[2:3] == ["linux"]:
            raise Fail("--sysroot in the MSIL link of %s, not a Linux triple" % target)
        if "-fuse-ld=lld" in o["flags"]:
            raise Fail("-fuse-ld=lld in an MSIL link")
        # -L and --sysroot= go to the search, not to the record.
        o["raw"] = [a for a in o["raw"] if not a.startswith("--sysroot=")]
        seq = resolve(o, lambda n: ["lib%s.a" % n], ())
        seq = [s for s in seq if s[1] != "-Wl,--lto-emit-asm"]
        value, lines, plant, errors = total(seq, ("msil", "stock"), target)
        if errors:
            for e in errors:
                sys.stderr.write("msil-ld: error: %s\n" % e)
            return 1
        ld = [a[len("--ld-path=") :] for a in argv if a.startswith("--ld-path=")]
        lines += ["ld-path %s" % p for p in ld]
        name = os.path.splitext(os.path.basename(o["out"]))[0]
        write_text(o["out"], il_text(name, value, plant))
        write_text(o["out"] + ".inputs", "".join(line + "\n" for line in lines))
        return 0
    if "-fuse-ld=lld" not in o["flags"]:
        raise Fail("a native link without -fuse-ld=lld")
    if "-nodefaultlibs" in o["flags"] or "-Wl,--lto-emit-asm" in o["flags"]:
        raise Fail("an MSIL link flag in a native link")
    names = (
        (lambda n: ["%s.lib" % n]) if is_msvc(target) else (lambda n: ["lib%s.a" % n])
    )
    seq = resolve(o, names, ("m",))
    seq = [s for s in seq if s[1] != "-fuse-ld=lld"]
    value, lines, _, _ = total(seq, ("native",), target)
    native_program(o["out"], target, value, ilasm)
    write_text(o["out"] + ".inputs", "".join(line + "\n" for line in lines))
    return 0


def ld_lld(argv):
    e7 = ["-dynamic-linker", "/lib64/ld-linux-x86-64.so.2", "--as-needed"]
    e7 += ["-rpath", "$ORIGIN"]
    if argv[:5] != e7:
        raise Fail("not the E7 command line: %r" % argv[:5])
    rest = argv[5:]
    if "-o" not in rest:
        raise Fail("no -o")
    i = rest.index("-o")
    out, rest = rest[i + 1], rest[:i] + rest[i + 2 :]
    if not rest or not os.path.isfile(rest[0]) or not rest[0].endswith(".o"):
        raise Fail("the first input is not start.o: %r" % rest[:1])
    libs = rest[-3:]
    want = ["libm.so.6", "libc.so.6", "libgcc_s.so.1"]
    if [os.path.basename(x) for x in libs] != want or not all(
        map(os.path.isfile, libs)
    ):
        raise Fail("the last inputs are not the sysroot's %s: %r" % (want, libs))
    o = {"flags": [a for a in rest[1:-3] if a.startswith("-L")]}
    o["raw"] = rest[1:-3]
    seq = resolve(o, lambda n: ["lib%s.a" % n], ())
    value, lines, _, _ = total(seq, ("native",), "x86_64-unknown-linux-gnu")
    native_program(out, "x86_64-unknown-linux-gnu", value, None)
    write_text(out + ".inputs", "".join(line + "\n" for line in lines))
    return 0


def ar(argv):
    if len(argv) < 2 or argv[0] != "rcs":
        raise Fail("expected 'rcs ARCHIVE MEMBER...', got %r" % argv)
    members = [
        {"name": os.path.basename(m), "object": read_json(m, "member")}
        for m in argv[2:]
    ]
    write_text(argv[1], json.dumps({"members": members}, sort_keys=True) + "\n")
    return 0


def main(argv):
    if len(argv) < 2 or argv[0] != "--as":
        return fail("usage: prog-cc.py --as cc|ar|ld-lld ARGS...")
    mode, rest = argv[1], argv[2:]
    try:
        if mode == "cc":
            ilasm = None
            if rest[:1] == ["--ilasm"]:
                ilasm, rest = rest[1], rest[2:]
            return clang(rest, ilasm)
        if mode == "ar":
            return ar(rest)
        if mode == "ld-lld":
            return ld_lld(rest)
        return fail("unknown --as %r" % mode)
    except Fail as e:
        return fail(str(e))


def fail(msg):
    sys.stderr.write("prog-cc: error: %s\n" % msg)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

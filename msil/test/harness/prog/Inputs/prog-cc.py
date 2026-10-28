#!/usr/bin/env python3
# ===-- prog-cc.py - Stand-in tools for the msil-prog.py self-tests -------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Stand-ins for clang (with msil-ld behind it), llc, llvm-ar and ld.lld in
the self-tests of %{msil_prog}, msil/utils/msil-prog.py and the composites
under msil_pipeline=obj (harness/prog, harness/pipeline-obj).

    prog-cc.py --as cc [--ilasm ILASM] ARGS...   compile (-c), -S, or link
    prog-cc.py --as llc ARGS...                     llc (-filetype=obj or text)
    prog-cc.py --as ar rcs ARCHIVE MEMBER...
    prog-cc.py --as ld-lld ARGS...                  the E7 oracle's ld.lld
                                                    (-rpath $ORIGIN from M15;
                                                    -shared from M22)

harness/prog/lit.local.cfg puts them into %msil_prog_build (and
harness/pipeline-obj/lit.local.cfg also into %clang_msil, %llc_msil,
%clang_native and %{msil_llc}), so every other step of the composites
(ilasm, msil-verify, jitall, msil-run, msil-equiv and the transcript
comparison) is the real one. A "source" is any text file (C or IR); the
stand-ins read these markers from it:

    prog-value: N          the source adds N to the program's exit status
    prog-plant: object     its object holds the build directory (for --det)
    prog-plant: il         the linked output holds the build directory
                           (--det): the text, and under a PE link the image
    prog-error: compile M  compiling it fails with the message M
    prog-error: link M     linking it fails with 'msil-ld: error: M'

A file named start.c is the E7 oracle's msil/utils/linux-oracle/start.c: it
must be compiled with exactly the flags of plan 5.12.8 and adds nothing.

An "object" is JSON (kind msil, stock or native, target, level, source,
value; an MSIL object also says whether it is bitcode, -flto, or a CIL
object, and llc's whether llc wrote it); an archive is JSON with its
members. The program's exit status is the sum of the values of every object
and every archive member that the link sees (no lazy extraction: these are
plumbing tests, not linker tests), modulo 256.

An MSIL link (an MSIL --target; no -nodefaultlibs from M15, and --sysroot=
only for a Linux triple) writes a program 'X' that returns that status:
ILAsm text for '-Wl,--lto-emit-asm -o X.il', or for '-o X.exe' (msil-ld's
own image, M20) an EXE that the ilasm of $PROG_CC_ILASM (with the options
of $PROG_CC_ILASM_FLAGS) assembles from the same text, made deterministic
(COFF time stamp 0, MVID from the text) the way msil-ld's image is. Next to
it, X.il.inputs or X.exe.inputs has one line per input in link order:
'object <path> <source> <value>', 'member <archive> <member> <value>',
'arg <argument>' and 'ld-path <path>' (-L and --sysroot= are not recorded).
A native link (clang with -fuse-ld=lld, clang with one source and no -c, or
ld-lld) writes a program with the same status: for a Windows target an EXE
that ILASM assembles from the same text (it runs directly on Windows, as a
native program would), else a shell script 'exit <status>' (it runs on
Linux and under WSL).

The text forms (O3's reference side, harness/pipeline-obj): 'cc' with -S and
an MSIL --target, and 'llc' without -filetype=obj, write the text of the
program of that one source, named by -msil-assembly-name=<name> (after
-mllvm for clang) or else after the output's stem, so it equals what the
link of that source's object writes when the names agree.

Shared libraries (msil-prog.py --shlibs, M22). An MSIL link with -shared
writes a library assembly '<name>.dll' (-o must end in .dll) that the ilasm
of $PROG_CC_ILASM assembles as a DLL: a public class '<name>' whose static
'value'() returns the sum of the values of the library's objects (with
-Wl,--interface-only: an interface, whose 'value'() throws), with the
records <name>.dll.inputs and <name>.dll.json (what a later link reads).
A link that names an earlier library (a positional .dll) records it as
'library <path> <name> <value>[ interface]'; a program link may name only
libraries, never an interface, and its program calls each library's
'value'() at run time and adds it to the status (modulo 256), so the
library must be next to the program when it runs. Natively, for a Windows
target, clang -fuse-ld=lld -shared writes <name>.dll the same way (a .NET
Framework DLL) and an import library <name>.lib (MSVC; for MinGW the
-Wl,--out-implib= file), a JSON record that later links name; with
-Wl,/force:unresolved (MinGW: -Wl,-Xlink=-force:unresolved) it is an
interface. On MSVC every object of a link with libraries must be compiled
with -fms-runtime-lib=dll (one C runtime). An ELF library is a JSON record
'lib<name>.so': ld-lld -shared -Bsymbolic --as-needed -soname lib<name>.so
-rpath $ORIGIN (E7) or clang -fuse-ld=lld -shared with -Wl,-Bsymbolic,
-Wl,-soname,lib<name>.so and -Wl,-rpath,$ORIGIN (the driver recipe, whose
program link then needs -Wl,-rpath,$ORIGIN too); an ELF program adds its
libraries' values when it is linked, and every object of an ELF link with
libraries must be -fPIC.

Every invocation checks the arguments that msil-prog.py and the composites
must pass (plan 5.12.4) and fails with 'prog-cc: error: <why>' (exit status
1) when one is wrong. A compile must have %{msil_cflags} (-std=gnu17,
-ffreestanding, -Werror=return-type and an -I directory with msil_test.h);
with $PROG_CC_CTEST set (harness/prog/Inputs/nested-ctest), c-testsuite's
-std=gnu17 -w instead, and none of those other flags.
"""

import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

LEVELS = ("0", "1", "2", "3", "s", "z")
LLC_LEVELS = ("0", "1", "2", "3")
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


def markers(src):
    """(value, plant object, plant il, link error) of a source, or raises
    Fail after printing a compile error."""
    with open(src, encoding="utf-8", errors="replace") as f:
        text = f.read()
    value, plant, plant_il, link_error = 0, None, False, None
    for m in MARKER_RE.finditer(text):
        what, arg, rest = m.group(1), m.group(2), m.group(3) or ""
        if what == "value":
            value += int(arg)
        elif what == "plant" and arg == "object":
            plant = os.getcwd()
        elif what == "plant" and arg == "il":
            plant_il = True
        elif what == "error" and arg == "compile":
            sys.stderr.write("%s:1:1: error: %s\n" % (src, rest))
            raise Fail("compile error")
        elif what == "error" and arg == "link":
            link_error = rest
    return value, plant, plant_il, link_error


def check_cflags(flags, what):
    if os.environ.get("PROG_CC_CTEST"):
        # c-testsuite's flags (msil/test/Programs/c-testsuite redefines
        # %{msil_cflags} to -std=gnu17 -w), for the executing nested run of
        # harness/ctest-lists.test: none of the harness's own flags.
        if "-std=gnu17" not in flags or "-w" not in flags:
            raise Fail("%s without c-testsuite's -std=gnu17 -w" % what)
        for f in flags:
            if f in ("-ffreestanding", "-Werror=return-type") or f.startswith("-I"):
                raise Fail("%s with %s, which c-testsuite's flags lack" % (what, f))
        return
    for need in ("-std=gnu17", "-ffreestanding", "-Werror=return-type"):
        if need not in flags:
            raise Fail("%s without %s (%%{msil_cflags})" % (what, need))
    includes = [f[2:] for f in flags if f.startswith("-I")]
    if not any(os.path.isfile(os.path.join(d, "msil_test.h")) for d in includes):
        raise Fail("no -I directory with msil_test.h in %r" % includes)


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
    check_cflags(flags, "compile")
    obj = {"stand-in": "prog-cc", "target": target, "level": o["levels"][0]}
    if "-fPIC" in flags and "-fno-pic" in flags:
        raise Fail("a compile with both -fPIC and -fno-pic")
    if "-fms-runtime-lib=dll" in flags and not is_msvc(target):
        raise Fail("-fms-runtime-lib=dll in a compile for %s" % target)
    if target.endswith("-msil"):
        obj["kind"] = "msil"
        # -flto: bitcode (msil_pipeline=lto); without: a CIL object (obj, M20).
        obj["bitcode"] = "-flto" in flags
        if "-fno-ms-volatile" in flags:
            raise Fail("-fno-ms-volatile in an MSIL compile")
        for f in ("-fPIC", "-fno-pic", "-fms-runtime-lib=dll"):
            if f in flags:
                raise Fail("%s in an MSIL compile" % f)
    elif "-flto" in flags:
        obj["kind"] = "stock"
        if ("-fno-ms-volatile" in flags) != is_msvc(target):
            raise Fail(
                "a stock-clang compile for %s %s -fno-ms-volatile"
                % (target, "without" if is_msvc(target) else "with")
            )
    else:
        obj["kind"] = "native"
        if "-fno-ms-volatile" in flags:
            raise Fail("-fno-ms-volatile in a native compile")
    try:
        value, plant, plant_il, link_error = markers(src)
    except Fail:
        return 1
    obj["source"] = os.path.basename(src)
    obj["value"], obj["plant"], obj["link-error"] = value, plant, link_error
    if plant_il:
        obj["plant-il"] = True
    if "-fdebug-compilation-dir=." in flags:
        obj["debug-dir"] = "."
    if "-fPIC" in flags:
        obj["pic"] = True
    if "-fms-runtime-lib=dll" in flags:
        obj["crt"] = "dll"
    write_text(o["out"], json.dumps(obj, sort_keys=True) + "\n")
    return 0


def assembly_name(flags, out):
    """-msil-assembly-name=<name> (O3's reference), else the output's stem."""
    names = [f.split("=", 1)[1] for f in flags if f.startswith("-msil-assembly-name=")]
    if len(names) > 1:
        raise Fail("-msil-assembly-name given twice")
    return names[0] if names else os.path.splitext(os.path.basename(out))[0]


def text_source(o, src, what):
    """The -S text of one source: the program a link of its object writes."""
    try:
        value, _, _, _ = markers(src)
    except Fail:
        return 1
    name = assembly_name(o["flags"], o["out"])
    write_text(o["out"], il_text(name, value % 256, None))
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
        elif a.endswith(".dll"):
            # A library assembly of an MSIL link; its record lies beside it.
            seq.append(("library", a, read_json(a + ".json", "library record")))
        else:
            data = read_json(a, "object")
            kind = "library" if data.get("kind") == "shlib" else "object"
            seq.append((kind, a, data))
    return seq


class Linked:
    """What total() found in a link: the status (modulo 256), the record
    lines, a planted directory, the link errors of the sources, the
    libraries named (their records) and the objects seen."""

    def __init__(self, value, lines, plant, errors, libs, objects):
        self.value, self.lines, self.plant = value, lines, plant
        self.errors, self.libs, self.objects = errors, libs, objects


def total(seq, kinds, target, flavor=None, drop=()):
    """The inputs of a link; flavor: the kind of library it may name ('msil',
    'windows' or 'elf'; None: none); drop: arguments left out of the
    record."""
    value, lines, plant, errors, libs, seen = 0, [], None, [], [], []
    for kind, path, data in seq:
        if kind == "arg":
            if path not in drop:
                lines.append("arg %s" % path)
            continue
        if kind == "library":
            if data.get("stand-in") != "prog-cc" or data.get("kind") != "shlib":
                raise Fail("%s is not a library of this stand-in" % path)
            if flavor is None or data.get("flavor") != flavor:
                raise Fail(
                    "%s: a %s library in a link of %s"
                    % (path, data.get("flavor"), target)
                )
            if data.get("target") != target:
                raise Fail(
                    "%s: a library for %s in a link of %s"
                    % (path, data.get("target"), target)
                )
            lines.append(
                "library %s %s %d%s"
                % (
                    path,
                    data["name"],
                    data["value"],
                    " interface" if data["interface"] else "",
                )
            )
            libs.append(data)
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
            seen.append(obj)
            if obj.get("plant-il"):
                plant = os.getcwd()
            if obj.get("link-error"):
                errors.append(obj["link-error"])
            if member is None:
                lines.append("object %s %s %d" % (path, obj["source"], obj["value"]))
            else:
                lines.append("member %s %s %d" % (path, member, obj["value"]))
    return Linked(value % 256, lines, plant, errors, libs, seen)


def check_objects(linked, shared, target, elf):
    """The compile options a link with libraries needs: -fPIC on ELF (a
    -fno-pic program would copy the libraries' data) and one C runtime on
    MSVC (-fms-runtime-lib=dll)."""
    if not (shared or linked.libs):
        return
    for obj in linked.objects:
        if elf and not obj.get("pic"):
            raise Fail(
                "%s was not compiled with -fPIC, in an ELF link with shared "
                "libraries" % obj["source"]
            )
        if not elf and is_msvc(target) and obj.get("crt") != "dll":
            raise Fail(
                "%s was not compiled with -fms-runtime-lib=dll, in an MSVC link "
                "with DLLs" % obj["source"]
            )


def library_record(path, flavor, target, name, linked, interface):
    """The JSON record of a library that later links read."""
    rec = {"stand-in": "prog-cc", "kind": "shlib", "flavor": flavor}
    rec.update({"target": target, "name": name, "value": linked.value})
    rec["interface"] = interface
    write_text(path, json.dumps(rec, sort_keys=True) + "\n")


def write_record(path, lines):
    write_text(path, "".join(line + "\n" for line in lines))


MSCORLIB = (
    ".assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) "
    ".ver 4:0:0:0 }\n"
)


def il_text(name, value, plant, plant_code=False, libs=()):
    """The program 'name' that returns value; plant (a directory) goes into a
    comment, and with plant_code also into the code (a PE keeps it). libs:
    the names of the libraries whose 'value'() the program adds at run time
    (modulo 256)."""
    text = "// The stand-in program of harness/prog/Inputs/prog-cc.py.\n"
    if plant:
        text += "// planted: %s\n" % plant
    code = "  .maxstack %d\n" % (2 if libs else 1)
    if plant and plant_code:
        code += '  ldstr "%s"\n  pop\n' % plant.replace("\\", "/")
    code += "  ldc.i4 %d\n" % value
    for lib in libs:
        code += "  call int32 ['%s']'%s'::'value'()\n  add\n" % (lib, lib)
    if libs:
        code += "  ldc.i4 255\n  and\n"
    externs = "".join(".assembly extern '%s' { .ver 0:0:0:0 }\n" % lib for lib in libs)
    return text + (
        MSCORLIB + externs + ".assembly '%s' { .ver 0:0:0:0 }\n\n"
        ".method public static int32 'main'() cil managed\n{\n"
        "%s  ret\n} // end of method 'main'\n\n"
        ".method private hidebysig static int32 '<msil>Main'(string[] args) cil managed\n"
        "{\n  .entrypoint\n  .maxstack 1\n  call int32 'main'()\n  ret\n"
        "} // end of method '<msil>Main'\n" % (name, code)
    )


def lib_text(name, value, plant, interface):
    """The library assembly 'name': public class 'name' with a static
    'value'() that returns value (an interface's throws); plant as for
    il_text, kept in the code."""
    text = "// A stand-in library of harness/prog/Inputs/prog-cc.py.\n"
    if plant:
        text += "// planted: %s\n" % plant
    code = "    .maxstack 1\n"
    if plant:
        code += '    ldstr "%s"\n    pop\n' % plant.replace("\\", "/")
    if interface:
        code += "    ldnull\n    throw\n"
    else:
        code += "    ldc.i4 %d\n    ret\n" % value
    return text + (
        MSCORLIB + ".assembly '%s' { .ver 0:0:0:0 }\n\n"
        ".class public abstract auto ansi sealed '%s' extends [mscorlib]System.Object\n"
        "{\n  .method public static int32 'value'() cil managed\n  {\n"
        "%s  }\n}\n" % (name, name, code)
    )


def make_deterministic(path, text):
    """Give an ilasm image the bytes msil-ld's would have for the same
    content: every copy of the COFF time stamp zeroed and the MVID taken from
    the text (ilasm stamps both; plan 5.9.12 derives them from the
    content)."""
    with open(path, "rb") as f:
        data = bytearray(f.read())
    u16 = lambda o: struct.unpack_from("<H", data, o)[0]
    u32 = lambda o: struct.unpack_from("<I", data, o)[0]
    pe = u32(0x3C)
    stamp = bytes(data[pe + 8 : pe + 12])
    if stamp != b"\0\0\0\0":
        data = bytearray(bytes(data).replace(stamp, b"\0\0\0\0"))
    nsections, opt_size, opt = u16(pe + 6), u16(pe + 20), pe + 24
    dirs = opt + {0x10B: 96, 0x20B: 112}[u16(opt)]
    sections = []
    for i in range(nsections):
        s = opt + opt_size + 40 * i
        vsize, va, rawsize, raw = struct.unpack_from("<IIII", data, s + 8)
        sections.append((va, max(vsize, rawsize), raw))

    def offset(rva):
        for va, size, raw in sections:
            if va <= rva < va + size:
                return raw + rva - va
        raise Fail("%s: RVA 0x%X lies in no section" % (path, rva))

    cli = offset(u32(dirs + 14 * 8))
    root = offset(u32(cli + 8))
    at = root + 16 + u32(root + 12) + 2
    count = u16(at)
    at += 2
    for _ in range(count):
        off = u32(at)
        end = data.index(b"\0", at + 8)
        name = bytes(data[at + 8 : end])
        if name == b"#GUID":
            mvid = hashlib.sha256(text.encode("utf-8")).digest()[:16]
            data[root + off : root + off + 16] = mvid
        at = at + 8 + ((end - (at + 8)) // 4 + 1) * 4
    with open(path, "wb") as f:
        f.write(bytes(data))


def msil_image(out, text, kind="-exe"):
    """msil-ld's image of the program text (kind -exe) or library text
    (-dll): ilasm of $PROG_CC_ILASM, made deterministic."""
    ilasm = os.environ.get("PROG_CC_ILASM")
    if not ilasm or not os.path.isfile(ilasm):
        raise Fail("an MSIL link to an image needs $PROG_CC_ILASM (got %r)" % ilasm)
    flags = os.environ.get("PROG_CC_ILASM_FLAGS", "").split()
    tmp = tempfile.mkdtemp(prefix="prog-cc-")
    try:
        il = os.path.join(tmp, os.path.splitext(os.path.basename(out))[0] + ".il")
        write_text(il, text)
        image = os.path.join(tmp, os.path.basename(out))
        res = subprocess.run(
            [ilasm, "-nologo", "-quiet", kind, *flags, "-output=" + image, il],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=120,
        )
        if res.returncode != 0 or not os.path.isfile(image):
            sys.stderr.write(res.stdout.decode("utf-8", "replace"))
            raise Fail("ilasm %s failed with status %d" % (kind, res.returncode))
        make_deterministic(image, text)
        shutil.copyfile(image, out)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def native_program(out, target, value, ilasm, libs=(), library=None):
    """A program with the exit status value: an EXE through ilasm for a
    Windows target (that adds the 'value'() of each of libs at run time),
    else a shell script. library: (value, interface) for a Windows DLL
    instead."""
    if "windows" in target.split("-"):
        il = out + ".il"
        name = os.path.splitext(os.path.basename(out))[0]
        if library:
            write_text(il, lib_text(name, library[0], None, library[1]))
        else:
            write_text(il, il_text(name, value, None, libs=libs))
        if not ilasm:
            raise Fail("a Windows native link needs --ilasm")
        kind = "-dll" if library else "-exe"
        res = subprocess.run(
            [ilasm, "-nologo", "-quiet", kind, "-pe64", "-x64", "-output=" + out, il],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=120,
        )
        if res.returncode != 0:
            sys.stderr.write(res.stdout.decode("utf-8", "replace"))
            raise Fail("ilasm %s failed with status %d" % (kind, res.returncode))
        return
    write_text(out, "#!/bin/sh\nexit %d\n" % value)
    os.chmod(out, 0o755)


def clang(argv, ilasm):
    o = parse(argv)
    o["raw"] = argv
    if not o["out"]:
        raise Fail("no -o")
    target = o["target"]
    if "-S" in o["flags"]:
        # The -S text of O3's reference side (harness/pipeline-obj).
        if not target or not target.endswith("-msil"):
            raise Fail("-S for a target that is not MSIL: %r" % target)
        if len(o["inputs"]) != 1 or not os.path.isfile(o["inputs"][0]):
            raise Fail("expected one existing source, got %r" % o["inputs"])
        if len(o["levels"]) != 1 or o["levels"][0] not in LEVELS:
            raise Fail("expected exactly one -O<level>, got %r" % o["levels"])
        if "-c" in o["flags"]:
            raise Fail("-S with -c")
        if "-mllvm" in o["flags"] and not any(
            f.startswith("-msil-assembly-name=") for f in o["flags"]
        ):
            raise Fail("-mllvm without -msil-assembly-name=")
        if o["inputs"][0].endswith(".c"):
            check_cflags(o["flags"], "-S")
        return text_source(o, o["inputs"][0], "-S")
    if "-c" in o["flags"]:
        if len(o["inputs"]) != 1 or not os.path.isfile(o["inputs"][0]):
            raise Fail("expected one existing source, got %r" % o["inputs"])
        return compile_source(o, o["inputs"][0])
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
    sources = [a for a in o["inputs"] if os.path.splitext(a)[1] in (".c", ".ll")]
    if sources and not target.endswith("-msil"):
        # Phase 1's native leg of an IR test: compile and link one source.
        if len(sources) != 1 or len(o["inputs"]) != 1:
            raise Fail(
                "a native one-step build of more than one source: %r" % o["inputs"]
            )
        if sources[0].endswith(".ll") and "-Wno-override-module" not in o["flags"]:
            raise Fail("native IR build without -Wno-override-module")
        if len(o["levels"]) != 1 or o["levels"][0] not in LEVELS:
            raise Fail("expected exactly one -O<level>, got %r" % o["levels"])
        try:
            value, _, _, _ = markers(sources[0])
        except Fail:
            return 1
        native_program(o["out"], target, value % 256, ilasm)
        return 0
    if target.endswith("-msil"):
        if len(o["levels"]) != 1:
            raise Fail("an MSIL link without exactly one -O<level> (--lto-O<level>)")
        text_link = "-Wl,--lto-emit-asm" in o["flags"]
        shared = "-shared" in o["flags"]
        interface = "-Wl,--interface-only" in o["flags"]
        if interface and not shared:
            raise Fail("-Wl,--interface-only without -shared")
        if shared:
            # msil-ld writes no --lto-emit-asm text for a library (M22).
            if text_link or not o["out"].endswith(".dll"):
                raise Fail(
                    "an MSIL -shared link writes -o <name>.dll and no "
                    "-Wl,--lto-emit-asm text, not %r" % o["out"]
                )
        elif text_link != o["out"].endswith(".il") or not o["out"].endswith(
            (".il", ".exe")
        ):
            raise Fail(
                "an MSIL link writes -Wl,--lto-emit-asm -o <name>.il or -o <name>.exe, "
                "not %r" % o["out"]
            )
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
        drop = ("-Wl,--lto-emit-asm", "-shared", "-Wl,--interface-only")
        linked = total(seq, ("msil", "stock"), target, "msil", drop)
        if text_link and linked.libs:
            raise Fail("--lto-emit-asm cannot import from a .NET library")
        if not shared and any(lib["interface"] for lib in linked.libs):
            raise Fail("a program linked against an interface library")
        if linked.errors:
            for e in linked.errors:
                sys.stderr.write("msil-ld: error: %s\n" % e)
            return 1
        ld = [a[len("--ld-path=") :] for a in argv if a.startswith("--ld-path=")]
        lines = linked.lines + ["ld-path %s" % p for p in ld]
        name = os.path.splitext(os.path.basename(o["out"]))[0]
        if shared:
            text = lib_text(name, linked.value, linked.plant, interface)
            msil_image(o["out"], text, "-dll")
            library_record(o["out"] + ".json", "msil", target, name, linked, interface)
        elif text_link:
            write_text(o["out"], il_text(name, linked.value, linked.plant))
        else:
            libs = [lib["name"] for lib in linked.libs]
            text = il_text(name, linked.value, linked.plant, True, libs)
            msil_image(o["out"], text)
        write_record(o["out"] + ".inputs", lines)
        return 0
    if "-fuse-ld=lld" not in o["flags"]:
        raise Fail("a native link without -fuse-ld=lld")
    if "-nodefaultlibs" in o["flags"] or "-Wl,--lto-emit-asm" in o["flags"]:
        raise Fail("an MSIL link flag in a native link")
    names = (
        (lambda n: ["%s.lib" % n]) if is_msvc(target) else (lambda n: ["lib%s.a" % n])
    )
    seq = resolve(o, names, ("m",))
    return native_link(o, target, seq, ilasm)


def native_link(o, target, seq, ilasm):
    """The native link of clang -fuse-ld=lld: a program, or with -shared a
    library (see the module documentation)."""
    flags, out = o["flags"], o["out"]
    shared = "-shared" in flags
    windows = "windows" in target.split("-")
    msvc = is_msvc(target)
    name = os.path.splitext(os.path.basename(out))[0]
    drop = {"-fuse-ld=lld", "-shared"}
    interface = False
    implib = None
    if shared and windows:
        if not out.endswith(".dll"):
            raise Fail("a Windows -shared link writes -o <name>.dll, not %r" % out)
        force = "-Wl,/force:unresolved" if msvc else "-Wl,-Xlink=-force:unresolved"
        interface = force in flags
        drop.add(force)
        if msvc:
            implib = out[: -len(".dll")] + ".lib"
        else:
            implibs = [f for f in flags if f.startswith("-Wl,--out-implib=")]
            if len(implibs) != 1:
                raise Fail("a MinGW -shared link without one -Wl,--out-implib=")
            implib = implibs[0][len("-Wl,--out-implib=") :]
            drop.add(implibs[0])
    elif shared:
        base = os.path.basename(out)
        if not (base.startswith("lib") and base.endswith(".so")):
            raise Fail("an ELF -shared link writes -o lib<name>.so, not %r" % out)
        name = base[len("lib") : -len(".so")]
        want = ["-Wl,-Bsymbolic", "-Wl,-soname,%s" % base, "-Wl,-rpath,$ORIGIN"]
        if not all(w in flags for w in want):
            raise Fail("an ELF -shared link without %s" % " ".join(want))
        drop.update(want)
    elif not windows:
        drop.add("-Wl,-rpath,$ORIGIN")
    linked = total(seq, ("native",), target, "windows" if windows else "elf", drop)
    check_objects(linked, shared, target, not windows)
    if not shared and any(lib["interface"] for lib in linked.libs):
        raise Fail("a program linked against an interface library")
    if not shared and linked.libs and not windows:
        if "-Wl,-rpath,$ORIGIN" not in flags:
            raise Fail(
                "an ELF program linked against shared libraries without "
                "-Wl,-rpath,$ORIGIN"
            )
    if shared and windows:
        native_program(out, target, 0, ilasm, library=(linked.value, interface))
        library_record(implib, "windows", target, name, linked, interface)
    elif shared:
        library_record(out, "elf", target, name, linked, False)
    elif windows:
        libs = [lib["name"] for lib in linked.libs]
        native_program(out, target, linked.value, ilasm, libs)
    else:
        value = (linked.value + sum(lib["value"] for lib in linked.libs)) % 256
        native_program(out, target, value, ilasm)
    write_record(out + ".inputs", linked.lines)
    return 0


def llc(argv):
    """llc -mtriple=<triple>-msil -O<L> [-filetype=obj] SRC
    [-msil-assembly-name=<name>] -o OUT."""
    out, triples, levels, flags, sources = None, [], [], [], []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "-o":
            if i + 1 >= len(argv):
                raise Fail("-o needs a value")
            out = argv[i + 1]
            i += 2
            continue
        if a.startswith("-mtriple="):
            triples.append(a[len("-mtriple=") :])
        elif re.match(r"^-O.$", a):
            levels.append(a[2:])
        elif a.startswith("-"):
            flags.append(a)
        else:
            sources.append(a)
        i += 1
    if not out:
        raise Fail("no -o")
    if len(triples) != 1 or not triples[0].endswith("-msil"):
        raise Fail("expected one MSIL -mtriple=, got %r" % triples)
    if len(levels) != 1 or levels[0] not in LLC_LEVELS:
        raise Fail("expected exactly one of -O0..-O3, got %r" % levels)
    if len(sources) != 1 or not sources[0].endswith(".ll"):
        raise Fail("expected one .ll source, got %r" % sources)
    if not os.path.isfile(sources[0]):
        raise Fail("%s: no such file" % sources[0])
    allowed = {"-filetype=obj"}
    bad = [
        f
        for f in flags
        if f not in allowed and not f.startswith("-msil-assembly-name=")
    ]
    if bad:
        raise Fail("unexpected llc options %r" % bad)
    o = {"out": out, "flags": flags}
    if "-filetype=obj" in flags:
        if any(f.startswith("-msil-assembly-name=") for f in flags):
            raise Fail("-msil-assembly-name= with -filetype=obj")
        try:
            value, plant, plant_il, link_error = markers(sources[0])
        except Fail:
            return 1
        obj = {"stand-in": "prog-cc", "kind": "msil", "bitcode": False, "llc": True}
        obj.update({"target": triples[0], "level": levels[0]})
        obj.update({"source": os.path.basename(sources[0]), "value": value})
        obj.update({"plant": plant, "link-error": link_error})
        if plant_il:
            obj["plant-il"] = True
        write_text(out, json.dumps(obj, sort_keys=True) + "\n")
        return 0
    return text_source(o, sources[0], "llc")


E7_TARGET = "x86_64-unknown-linux-gnu"
E7_SYSROOT_LIBS = ["libm.so.6", "libc.so.6", "libgcc_s.so.1"]


def split_out(rest):
    """(the -o value, the other arguments)."""
    if "-o" not in rest or rest.index("-o") + 1 >= len(rest):
        raise Fail("no -o")
    i = rest.index("-o")
    return rest[i + 1], rest[:i] + rest[i + 2 :]


def check_sysroot_libs(libs):
    if [os.path.basename(x) for x in libs] != E7_SYSROOT_LIBS or not all(
        map(os.path.isfile, libs)
    ):
        raise Fail(
            "the last inputs are not the sysroot's %s: %r" % (E7_SYSROOT_LIBS, libs)
        )


def ld_lld(argv):
    if argv[:1] == ["-shared"]:
        return ld_lld_shared(argv)
    e7 = ["-dynamic-linker", "/lib64/ld-linux-x86-64.so.2", "--as-needed"]
    e7 += ["-rpath", "$ORIGIN"]
    if argv[:5] != e7:
        raise Fail("not the E7 command line: %r" % argv[:5])
    out, rest = split_out(argv[5:])
    if not rest or not os.path.isfile(rest[0]) or not rest[0].endswith(".o"):
        raise Fail("the first input is not start.o: %r" % rest[:1])
    check_sysroot_libs(rest[-3:])
    o = {"flags": [a for a in rest[1:-3] if a.startswith("-L")]}
    o["raw"] = rest[1:-3]
    seq = resolve(o, lambda n: ["lib%s.a" % n], ())
    linked = total(seq, ("native",), E7_TARGET, "elf")
    check_objects(linked, False, E7_TARGET, True)
    value = (linked.value + sum(lib["value"] for lib in linked.libs)) % 256
    native_program(out, E7_TARGET, value, None)
    write_record(out + ".inputs", linked.lines)
    return 0


def ld_lld_shared(argv):
    """The E7 recipe's shared library: -shared -Bsymbolic --as-needed
    -soname lib<name>.so -rpath $ORIGIN <objects> <libraries> <sysroot libs>
    -o lib<name>.so."""
    head = ["-shared", "-Bsymbolic", "--as-needed", "-soname"]
    if argv[:4] != head or argv[5:7] != ["-rpath", "$ORIGIN"]:
        raise Fail("not the E7 library command line: %r" % argv[:7])
    soname = argv[4]
    out, rest = split_out(argv[7:])
    if os.path.basename(out) != soname or not soname.startswith("lib"):
        raise Fail("-soname %s for the output %s" % (soname, out))
    check_sysroot_libs(rest[-3:])
    o = {"flags": [], "raw": rest[:-3]}
    seq = resolve(o, lambda n: ["lib%s.a" % n], ())
    linked = total(seq, ("native",), E7_TARGET, "elf")
    check_objects(linked, True, E7_TARGET, True)
    name = soname[len("lib") : -len(".so")]
    library_record(out, "elf", E7_TARGET, name, linked, False)
    write_record(out + ".inputs", linked.lines)
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
        return fail("usage: prog-cc.py --as cc|llc|ar|ld-lld ARGS...")
    mode, rest = argv[1], argv[2:]
    try:
        if mode == "cc":
            ilasm = None
            if rest[:1] == ["--ilasm"]:
                ilasm, rest = rest[1], rest[2:]
            return clang(rest, ilasm)
        if mode == "llc":
            return llc(rest)
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

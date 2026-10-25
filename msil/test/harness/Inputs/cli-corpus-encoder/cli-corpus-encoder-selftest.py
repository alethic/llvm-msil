#!/usr/bin/env python3
# ===-- cli-corpus-encoder-selftest.py - Self-test of --mode encoder ------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Self-test of msil/utils/mdtools/cli-corpus-check.py --mode encoder.

    python cli-corpus-encoder-selftest.py CLI_CORPUS_CHECK_PY WORKDIR

Run by msil/test/harness/cli-corpus-encoder.test. It loads CLI_CORPUS_CHECK_PY
as a module (sys.dont_write_bytecode keeps __pycache__ out of the source
tree) and runs the encoder mode's pieces on one fixture module that this
script holds: the 'llc -show-mc-encoding' listing of five methods with
bodies ('main', 'helper', 'flt', 'far' and '.cctor') and a P/Invoke
declaration, and the image that ilasm makes of it, written out here as
ilasm's method bodies (hand-assembled bytes, tokens included) and the
'llvm-cil-dump --headers --tables' lines of its sections and metadata rows.
The listing exercises every token fixup class that Phase 1 code has
(fixup_cil_method_token on a MethodDef and on a MemberRef,
fixup_cil_field_token, fixup_cil_sig_token), FK_PCRel_1, FK_PCRel_4 and a
switch with two entries, a tiny and a fat header, a .cctor, a .class block
and fields between the methods. WORKDIR is emptied first; each compare case
writes the listing, dump and image it compares under WORKDIR/compare-<case>/
for inspection, each main or usage case its stand-in tools, program and
work directory under WORKDIR/<kind>-<case>/.

  * compare cases: parse_encoded_listing(listing), ILImage(dump, image) and
    EncoderMode().compare_module() on a fresh copy of the fixture with one
    change. Each case checks the counts (methods, equal, snan-quiet, token
    and branch fixups) and that exactly the expected methods are reported,
    each for the expected reason. The unchanged fixture has every method
    equal. These must fail: one code byte changed in the listing; ilasm's
    code one byte longer; a token from a table that does not suit the fixup
    class (a Field token for fixup_cil_method_token, a MethodDef token for
    fixup_cil_field_token); a MemberRef whose signature is a field signature
    (0x06) under fixup_cil_method_token, with the right scope, parent and
    name; a wrong name (a MethodDef, a MemberRef, a MemberRef's parent type,
    its scope, a Field, a StandAloneSig's signature); a short branch whose
    value does not fit a byte; an instruction line without an encoding
    comment (in the middle and as the last line of a method); a body
    missing from ilasm's image and an extra body in it; a .maxstack
    difference (fat and tiny header); a float32 difference that is no
    quieted signaling NaN (the quiet bit set on 1.0, another NaN payload).
    A float32 signaling NaN that ilasm wrote with the quiet bit set is
    counted as snan-quiet, not as a mismatch.
  * main cases: main() with --mode encoder on one .c program at -O0, with
    the module's run() and run_split() replaced by fakes that write the
    fixture's files for the empty stand-ins of clang, llc, ilasm and
    llvm-cil-dump under WORKDIR (nothing is executed), and the module's
    REPO pointed under WORKDIR so that a --work there is accepted (WORKDIR
    is lit's %t, which may lie in the repository with the build directory).
    Exit 0 and the summary '5 of 5 methods equal' with --min-methods 5;
    exit 1 with --min-methods 6, everything else equal; exit 1 for a
    listing with a changed code byte; exit 1 when ilasm fails
    (tool-failure); and tool-failure-cp1252: ilasm fails with output that
    cp1252 cannot spell (U+FFFD, as run() decodes invalid UTF-8 with
    errors='replace', and other characters outside cp1252) while stdout
    and stderr are strict cp1252 streams, as a pipe or a file is on
    Windows: exit 1 with the ilasm FAIL line, its output line and the
    summary line, and no 'Traceback' or 'UnicodeEncodeError'. An
    exception out of main() fails its case.
  * usage case: --min-methods 0 exits 2 before any tool runs.

It prints 'ok: <case>' on stdout for each case that passes and, for one that
fails, 'FAIL: <case>' on stdout and the reasons with the output it saw on
stderr. Exit status 0 when every case passes, 1 otherwise, 2 on a usage
error of this script. Nothing runs clang, llc, ilasm or llvm-cil-dump.
"""

import contextlib
import importlib.util
import io
import os
import re
import shutil
import sys
import traceback

sys.dont_write_bytecode = True


class SetupError(Exception):
    pass


# ------------------------------------------------------------------ listing
# The module as 'llc -show-mc-encoding' prints it (MCAsmStreamer: the
# encoding comment at column 40, each fixup on its own line below it).

COMMENT_COLUMN = 40
METHOD_TOKEN = "fixup_cil_method_token"
FIELD_TOKEN = "fixup_cil_field_token"
SIG_TOKEN = "fixup_cil_sig_token"
ALLOC = (
    "native int [mscorlib]System.Runtime.InteropServices.Marshal::"
    "AllocHGlobal(native int)"
)


def inst(text, encoding, *fixups):
    head = "  " + text
    lines = [
        head + " " * max(1, COMMENT_COLUMN - len(head)) + "// encoding: [%s]" % encoding
    ]
    for letter, offset, value, kind in fixups:
        lines.append(
            " " * COMMENT_COLUMN
            + "//   fixup %s - offset: %d, value: %s, kind: %s"
            % (letter, offset, value, kind)
        )
    return lines


def label(name, comment):
    head = name + ":"
    return [head + " " * max(1, COMMENT_COLUMN - len(head)) + "// " + comment]


FAR_NOPS = 130  # 'far' branches over them: 130 does not fit a short branch

LISTING = (
    [
        "// MSIL module generated by LLVM",
        '// source_filename = "fixture.c"',
        '// target triple   = "x86_64-pc-windows-msvc-msil"',
        ".assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) "
        ".ver 4:0:0:0 }",
        ".assembly 'fixture' { .ver 0:0:0:0 }",
        "",
        ".method public static int32 'main'(int32, native int) cil managed",
        "{                                       // -- Begin function main",
        "                                        // @main",
        "  .maxstack 3",
        "  .locals (native int)",
        "// %bb.0:                               // %entry",
    ]
    + inst("ldarg.0", "0x02")  # 0
    + inst(
        "call int32 'helper'(int32)",  # 1
        "0x28,A,A,A,A",
        ("A", 1, "int32 'helper'(int32)", METHOD_TOKEN),
    )
    + inst(
        "switch (@BB0_1, @BB0_2)",  # 6
        "0x45,0x02,0x00,0x00,0x00,A,A,A,A,B,B,B,B",
        ("A", 5, "@BB0_1-8", "FK_PCRel_4"),
        ("B", 9, "@BB0_2-4", "FK_PCRel_4"),
    )
    + inst("br @BB0_3", "0x38,A,A,A,A", ("A", 1, "@BB0_3-4", "FK_PCRel_4"))  # 19
    + label("@BB0_1", "%alloc")  # 24
    + inst("ldc.i4.s 64", "0x1f,0x40")
    + inst("conv.i", "0xd3")  # 26
    + inst("call " + ALLOC, "0x28,A,A,A,A", ("A", 1, ALLOC, METHOD_TOKEN))  # 27
    + inst(
        "stsfld native int 'v_one'",  # 32
        "0x80,A,A,A,A",
        ("A", 1, "native int 'v_one'", FIELD_TOKEN),
    )
    + inst("br.s @BB0_3", "0x2b,A", ("A", 1, "@BB0_3-1", "FK_PCRel_1"))  # 37
    + label("@BB0_2", "%indirect")  # 39
    + inst(
        "ldsfld native int 'v_one'",
        "0x7e,A,A,A,A",
        ("A", 1, "native int 'v_one'", FIELD_TOKEN),
    )
    + inst("stloc.0", "0x0a")  # 44
    + inst("ldarg.0", "0x02")  # 45
    + inst("ldloc.0", "0x06")  # 46
    + inst(
        "calli int32(int32)", "0x29,A,A,A,A", ("A", 1, "int32(int32)", SIG_TOKEN)
    )  # 47
    + inst("ret", "0x2a")  # 52
    + label("@BB0_3", "%done")  # 53
    + inst("ldc.i4.0", "0x16")
    + inst("ret", "0x2a")  # 54
    + [
        "} // end of method 'main'",
        "",
        "                                        // -- End function",
        ".method assembly static int32 'helper'(int32) cil managed",
        "{                                       // -- Begin function helper",
        "                                        // @helper",
        "  .maxstack 8",
        "// %bb.0:                               // %entry",
    ]
    + inst("ldarg.0", "0x02")
    + inst("ldc.i4.7", "0x1d")
    + inst("add", "0x58")
    + inst("ret", "0x2a")
    + [
        "} // end of method 'helper'",
        "",
        "                                        // -- End function",
        ".method assembly static float32 'flt'() cil managed",
        "{                                       // -- Begin function flt",
        "                                        // @flt",
        "  .maxstack 1",
        "// %bb.0:                               // %entry",
    ]
    + inst("ldc.r4 float32(0x3F800000)", "0x22,0x00,0x00,0x80,0x3f")
    + inst("ret", "0x2a")
    + [
        "} // end of method 'flt'",
        "",
        "                                        // -- End function",
        ".method assembly static int32 'far'() cil managed",
        "{                                       // -- Begin function far",
        "                                        // @far",
        "  .maxstack 1",
        "// %bb.0:                               // %entry",
    ]
    + inst("br @far_end", "0x38,A,A,A,A", ("A", 1, "@far_end-4", "FK_PCRel_4"))  # 0
    + inst("nop", "0x00") * FAR_NOPS  # 5 .. 134
    + label("@far_end", "%end")  # 135
    + inst("ldc.i4.0", "0x16")
    + inst("ret", "0x2a")
    + [
        "} // end of method 'far'",
        "",
        "                                        // -- End function",
        ".method private hidebysig specialname rtspecialname static void .cctor() "
        "cil managed",
        "{",
        "  .maxstack 1",
    ]
    + inst("ldc.i4.1", "0x17")
    + inst("conv.i", "0xd3")
    + inst(
        "stsfld native int 'v_two'",
        "0x80,A,A,A,A",
        ("A", 1, "native int 'v_two'", FIELD_TOKEN),
    )
    + inst("ret", "0x2a")
    + [
        "} // end of method '.cctor'",
        '.method public hidebysig static pinvokeimpl("ucrtbase" nomangle cdecl) '
        "int32 'puts'(native int) cil managed preservesig {}",
        "",
        "// ---- data segment: 16 bytes (16 initialized + 0 zero), align 16 ----",
        ".class private explicit ansi sealed '<msil>image_t' extends "
        "[mscorlib]System.ValueType",
        "{",
        "  .pack 1",
        "  .size 16",
        "}",
        ".field public static initonly native int 'v_one' // +0",
        ".field public static initonly native int 'v_two' // +8",
    ]
)

# ------------------------------------------------------------ ilasm's image
# ilasm's bodies of the listing's methods, in MethodDef order: (name, tiny,
# max stack, local signature token, code). The tokens are those of the
# tables below: MethodDef 2 'helper', MemberRef 1 AllocHGlobal, Field 1
# 'v_one', Field 2 'v_two', StandAloneSig 2 'int32(int32)'.

BODIES = (
    (
        "main",
        False,
        3,
        0x11000001,
        "02"  # 0 ldarg.0
        " 28 02 00 00 06"  # 1 call 'helper'
        " 45 02 00 00 00 05 00 00 00 14 00 00 00"  # 6 switch (24, 39)
        " 38 1d 00 00 00"  # 19 br 53
        " 1f 40"  # 24 ldc.i4.s 64
        " d3"  # 26 conv.i
        " 28 01 00 00 0a"  # 27 call AllocHGlobal
        " 80 01 00 00 04"  # 32 stsfld 'v_one'
        " 2b 0e"  # 37 br.s 53
        " 7e 01 00 00 04"  # 39 ldsfld 'v_one'
        " 0a"  # 44 stloc.0
        " 02"  # 45 ldarg.0
        " 06"  # 46 ldloc.0
        " 29 02 00 00 11"  # 47 calli int32(int32)
        " 2a"  # 52 ret
        " 16"  # 53 ldc.i4.0
        " 2a",  # 54 ret
    ),
    ("helper", True, 8, 0, "02 1d 58 2a"),
    ("flt", False, 1, 0, "22 00 00 80 3f 2a"),
    ("far", False, 1, 0, "38 82 00 00 00" + " 00" * FAR_NOPS + " 16 2a"),
    (".cctor", False, 1, 0, "17 d3 80 02 00 00 04 2a"),
)

# MethodDef rows: (name, body or None, implflags, flags, signature, params).
METHODDEFS = (
    ("main", "main", 0x0000, 0x0016, "00 02 08 08 18", 1),
    ("helper", "helper", 0x0000, 0x0013, "00 01 08 08", 3),
    ("flt", "flt", 0x0000, 0x0013, "00 00 0C", 4),
    ("far", "far", 0x0000, 0x0013, "00 00 08", 4),
    (".cctor", ".cctor", 0x0000, 0x1891, "00 00 01", 4),
    ("puts", None, 0x0080, 0x2096, "00 01 08 18", 4),
)

# The other tables, in llvm-cil-dump's order: (table, row size, rows).
TABLES_BEFORE = (
    (
        "Module",
        10,
        [
            "Generation=0x0000 Name='fixture.O0.dll' "
            "Mvid={00000000-0000-0000-0000-000000000000} EncId=0 EncBaseId=0"
        ],
    ),
    (
        "TypeRef",
        6,
        [
            "ResolutionScope=0x23000001 TypeName='Marshal' "
            "TypeNamespace='System.Runtime.InteropServices'",
            "ResolutionScope=0x23000001 TypeName='Environment' TypeNamespace='System'",
            "ResolutionScope=0x23000001 TypeName='ValueType' TypeNamespace='System'",
            # Marshal in another assembly.
            "ResolutionScope=0x23000002 TypeName='Marshal' "
            "TypeNamespace='System.Runtime.InteropServices'",
        ],
    ),
    (
        "TypeDef",
        14,
        [
            "Flags=0x00000000 TypeName='<Module>' TypeNamespace='' Extends=0 "
            "FieldList=0x04000001 MethodList=0x06000001",
            "Flags=0x00000110 TypeName='<msil>image_t' TypeNamespace='' "
            "Extends=0x01000003 FieldList=0x04000003 MethodList=0x06000007",
        ],
    ),
    (
        "Field",
        6,
        [
            "Flags=0x0036 Name='v_one' Signature=06 18",
            "Flags=0x0036 Name='v_two' Signature=06 18",
        ],
    ),
)
TABLES_AFTER = (
    (
        "Param",
        6,
        [
            "Flags=0x0000 Sequence=0x0001 Name='A_0'",
            "Flags=0x0000 Sequence=0x0002 Name='A_1'",
            "Flags=0x0000 Sequence=0x0001 Name='A_0'",
            "Flags=0x0000 Sequence=0x0001 Name='A_0'",
        ],
    ),
    (
        "MemberRef",
        6,
        [
            # 1: the call's member; 2: the same name and parent with a field
            # signature; 3: another name; 4: the same name, another parent;
            # 5: the same name and parent type in another assembly.
            "Class=0x01000001 Name='AllocHGlobal' Signature=00 01 18 18",
            "Class=0x01000001 Name='AllocHGlobal' Signature=06 18",
            "Class=0x01000001 Name='FreeHGlobal' Signature=00 01 01 18",
            "Class=0x01000002 Name='AllocHGlobal' Signature=00 01 18 18",
            "Class=0x01000004 Name='AllocHGlobal' Signature=00 01 18 18",
        ],
    ),
    ("ClassLayout", 8, ["PackingSize=0x0001 ClassSize=0x00000010 Parent=0x02000002"]),
    (
        "StandAloneSig",
        2,
        [
            # 1: main's locals; 2: the calli's signature; 3: another one.
            "Signature=07 01 18",
            "Signature=00 01 08 08",
            "Signature=00 01 18 18",
        ],
    ),
    ("ModuleRef", 2, ["Name='ucrtbase'"]),
    (
        "ImplMap",
        8,
        [
            "MappingFlags=0x0201 MemberForwarded=0x06000006 ImportName='puts' "
            "ImportScope=0x1A000001"
        ],
    ),
    (
        "Assembly",
        22,
        [
            "HashAlgId=0x00000000 MajorVersion=0x0000 MinorVersion=0x0000 "
            "BuildNumber=0x0000 RevisionNumber=0x0000 Flags=0x00000000 PublicKey=- "
            "Name='fixture' Culture=''"
        ],
    ),
    (
        "AssemblyRef",
        20,
        [
            "MajorVersion=0x0004 MinorVersion=0x0000 BuildNumber=0x0000 "
            "RevisionNumber=0x0000 Flags=0x00000000 "
            "PublicKeyOrToken=B7 7A 5C 56 19 34 E0 89 Name='mscorlib' Culture='' "
            "HashValue=-",
            "MajorVersion=0x0008 MinorVersion=0x0000 BuildNumber=0x0000 "
            "RevisionNumber=0x0000 Flags=0x00000000 "
            "PublicKeyOrToken=B0 3F 5F 7F 11 D5 0A 3A Name='System.Runtime' "
            "Culture='' HashValue=-",
        ],
    ),
)

TEXT_RVA = 0x2000
TEXT_RAW = 0x200
FILE_ALIGN = 0x200
BODIES_AT = 0x50  # the CLI header and the IAT come first in ilasm's .text

# The counts of the unchanged fixture.
METHODS = 5
TOKENS = 6  # main: 5, .cctor: 1
BRANCHES = 5  # main: 2 switch entries, br, br.s; far: br


class Fixture:
    """A fresh, changeable copy of the listing, the bodies and the rows."""

    def __init__(self):
        self.listing = "\n".join(LISTING) + "\n"
        self.bodies = [
            {
                "name": name,
                "tiny": tiny,
                "maxstack": maxstack,
                "locals": localsig,
                "code": bytearray.fromhex(code),
            }
            for name, tiny, maxstack, localsig, code in BODIES
        ]
        self.methoddefs = [list(row) for row in METHODDEFS]

    def edit_listing(self, old, new):
        """Replace the one occurrence of the whole lines old (a list, as
        inst() makes it) with the lines new."""
        old = "".join("\n" + line for line in old) + "\n"
        new = "".join("\n" + line for line in new) + "\n"
        count = self.listing.count(old)
        if count != 1:
            raise SetupError(
                "%r occurs %d times in the listing, expected once" % (old, count)
            )
        self.listing = self.listing.replace(old, new)

    def body(self, name):
        for b in self.bodies:
            if b["name"] == name:
                return b
        raise SetupError("no body %r" % name)

    def edit_code(self, name, offset, old, new):
        """Replace the bytes old (hex) at offset of ilasm's code of name."""
        code = self.body(name)["code"]
        old, new = bytes.fromhex(old), bytes.fromhex(new)
        if bytes(code[offset : offset + len(old)]) != old:
            raise SetupError(
                "ilasm's code of %r has %s at %d, not %s"
                % (
                    name,
                    code[offset : offset + len(old)].hex(" "),
                    offset,
                    old.hex(" "),
                )
            )
        code[offset : offset + len(old)] = new

    def set_token(self, name, offset, old, new):
        self.edit_code(
            name,
            offset,
            old.to_bytes(4, "little").hex(),
            new.to_bytes(4, "little").hex(),
        )

    def methoddef(self, name):
        for row in self.methoddefs:
            if row[0] == name:
                return row
        raise SetupError("no MethodDef %r" % name)

    def build(self):
        """(listing, dump, image) bytes."""
        text = bytearray(BODIES_AT)
        rvas = {}
        for b in self.bodies:
            code = bytes(b["code"])
            if b["tiny"]:
                if len(code) >= 64 or b["maxstack"] != 8 or b["locals"]:
                    raise SetupError("%r cannot have a tiny header" % b["name"])
                header = bytes([len(code) << 2 | 0x2])
            else:
                text += bytes(-len(text) % 4)
                flags = 0x3003 | (0x10 if b["locals"] else 0)
                header = (
                    flags.to_bytes(2, "little")
                    + b["maxstack"].to_bytes(2, "little")
                    + len(code).to_bytes(4, "little")
                    + b["locals"].to_bytes(4, "little")
                )
            rvas[b["name"]] = TEXT_RVA + len(text)
            text += header + code
        vsize = len(text)
        rawsize = vsize + (-vsize % FILE_ALIGN)
        image = bytes(TEXT_RAW) + bytes(text) + bytes(rawsize - vsize)
        lines = [
            "Format: PE32+ machine=0x8664 dll",
            "COFF: sections=1 timestamp=0x00000000 characteristics=0x2022 symbols=0",
            "Optional: imagebase=0x0000000140000000 sectionalign=0x2000 "
            "filealign=0x200 subsystem=3 dllchars=0x8540 stack=0x400000,0x4000 "
            "heap=0x100000,0x2000",
            "Section: .text rva=0x%08X vsize=0x%X raw=0x%X rawsize=0x%X "
            "flags=0x60000020" % (TEXT_RVA, vsize, TEXT_RAW, rawsize),
            "CLI: flags=0x00000001 entrypoint=0x00000000 metadata=0x00000000,0x0 "
            "runtime=2.5",
        ]

        def table(name, rowsize, rows):
            lines.append("Table: %s rows=%d rowsize=%d" % (name, len(rows), rowsize))
            for i, row in enumerate(rows, 1):
                lines.append("  %s[%d]: %s" % (name, i, row))

        for t in TABLES_BEFORE:
            table(*t)
        table(
            "MethodDef",
            14,
            [
                "RVA=0x%08X ImplFlags=0x%04X Flags=0x%04X Name='%s' Signature=%s "
                "ParamList=0x%08X"
                % (rvas[body] if body else 0, impl, flags, name, sig, 0x08000000 | par)
                for name, body, impl, flags, sig, par in self.methoddefs
            ],
        )
        for t in TABLES_AFTER:
            table(*t)
        dump = "".join(line + "\n" for line in lines)
        return self.listing.encode("utf-8"), dump.encode("utf-8"), image


# ------------------------------------------------------------ compare cases
# Each case changes a fresh Fixture and returns what compare_module must
# give: the counts that differ from the unchanged fixture's and
# {method: regex its reason must match} for exactly the reported methods.


def expect(mismatches=None, **counts):
    want = {
        "methods": METHODS,
        "equal": METHODS - len(mismatches or {}),
        "snan": 0,
        "tokens": TOKENS,
        "branches": BRANCHES,
    }
    want.update(counts)
    want["mismatches"] = mismatches or {}
    return want


def case_equal(fix):
    return expect()


def case_code_byte(fix):
    # Our encoder wrote stloc.1's opcode for 'stloc.0'.
    fix.edit_listing(inst("stloc.0", "0x0a"), inst("stloc.0", "0x0b"))
    return expect(
        {
            "main": r"^code differs at offset 44 of 55 \(ilasm: 55 bytes\) "
            r"in 'stloc\.0': ours 0b 02 06 29 00 00 00 00, ilasm 0a 02 06 29"
        }
    )


def case_code_length(fix):
    # ilasm's body is one byte longer; the bytes the listing has are equal.
    fix.edit_code("helper", 3, "2a", "2a 2a")
    return expect(
        {
            "helper": r"^code differs at offset 4 of 4 \(ilasm: 5 bytes\): "
            r"ours , ilasm 2a$"
        }
    )


def case_token_table(fix):
    # A Field token where the method fixup expects MethodDef or MemberRef.
    fix.set_token("main", 2, 0x06000002, 0x04000001)
    return expect(
        {
            "main": r"^\"call int32 'helper'\(int32\)\" at offset 2: ilasm wrote a "
            r"Field token 0x04000001 for fixup_cil_method_token$"
        }
    )


def case_token_table_field(fix):
    # A MethodDef token where the field fixup expects Field or MemberRef.
    fix.set_token("main", 40, 0x04000001, 0x06000002)
    return expect(
        {
            "main": r"^\"ldsfld native int 'v_one'\" at offset 40: ilasm wrote a "
            r"MethodDef token 0x06000002 for fixup_cil_field_token$"
        }
    )


def case_memberref_kind(fix):
    # The MemberRef has the call's scope, parent and name, but a field
    # signature (0x06): only its member kind can be why.
    fix.set_token("main", 28, 0x0A000001, 0x0A000002)
    return expect(
        {
            "main": r"^'call " + re.escape(ALLOC) + r"' at offset 28: MemberRef "
            r"0x0A000002 with a field signature .*for fixup_cil_method_token$"
        }
    )


def case_wrong_methoddef(fix):
    fix.set_token("main", 2, 0x06000002, 0x06000003)
    return expect(
        {
            "main": r"^\"call int32 'helper'\(int32\)\" at offset 2: MethodDef 'flt' "
            r"for \"int32 'helper'\(int32\)\"$"
        }
    )


def case_wrong_memberref(fix):
    fix.set_token("main", 28, 0x0A000001, 0x0A000003)
    return expect(
        {
            "main": r"^'call " + re.escape(ALLOC) + r"' at offset 28: MemberRef "
            r"System\.Runtime\.InteropServices\.Marshal::FreeHGlobal for "
        }
    )


def case_wrong_parent(fix):
    fix.set_token("main", 28, 0x0A000001, 0x0A000004)
    return expect(
        {
            "main": r"^'call " + re.escape(ALLOC) + r"' at offset 28: MemberRef "
            r"System\.Environment::AllocHGlobal for "
        }
    )


def case_wrong_scope(fix):
    # System.Runtime's Marshal where the value names [mscorlib]'s.
    fix.set_token("main", 28, 0x0A000001, 0x0A000005)
    return expect(
        {
            "main": r"^'call " + re.escape(ALLOC) + r"' at offset 28: MemberRef "
            r"\[System\.Runtime\]System\.Runtime\.InteropServices\.Marshal::"
            r"AllocHGlobal for "
        }
    )


def case_wrong_field(fix):
    fix.set_token("main", 40, 0x04000001, 0x04000002)
    return expect(
        {
            "main": r"^\"ldsfld native int 'v_one'\" at offset 40: Field 'v_two' for "
            r"\"native int 'v_one'\"$"
        }
    )


def case_wrong_sig(fix):
    fix.set_token("main", 48, 0x11000002, 0x11000003)
    return expect(
        {
            "main": r"^'calli int32\(int32\)' at offset 48: StandAloneSig "
            r"'native int\(native int\)' for 'int32\(int32\)'$"
        }
    )


def case_branch_too_far(fix):
    # A short branch over the 130 nops: 132 - 1 - 1 = 130 > 127.
    fix.edit_listing(
        inst("br @far_end", "0x38,A,A,A,A", ("A", 1, "@far_end-4", "FK_PCRel_4")),
        inst("br.s @far_end", "0x2b,A", ("A", 1, "@far_end-1", "FK_PCRel_1")),
    )
    return expect(
        {"far": r"^'br\.s @far_end': the fixup value 130 does not fit 1 byte\(s\)$"}
    )


def case_unencoded(fix):
    fix.edit_listing(inst("add", "0x58"), ["  add"])
    return expect({"helper": r"^1 line\(s\) without an encoding comment, first 'add'$"})


def case_unencoded_last(fix):
    # The last line of the method: only the closing brace ends it.
    end = ["} // end of method 'helper'"]
    fix.edit_listing(inst("ret", "0x2a") + end, ["  ret"] + end)
    return expect({"helper": r"^1 line\(s\) without an encoding comment, first 'ret'$"})


def case_body_missing(fix):
    fix.methoddef("flt")[1] = None
    return expect({"flt": r"^ilasm's image has no body for it$"})


def case_body_extra(fix):
    # A row that the listing has no method for, with 'helper''s body.
    fix.methoddefs.append(["extra", "helper", 0x0000, 0x0013, "00 01 08 08", 4])
    return expect(
        {"extra": r"^1 body\(ies\) in ilasm's image that the listing does not have$"},
        equal=METHODS,
    )


def case_maxstack(fix):
    fix.body("main")["maxstack"] = 2
    return expect({"main": r"^\.maxstack 3, ilasm's 2$"})


def case_maxstack_tiny(fix):
    # A tiny header means .maxstack 8.
    fix.edit_listing(["  .maxstack 8"], ["  .maxstack 2"])
    return expect({"helper": r"^\.maxstack 2, ilasm's 8$"})


def flt_listing(fix, value):
    fix.edit_listing(
        inst("ldc.r4 float32(0x3F800000)", "0x22,0x00,0x00,0x80,0x3f"),
        inst(
            "ldc.r4 float32(0x%08X)" % value,
            ",".join(["0x22"] + ["0x%02x" % b for b in value.to_bytes(4, "little")]),
        ),
    )


def flt_ilasm(fix, value):
    fix.edit_code("flt", 1, "00 00 80 3f", value.to_bytes(4, "little").hex())


def case_snan_quiet(fix):
    # ilasm sets the quiet bit of a float32 signaling NaN (plan 5.12.6).
    flt_listing(fix, 0x7FA00000)
    flt_ilasm(fix, 0x7FE00000)
    return expect(snan=1, equal=METHODS - 1)


def case_float_quiet_bit(fix):
    # The same bit on 1.0, which is no NaN: a difference.
    flt_ilasm(fix, 0x3FC00000)
    return expect(
        {
            "flt": r"^code differs at offset 3 of 6 \(ilasm: 6 bytes\) in "
            r"'ldc\.r4 float32\(0x3F800000\)': ours 80 3f 2a, ilasm c0 3f 2a$"
        }
    )


def case_float_snan_other(fix):
    # A signaling NaN that ilasm wrote as another value than its quiet form.
    flt_listing(fix, 0x7FA00000)
    flt_ilasm(fix, 0x7FA00001)
    return expect(
        {
            "flt": r"^code differs at offset 1 of 6 \(ilasm: 6 bytes\) in "
            r"'ldc\.r4 float32\(0x7FA00000\)': ours 00 00 a0 7f 2a, "
            r"ilasm 01 00 a0 7f 2a$"
        }
    )


COMPARE_CASES = (
    ("equal", case_equal),
    ("code-byte", case_code_byte),
    ("code-length", case_code_length),
    ("token-table", case_token_table),
    ("token-table-field", case_token_table_field),
    ("memberref-kind", case_memberref_kind),
    ("wrong-methoddef", case_wrong_methoddef),
    ("wrong-memberref", case_wrong_memberref),
    ("wrong-parent", case_wrong_parent),
    ("wrong-scope", case_wrong_scope),
    ("wrong-field", case_wrong_field),
    ("wrong-sig", case_wrong_sig),
    ("branch-too-far", case_branch_too_far),
    ("unencoded", case_unencoded),
    ("unencoded-last", case_unencoded_last),
    ("body-missing", case_body_missing),
    ("body-extra", case_body_extra),
    ("maxstack", case_maxstack),
    ("maxstack-tiny", case_maxstack_tiny),
    ("snan-quiet", case_snan_quiet),
    ("float-quiet-bit", case_float_quiet_bit),
    ("float-snan-other", case_float_snan_other),
)


def write_file(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def check_compare(module, work, label, mutate):
    fix = Fixture()
    want = mutate(fix)
    listing, dump, image = fix.build()
    case_dir = os.path.join(work, "compare-" + label)
    write_file(os.path.join(case_dir, "fixture.O0.il"), listing)
    write_file(os.path.join(case_dir, "fixture.O0.txt"), dump)
    write_file(os.path.join(case_dir, "fixture.O0.dll"), image)
    try:
        methods = module.parse_encoded_listing(listing)
        result = module.EncoderMode().compare_module(
            methods, module.ILImage(dump, image)
        )
    except module.CheckError as e:
        return ["compare raised CheckError: %s" % e], ""
    output = "".join(
        "%s=%r\n" % (k, result.get(k))
        for k in ("methods", "equal", "snan", "tokens", "branches", "mismatches")
    )
    errors = []
    for key in ("methods", "equal", "snan", "tokens", "branches"):
        if result.get(key) != want[key]:
            errors.append("%s is %r, expected %r" % (key, result.get(key), want[key]))
    got = {}
    for name, why in result.get("mismatches", []):
        if name in got:
            errors.append("method %r is reported twice" % name)
        got[name] = why
    if sorted(got) != sorted(want["mismatches"]):
        errors.append(
            "reported methods %r, expected %r"
            % (sorted(got), sorted(want["mismatches"]))
        )
    for name, pattern in want["mismatches"].items():
        if name in got and not re.search(pattern, got[name]):
            errors.append("the reason for %r does not match %r" % (name, pattern))
    return errors, output


# --------------------------------------------------------------- main cases
# main() with fake tools: run() and run_split() answer each command from the
# fixture instead of running it.


ILASM_FAILURE = "Error: fixture failure\n***** FAILURE *****\n"

# ilasm's output for tool-failure-cp1252, as run() hands it on: decoded as
# UTF-8 with errors='replace', so the invalid bytes 0xFF 0xFE are U+FFFD
# twice; U+2603 and U+4E2D are valid UTF-8 outside cp1252 as well, and
# U+00E9 is in cp1252.
ILASM_FAILURE_NON_CP1252 = (
    b"fixture.il(3) : error : bad bytes \xff\xfe in 'm\xc3\xa9thode' "
    b"\xe2\x98\x83 \xe4\xb8\xad\n***** FAILURE *****\n"
).decode("utf-8", errors="replace")


class FakeTools:
    def __init__(
        self, work, listing, dump, image, ilasm_fails=False, ilasm_output=ILASM_FAILURE
    ):
        self.listing, self.dump, self.image = listing, dump, image
        self.ilasm_fails = ilasm_fails
        self.ilasm_output = ilasm_output
        self.calls = []
        bin_dir = os.path.join(work, "bin")
        self.paths = {}
        for tool in ("clang", "llc", "ilasm", "llvm-cil-dump"):
            path = os.path.join(bin_dir, tool)
            write_file(path, b"")  # never executed: run() is a fake
            self.paths[tool] = path

    def tool(self, cmd):
        return os.path.basename(cmd[0])

    def run(self, cmd, timeout, cwd=None):
        tool = self.tool(cmd)
        self.calls.append(tool)
        if tool in ("clang", "llc"):
            out = cmd[cmd.index("-o") + 1]
            data = self.listing if tool == "llc" else b"; clang -S -emit-llvm\n"
            if tool == "llc" and "-show-mc-encoding" not in cmd:
                return 1, "llc ran without -show-mc-encoding"
            write_file(out, data)
            return 0, ""
        if tool == "ilasm":
            if self.ilasm_fails:
                return 1, self.ilasm_output
            out = [a for a in cmd if a.startswith("-output=")]
            if not out or "-dll" not in cmd:
                return 1, "ilasm ran without -dll -output="
            write_file(out[0][len("-output=") :], self.image)
            return 0, ""
        return None, "unexpected command %r" % (cmd,)

    def run_split(self, cmd, timeout, cwd=None):
        tool = self.tool(cmd)
        self.calls.append(tool)
        if tool == "llvm-cil-dump" and "--headers" in cmd and "--tables" in cmd:
            return 0, self.dump, ""
        return None, b"", "unexpected command %r" % (cmd,)


def capture_stream(encoding):
    """io.StringIO, or with an encoding a text stream that encodes into
    bytes with strict errors, as a pipe or a file in that encoding does."""
    if encoding is None:
        return io.StringIO()
    return io.TextIOWrapper(
        io.BytesIO(), encoding=encoding, errors="strict", newline="\n"
    )


def captured_text(stream):
    if isinstance(stream, io.StringIO):
        return stream.getvalue()
    stream.flush()
    return stream.buffer.getvalue().decode(stream.encoding, errors="replace")


def run_main(module, work, argv, tools, encoding=None):
    """main() with argv and the fake tools: (exit status, stdout, stderr).
    An exception out of main() gives the status 'raised <name>' and its
    traceback at the end of stderr."""
    saved = (module.run, module.run_split, module.REPO, sys.argv)
    module.run, module.run_split = tools.run, tools.run_split
    # lit's %t may lie in the repository; the work directory must not.
    module.REPO = os.path.join(work, "not-the-repository")
    sys.argv = [module.SCRIPT] + argv
    out, err = capture_stream(encoding), capture_stream(encoding)
    raised = ""
    try:
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            try:
                status = module.main()
            except SystemExit as e:
                status = e.code
    except Exception as e:
        status = "raised " + type(e).__name__
        raised = traceback.format_exc()
    finally:
        module.run, module.run_split, module.REPO, sys.argv = saved
    return status, captured_text(out), captured_text(err) + raised


SUMMARY = (
    r"^cli-corpus-check\.py: encoder: %d of %d methods equal to ilasm \(0 of them "
    r"up to a float32 signaling NaN that ilasm quiets: snan-quiet\), %d mismatches; "
    r"%d token and %d branch fixups checked; %d of 1 modules compared, at O0, in "
)
ILASM_FAIL_LINE = (
    r"^cli-corpus-check\.py: encoder: FAIL fixture -O0: ilasm of .*"
    r"fixture\.c failed \(1\):"
)


def main_cases():
    """(label, fixture change, ilasm fails, --min-methods, exit status,
    regexes of stdout lines, regexes no stdout line may match)."""
    equal = SUMMARY % (METHODS, METHODS, 0, TOKENS, BRANCHES, 1)
    return (
        ("equal", None, False, METHODS, 0, [equal], [r"FAIL"]),
        (
            "min-methods",
            None,
            False,
            METHODS + 1,
            1,
            [
                equal,
                r"^cli-corpus-check\.py: encoder: FAIL only %d methods were compared, "
                r"fewer than --min-methods %d$" % (METHODS, METHODS + 1),
            ],
            [r"FAIL fixture"],
        ),
        (
            "mismatch",
            case_code_byte,
            False,
            METHODS,
            1,
            [
                SUMMARY % (METHODS - 1, METHODS, 1, TOKENS, BRANCHES, 1),
                r"^cli-corpus-check\.py: encoder: FAIL fixture -O0 method 'main': "
                r"code differs at offset 44 of 55",
            ],
            [r"FAIL only"],
        ),
        (
            "tool-failure",
            None,
            True,
            1,
            1,
            [ILASM_FAIL_LINE, SUMMARY % (0, 0, 0, 0, 0, 0)],
            [],
        ),
    )


def run_encoder(
    module,
    case_dir,
    fix,
    min_methods,
    ilasm_fails=False,
    ilasm_output=ILASM_FAILURE,
    encoding=None,
):
    """main() with --mode encoder on WORKDIR/<case>/src/fixture.c at -O0 and
    fake tools that answer from fix, stdout and stderr captured as
    capture_stream(encoding) makes them: (exit status, tools run, stdout,
    stderr, output)."""
    tools = FakeTools(
        case_dir, *fix.build(), ilasm_fails=ilasm_fails, ilasm_output=ilasm_output
    )
    source = os.path.join(case_dir, "src", "fixture.c")
    write_file(source, b"int main(void) { return 0; }\n")
    argv = ["--mode", "encoder", "--levels", "O0", "--jobs", "1"]
    argv += ["--work", os.path.join(case_dir, "work")]
    argv += ["--clang", tools.paths["clang"], "--llc", tools.paths["llc"]]
    argv += ["--ilasm-framework", tools.paths["ilasm"]]
    argv += ["--cil-dump", tools.paths["llvm-cil-dump"]]
    argv += ["--min-methods", str(min_methods), source]
    got, out, err = run_main(module, case_dir, argv, tools, encoding)
    output = "exit %r\ntools %r\n---- stdout ----\n%s---- stderr ----\n%s" % (
        got,
        tools.calls,
        out,
        err,
    )
    return got, tools.calls, out, err, output


def check_main(
    module, work, label, mutate, ilasm_fails, min_methods, status, want, never
):
    fix = Fixture()
    if mutate:
        mutate(fix)
    case_dir = os.path.join(work, "main-" + label)
    got, calls, out, _, output = run_encoder(
        module, case_dir, fix, min_methods, ilasm_fails
    )
    errors = []
    if got != status:
        errors.append("main() exited %r, expected %r" % (got, status))
    ran = ["clang", "llc", "ilasm"] + ([] if ilasm_fails else ["llvm-cil-dump"])
    if calls != ran:
        errors.append("the tools ran as %r, expected %r" % (calls, ran))
    for pattern in want:
        if not re.search(pattern, out, re.M):
            errors.append("no stdout line matches %r" % pattern)
    for pattern in never:
        if re.search(pattern, out, re.M):
            errors.append("a stdout line matches %r" % pattern)
    return errors, output


def check_main_tool_failure_cp1252(module, work):
    """'main tool-failure' with ilasm output that cp1252 cannot spell and
    stdout and stderr strict cp1252 streams (a pipe or a file on Windows):
    no line may raise UnicodeEncodeError and cost the rest."""
    try:
        ILASM_FAILURE_NON_CP1252.encode("cp1252")
        raise SetupError("cp1252 can spell the ilasm output")
    except UnicodeEncodeError:
        pass
    case_dir = os.path.join(work, "main-tool-failure-cp1252")
    got, calls, out, err, output = run_encoder(
        module,
        case_dir,
        Fixture(),
        1,
        ilasm_fails=True,
        ilasm_output=ILASM_FAILURE_NON_CP1252,
        encoding="cp1252",
    )
    errors = []
    if got != 1:
        errors.append("main() exited %r, expected 1" % (got,))
    if calls != ["clang", "llc", "ilasm"]:
        errors.append(
            "the tools ran as %r, expected %r" % (calls, ["clang", "llc", "ilasm"])
        )
    for pattern in (
        ILASM_FAIL_LINE,
        # ilasm's line with the characters outside cp1252, in whatever
        # spelling, and U+00E9 as itself.
        r"^fixture\.il\(3\) : error : bad bytes .+ in 'm\u00e9thode' .+$",
        SUMMARY % (0, 0, 0, 0, 0, 0),
    ):
        if not re.search(pattern, out, re.M):
            errors.append("no stdout line matches %r" % pattern)
    for word in ("Traceback", "UnicodeEncodeError"):
        for stream, text in (("stdout", out), ("stderr", err)):
            if word in text:
                errors.append("%s holds %r" % (stream, word))
    return errors, output


def check_usage_min_methods(module, work):
    case_dir = os.path.join(work, "usage-min-methods-zero")
    got, calls, _, err, output = run_encoder(module, case_dir, Fixture(), 0)
    errors = []
    if got != 2:
        errors.append("main() exited %r, expected 2" % (got,))
    if calls:
        errors.append("tools ran: %r" % calls)
    if "--min-methods must be at least 1" not in err:
        errors.append("stderr does not name '--min-methods must be at least 1'")
    if os.path.exists(os.path.join(case_dir, "work")):
        errors.append("the work directory was created")
    return errors, output


# --------------------------------------------------------------------- main


def load(path):
    spec = importlib.util.spec_from_file_location("cli_corpus_check", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main(argv):
    if len(argv) != 2:
        sys.stderr.write(
            "usage: cli-corpus-encoder-selftest.py CLI_CORPUS_CHECK_PY WORKDIR\n"
        )
        return 2
    module = load(os.path.abspath(argv[0]))
    work = os.path.abspath(argv[1])
    if os.path.lexists(work):
        shutil.rmtree(work)
    os.makedirs(work)

    failed = 0

    def report(label, errors, output):
        nonlocal failed
        if errors:
            failed += 1
            sys.stdout.write("FAIL: %s\n" % label)
            sys.stderr.write(
                "FAIL: %s: %s\n---- output ----\n%s---- end ----\n"
                % (label, "\n  ".join(errors), output)
            )
        else:
            sys.stdout.write("ok: %s\n" % label)

    for label, mutate in COMPARE_CASES:
        try:
            errors, output = check_compare(module, work, label, mutate)
        except SetupError as e:
            errors, output = ["setup: %s" % e], ""
        report("compare " + label, errors, output)
    for label, mutate, ilasm_fails, min_methods, status, want, never in main_cases():
        try:
            errors, output = check_main(
                module,
                work,
                label,
                mutate,
                ilasm_fails,
                min_methods,
                status,
                want,
                never,
            )
        except SetupError as e:
            errors, output = ["setup: %s" % e], ""
        report("main " + label, errors, output)
    try:
        errors, output = check_main_tool_failure_cp1252(module, work)
    except SetupError as e:
        errors, output = ["setup: %s" % e], ""
    report("main tool-failure-cp1252", errors, output)
    errors, output = check_usage_min_methods(module, work)
    report("usage min-methods-zero", errors, output)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

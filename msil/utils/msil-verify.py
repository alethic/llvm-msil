#!/usr/bin/env python3
# ===-- msil-verify.py - Verify an assembly of the MSIL test suite --------===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===#
"""Check an assembly with ILVerify (and PEVerify on Windows) for real IL errors.

    msil-verify.py --ilverify PATH --runtime-dir DIR [--allow FILE]
                   [--peverify PATH] [--ildasm PATH --ilasm PATH]
                   [--keep-copy DIR] [--timeout S] [--mem-limit-mb N] [-v]
                   [--dep FILE...]... [--] prog.dll
    msil-verify.py --skip [--dep FILE...] [--] prog.dll   (no verifier: a no-op)
    msil-verify.py --roundtrip --output OUT.dll --ildasm PATH --ilasm PATH
                   [--keep-copy DIR] [--timeout S] [--mem-limit-mb N] [-v]
                   prog.dll              (the ildasm -> ilasm round trip)

The backend's output is unverifiable by design (Design.md section 1), so this
wrapper fails only on errors outside the policy of plan D21. It runs two
passes; every error of either pass fails.

Pass 1 verifies the assembly itself:

  * ILVerify: an '[IL]: Error [Code]: [where][offset 0x...] message' line is
    accepted only when an entry of --allow (default: ilverify-allowed.txt next
    to this script) matches both its code and its message. The entries pin
    each code to the message of the construct that produces it, so StackByRef
    is accepted only as '[found Native Int] Expected ByRef on the stack.': an
    int32, int64 or float64 address fails even without PEVerify.
    'Error []: ... ImportCalli not implemented' (ILVerify cannot verify calli)
    is a note.
  * PEVerify (Windows, '/nologo /il'): it catches native int / int64 mixing,
    which ILVerify misses. Every '[IL]: Error:' message fails except the
    unverifiable-by-design ones in PEVERIFY_ALLOWED below, which include
    exactly one 'Unexpected type on the stack' message: the native int
    addresses passed to System.Buffer::MemoryCopy(void*, void*, int64, int64)
    for MEMMOVE (plan D15/D21). Two more reports come from shapes that plan
    D12 and D2 always produce, and both need the ildasm listing that pass 2
    makes (plan section 12, 'D21 and M0.b item (7) refined'):
    - 'Cannot modify an imaged based (RVA) static' is accepted only where the
      listing shows 'ldsflda valuetype '<msil>image_t' '<msil>image'' at the
      reported offset: the image copy of D12 in <Module>::.cctor
      ('ldsfld native int '<msil>data'; ldsflda ...; ldc.i4 N; cpblk'). The
      same message for any other RVA field, or without a listing, fails.
    - A function address used as a native int (D2; 'ldftn <method>'):
      PEVerify types it as a method pointer and rejects every such use, with
      a message that depends on the use ('[found <type>::<method>][expected
      Native Int] Unexpected type on the stack.' for an argument, local or
      return value; '... Non-compatible types on the stack.' in a compare;
      '[found <type>::<method>] Expected numeric type on the stack.' in
      arithmetic). Every PEVerify error in a method that contains ldftn is
      therefore a note, but only when pass 2 runs: pass 2 verifies that
      method again with every ldftn replaced, so a real error in it still
      fails there. Without --ildasm these errors fail.

Pass 2 verifies a neutralized copy (--ildasm and --ilasm). ILVerify does not
follow control flow out of a block in which it reported an error, and
PEVerify stops at the first error of most kinds in a method, so in pass 1 an
allowed error (the localloc frame of D13, a '.locals' without init, an
ldind/stind through a native int, cpblk, initblk, calli, the image ldsflda,
an ldftn) hides the errors behind it, native int/int64 mixing, a bad join and
a switch on a native int included (llvm/docs/MSIL/recon/cil-runtime.md 13).
The copy is the assembly disassembled with ildasm, rewritten so that none of
these constructs is left, and reassembled with ilasm in a private temporary
directory:

    .locals (...)          .locals init (...)
    localloc               pop; ldc.i4.0; conv.i
    cpblk                  call void '<msil-verify>cpblk'(native int, native int, int32)
    initblk                call void '<msil-verify>initblk'(native int, int32, int32)
    ldind.<t>              call <T> '<msil-verify>ldind.<t>'(native int)
    stind.<t>              call void '<msil-verify>stind.<t>'(native int, <T>)
    calli <R>(<P...>)      pop; call <R> '<msil-verify>calli.<k>'(<P...>)
    ldftn <method>         ldc.i4.0; conv.i
    ldsflda <the image>    ldc.i4.0; conv.i   (only '<msil>image', see above)
    br.s, brtrue.s, ...    br, brtrue, ... (the rewritten code is longer)
    unaligned., volatile.  nop, before a rewritten instruction

The copy also replaces each ldc.r4/ldc.r8 operand that ildasm prints in a
form ilasm cannot parse ('inf', '-inf', 'nan', '-nan(ind)', 'nan(snan)', ...;
other NaNs come out as bytes, which reassemble) with a float32(0x...) or
float64(0x...) literal: the same infinity, or the default quiet NaN of the
same sign. Verification needs only the type, so the payload is not kept.

The '<msil-verify>' stubs are added to the copy ('ldnull; throw'). They take
every address as native int, so ILVerify still rejects an int32, int64 or
float64 address (it does not convert int32 to native int implicitly), and
they check the other operands too: a cpblk or initblk size or an initblk
value that is not int32, and a stored value or calli argument of the wrong
type. A function address and the image address are native int in the copy,
as the code uses them, so the value a function relocation of D12 stores with
stind.i, the image source of cpblk and a calli target pass without an
allowance. Every replacement has the stack effect of the instruction it
replaces, so .maxstack stays valid. ILVerify and PEVerify then run on the
copy with no allowance except PEVerify's MemoryCopy message; an ldsflda of
any other RVA field stays in the copy and fails, and a calli whose signature
the rewrite cannot express (vararg, instance, explicit, function pointer
types) stays in the copy and is a note in both tools. Offsets in pass-2
messages refer to the copy; --keep-copy DIR keeps its .il and .dll. Without
--ildasm, pass 2 is skipped with a note (lit makes a missing ildasm an error
under --param msil_require_verify=1).

MSILStackCheck (in the backend) checks .maxstack and stack depths, and jitall
finds what the JIT rejects; neither checks stack types, and the JIT accepts
most type errors silently (plan section 9), so pass 2 is what catches a type
error behind an allowed construct. Only PEVerify reports native int/int64
mixing, so on Linux nothing does. Output this wrapper cannot account for (a
crash, a load failure, an error count that does not match the error lines)
fails too. Every child process (ildasm, ilasm, ILVerify, PEVerify) runs under
the timeout and memory cap of msil-run.py. An empty --peverify or --ildasm
means that tool is not available.

--dep FILE... (one or more files; repeatable; plan 5.12.4, M15) names the
files the assembly needs beside it: the assemblies it references and the
native libraries it calls. Each managed one (a PE file with a CLI header) is
a reference assembly of ILVerify ('-r FILE') in both passes, and every one is
copied next to the assembly that PEVerify checks: pass 1 then verifies a copy
of prog.dll in a private directory, pass 2 the neutralized copy, beside the
copies of the dependencies. A native library is only copied. A missing file,
or two of one name (or one named like the assembly), is a usage error.
--dep takes every argument up to the next option, so the lit composites pass
'--dep %{msil_deps} --' before the assembly; an empty list is allowed.

--roundtrip does not verify. It checks that the assembly survives the
ildasm -> ilasm round trip that Design.md 5.5 asks of the backend's output
(lit parameter msil_roundtrip, feature msil-roundtrip, the M10 round-trip
leg): --ildasm disassembles prog.dll, --ilasm reassembles that listing
unchanged into --output (an image of the same kind), and --ildasm
disassembles the result again. Both listings must be equal apart from the
lines that differ between any two builds of one module ('// MVID:' and
'// Image base:') and the RVA data labels I_<RVA>, which are renumbered in
order of first appearance (where the data lies is the image's layout); a
difference is printed as a unified diff. The only edit
to the listing is that an image layout directive whose value ildasm did not
format ('.imagebase 0x%016I64x' from the Linux ildasm 10.0.12) is dropped,
because ilasm rejects it; ilasm then uses its default, which is what the
backend's output gets too. The lit leg runs --output afterwards and compares
its transcript as usual. Use the .NET (NuGet) ildasm and ilasm: the .NET
Framework ildasm prints infinite ldc.r4/ldc.r8 operands as 'inf', which
neither the .NET Framework ilasm nor the .NET ilasm 10.0.12 reads back
(llvm/docs/MSIL/recon/cil-runtime.md 15).

Exit status: 0 if clean, 1 on a disallowed error (with --roundtrip: on a tool
failure or a listing difference), 2 on a usage error.
"""

import argparse
import difflib
import importlib.util
import os
import re
import shutil
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

# '[IL]: Error [Code]: [<where>][offset 0x...]<message>' (ILVerify) and
# '[IL]: Error: [<where>][offset 0x...]<message>' (PEVerify). <where> may
# contain brackets ('<msil>Main(string[])'), so split_location() parses it.
ILVERIFY_ERROR_RE = re.compile(r"^\[IL\]: Error \[(?P<code>\w*)\]: \[(?P<body>.*)$")
PEVERIFY_ERROR_RE = re.compile(r"^\[IL\]: Error: \[(?P<body>.*)$")
LOCATED_RE = re.compile(
    r"^(?P<where>.*?)\]\[offset (?P<offset>0x[0-9A-Fa-f]+)\](?P<msg>.*)$"
)
# Without an offset (ImportCalli): <where> ends with the method's parameter list.
UNLOCATED_RE = re.compile(r"^(?P<where>.*?\))\](?P<msg>.*)$")
# Both tools end with 'N Error(s) Verifying <file>' when they found errors.
SUMMARY_RE = re.compile(r"^(\d+) Error\(s\) Verifying ", re.M)

INSTRUCTION_CANNOT_BE_VERIFIED = re.compile(r"Instruction cannot be verified\.")
# MEMMOVE: the two native int addresses passed to
# [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64) (D15).
# Exactly this message; every other 'Unexpected type on the stack' (for
# example '[found Native Int][expected Long]') stays fatal (D21).
MEMORYCOPY_NATIVE_INT = re.compile(
    r"\[found Native Int\]\[expected unmanaged pointer\] Unexpected type on the stack\."
)

# PEVerify messages (after the '[offset 0x...]' field) that are expected in
# backend output, as full-match regular expressions.
PEVERIFY_ALLOWED = [
    # localloc (the frame, D13), cpblk and initblk (D15), calli.
    INSTRUCTION_CANNOT_BE_VERIFIED,
    # ldind.*/stind.* through a native int address (D2); an int32, int64 or
    # float64 address ('[found Int32]' ...) fails.
    re.compile(r"\[found Native Int\] Expected ByRef on the stack\."),
    # .locals without 'init' (D13).
    re.compile(
        r"initlocals must be set for verifiable methods with one or more "
        r"local variables\."
    ),
    # ldarg/ldloc of an argument or local whose own type is a pointer (void*).
    # Phase 1 never declares one (D2: pointers are native int), so nothing
    # produces it yet; kept because D21 allows ILVerify's UnmanagedPointer
    # (phase-3 typed signatures). A void* parameter of a callee such as
    # Buffer::MemoryCopy does not produce it.
    re.compile(r"Unmanaged pointers are not a verifiable type\."),
    MEMORYCOPY_NATIVE_INT,
]

# Pass 2 allows nothing else.
PEVERIFY_ALLOWED_IN_COPY = [MEMORYCOPY_NATIVE_INT]

# Pass 1 only, and only at the image ldsflda of the listing (D12): PEVerify
# reports taking the address of an RVA static at the ldsflda, whatever is
# done with it, and stops checking the method there.
RVA_STATIC = re.compile(r"Cannot modify an imaged based \(RVA\) static")
# The operand of that ldsflda as ildasm prints it, whitespace folded.
IMAGE_FIELD = "valuetype '<msil>image_t' '<msil>image'"


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


def load_allow(path):
    """Entries of an allow file: 'CODE' or 'CODE: MESSAGE', both full-match
    regular expressions; '#' starts a comment. Returns [(code, message or None)]."""
    entries = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            code, sep, message = line.partition(":")
            entries.append(
                (re.compile(code.strip()), re.compile(message.strip()) if sep else None)
            )
    return entries


def allowed(allow, code, message):
    return any(
        c.fullmatch(code) and (m is None or m.fullmatch(message)) for c, m in allow
    )


def split_location(body):
    """'<where>][offset 0x..]<message>' -> (where, offset or '', message)."""
    m = LOCATED_RE.match(body) or UNLOCATED_RE.match(body)
    if not m:
        where, _, msg = body.partition("]")
        return where, "", msg.strip()
    return m.group("where"), m.groupdict().get("offset") or "", m.group("msg").strip()


def short(where):
    # "C:\...\x.dll : <Module>::f(int32)" -> "<Module>::f(int32)"
    return where.split(" : ", 1)[-1]


def method_name(where):
    # "<Module>::f(int32)" or "<Module>::f" -> "f"
    return short(where).split("(", 1)[0].rsplit("::", 1)[-1].strip("'")


def method_key(where):
    # "<Module>::f(int32)" or "<Module>::f" -> ("<Module>", "f")
    typ, _, name = short(where).split("(", 1)[0].rpartition("::")
    return typ.strip("'"), name.strip("'")


def run(cmd, args, cwd=None):
    res = msil_run.run_limited(
        cmd, cwd, dict(os.environ), args.timeout, args.mem_limit_mb
    )
    output = msil_run.text_of(res.stdout + res.stderr)
    tool = os.path.basename(cmd[0])
    if res.timed_out:
        return res, output, "%s did not finish within %g s" % (tool, args.timeout)
    if res.memory_limit:
        return res, output, "%s hit the %d MiB memory cap" % (tool, args.mem_limit_mb)
    return res, output, None


def check_counts(name, res, output, nerrors, ok_status):
    """Return a failure message if the output does not account for the status."""
    summaries = SUMMARY_RE.findall(output)
    if res.returncode == 0 and nerrors == 0 and not summaries:
        return None
    if res.returncode == ok_status and summaries and int(summaries[-1]) == nerrors:
        return None
    return (
        "%s exited with status %s and reported errors this wrapper cannot attribute "
        "(%d understood):\n%s"
        % (name, msil_run.describe_exit(res.returncode), nerrors, output)
    )


def is_managed(path):
    """Whether path is a PE file with a CLI header (data directory 14), that
    is, an assembly or a module rather than a native library."""
    try:
        with open(path, "rb") as f:
            data = f.read(4096)
    except OSError:
        return False
    if data[:2] != b"MZ" or len(data) < 0x40:
        return False
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    opt = pe + 24
    if data[pe : pe + 4] != b"PE\0\0" or len(data) < opt + 2:
        return False
    magic = struct.unpack_from("<H", data, opt)[0]
    count_at, dirs_at = {0x10B: (92, 96), 0x20B: (108, 112)}.get(magic, (None, None))
    if count_at is None or len(data) < opt + dirs_at + 15 * 8:
        return False
    if struct.unpack_from("<I", data, opt + count_at)[0] <= 14:
        return False
    rva, size = struct.unpack_from("<II", data, opt + dirs_at + 14 * 8)
    return rva != 0 and size != 0


def run_ilverify(args, assembly, allow, label, calli_methods=None):
    """ILVerify one assembly; returns (failures, notes). calli_methods: names of
    the methods that still contain calli (pass 2), or None (pass 1)."""
    cmd = [args.ilverify, assembly]
    for d in args.runtime_dir:
        cmd += ["-r", os.path.join(d, "*.dll")]
    for dep in args.managed_deps:
        cmd += ["-r", os.path.abspath(dep)]
    res, output, limit = run(cmd, args)
    if limit:
        return ["%s: %s" % (label, limit)], []
    bad, notes, nerrors = [], [], 0
    for line in output.splitlines():
        m = ILVERIFY_ERROR_RE.match(line.strip())
        if not m:
            continue
        nerrors += 1
        code = m.group("code")
        where, offset, msg = split_location(m.group("body"))
        if (
            code == ""
            and "ImportCalli not implemented" in msg
            and (calli_methods is None or method_name(where) in calli_methods)
        ):
            notes.append(
                "%s: note: %s contains calli; the rest of the method is not "
                "verified" % (label, short(where))
            )
        elif not allowed(allow, code, msg):
            bad.append(
                "%s: [%s] %s%s %s"
                % (
                    label,
                    code or "?",
                    short(where),
                    "[offset %s]" % offset if offset else "",
                    msg,
                )
            )
    # ILVerify 10.0.12 exits with 2 when it reports verification errors.
    problem = check_counts("ilverify", res, output, nerrors, 2)
    if problem:
        bad.append("%s: %s" % (label, problem))
    return bad, notes


def run_peverify(args, assembly, allowed_msgs, label, calli_methods=None, listing=None):
    """PEVerify one assembly; returns (failures, notes). listing: the ildasm
    Listing of the assembly when pass 2 runs (pass 1), else None."""
    res, output, limit = run([args.peverify, "/nologo", "/il", assembly], args)
    if limit:
        return ["%s: %s" % (label, limit)], []
    bad, notes, nerrors = [], [], 0
    for line in output.splitlines():
        m = PEVERIFY_ERROR_RE.match(line.strip())
        if not m:
            continue
        nerrors += 1
        where, offset, msg = split_location(m.group("body"))
        if any(p.fullmatch(msg) for p in allowed_msgs):
            continue
        if listing is not None:
            if RVA_STATIC.fullmatch(msg) and listing.instruction_at(where, offset) == (
                "ldsflda",
                IMAGE_FIELD,
            ):
                continue
            if listing.contains(where, "ldftn"):
                notes.append(
                    "%s: note: %s%s %s (the method contains ldftn; the "
                    "neutralized copy verifies it again)"
                    % (
                        label,
                        short(where),
                        "[offset %s]" % offset if offset else "",
                        msg,
                    )
                )
                continue
        if (
            calli_methods
            and INSTRUCTION_CANNOT_BE_VERIFIED.fullmatch(msg)
            and method_name(where) in calli_methods
        ):
            notes.append(
                "%s: note: %s contains calli; the rest of the method is not "
                "verified" % (label, short(where))
            )
            continue
        bad.append(
            "%s: %s%s %s"
            % (label, short(where), "[offset %s]" % offset if offset else "", msg)
        )
    problem = check_counts("peverify", res, output, nerrors, 1)
    if problem:
        bad.append("%s: %s" % (label, problem))
    return bad, notes


# ---------------------------------------------------------------- pass 2

# ildasm prints every instruction as '  IL_xxxx:  opcode  operands'.
INSTR_RE = re.compile(
    r"^(?P<lead>\s*IL_[0-9A-Fa-f]+:\s*)(?P<op>[A-Za-z][A-Za-z0-9.]*)(?P<rest>.*)$"
)
IL_LABEL_RE = re.compile(r"IL_([0-9A-Fa-f]+)")
# A quoted name, which may contain parentheses ('<msil-verify>' stubs, IR
# names): skipped when counting the parentheses of an operand.
QUOTED_RE = re.compile(r"'(?:[^'\\]|\\.)*'")
LOCALS_RE = re.compile(r"^(?P<lead>\s*\.locals)\s+\(")
# Image layout directives: verification does not depend on them, and the
# Linux ildasm 10.0.12 prints '.imagebase 0x%016I64x' (a Windows format
# string), which ilasm rejects. The copy uses ilasm's defaults.
IMAGE_LAYOUT_RE = re.compile(r"^\s*\.(?:imagebase|stackreserve|file\s+alignment)\b")
END_METHOD_RE = re.compile(
    r"^\s*\}\s*// end of (?P<glob>global )?method (?P<name>.*?)\s*$"
)
PREFIXES = {"unaligned.", "volatile.", "tail.", "readonly.", "constrained.", "no."}
SHORT_BRANCHES = {
    "br.s",
    "brfalse.s",
    "brtrue.s",
    "brnull.s",
    "brzero.s",
    "brinst.s",
    "beq.s",
    "bge.s",
    "bgt.s",
    "ble.s",
    "blt.s",
    "bne.un.s",
    "bge.un.s",
    "bgt.un.s",
    "ble.un.s",
    "blt.un.s",
    "leave.s",
}
# Stack type of the value an ldind.<t> loads or an stind.<t> stores.
IND_TYPES = {
    "i1": "int32",
    "u1": "int32",
    "i2": "int32",
    "u2": "int32",
    "i4": "int32",
    "u4": "int32",
    "i8": "int64",
    "u8": "int64",
    "i": "native int",
    "r4": "float32",
    "r8": "float64",
    "ref": "object",
}
# A managed calli signature '<ret>(<params>)', optionally 'unmanaged <conv>':
# the arguments are checked the same way.
CALLI_SIG_RE = re.compile(
    r"^(?:default\s+|unmanaged\s+(?:cdecl|stdcall|thiscall|fastcall)\s+)?"
    r"(?P<ret>[^()]+?)\s*\((?P<params>[^()]*)\)$"
)
CALLI_UNSUPPORTED_RE = re.compile(r"\b(?:vararg|instance|explicit)\b|\.\.\.")
# An ldc.r4/ldc.r8 operand that ildasm prints with the C runtime's text for
# an infinity or a NaN ('inf', '-inf', 'nan', '-nan', '-nan(ind)',
# 'nan(snan)', '-nan(snan)': the .NET Framework 4.8 ildasm), which ilasm
# cannot parse.
NONFINITE_RE = re.compile(
    r"(?P<sign>[-+]?)(?:(?P<inf>inf(?:inity)?)|nan(?:\(\w*\))?)", re.IGNORECASE
)
# Opcode -> (literal type, hex digits, infinity, default quiet NaN, sign bit).
NONFINITE_BITS = {
    "ldc.r4": ("float32", 8, 0x7F800000, 0x7FC00000, 0x80000000),
    "ldc.r8": (
        "float64",
        16,
        0x7FF0000000000000,
        0x7FF8000000000000,
        0x8000000000000000,
    ),
}


def nonfinite_literal(op, operand):
    """The ilasm literal for the infinity or NaN operand (NONFINITE_RE) of
    op (ldc.r4 or ldc.r8): the same infinity, or the default quiet NaN of
    the same sign (the copy needs only a constant of the right type)."""
    m = NONFINITE_RE.fullmatch(operand)
    typ, digits, inf, nan, sign = NONFINITE_BITS[op]
    bits = (inf if m.group("inf") else nan) | (sign if m.group("sign") == "-" else 0)
    return "%s(0x%0*X)" % (typ, digits, bits)


class Listing:
    """The instructions of every method of an ildasm listing, by offset.

    Pass 1 uses it to bind a PEVerify message to the instruction it reports.
    Methods are keyed by (type, name), the most a PEVerify location says; an
    overloaded name answers only what all of its bodies agree on."""

    def __init__(self, text):
        self.bodies = {}  # (type, name) -> [{offset: (opcode, operand)}]
        body = {}
        for line in text.splitlines():
            m = INSTR_RE.match(line)
            if m:
                offset = int(IL_LABEL_RE.search(m.group("lead")).group(1), 16)
                body[offset] = (m.group("op"), " ".join(m.group("rest").split()))
                continue
            em = END_METHOD_RE.match(line)
            if em:
                name = em.group("name")
                key = (
                    ("<Module>", name.strip("'"))
                    if em.group("glob")
                    else method_key(name)
                )
                self.bodies.setdefault(key, []).append(body)
                body = {}

    def instruction_at(self, where, offset):
        """(opcode, operand) at the PEVerify offset '0x...' of the method at
        where, or None when it is unknown or the bodies of the name disagree."""
        if not offset:
            return None
        found = {b.get(int(offset, 16)) for b in self.bodies.get(method_key(where), [])}
        return found.pop() if len(found) == 1 else None

    def contains(self, where, opcode):
        """Whether every body of the method at where contains opcode."""
        bodies = self.bodies.get(method_key(where), [])
        return bool(bodies) and all(
            any(op == opcode for op, _ in b.values()) for b in bodies
        )


def paren_depth(text):
    """Opening minus closing parentheses outside quoted names."""
    text = QUOTED_RE.sub("", text)
    return text.count("(") - text.count(")")


def neutralize(text):
    """Rewrite ildasm output as described in the module docstring.

    Returns (new text, {construct: count}, names of methods that still
    contain calli)."""
    out, stubs, stats = [], {}, {}
    calli_sigs = {}
    pending_prefixes = []  # (lead, original line) of prefixes not yet emitted
    method_has_calli = False
    calli_methods = set()

    def stub(name, ret, params):
        stubs.setdefault(name, (ret, params))
        return "call %s '<msil-verify>%s'(%s)" % (ret, name, params)

    def count(what):
        stats[what] = stats.get(what, 0) + 1

    lines = text.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        i += 1
        m = INSTR_RE.match(line)
        if not m:
            out.extend(l for _, l in pending_prefixes)
            pending_prefixes = []
            if IMAGE_LAYOUT_RE.match(line):
                continue
            lm = LOCALS_RE.match(line)
            if lm:
                line = lm.group("lead") + " init (" + line[lm.end() :]
                count(".locals")
            em = END_METHOD_RE.match(line)
            if em:
                if method_has_calli:
                    calli_methods.add(em.group("name").rsplit("::", 1)[-1].strip("'"))
                method_has_calli = False
            out.append(line)
            continue
        lead, op, rest = m.group("lead"), m.group("op"), m.group("rest").strip()
        if op in PREFIXES:
            pending_prefixes.append((lead, line))
            continue
        indent = " " * len(lead)
        new, what = None, op
        if op == "localloc":
            new = ["pop", "ldc.i4.0", "conv.i"]
        elif op == "cpblk":
            new = [stub(op, "void", "native int, native int, int32")]
        elif op == "initblk":
            new = [stub(op, "void", "native int, int32, int32")]
        elif op == "ldftn":
            # A function address used as a native int (D2). ildasm wraps a
            # long parameter list over several lines; drop them too.
            depth = paren_depth(rest)
            while depth > 0 and i < len(lines):
                depth += paren_depth(lines[i])
                i += 1
            new = ["ldc.i4.0", "conv.i"]
        elif op == "ldsflda" and " ".join(rest.split()) == IMAGE_FIELD:
            # The image copy of D12; any other RVA field stays and fails.
            new, what = ["ldc.i4.0", "conv.i"], "ldsflda '<msil>image'"
        elif op.startswith("ldind.") and op[6:] in IND_TYPES:
            new = [stub(op, IND_TYPES[op[6:]], "native int")]
        elif op.startswith("stind.") and op[6:] in IND_TYPES:
            new = [stub(op, "void", "native int, " + IND_TYPES[op[6:]])]
        elif op == "calli":
            sig = CALLI_SIG_RE.match(rest)
            if sig and not CALLI_UNSUPPORTED_RE.search(rest):
                ret, params = sig.group("ret").strip(), sig.group("params").strip()
                key = (ret, params)
                if key not in calli_sigs:
                    calli_sigs[key] = "calli.%d" % len(calli_sigs)
                new = ["pop", stub(calli_sigs[key], ret, params)]
            else:
                method_has_calli = True
                count("calli kept")
        elif op in SHORT_BRANCHES:
            out.extend(l for _, l in pending_prefixes)
            pending_prefixes = []
            out.append(lead + op[:-2] + " " + rest)
            count("short branch")
            continue
        elif op in NONFINITE_BITS and NONFINITE_RE.fullmatch(rest):
            # Not a neutralized construct: a constant ilasm could not read.
            out.extend(l for _, l in pending_prefixes)
            pending_prefixes = []
            out.append(lead + op + " " + nonfinite_literal(op, rest))
            count("%s %s" % (op, rest))
            continue
        if new is None:
            out.extend(l for _, l in pending_prefixes)
            out.append(line)
        else:
            out.extend(plead + "nop" for plead, _ in pending_prefixes)
            out.append(lead + new[0])
            out.extend(indent + n for n in new[1:])
            count(what)
        pending_prefixes = []
    out.extend(l for _, l in pending_prefixes)

    if stubs:
        out.append("")
        out.append(
            "// msil-verify.py: stubs that replace the neutralized instructions."
        )
        for name, (ret, params) in sorted(stubs.items()):
            out.append(
                ".method assembly static %s '<msil-verify>%s'(%s) cil managed"
                % (ret, name, params)
            )
            out.append("{")
            out.append("  .maxstack 1")
            out.append("  ldnull")
            out.append("  throw")
            out.append("}")
    return "\n".join(out) + "\n", stats, calli_methods


def ilasm_flags_for(path):
    """ilasm options that rebuild an image of the same kind as the PE file."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"MZ" or len(data) < 0x40:
        raise ValueError("%s is not a PE file" % path)
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe : pe + 4] != b"PE\0\0" or len(data) < pe + 26:
        raise ValueError("%s is not a PE file" % path)
    machine, characteristics = (
        struct.unpack_from("<H", data, pe + 4)[0],
        struct.unpack_from("<H", data, pe + 22)[0],
    )
    magic = struct.unpack_from("<H", data, pe + 24)[0]
    flags = ["-dll" if characteristics & 0x2000 else "-exe"]
    if magic == 0x20B:  # PE32+
        flags.append("-pe64")
    flags += {0x8664: ["-x64"], 0xAA64: ["-arm64"]}.get(machine, [])
    return flags


COPY_LABEL = "neutralized copy"


def disassemble(args, private, assembly=None, label=COPY_LABEL):
    """ildasm assembly (default: the assembly under test) into the directory
    private. Returns (listing text, None), or (None, failure message)."""
    assembly = assembly or args.assembly
    stem = os.path.splitext(os.path.basename(assembly))[0]
    disasm = os.path.join(private, stem + ".ildasm.il")
    cmd = [args.ildasm, "-utf8", "-out=" + disasm, os.path.abspath(assembly)]
    # The .NET Framework ildasm shows a progress window unless -nobar is
    # given; the .NET (NuGet) ildasm rejects the option.
    res, output, limit = run(cmd[:1] + ["-nobar"] + cmd[1:], args, cwd=private)
    if not limit and res.returncode != 0 and "-nobar" in output:
        res, output, limit = run(cmd, args, cwd=private)
    if limit or res.returncode != 0 or not os.path.isfile(disasm):
        return None, "%s: ildasm failed (status %s): %s\n%s" % (
            label,
            msil_run.describe_exit(res.returncode),
            limit or "",
            output,
        )
    with open(disasm, encoding="utf-8-sig") as f:
        return f.read(), None


def run_copy_pass(args, private, listing_text):
    """Pass 2: verify a neutralized copy of listing_text (the ildasm listing),
    built in the directory private. Returns (failures, notes)."""
    label = COPY_LABEL
    try:
        flags = ilasm_flags_for(args.assembly)
    except (OSError, ValueError, struct.error) as e:
        return ["%s: %s" % (label, e)], []
    stem = os.path.splitext(os.path.basename(args.assembly))[0]
    text, stats, calli_methods = neutralize(listing_text)
    copy_il = os.path.join(private, stem + ".il")
    copy_dll = os.path.join(private, stem + (".dll" if "-dll" in flags else ".exe"))
    with open(copy_il, "w", encoding="utf-8-sig", newline="\n") as f:
        f.write(text)
    res, output, limit = run(
        [args.ilasm, "-nologo", "-quiet"] + flags + ["-output=" + copy_dll, copy_il],
        args,
        cwd=private,
    )
    if limit or res.returncode != 0 or not os.path.isfile(copy_dll):
        return [
            "%s: ilasm could not reassemble the rewritten disassembly (status %s; "
            "rerun with --keep-copy DIR to see it): %s\n%s"
            % (label, msil_run.describe_exit(res.returncode), limit or "", output)
        ], []
    if args.verbose:
        print(
            "msil-verify: %s: %s"
            % (
                label,
                ", ".join("%d x %s" % (n, what) for what, n in sorted(stats.items()))
                or "unchanged",
            )
        )
    # PEVerify resolves the references of the copy from its directory.
    msil_run.copy_deps(
        args.dep,
        [],
        private,
        [os.path.basename(p) for p in (copy_il, copy_dll)] + [stem + ".ildasm.il"],
    )
    bad, notes = run_ilverify(
        args, copy_dll, [], "ilverify (%s)" % label, calli_methods
    )
    if args.peverify:
        pbad, pnotes = run_peverify(
            args,
            copy_dll,
            PEVERIFY_ALLOWED_IN_COPY,
            "peverify (%s)" % label,
            calli_methods,
        )
        bad += pbad
        notes += pnotes
    if bad and not args.keep_copy:
        notes.append(
            "msil-verify: note: offsets in '%s' messages refer to the copy; "
            "rerun with --keep-copy DIR to see it" % label
        )
    return bad, notes


ROUNDTRIP_LABEL = "round trip"
# An image layout directive whose value ildasm did not format: the Linux
# ildasm 10.0.12 prints '.imagebase 0x%016I64x' and '.stackreserve
# 0x%016I64x' (a Windows format string), which ilasm rejects.
UNFORMATTED_LAYOUT_RE = re.compile(
    r"^\s*\.(?:imagebase|stackreserve|file\s+alignment)\b"
    r"(?!\s+0x[0-9A-Fa-f]+\s*(?://.*)?$)"
)
# Listing lines that differ between two builds of the same module.
BUILD_SPECIFIC_RE = re.compile(r"^\s*//\s*(?:MVID|Image base):")
# ildasm names RVA data after its RVA ('.data cil I_00002060 = bytearray (',
# '.field ... at I_00002060'). Where the data lies is the image's layout: ilasm
# puts it first in .text, writePEImage (plan 5.7.7) last, so the labels are
# renumbered in the order of their first appearance before the listings are
# compared. Only these two forms are labels: a field or method named like one
# is left alone, though ildasm prints its name unquoted ('.field public static
# int32 I_0000ABCD', 'ldsfld int32 I_0000ABCD'), as
# msil/test/harness/legs/roundtrip-data-labels.test checks.
DATA_LABEL_RE = re.compile(
    r"^(\s*\.data\s+(?:\w+\s+)?|\s*\.field\b.*\sat\s+)(I_[0-9A-F]{8})(?=\s*(?:=|$))"
)


def roundtrip(args, private):
    """--roundtrip: ildasm args.assembly, ilasm the listing into args.output,
    ildasm that, and compare the listings. Returns a list of failures."""
    label = ROUNDTRIP_LABEL
    try:
        flags = ilasm_flags_for(args.assembly)
    except (OSError, ValueError, struct.error) as e:
        return ["%s: %s" % (label, e)]
    first, failure = disassemble(args, private, args.assembly, label)
    if failure:
        return [failure]
    stem = os.path.splitext(os.path.basename(args.assembly))[0]
    source = os.path.join(private, stem + ".roundtrip.il")
    with open(source, "w", encoding="utf-8-sig", newline="\n") as f:
        f.write(
            "".join(
                line + "\n"
                for line in first.splitlines()
                if not UNFORMATTED_LAYOUT_RE.match(line)
            )
        )
    output = os.path.abspath(args.output)
    if os.path.exists(output):
        os.remove(output)  # never compare against a stale image
    os.makedirs(os.path.dirname(output), exist_ok=True)
    res, text, limit = run(
        [args.ilasm, "-nologo", "-quiet"] + flags + ["-output=" + output, source],
        args,
        cwd=private,
    )
    if limit or res.returncode != 0 or not os.path.isfile(output):
        return [
            "%s: ilasm could not reassemble the disassembly of %s (status %s; "
            "rerun with --keep-copy DIR to see it): %s\n%s"
            % (
                label,
                os.path.basename(args.assembly),
                msil_run.describe_exit(res.returncode),
                limit or "",
                text,
            )
        ]
    second, failure = disassemble(args, private, output, label)
    if failure:
        return [failure]

    def comparable(listing):
        labels = {}

        def renumber(m):
            label = labels.setdefault(m.group(2), "I_%d" % len(labels))
            return m.group(1) + label

        return [
            DATA_LABEL_RE.sub(renumber, l)
            for l in listing.splitlines(True)
            if not BUILD_SPECIFIC_RE.match(l)
        ]

    a, b = comparable(first), comparable(second)
    if a != b:
        diff = "".join(
            difflib.unified_diff(
                a,
                b,
                "ildasm %s" % os.path.basename(args.assembly),
                "ildasm %s" % os.path.basename(output),
            )
        )
        return [
            "%s: the disassembly of the reassembled %s differs from that of %s:\n%s"
            % (label, os.path.basename(output), os.path.basename(args.assembly), diff)
        ]
    if args.verbose:
        print(
            "msil-verify: %s: %s -> %s, %d listing lines identical"
            % (label, args.assembly, args.output, len(a))
        )
    return []


def run_peverify_with_deps(args, listing):
    """Pass 1 of PEVerify: on the assembly itself, or with --dep on a copy in
    a private directory beside copies of the dependencies, where PEVerify
    resolves its references."""
    if not args.dep:
        return run_peverify(
            args, args.assembly, PEVERIFY_ALLOWED, "peverify", listing=listing
        )
    beside = tempfile.mkdtemp(prefix="msil-verify-deps-")
    try:
        name = os.path.basename(args.assembly)
        shutil.copy2(args.assembly, os.path.join(beside, name))
        msil_run.copy_deps(args.dep, [], beside, [name])
        return run_peverify(
            args,
            os.path.join(beside, name),
            PEVERIFY_ALLOWED,
            "peverify",
            listing=listing,
        )
    finally:
        msil_run.remove_tree(beside)


def verify(args, allow, private):
    """Both passes; private is the directory of pass 2 (None: no pass 2).
    Returns (failures, notes)."""
    # Pass 1 needs the listing of pass 2 (the image ldsflda, the methods
    # that contain ldftn), so ildasm runs first.
    listing_text, failure = disassemble(args, private) if private else (None, None)
    listing = Listing(listing_text) if listing_text is not None else None
    bad, notes = run_ilverify(args, args.assembly, allow, "ilverify")
    if args.peverify:
        pbad, pnotes = run_peverify_with_deps(args, listing)
        bad += pbad
        notes += pnotes
    if not private:
        notes.append(
            "msil-verify: note: no --ildasm; the neutralized pass did not run, "
            "so errors behind an allowed one are not checked"
        )
        return bad, notes
    if failure:
        return bad + [failure], notes
    # Pass 2 replaces the notes of pass 1 with its own: it verifies every
    # calli it can rewrite and every method that contains ldftn again.
    if args.verbose:
        for n in notes:
            print(n)
    cbad, notes = run_copy_pass(args, private, listing_text)
    return bad + cbad, notes


def keep_copy(args, private):
    if args.keep_copy:
        os.makedirs(args.keep_copy, exist_ok=True)
        for name in os.listdir(private):
            shutil.copy2(os.path.join(private, name), args.keep_copy)


def main_roundtrip(p, args):
    if not (args.ildasm and args.ilasm and args.output):
        p.error("--roundtrip needs --ildasm, --ilasm and --output")
    if args.skip:
        p.error("--roundtrip and --skip exclude each other")
    if not os.path.isfile(args.assembly):
        sys.stderr.write("msil-verify: error: %s not found\n" % args.assembly)
        return 2
    private = tempfile.mkdtemp(prefix="msil-roundtrip-")
    try:
        bad = roundtrip(args, private)
    except OSError as e:
        sys.stderr.write("msil-verify: error: %s\n" % e)
        return 2
    finally:
        keep_copy(args, private)
        msil_run.remove_tree(private)
    for b in bad:
        print(b, file=sys.stderr)
    return 1 if bad else 0


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument(
        "--ilverify", help="ILVerify executable (dotnet tool dotnet-ilverify)"
    )
    p.add_argument(
        "--runtime-dir",
        action="append",
        default=[],
        help="directory of reference assemblies, e.g. a Microsoft.NETCore.App "
        "runtime directory (repeatable)",
    )
    p.add_argument(
        "--allow",
        default=os.path.join(HERE, "ilverify-allowed.txt"),
        help="file of allowed ILVerify errors, 'CODE' or 'CODE: MESSAGE' "
        "regexes (default: ilverify-allowed.txt next to this script)",
    )
    p.add_argument("--peverify", help="also run PEVerify (Windows); empty: no PEVerify")
    p.add_argument(
        "--ildasm",
        help="ildasm for the neutralized pass (pass 2) or --roundtrip; empty: "
        "skip pass 2",
    )
    p.add_argument(
        "--ilasm", help="ilasm that reassembles the neutralized copy or --roundtrip"
    )
    p.add_argument(
        "--keep-copy",
        metavar="DIR",
        help="keep the neutralized copy (.il and the reassembled image), or the "
        "listings of --roundtrip, in DIR",
    )
    p.add_argument("--skip", action="store_true", help="no verifier available; succeed")
    p.add_argument(
        "--roundtrip",
        action="store_true",
        help="do not verify: check the ildasm -> ilasm round trip of the assembly "
        "(needs --ildasm, --ilasm and --output)",
    )
    p.add_argument(
        "--output",
        metavar="FILE",
        help="with --roundtrip: the reassembled image",
    )
    p.add_argument(
        "--timeout",
        type=float,
        default=msil_run.DEFAULT_TIMEOUT,
        help="wall-clock limit per child process in seconds (default %g)"
        % msil_run.DEFAULT_TIMEOUT,
    )
    p.add_argument(
        "--mem-limit-mb",
        type=int,
        default=msil_run.DEFAULT_MEM_LIMIT_MB,
        help="memory cap per child process in MiB (default %d)"
        % msil_run.DEFAULT_MEM_LIMIT_MB,
    )
    p.add_argument("-v", "--verbose", action="store_true")
    p.add_argument(
        "--dep",
        nargs="*",
        action="extend",
        default=[],
        metavar="FILE",
        help="files the assembly needs beside it: managed ones become ILVerify "
        "references, all are copied next to it for PEVerify (repeatable; takes "
        "every argument up to the next option, so end with '--')",
    )
    p.add_argument("assembly")
    args = p.parse_args()
    try:
        msil_run.check_deps(args.dep, [])
    except msil_run.ConfigError as e:
        p.error(str(e))
    names = {}
    for f in args.dep + [args.assembly]:
        key = os.path.normcase(os.path.basename(f))
        if key in names:
            p.error(
                "dependency %s and %s have the same name %s"
                % (f, names[key], os.path.basename(f))
            )
        names[key] = f
    args.managed_deps = [d for d in args.dep if is_managed(d)]

    if args.roundtrip:
        return main_roundtrip(p, args)
    if args.output:
        p.error("--output needs --roundtrip")
    if args.skip:
        print("msil-verify: skipped (no ILVerify configured)")
        return 0
    if not args.ilverify:
        p.error("--ilverify is required (or --skip)")
    if args.ildasm and not args.ilasm:
        p.error("--ildasm needs --ilasm")
    if not os.path.isfile(args.assembly):
        sys.stderr.write("msil-verify: error: %s not found\n" % args.assembly)
        return 2
    try:
        allow = load_allow(args.allow)
    except (OSError, re.error) as e:
        sys.stderr.write("msil-verify: error: cannot read %s: %s\n" % (args.allow, e))
        return 2

    private = tempfile.mkdtemp(prefix="msil-verify-") if args.ildasm else None
    try:
        bad, notes = verify(args, allow, private)
    except (OSError, msil_run.ConfigError) as e:
        sys.stderr.write("msil-verify: error: %s\n" % e)
        return 2
    finally:
        if private:
            keep_copy(args, private)
            msil_run.remove_tree(private)
    for n in notes:
        print(n)
    sys.stdout.flush()
    for b in bad:
        print(b, file=sys.stderr)
    if bad:
        print(
            "msil-verify: %d disallowed verifier error(s) in %s"
            % (len(bad), os.path.basename(args.assembly)),
            file=sys.stderr,
        )
        return 1
    if args.verbose:
        print("msil-verify: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())

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
                   [--peverify PATH] [--skip-peverify REASON]
                   [--ildasm PATH --ilasm PATH]
                   [--md-baseline FILE [--equiv-skip FILE --test PATH
                   [--triple T] [--level L]]] [--keep-copy DIR] [--timeout S]
                   [--mem-limit-mb N] [-v] [--dep-dir DIR]... [--also-verify-deps]
                   [--dep FILE...]... [--] prog.dll
    msil-verify.py --skip [--dep-dir DIR]... [--also-verify-deps] [--dep FILE...]
                   [--] prog.dll                         (no verifier: a no-op)
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
      listing shows 'ldsflda valuetype '<msil>image_t' '<msil>image'' or
      'ldsflda valuetype '<msil>relocs_t' '<msil>relocs'' at the reported
      offset: the image copy of D12 in <Module>::.cctor ('ldsfld native int
      '<msil>data'; ldsflda ...; ldc.i4 N; cpblk'), and the relocation table
      that the loop of plan 5.9.10 reads there (above 32 relocations: '-S' at
      the default threshold and msil-ld's output). A -shared library copies
      the image in '<msil>Data'::.cctor, its first initialization phase
      (plan 5.9.15, M22; the fields stay in <Module>), and the same check
      accepts the message there, at the same ldsfldas (plan section 12, the
      M22 row 'D21 refined'). The same message for any other RVA field, or
      without a listing, fails.
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
ldind/stind through a native int, cpblk, initblk, calli, the image or
relocation-table ldsflda, an ldftn) hides the errors behind it, native
int/int64 mixing, a bad join and
a switch on a native int included (llvm/docs/MSIL/recon/cil-runtime.md 13).
The copy is the assembly disassembled with ildasm, rewritten so that none of
these constructs is left, and reassembled with ilasm in a private temporary
directory:

    .locals (...)          .locals init (...)
    localloc               pop; ldc.i4.0; conv.i
    cpblk                  call void '<msil-verify>'::'cpblk'(native int, native int, int32)
    initblk                call void '<msil-verify>'::'initblk'(native int, int32, int32)
    ldind.<t>              call <T> '<msil-verify>'::'ldind.<t>'(native int)
    stind.<t>              call void '<msil-verify>'::'stind.<t>'(native int, <T>)
    calli <R>(<P...>)      pop; call <R> '<msil-verify>'::'calli.<k>'(<P...>)
    ldftn <method>         ldc.i4.0; conv.i
    ldsflda <the image>    ldc.i4.0; conv.i   (only '<msil>image', see above;
                                               in any method, '<msil>Data'::
                                               .cctor included)
    ldsflda <the relocs>   ldc.i4.0; conv.i   (only '<msil>relocs', see above)
    br.s, brtrue.s, ...    br, brtrue, ... (the rewritten code is longer)
    unaligned., volatile.  nop, before a rewritten instruction

The copy also replaces each ldc.r4/ldc.r8 operand that ildasm prints in a
form ilasm cannot parse ('inf', '-inf', 'nan', '-nan(ind)', 'nan(snan)', ...;
other NaNs come out as bytes, which reassemble) with a float32(0x...) or
float64(0x...) literal: the same infinity, or the default quiet NaN of the
same sign. Verification needs only the type, so the payload is not kept.

The stubs ('ldnull; throw') are added to the copy as static methods of a
type of their own, the private abstract sealed class '<msil-verify>' (based
on the listing's System.Object), never to <Module>: msil-ld fills <Module> up
to the method limit of a type before it spills members (plan 5.9.12), and a
few more methods would make the copy fail to load. They take
every address as native int, so ILVerify still rejects an int32, int64 or
float64 address (it does not convert int32 to native int implicitly), and
they check the other operands too: a cpblk or initblk size or an initblk
value that is not int32, and a stored value or calli argument of the wrong
type. A function address and the image and relocation-table addresses are
native int in the copy, as the code uses them, so the value a function
relocation of D12 stores with stind.i, the image source of cpblk, the
entries the relocation loop reads and a calli target pass without an
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

--dep-dir DIR (repeatable; plan 5.12.4, M22) adds every file directly in DIR
as if each were given with --dep: the managed ones are ILVerify references
in both passes and every one is copied next to the assembly for PEVerify.
The lit composites pass '--dep-dir <build directory>/shlib', the library
assemblies of %{msil_shlibs}; a DIR that does not exist, or is empty, adds
nothing. --also-verify-deps then verifies each managed file of the --dep-dir
directories too, after the assembly, with the same options (both passes,
the allowances above), the other files of those directories and the --dep
files as its dependencies, and the messages of each prefixed with its file
name; O6 (--md-baseline) concerns the assembly only. A line
'msil-verify: also verified <files>' names them.

--md-baseline FILE (M20, plan 5.12.4 and 5.12.6: the oracle O6) also runs
PEVerify '/nologo /md /hresult' on the assembly and on FILE, the image that
ilasm built from the same program's -S text, and fails unless every
metadata message of the assembly ('[MD](<hresult>): <text>') is one of
FILE's. The two writers number their rows differently, so each
'[token:0x<8>]' in a message is replaced by the row it designates before
the comparison: '<table> '<name>'' for a row with a name (a Field, MethodDef
or Param with its owner, '<table> '<owner>'::'<name>''), else
'<table>[<row>]'; the image's own path becomes '<image>'. msil-ld's output
is the image that runs (the --gc-sections link), so it may lack members the
baseline has, never the reverse. Without PEVerify (--peverify empty, every
host but Windows) O6 is skipped with a note. Pass 1's PEVerify runs with
'/il' and never prints the metadata messages.

--equiv-skip FILE --test PATH [--triple T] [--level L] (with --md-baseline;
both or neither of the first two; plan section 12, 'M21.c extended'): when
FILE (msil/utils/equiv-allowed.txt) has a
'skip O6[@<triple>][:<level>] <path> <reason>' line that covers PATH (lit's
%s) for this build's triple and level, O6 prints 'O6 skipped: <reason>' and
does not run; both passes still do. The lines are matched exactly as
msil-equiv.py matches its O3 and O4 lines (the same parser: a repository-
relative path or a directory above it ending in '/'; a line that names a
triple or a level covers only that triple or level), so O6 is skipped only
for a test that names its own difference, never weakened for another one.
They name the images in which msil-ld has, by design, members that the -S
build lacks (plan 5.8.10: the address fields of ALIAS symbols and the zero
fields of UNDEFINED WEAK ones), which PEVerify /md reports in an environment
where it reports every field.

--skip-peverify REASON (M21, plan 5.11.10) leaves PEVerify out of both passes
and out of O6, and prints 'PEVerify skipped: REASON' (with --md-baseline
also 'msil-verify: note: O6 skipped: PEVerify skipped: REASON'), whatever
--peverify says. PEVerify cannot load an image bound to .NET 8 or later
reference assemblies (llvm/docs/MSIL/recon/p2/bcl-binding.md 3.5): it stops
with 'The system cannot find the file specified' after metadata errors and
verifies no IL. lit's reference legs refs-net8 and refs-net10 (and
msil_references=<the path of a .NET pack>, whose System.Runtime gives <n>)
pass '--skip-peverify "bound to .NET <n> references"' and the reference pack
as --runtime-dir, so ILVerify checks the image against the assemblies it was
bound to; the same programs' PEVerify runs in every other leg. REASON must
not be empty.

--roundtrip does not verify. It checks that the assembly survives the
ildasm -> ilasm round trip that Design.md 5.5 asks of the backend's output
(lit parameter msil_roundtrip, feature msil-roundtrip, the M10 round-trip
leg): --ildasm disassembles prog.dll, --ilasm reassembles that listing
unchanged into --output (an image of the same kind), and --ildasm
disassembles the result again. Both listings must be equal apart from the
lines that differ between any two builds of one module ('// MVID:' and
'// Image base:') and the RVA data labels I_<RVA>, which are renumbered in
order of first appearance (where the data lies is the image's layout); a
difference is printed as a unified diff. One difference
is accepted, and only line for line: an 'ldc.r4' whose float32 operand is a
signaling NaN (exponent bits all ones, fraction nonzero, bit 22 clear) in the
first listing may come back as the same instruction with bit 22 set, the
quiet NaN of the same payload, because both ilasms quiet float32 signaling
NaN constants while the MC encoder and msil-ld keep their bits (plan 5.12.6,
the 'snan-quiet' entry, and section 12, 'M21.c extended'); ildasm prints such
an operand as its bytes, '(01 00 80 7F)' coming back as '(01 00 C0 7F)', or
as 'float32(0x7F800001)'. A float64 operand, a quiet NaN that changes, a
payload that changes otherwise and every other line still fail; -v counts the
quieted constants.

One more difference is accepted, for library outputs: a top-level '.class'
block whose class a '.custom' inside the listing's '.assembly' block names
may stand at another position in the second listing (plan 5.12.6 O2 and
section 12, '5.12.6 O2 clarified (library outputs)'). In practice it is the
'<msil>SymbolTable' attribute class of every -shared output: ilasm defines a
TypeDef at its first reference, and the '.assembly' block precedes every
class, so the class comes back as the first one of the module. The block,
from its '.class' line to its '} // end of class' line and the blank line
that ildasm prints after it, is compared at its position in the second
listing, so its own lines and every other line (the '.assembly' block's
included) must still be equal, and the other acceptances apply to it and
around it as before. Any other moved class, a changed or missing block and a
block that no '.custom' of the '.assembly' block names still fail, so an EXE,
whose '.assembly' block names none of its classes, round-trips strictly; -v
names the moved classes, and a failing diff shows the first listing with
the accepted move applied.

The only edit to the listing is that an image layout directive whose value
ildasm did not format ('.imagebase 0x%016I64x' from the Linux ildasm
10.0.12) is dropped, because ilasm rejects it; ilasm then uses its default,
which is what the backend's output gets too. The lit leg runs --output
afterwards and compares its transcript as usual. Use the .NET (NuGet) ildasm
and ilasm: the .NET Framework ildasm prints infinite ldc.r4/ldc.r8 operands
as 'inf', which neither the .NET Framework ilasm nor the .NET ilasm 10.0.12
reads back (llvm/docs/MSIL/recon/cil-runtime.md 15).

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

# Pass 1 only, and only at an ldsflda of the image or of the relocation table
# in the listing (D12, plan 5.9.10): PEVerify reports taking the address of an
# RVA static at the ldsflda, whatever is done with it, and stops checking the
# method there.
RVA_STATIC = re.compile(r"Cannot modify an imaged based \(RVA\) static")
# The operands of those ldsfldas as ildasm prints them, whitespace folded:
# the image copy of D12 and the '<msil>relocs' table that the relocation loop
# of plan 5.9.10 reads (above its threshold of 32 relocations).
IMAGE_FIELD = "valuetype '<msil>image_t' '<msil>image'"
RELOCS_FIELD = "valuetype '<msil>relocs_t' '<msil>relocs'"
RVA_FIELDS = (IMAGE_FIELD, RELOCS_FIELD)
# The type of the '<msil-verify>' stubs in the neutralized copy: a type of its
# own, so that a <Module> already near the method limit of a type (msil-ld
# fills it up to the limit before it spills, plan 5.9.12) stays loadable.
STUB_TYPE = "'<msil-verify>'"
# The scope of System.ValueType or System.Object in the listing, for the base
# type of STUB_TYPE ('[mscorlib]', or a reference pack's facade).
SYSTEM_SCOPE_RE = re.compile(r"(\[[^\]\s]+\])\s*System\.(?:Object|ValueType)\b")


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


def load_msil_equiv():
    """msil-equiv.py, whose allow-list parser and test-path rule the O6 skip
    lines share with O3 and O4 (msil-prog.py --equiv-skip uses them too).
    Loaded only for --equiv-skip."""
    spec = importlib.util.spec_from_file_location(
        "msil_equiv", os.path.join(HERE, "msil-equiv.py")
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


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
            if RVA_STATIC.fullmatch(msg) and listing.instruction_at(where, offset) in [
                ("ldsflda", field) for field in RVA_FIELDS
            ]:
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
        return "call %s %s::'%s'(%s)" % (ret, STUB_TYPE, name, params)

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
        elif op == "ldsflda" and " ".join(rest.split()) in RVA_FIELDS:
            # The image copy of D12 and the relocation table of 5.9.10; any
            # other RVA field stays and fails.
            new = ["ldc.i4.0", "conv.i"]
            what = "ldsflda '%s'" % rest.split("'")[-2]
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
        scope = SYSTEM_SCOPE_RE.search(text)
        out.append("")
        out.append(
            "// msil-verify.py: stubs that replace the neutralized instructions."
        )
        out.append(
            ".class private abstract auto ansi sealed %s extends %sSystem.Object"
            % (STUB_TYPE, scope.group(1) if scope else "[mscorlib]")
        )
        out.append("{")
        for name, (ret, params) in sorted(stubs.items()):
            out.append(
                "  .method assembly static %s '%s'(%s) cil managed"
                % (ret, name, params)
            )
            out.append("  {")
            out.append("    .maxstack 1")
            out.append("    ldnull")
            out.append("    throw")
            out.append("  }")
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
# An ldc.r4 line of an ildasm listing with its operand as bytes ('(01 00 80
# 7F)', little-endian; how ildasm prints a NaN) or as 'float32(0x7F800001)'.
LDC_R4_RE = re.compile(
    r"^(?P<lead>\s*IL_[0-9A-Fa-f]+:\s*ldc\.r4\s+)"
    r"(?:\((?P<bytes>[0-9A-Fa-f]{2}(?:\s+[0-9A-Fa-f]{2}){3})\)"
    r"|float32\(0x(?P<hex>[0-9A-Fa-f]{8})\))\s*$"
)
FLOAT32_EXPONENT = 0x7F800000
FLOAT32_FRACTION = 0x007FFFFF
FLOAT32_QUIET = 0x00400000


def ldc_r4_operand(line):
    """(lead, form, bits) of an ildasm ldc.r4 line whose operand is bytes
    or float32(0x...), else None."""
    m = LDC_R4_RE.match(line.rstrip("\r\n"))
    if not m:
        return None
    if m.group("bytes"):
        bits = int.from_bytes(
            bytes(int(b, 16) for b in m.group("bytes").split()), "little"
        )
        return m.group("lead"), "bytes", bits
    return m.group("lead"), "hex", int(m.group("hex"), 16)


def is_float32_snan(bits):
    """A float32 signaling NaN: exponent all ones, fraction nonzero, quiet
    bit (22) clear."""
    return (
        bits & FLOAT32_EXPONENT == FLOAT32_EXPONENT
        and bits & FLOAT32_FRACTION != 0
        and not bits & FLOAT32_QUIET
    )


def snan_quieted(first, second):
    """Whether the listing line second is the line first with its ldc.r4
    float32 signaling NaN quieted (bit 22 set) and nothing else changed:
    the one difference --roundtrip accepts (ilasm quiets such constants)."""
    a, b = ldc_r4_operand(first), ldc_r4_operand(second)
    if not a or not b or a[:2] != b[:2]:
        return False
    return is_float32_snan(a[2]) and b[2] == a[2] | FLOAT32_QUIET


# The '.assembly NAME' line of an ildasm listing (not '.assembly extern'),
# which opens the block of the Assembly row's attributes; the block ends at
# the next line that starts with '}'.
ASSEMBLY_BLOCK_RE = re.compile(r"^\.assembly\s+(?!extern\b)\S")
# A '.custom' line in that block and the class that its constructor belongs
# to, as ildasm prints it: ".custom instance void '<msil>SymbolTable'::.ctor(
# uint8[]) = ( 01 00 ...". A class of another assembly ('[mscorlib]System.
# ...') has no block in the listing.
ASSEMBLY_CUSTOM_RE = re.compile(
    r"^\s+\.custom\s+(?:instance\s+)?void\s+(?P<cls>[^\[\s].*?)::\.ctor\("
)
# The first and last lines of a top-level class block (nested classes and
# members are indented): '.class private auto ansi ... NAME' and
# '} // end of class NAME'.
CLASS_BLOCK_START_RE = re.compile(r"^\.class\s")
CLASS_BLOCK_END_RE = re.compile(r"^\} // end of class (?P<cls>.+?)\s*$")


def assembly_attribute_classes(listing):
    """The class names that the '.custom' lines inside the '.assembly' block
    of listing (a list of lines) name."""
    names, inside = set(), False
    for line in listing:
        text = line.rstrip("\r\n")
        if not inside:
            inside = bool(ASSEMBLY_BLOCK_RE.match(text))
        elif text.startswith("}"):
            break
        else:
            m = ASSEMBLY_CUSTOM_RE.match(text)
            if m:
                names.add(m.group("cls"))
    return names


def class_blocks(listing, names):
    """[(name, start, end)] of the top-level class blocks of listing whose
    class is one of names, in listing order: from the '.class' line to the
    '} // end of class' line and the blank line that ildasm prints after it
    (end exclusive)."""
    blocks, start = [], None
    for i, line in enumerate(listing):
        text = line.rstrip("\r\n")
        if CLASS_BLOCK_START_RE.match(text):
            start = i
            continue
        m = CLASS_BLOCK_END_RE.match(text) if start is not None else None
        if not m:
            continue
        if m.group("cls") in names:
            end = i + 1
            if end < len(listing) and not listing[end].strip():
                end += 1
            blocks.append((m.group("cls"), start, end))
        start = None
    return blocks


def move_attribute_classes(first, second):
    """The difference --roundtrip accepts for library outputs: ilasm defines
    a TypeDef at its first reference, so a class that a '.custom' of the
    '.assembly' block names (the '<msil>SymbolTable' attribute class of a
    -shared output) comes back first. Returns (first with the top-level
    blocks of those classes moved to their positions in second, the names
    of the blocks that moved, in the order of second). The k-th block of a
    class goes where the k-th block of that class stands in second; nothing
    moves unless both listings have the same blocks of those classes. The
    blocks' lines are moved as they are, so a changed block, a moved block
    of another class and every other difference remain for the comparison."""
    names = assembly_attribute_classes(first)
    if not names:
        return first, []
    a, b = class_blocks(first, names), class_blocks(second, names)
    if not a or sorted(n for n, _, _ in a) != sorted(n for n, _, _ in b):
        return first, []

    def split(listing, blocks):
        """(the lines outside blocks, the index in them of each block)."""
        rest, at, pos = [], [], 0
        for _, start, end in blocks:
            rest.extend(listing[pos:start])
            at.append(len(rest))
            pos = end
        rest.extend(listing[pos:])
        return rest, at

    rest, at_a = split(first, a)
    _, at_b = split(second, b)
    # The blocks of the first listing per class, with their (index, rank).
    pending = {}
    for rank, ((name, start, end), at) in enumerate(zip(a, at_a)):
        pending.setdefault(name, []).append((first[start:end], (at, rank)))
    moved_first, pos, moved = [], 0, []
    for rank, ((name, _, _), at) in enumerate(zip(b, at_b)):
        lines, where = pending[name].pop(0)
        if where != (at, rank) and name not in moved:
            moved.append(name)
        moved_first.extend(rest[pos:at])
        moved_first.extend(lines)
        pos = max(pos, at)
    moved_first.extend(rest[pos:])
    return moved_first, moved


def moved_classes(moved):
    """'class block NAME' or 'class blocks NAME, NAME' of the names that
    move_attribute_classes returned."""
    return "class block%s %s" % ("" if len(moved) == 1 else "s", ", ".join(moved))


def renumber_data_labels(listing):
    """listing (a list of lines) with its RVA data labels I_<RVA> renumbered
    I_0, I_1, ... in the order of their first appearance (DATA_LABEL_RE)."""
    labels = {}

    def renumber(m):
        return m.group(1) + labels.setdefault(m.group(2), "I_%d" % len(labels))

    return [DATA_LABEL_RE.sub(renumber, line) for line in listing]


def compare_listings(first, second):
    """Compare two ildasm listings (lists of lines without the build-specific
    ones, BUILD_SPECIFIC_RE) for --roundtrip. Returns (quieted, moved, a, b):
    the count of float32 signaling NaN constants that the second listing has
    quieted, the names of the class blocks that it has moved
    (move_attribute_classes), and the two listings as compared, their data
    labels renumbered, a being the first listing with those accepted
    differences applied (the blocks at their positions in the second, the
    quieted lines taken from the second). The listings are equal up to the
    accepted differences when a == b, and a unified diff of a and b names
    only the real differences. Only a line-for-line quieting counts. The
    blocks move before the labels are renumbered, so a label inside a moved
    block gets its number in the order of the second listing in both."""
    first, moved = move_attribute_classes(first, second)
    first, second = renumber_data_labels(first), renumber_data_labels(second)
    if len(first) != len(second):
        return 0, moved, first, second
    quieted, merged = 0, []
    for a, b in zip(first, second):
        if a != b and snan_quieted(a, b):
            quieted += 1
            merged.append(b)
        else:
            merged.append(a)
    return quieted, moved, merged, second


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
        return [l for l in listing.splitlines(True) if not BUILD_SPECIFIC_RE.match(l)]

    quieted, moved, a, b = compare_listings(comparable(first), comparable(second))
    if a != b:
        # The diff shows the first listing with the accepted differences
        # already applied, so it names only the real differences.
        diff = "".join(
            difflib.unified_diff(
                a,
                b,
                "ildasm %s" % os.path.basename(args.assembly),
                "ildasm %s" % os.path.basename(output),
            )
        )
        one = len(moved) == 1
        note = (
            "(in the diff, the first listing's %s %s at %s position in the "
            "second: a move that --roundtrip accepts)\n"
            % (
                moved_classes(moved),
                "stands" if one else "stand",
                "its" if one else "their",
            )
            if moved
            else ""
        )
        return [
            "%s: the disassembly of the reassembled %s differs from that of %s:\n%s%s"
            % (
                label,
                os.path.basename(output),
                os.path.basename(args.assembly),
                note,
                diff,
            )
        ]
    if args.verbose:
        accepted = []
        if quieted:
            accepted.append(
                "%d float32 signaling NaN constant%s that ilasm quieted"
                % (quieted, "" if quieted == 1 else "s")
            )
        if moved:
            accepted.append(
                "the position of the %s, which the .assembly block's .custom "
                "names and ilasm moved" % moved_classes(moved)
            )
        print(
            "msil-verify: %s: %s -> %s, %d listing lines identical%s"
            % (
                label,
                args.assembly,
                args.output,
                len(a),
                " up to " + " and ".join(accepted) if accepted else "",
            )
        )
    return []


# ---------------------------------------------------------------- O6

# A PEVerify /md message with /hresult: '[MD](0x8013129D): <text>'; its token,
# '[token:0x04000001]', is named after the row it designates.
MD_LINE_RE = re.compile(r"^\[MD\](?:\((?P<hr>0x[0-9A-Fa-f]+)\))?:\s*(?P<msg>.*?)\s*$")
MD_TOKEN_RE = re.compile(r"\[token:0x(?P<tok>[0-9A-Fa-f]{8})\]")

# ECMA-335 II.22: the tables by number, their columns and coded indexes.
# Column kinds: 'u16', 'u32', 'str' (#Strings), 'guid', 'blob', ('i', table)
# and ('c', coded index).
MD_TABLES = (
    "Module TypeRef TypeDef FieldPtr Field MethodPtr MethodDef ParamPtr Param "
    "InterfaceImpl MemberRef Constant CustomAttribute FieldMarshal DeclSecurity "
    "ClassLayout FieldLayout StandAloneSig EventMap EventPtr Event PropertyMap "
    "PropertyPtr Property MethodSemantics MethodImpl ModuleRef TypeSpec ImplMap "
    "FieldRVA EncLog EncMap Assembly AssemblyProcessor AssemblyOS AssemblyRef "
    "AssemblyRefProcessor AssemblyRefOS File ExportedType ManifestResource "
    "NestedClass GenericParam MethodSpec GenericParamConstraint"
).split()
MD_CODED = {
    "TypeDefOrRef": (2, (0x02, 0x01, 0x1B)),
    "HasConstant": (2, (0x04, 0x08, 0x17)),
    "HasCustomAttribute": (
        5,
        (0x06, 0x04, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x00, 0x0E, 0x17, 0x14, 0x11)
        + (0x1A, 0x1B, 0x20, 0x23, 0x26, 0x27, 0x28, 0x2A, 0x2C, 0x2B),
    ),
    "HasFieldMarshal": (1, (0x04, 0x08)),
    "HasDeclSecurity": (2, (0x02, 0x06, 0x20)),
    "MemberRefParent": (3, (0x02, 0x01, 0x1A, 0x06, 0x1B)),
    "HasSemantics": (1, (0x14, 0x17)),
    "MethodDefOrRef": (1, (0x06, 0x0A)),
    "MemberForwarded": (1, (0x04, 0x06)),
    "Implementation": (2, (0x26, 0x23, 0x27)),
    "CustomAttributeType": (3, (None, None, 0x06, 0x0A, None)),
    "ResolutionScope": (2, (0x00, 0x1A, 0x23, 0x01)),
    "TypeOrMethodDef": (1, (0x02, 0x06)),
}
MD_SCHEMA = {
    0x00: ("u16", "str", "guid", "guid", "guid"),
    0x01: (("c", "ResolutionScope"), "str", "str"),
    0x02: ("u32", "str", "str", ("c", "TypeDefOrRef"), ("i", 0x04), ("i", 0x06)),
    0x03: (("i", 0x04),),
    0x04: ("u16", "str", "blob"),
    0x05: (("i", 0x06),),
    0x06: ("u32", "u16", "u16", "str", "blob", ("i", 0x08)),
    0x07: (("i", 0x08),),
    0x08: ("u16", "u16", "str"),
    0x09: (("i", 0x02), ("c", "TypeDefOrRef")),
    0x0A: (("c", "MemberRefParent"), "str", "blob"),
    0x0B: ("u16", ("c", "HasConstant"), "blob"),
    0x0C: (("c", "HasCustomAttribute"), ("c", "CustomAttributeType"), "blob"),
    0x0D: (("c", "HasFieldMarshal"), "blob"),
    0x0E: ("u16", ("c", "HasDeclSecurity"), "blob"),
    0x0F: ("u16", "u32", ("i", 0x02)),
    0x10: ("u32", ("i", 0x04)),
    0x11: ("blob",),
    0x12: (("i", 0x02), ("i", 0x14)),
    0x13: (("i", 0x14),),
    0x14: ("u16", "str", ("c", "TypeDefOrRef")),
    0x15: (("i", 0x02), ("i", 0x17)),
    0x16: (("i", 0x17),),
    0x17: ("u16", "str", "blob"),
    0x18: ("u16", ("i", 0x06), ("c", "HasSemantics")),
    0x19: (("i", 0x02), ("c", "MethodDefOrRef"), ("c", "MethodDefOrRef")),
    0x1A: ("str",),
    0x1B: ("blob",),
    0x1C: ("u16", ("c", "MemberForwarded"), "str", ("i", 0x1A)),
    0x1D: ("u32", ("i", 0x04)),
    0x1E: ("u32", "u32"),
    0x1F: ("u32",),
    0x20: ("u32", "u16", "u16", "u16", "u16", "u32", "blob", "str", "str"),
    0x21: ("u32",),
    0x22: ("u32", "u32", "u32"),
    0x23: ("u16", "u16", "u16", "u16", "u32", "blob", "str", "str", "blob"),
    0x24: ("u32", ("i", 0x23)),
    0x25: ("u32", "u32", "u32", ("i", 0x23)),
    0x26: ("u32", "str", "blob"),
    0x27: ("u32", "u32", "str", "str", ("c", "Implementation")),
    0x28: ("u32", "u32", "str", ("c", "Implementation")),
    0x29: (("i", 0x02), ("i", 0x02)),
    0x2A: ("u16", "u16", ("c", "TypeOrMethodDef"), "str"),
    0x2B: (("c", "MethodDefOrRef"), "blob"),
    0x2C: (("i", 0x2A), ("c", "TypeDefOrRef")),
}
# The name column (and namespace column) of the tables that have one.
MD_NAME_COLUMNS = {
    0x00: (1, None),
    0x01: (1, 2),
    0x02: (1, 2),
    0x04: (1, None),
    0x06: (3, None),
    0x08: (2, None),
    0x0A: (1, None),
    0x14: (1, None),
    0x17: (1, None),
    0x1A: (0, None),
    0x20: (7, None),
    0x23: (6, None),
    0x26: (1, None),
    0x27: (2, 3),
    0x28: (2, None),
    0x2A: (3, None),
}


class MetadataNames:
    """The rows of an image's metadata tables, enough to name the row that a
    metadata token designates (O6 compares PEVerify /md messages of two
    images whose row order differs). only: the numbers of the tables whose
    rows are decoded (every table when None); the others are only counted.
    Raises ValueError for a file that is not an IL image."""

    def __init__(self, path, only=None):
        with open(path, "rb") as f:
            self.data = data = f.read()
        u16 = lambda o: struct.unpack_from("<H", data, o)[0]
        u32 = lambda o: struct.unpack_from("<I", data, o)[0]
        if data[:2] != b"MZ":
            raise ValueError("%s is not a PE file" % path)
        pe = u32(0x3C)
        if data[pe : pe + 4] != b"PE\0\0":
            raise ValueError("%s is not a PE file" % path)
        nsections, opt_size = u16(pe + 6), u16(pe + 20)
        opt = pe + 24
        dirs = opt + {0x10B: 96, 0x20B: 112}.get(u16(opt), 0)
        if dirs == opt:
            raise ValueError("%s has an unknown optional header" % path)
        self.sections = []
        for i in range(nsections):
            s = opt + opt_size + 40 * i
            vsize, va, rawsize, raw = struct.unpack_from("<IIII", data, s + 8)
            self.sections.append((va, max(vsize, rawsize), raw))
        cli_rva = u32(dirs + 14 * 8)
        if not cli_rva:
            raise ValueError("%s has no CLI header" % path)
        cli = self.offset(cli_rva)
        root = self.offset(u32(cli + 8))
        if u32(root) != 0x424A5342:
            raise ValueError("%s has no metadata root" % path)
        vlen = u32(root + 12)
        at = root + 16 + vlen + 2
        streams = {}
        for _ in range(u16(at)):
            at += 2 if not streams else 0
            off, size = u32(at), u32(at + 4)
            end = data.index(b"\0", at + 8)
            name = data[at + 8 : end].decode("ascii", "replace")
            streams[name] = (root + off, size)
            at = at + 8 + ((end - (at + 8)) // 4 + 1) * 4
        tables = streams.get("#~") or streams.get("#-")
        self.strings = streams.get("#Strings", (0, 0))[0]
        if not tables:
            raise ValueError("%s has no metadata tables" % path)
        t = tables[0]
        heaps = data[t + 6]
        valid = struct.unpack_from("<Q", data, t + 8)[0]
        self.rows = [0] * len(MD_TABLES)
        at = t + 24
        for n in range(64):
            if valid >> n & 1:
                if n < len(MD_TABLES):
                    self.rows[n] = u32(at)
                at += 4
        if heaps & 0x40:
            at += 4
        sizes = {
            "u16": 2,
            "u32": 4,
            "str": 4 if heaps & 1 else 2,
            "guid": 4 if heaps & 2 else 2,
            "blob": 4 if heaps & 4 else 2,
        }

        def size(kind):
            if isinstance(kind, str):
                return sizes[kind]
            if kind[0] == "i":
                return 2 if self.rows[kind[1]] < 0x10000 else 4
            bits, targets = MD_CODED[kind[1]]
            most = max(self.rows[x] if x is not None else 0 for x in targets)
            return 2 if most < (1 << (16 - bits)) else 4

        self.table_rows = {}
        for n in range(len(MD_TABLES)):
            if not self.rows[n]:
                continue
            widths = [size(k) for k in MD_SCHEMA[n]]
            rowsize = sum(widths)
            if only is not None and n not in only:
                at += rowsize * self.rows[n]
                continue
            rows = []
            for r in range(self.rows[n]):
                cols, o = [], at + r * rowsize
                for w in widths:
                    cols.append(u16(o) if w == 2 else u32(o))
                    o += w
                rows.append(cols)
            self.table_rows[n] = rows
            at += rowsize * self.rows[n]

    def offset(self, rva):
        for va, size, raw in self.sections:
            if va <= rva < va + size:
                return raw + rva - va
        raise ValueError("RVA 0x%X lies in no section" % rva)

    def string(self, index):
        start = self.strings + index
        end = self.data.index(b"\0", start)
        return self.data[start:end].decode("utf-8", "replace")

    def name(self, table, row):
        """The (namespace-qualified) name of a row, or None."""
        rows = self.table_rows.get(table, [])
        if table not in MD_NAME_COLUMNS or not 1 <= row <= len(rows):
            return None
        col, ns = MD_NAME_COLUMNS[table]
        name = self.string(rows[row - 1][col])
        if ns is not None:
            space = self.string(rows[row - 1][ns])
            if space:
                name = space + "." + name
        return name

    def owner(self, table, row):
        """The TypeDef (for a Field or MethodDef) or MethodDef (for a Param)
        whose member list holds the row, as (table, row), or None."""
        parent, column = {0x04: (0x02, 4), 0x06: (0x02, 5), 0x08: (0x06, 5)}[table]
        found = None
        for p, cols in enumerate(self.table_rows.get(parent, []), 1):
            if cols[column] <= row:
                found = p
        return (parent, found) if found else None

    def describe(self, token):
        """A layout-independent text for the row of a metadata token."""
        table, row = token >> 24, token & 0xFFFFFF
        label = MD_TABLES[table] if table < len(MD_TABLES) else "table 0x%02X" % table
        name = self.name(table, row)
        if name is None:
            if table < len(MD_TABLES) and not 1 <= row <= self.rows[table]:
                return "%s[%d] (no such row)" % (label, row)
            return "%s[%d]" % (label, row)
        text = "%s '%s'" % (label, name)
        if table in (0x04, 0x06, 0x08):
            own = self.owner(table, row)
            if own:
                outer = self.name(*own)
                if table == 0x08:
                    grand = self.owner(*own)
                    if grand:
                        outer = "%s'::'%s" % (self.name(*grand), outer)
                text = "%s '%s'::'%s'" % (label, outer, name)
        return text


def assembly_row(path):
    """The name and version (major, minor, build, revision) of the Assembly
    row of the IL image at path; raises OSError when the file cannot be read
    and ValueError when it is not an IL image with an Assembly row.
    msil/test/lit.cfg.py reads the .NET major of a pack named by
    msil_references=<path> with it."""
    try:
        md = MetadataNames(path, only=(0x20,))
        rows = md.table_rows.get(0x20)
        if not rows:
            raise ValueError("%s has no Assembly row" % path)
        row = rows[0]
        return md.string(row[7]), tuple(row[1:5])
    except (struct.error, IndexError) as e:
        raise ValueError("%s is not a well-formed IL image (%s)" % (path, e))


def md_messages(args, image):
    """The PEVerify /md messages of image, each token named after its row;
    raises ValueError when PEVerify fails or the image cannot be read."""
    res, output, limit = run([args.peverify, "/nologo", "/md", "/hresult", image], args)
    if limit:
        raise ValueError("peverify /md %s: %s" % (image, limit))
    names = MetadataNames(image)
    full = os.path.abspath(image)
    messages = []
    for line in output.splitlines():
        m = MD_LINE_RE.match(line.strip())
        if not m:
            continue
        text = MD_TOKEN_RE.sub(
            lambda t: "[%s]" % names.describe(int(t.group("tok"), 16)), m.group("msg")
        )
        for spelling in {full, full.replace("\\", "/"), image}:
            text = text.replace(spelling, "<image>")
        hr = m.group("hr")
        messages.append("[MD]%s: %s" % ("(0x%s)" % hr[2:].upper() if hr else "", text))
    # 0: verified (warnings only); 1: errors, which are among the messages.
    if res.returncode not in (0, 1) or (res.returncode == 1 and not messages):
        raise ValueError(
            "peverify /md %s exited with status %s:\n%s"
            % (image, msil_run.describe_exit(res.returncode), output)
        )
    return messages


def check_md_baseline(args):
    """O6 (plan 5.12.6): every PEVerify /md message of the assembly is one of
    the baseline's. Returns (failures, notes)."""
    if args.skip_peverify is not None:
        return [], [
            "msil-verify: note: O6 skipped: PEVerify skipped: %s" % args.skip_peverify
        ]
    if not args.peverify:
        return [], ["msil-verify: note: O6 skipped: no PEVerify (--peverify)"]
    try:
        mine = md_messages(args, args.assembly)
        base = set(md_messages(args, args.md_baseline))
    except (OSError, ValueError, struct.error, IndexError) as e:
        return ["O6: %s" % e], []
    extra = sorted(set(m for m in mine if m not in base))
    if extra:
        return [
            "O6: peverify /md: %s (not reported for the baseline %s)"
            % (m, os.path.basename(args.md_baseline))
            for m in extra
        ], []
    return [], [
        "msil-verify: O6: the %d PEVerify /md messages of %s are among the %d of %s"
        % (len(mine), os.path.basename(args.assembly), len(base), args.md_baseline)
    ]


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


def verify_deps(args, allow):
    """--also-verify-deps: verify each managed file of the --dep-dir
    directories as the assembly is verified (both passes), with the other
    files of those directories and the --dep files as its dependencies.
    Returns (failures, notes), each prefixed with the file's name."""
    bad, notes, done = [], [], []
    for lib in args.dir_deps:
        if not is_managed(lib):
            continue
        name = os.path.basename(lib)
        sub = argparse.Namespace(**vars(args))
        sub.assembly = lib
        sub.dep = args.file_deps + [d for d in args.dir_deps if d != lib]
        sub.managed_deps = [d for d in sub.dep if is_managed(d)]
        sub.md_baseline = None
        if args.keep_copy:
            sub.keep_copy = os.path.join(args.keep_copy, name)
        private = tempfile.mkdtemp(prefix="msil-verify-") if args.ildasm else None
        try:
            lbad, lnotes = verify(sub, allow, private)
        finally:
            if private:
                keep_copy(sub, private)
                msil_run.remove_tree(private)
        bad += ["%s: %s" % (name, b) for b in lbad]
        notes += ["%s: %s" % (name, n) for n in lnotes]
        done.append(name)
    if done:
        notes.append("msil-verify: also verified %s" % ", ".join(done))
    return bad, notes


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
        "--skip-peverify",
        metavar="REASON",
        help="run no PEVerify (pass 1, pass 2 and O6) and print 'PEVerify "
        "skipped: REASON', e.g. for an image bound to .NET 8 references",
    )
    p.add_argument(
        "--ildasm",
        help="ildasm for the neutralized pass (pass 2) or --roundtrip; empty: "
        "skip pass 2",
    )
    p.add_argument(
        "--ilasm", help="ilasm that reassembles the neutralized copy or --roundtrip"
    )
    p.add_argument(
        "--md-baseline",
        metavar="FILE",
        help="O6: every PEVerify /md message of the assembly must be one of "
        "this image's (the ilasm build of the same program)",
    )
    p.add_argument(
        "--equiv-skip",
        metavar="FILE",
        help="with --md-baseline: equiv-allowed.txt, whose 'skip O6' lines "
        "covering --test skip O6",
    )
    p.add_argument("--test", help="the test's path, for --equiv-skip (lit's %%s)")
    p.add_argument(
        "--triple", help="the MSIL triple of the build, for qualified skip lines"
    )
    p.add_argument(
        "--level",
        choices=("O0", "O1", "O2", "O3", "Os", "Oz"),
        help="the optimization level of the build, for qualified skip lines",
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
    p.add_argument(
        "--dep-dir",
        action="append",
        default=[],
        metavar="DIR",
        help="every file in DIR as a --dep (repeatable; a missing or empty "
        "DIR adds nothing)",
    )
    p.add_argument(
        "--also-verify-deps",
        action="store_true",
        help="also verify each managed file of the --dep-dir directories",
    )
    p.add_argument("assembly")
    args = p.parse_args()
    try:
        msil_run.check_deps(args.dep, [])
    except msil_run.ConfigError as e:
        p.error(str(e))
    # --dep-dir: the files directly in each directory join the --dep files.
    args.dir_deps = [
        os.path.join(d, name)
        for d in args.dep_dir
        if os.path.isdir(d)
        for name in sorted(os.listdir(d))
        if os.path.isfile(os.path.join(d, name))
    ]
    args.file_deps = list(args.dep)
    args.dep = args.file_deps + args.dir_deps
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
    if args.skip_peverify is not None and not args.skip_peverify.strip():
        p.error("--skip-peverify needs a reason")
    if bool(args.equiv_skip) != bool(args.test):
        p.error("--equiv-skip and --test go together")
    if args.equiv_skip and not args.md_baseline:
        p.error("--equiv-skip selects the 'skip O6' lines; it needs --md-baseline")

    if args.roundtrip:
        if args.md_baseline:
            p.error("--md-baseline and --roundtrip exclude each other")
        if args.also_verify_deps:
            p.error("--also-verify-deps and --roundtrip exclude each other")
        if args.skip_peverify is not None:
            p.error("--skip-peverify and --roundtrip exclude each other")
        return main_roundtrip(p, args)
    if args.output:
        p.error("--output needs --roundtrip")
    skipped_note = []
    if args.skip_peverify is not None:
        # Both passes and O6 test args.peverify; O6 also says why it is off.
        args.skip_peverify = args.skip_peverify.strip()
        args.peverify = None
        skipped_note = ["PEVerify skipped: %s" % args.skip_peverify]
    if args.skip:
        for n in skipped_note:
            print(n)
        print("msil-verify: skipped (no ILVerify configured)")
        return 0
    if not args.ilverify:
        p.error("--ilverify is required (or --skip)")
    if args.ildasm and not args.ilasm:
        p.error("--ildasm needs --ilasm")
    if not os.path.isfile(args.assembly):
        sys.stderr.write("msil-verify: error: %s not found\n" % args.assembly)
        return 2
    if args.md_baseline and not os.path.isfile(args.md_baseline):
        sys.stderr.write(
            "msil-verify: error: --md-baseline %s not found\n" % args.md_baseline
        )
        return 2
    try:
        allow = load_allow(args.allow)
    except (OSError, re.error) as e:
        sys.stderr.write("msil-verify: error: cannot read %s: %s\n" % (args.allow, e))
        return 2
    o6_skip = None
    if args.equiv_skip:
        # The O6 skip lines of the allow-list (plan section 12, 'M21.c
        # extended'), matched as msil-equiv.py matches O3's and O4's.
        msil_equiv = load_msil_equiv()
        try:
            o6_skip = msil_equiv.Allowed(args.equiv_skip).skip_reason(
                "O6", msil_equiv.test_path(args.test), args.triple, args.level
            )
        except msil_equiv.UsageError as e:
            sys.stderr.write("msil-verify: error: --equiv-skip: %s\n" % e)
            return 2

    private = tempfile.mkdtemp(prefix="msil-verify-") if args.ildasm else None
    try:
        bad, notes = verify(args, allow, private)
        notes = skipped_note + notes
        if args.md_baseline and o6_skip is not None:
            notes.append("O6 skipped: %s" % o6_skip)
        elif args.md_baseline:
            mbad, mnotes = check_md_baseline(args)
            bad += mbad
            notes += mnotes
        if args.also_verify_deps:
            dbad, dnotes = verify_deps(args, allow)
            bad += dbad
            notes += dnotes
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

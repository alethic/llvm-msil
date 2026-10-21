"""Checks the ECMA-335 opcode table (cilopcodes.py) three ways:
1. against System.Reflection.Emit.OpCodes and the System.Reflection.Metadata
   ILOpCode enum (dumped by 'mdump --opcodes' into dotnet-opcodes.tsv);
2. by assembling an .il method that uses every opcode with both ilasms and
   decoding the bytes with this table (names and immediates must match);
3. by re-encoding the decoded instruction list with cliwriter.IL (no branch
   relaxation) and comparing the bytes with ilasm's.
Usage: check_opcodes.py <dotnet-opcodes.tsv> <workdir> <mdump.dll> <ilasm>...
"""
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cilopcodes as K  # noqa: E402
from cliwriter import IL, Label  # noqa: E402

tsv, work, mdump = sys.argv[1], sys.argv[2], sys.argv[3]
ilasms = sys.argv[4:]
ok = True

# 1. Table vs .NET.
emit, ilop = {}, {}
for line in open(tsv, encoding="utf-8"):
    f = line.rstrip("\n").split("\t")
    if f[0] == "ILOpCode":
        ilop[int(f[2], 16)] = f[1]
    else:
        emit[f[0]] = (int(f[1], 16), f[2])
reserved = {n for n in emit if n.startswith("prefix")}
ECMA_ONLY = {"no."}  # 0xFE19: in ECMA-335 III.2.2, absent from OpCodes, ILOpCode and both ilasms
mine = {n: (v, k) for n, v, k in K.OPCODES if n not in ECMA_ONLY}
for n, (v, k) in mine.items():
    if n not in emit:
        print("MISSING in OpCodes:", n)
        ok = False
    elif emit[n] != (v, k):
        print("MISMATCH", n, hex(v), k, "vs OpCodes", hex(emit[n][0]), emit[n][1])
        ok = False
    if v not in ilop:
        print("MISSING in ILOpCode:", n, hex(v))
        ok = False
for n in emit:
    if n not in mine and n not in reserved:
        print("EXTRA in OpCodes (not in table):", n, emit[n])
        ok = False
extra_ilop = sorted(v for v in ilop if v not in {v for v, _ in mine.values()})
print("table: %d opcodes; OpCodes has %d (+%d reserved prefixes %s); ILOpCode has %d values; ILOpCode extra: %s"
      % (len(mine), len(emit) - len(reserved), len(reserved), sorted(reserved), len(ilop),
         [hex(v) for v in extra_ilop]))

# 2. Every opcode in one method.
OPERAND = {
    K.NONE: None, K.VAR8: "1", K.VAR16: "1", K.I8: "-5", K.I32: "0x12345678",
    K.I64: "0x123456789ABCDEF0", K.R4: "float32(0x7F800001)", K.R8: "float64(0x7FF0000000000001)",
    K.STRING: '"s"', K.SIG: "void(int32)", K.TOK: "[mscorlib]System.String",
}
TYPE_OPND = "valuetype 'S'"
FIELD_OPND = {"ldfld": "int32 'S'::'a'", "ldflda": "int32 'S'::'a'", "stfld": "int32 'S'::'a'",
              "ldsfld": "int32 'g'", "ldsflda": "int32 'g'", "stsfld": "int32 'g'"}
METHOD_OPND = {"jmp": "void 'allops'(int32, int32)", "call": "void 'f'()",
               "callvirt": "instance string [mscorlib]System.Object::ToString()",
               "newobj": "instance void [mscorlib]System.Object::.ctor()", "ldftn": "void 'f'()",
               "ldvirtftn": "instance string [mscorlib]System.Object::ToString()"}
IMM = {"unaligned.": "2", "no.": "1"}
seq, lines = [], []
for n, v, k in K.OPCODES:
    if n in ECMA_ONLY:
        continue
    if k in (K.BR8, K.BR32):
        lines.append("  %s L_%d" % (n, len(seq)))
        lines.append("L_%d:" % len(seq))
        seq.append((n, "label"))
    elif k == K.SWITCH:
        lines.append("  switch (L_s0, L_s1)")
        lines.append("L_s0:")
        lines.append("  nop")
        lines.append("L_s1:")
        seq.append((n, "switch"))
        seq.append(("nop", None))
    elif k == K.TYPE:
        lines.append("  %s %s" % (n, TYPE_OPND))
        seq.append((n, "tok"))
    elif k == K.FIELD:
        lines.append("  %s %s" % (n, FIELD_OPND[n]))
        seq.append((n, "tok"))
    elif k == K.METHOD:
        lines.append("  %s %s" % (n, METHOD_OPND[n]))
        seq.append((n, "tok"))
    else:
        opnd = IMM.get(n, OPERAND[k])
        lines.append("  %s%s" % (n, "" if opnd is None else " " + opnd))
        seq.append((n, opnd))
il_text = """.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly allops { }
.class private sequential ansi sealed 'S' extends [mscorlib]System.ValueType { .field public int32 'a' }
.field assembly static int32 'g'
.method assembly static void 'f'() cil managed { ret }
.method assembly static void 'allops'(int32, int32) cil managed
{
  .maxstack 8
  .locals (int32, int32)
%s
}
""" % "\n".join(lines)
src = os.path.join(work, "allops.il")
open(src, "w", newline="\n").write(il_text)

for ilasm in ilasms:
    tag = "fw" if "Framework" in ilasm else "modern"
    dll = os.path.join(work, "allops-%s.dll" % tag)
    r = subprocess.run([ilasm, "-nologo", "-quiet", "-dll", "-pe64", "-x64", "-output=" + dll, src],
                       capture_output=True, text=True)
    if r.returncode:
        print(tag, "ilasm failed:", r.stdout[-2000:], r.stderr[-2000:])
        ok = False
        continue
    dump = subprocess.run(["dotnet", mdump, dll], capture_output=True, text=True).stdout.splitlines()
    i = next(j for j, l in enumerate(dump) if "Method" in l and "'allops'" in l)
    il_line = next(l for l in dump[i:] if l.strip().startswith("IL "))
    code = bytes.fromhex(il_line[il_line.index("[") + 1:il_line.index("]")].replace(" ", ""))
    # Decode with our table.
    byval = {v: (n, k) for n, v, k in K.OPCODES}
    pos, decoded = 0, []
    while pos < len(code):
        b = code[pos]
        if b == 0xFE:
            v = 0xFE00 | code[pos + 1]
            pos += 2
        else:
            v = b
            pos += 1
        n, k = byval[v]
        if k == K.SWITCH:
            cnt = struct.unpack_from("<I", code, pos)[0]
            raw = code[pos:pos + 4 + 4 * cnt]
        else:
            raw = code[pos:pos + K.OPERAND_SIZE[k]]
        pos += len(raw)
        decoded.append((n, k, raw))
    names = [d[0] for d in decoded]
    expect = [s[0] for s in seq]
    if names != expect:
        print(tag, "DECODE MISMATCH")
        for a, b in zip(names, expect):
            if a != b:
                print("  got", a, "expected", b)
                break
        ok = False
        continue
    bad = []
    for (n, k, raw), (_, opnd) in zip(decoded, seq):
        if opnd in (None, "label", "switch", "tok") or k in (K.SIG, K.TOK, K.STRING):
            continue
        if k == K.R4:
            val = struct.unpack("<I", raw)[0]
            want = int(opnd[8:-1], 16)
        elif k == K.R8:
            val = struct.unpack("<Q", raw)[0]
            want = int(opnd[8:-1], 16)
        else:
            val = int.from_bytes(raw, "little", signed=(k in (K.I8, K.I32, K.I64) and n not in IMM))
            want = int(opnd, 0)
        if val != want:
            bad.append((n, hex(val), hex(want)))
    # Known ilasm deviation (Status.md, Limits): ilasm sets the quiet bit of
    # float32 signaling-NaN constants; float64 ones are exact.
    known = [b for b in bad if b == ("ldc.r4", "0x7fc00001", "0x7f800001")]
    bad = [b for b in bad if b not in known]
    if known:
        print(tag, "known ilasm deviation:", known)
    if bad:
        print(tag, "IMMEDIATE MISMATCH", bad)
        ok = False
    # 3. Re-encode with cliwriter.IL from the decoded list and compare.
    il = IL()
    labels = {}
    pending = []
    for idx, (n, k, raw) in enumerate(decoded):
        if k in (K.BR8, K.BR32):
            lab = Label()
            il(n, lab)
            il.mark(lab)  # every branch in the test targets the next instruction
        elif k == K.SWITCH:
            l0, l1 = Label(), Label()
            il(n, [l0, l1])
            il.mark(l0)
            pending.append(l1)
        else:
            if k == K.NONE:
                opnd = None
            elif k in (K.R4,):
                opnd = struct.unpack("<I", raw)[0]
            elif k in (K.R8,):
                opnd = struct.unpack("<Q", raw)[0]
            elif k in (K.METHOD, K.FIELD, K.TYPE, K.TOK, K.SIG, K.STRING):
                opnd = struct.unpack("<I", raw)[0]
            else:
                opnd = int.from_bytes(raw, "little", signed=(k in (K.I8, K.I32, K.I64) and n not in IMM))
            il(n, opnd)
            if pending and n == "nop":
                il.mark(pending.pop())
    mine_bytes = il.assemble(relax=False)
    print("%s ilasm: %d opcodes decoded, %d bytes; re-encoded bytes %s"
          % (tag, len(decoded), len(code), "IDENTICAL" if mine_bytes == code else "DIFFER"))
    if mine_bytes != code:
        ok = False
        for j, (x, y) in enumerate(zip(mine_bytes, code)):
            if x != y:
                print("  first difference at", j, mine_bytes[j:j + 8].hex(), code[j:j + 8].hex())
                break
print("RESULT:", "PASS" if ok else "FAIL")

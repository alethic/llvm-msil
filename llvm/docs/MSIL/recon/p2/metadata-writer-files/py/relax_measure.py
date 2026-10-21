"""Measures how much smaller Phase-1 method bodies get when branches use the
short forms (Phase-1 text always prints the long forms, plan 5.1, and ilasm
does not shorten them): decode each body ilasm wrote, turn branch targets
into labels, re-encode with cliwriter.IL (relaxation on), and compare sizes,
including the tiny-header effect (code < 64 bytes, maxstack <= 8, no locals).
Usage: relax_measure.py <mdump output>..."""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cilopcodes as K  # noqa: E402
from cliwriter import IL, Label  # noqa: E402

byval = {v: (n, k) for n, v, k in K.OPCODES}
SHORT_LDC = "--short-ldc" in sys.argv
sys.argv = [a for a in sys.argv if a != "--short-ldc"]


def decode(code):
    pos, out = 0, []
    while pos < len(code):
        start = pos
        v = code[pos]
        if v == 0xFE:
            v = 0xFE00 | code[pos + 1]
            pos += 2
        else:
            pos += 1
        n, k = byval[v]
        if k == K.SWITCH:
            cnt = struct.unpack_from("<I", code, pos)[0]
            raw = code[pos:pos + 4 + 4 * cnt]
        else:
            raw = code[pos:pos + K.OPERAND_SIZE[k]]
        pos += len(raw)
        out.append((start, pos, n, k, raw))
    return out


tot_before = tot_after = tot_hdr_before = tot_hdr_after = 0
nbodies = 0
for path in sys.argv[1:]:
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    before = after = hb = ha = 0
    for i, l in enumerate(lines):
        if not l.strip().startswith("Body "):
            continue
        m = re.search(r"maxstack (\d+) locals (\S+) .* eh (\d+)", l)
        maxstack, locals_, eh = int(m.group(1)), m.group(2), int(m.group(3))
        il_line = next(x for x in lines[i + 1:i + 3] if x.strip().startswith("IL "))
        code = bytes.fromhex(il_line[il_line.index("[") + 1:il_line.index("]")].replace(" ", ""))
        ins = decode(code)
        labels = {}
        for s, e, n, k, raw in ins:
            if k == K.BR8:
                labels.setdefault(e + struct.unpack("<b", raw)[0], Label())
            elif k == K.BR32:
                labels.setdefault(e + struct.unpack("<i", raw)[0], Label())
            elif k == K.SWITCH:
                cnt = struct.unpack_from("<I", raw)[0]
                for j in range(cnt):
                    labels.setdefault(e + struct.unpack_from("<i", raw, 4 + 4 * j)[0], Label())
        il = IL()
        for s, e, n, k, raw in ins:
            if s in labels:
                il.mark(labels[s])
            if k == K.BR8:
                il(n, labels[e + struct.unpack("<b", raw)[0]])
            elif k == K.BR32:
                il(n, labels[e + struct.unpack("<i", raw)[0]])
            elif k == K.SWITCH:
                cnt = struct.unpack_from("<I", raw)[0]
                il(n, [labels[e + struct.unpack_from("<i", raw, 4 + 4 * j)[0]] for j in range(cnt)])
            elif k == K.NONE:
                il(n)
            elif n == "ldc.i4" and SHORT_LDC:
                v = struct.unpack("<i", raw)[0]
                if v == -1:
                    il("ldc.i4.m1")
                elif 0 <= v <= 8:
                    il("ldc.i4.%d" % v)
                elif -128 <= v <= 127:
                    il("ldc.i4.s", v)
                else:
                    il(n, v)
            else:
                signed = k in (K.I8, K.I32, K.I64) and n not in ("unaligned.", "no.")
                il(n, int.from_bytes(raw, "little", signed=signed))
        if len(code) in labels:
            il.mark(labels[len(code)])
        new = il.assemble(relax=True)
        assert len(new) <= len(code)
        before += len(code)
        after += len(new)
        # Header bytes: ilasm writes fat (12) for every body; tiny (1) is
        # possible when code < 64, maxstack <= 8, no locals and no EH.
        hb += 12
        ha += 1 if (len(new) < 64 and maxstack <= 8 and locals_ == "nil" and eh == 0) else 12
        nbodies += 1
    print("%s: IL code %d -> %d bytes with short branches (%.1f%% smaller); headers %d -> %d"
          % (os.path.basename(path), before, after, 100.0 * (before - after) / max(before, 1), hb, ha))
    tot_before += before
    tot_after += after
    tot_hdr_before += hb
    tot_hdr_after += ha
print("TOTAL %d bodies: IL code %d -> %d (%.1f%% smaller); headers %d -> %d"
      % (nbodies, tot_before, tot_after, 100.0 * (tot_before - tot_after) / max(tot_before, 1),
         tot_hdr_before, tot_hdr_after))

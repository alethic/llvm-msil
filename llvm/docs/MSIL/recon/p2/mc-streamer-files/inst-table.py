import json, sys
d = json.load(open(sys.argv[1]))
insts = d['!instanceof']['MSILInst']
rows = []
for name in insts:
    r = d[name]
    bits = r['Inst']
    # bits list LSB first
    v = 0
    for i, b in enumerate(bits):
        if b: v |= (1 << i)
    asm = r['AsmString']
    pseudo = r.get('isPseudo', 0)
    peak = r['StackPeakExtra']
    pv = sum((1<<i) for i,b in enumerate(peak) if b)
    rows.append((name, v, asm, pseudo, pv, r.get('isCodeGenOnly',0)))
multi = [x for x in rows if '\n' in x[2]]
pseudo_print = [x for x in rows if x[1]==0xFFFF and x[2]]
print("total records", len(rows))
print("records with asm string and cilop 0xFFFF:")
for x in sorted(pseudo_print): print("  %-22s peak=%d %r" % (x[0], x[4], x[2]))
print("records with empty asm (never printed):", sorted(x[0] for x in rows if not x[2]))
ops = {}
for x in rows:
    if x[1] != 0xFFFF: ops.setdefault(x[1], set()).add(x[2].split(' ')[0])
print("distinct CIL opcodes:", len(ops))
for k in sorted(ops): print("  0x%04X %s" % (k, sorted(ops[k])))

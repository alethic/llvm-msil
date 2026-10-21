import json, sys
d = json.load(open('msil-records.json'))
ecma = {}
for line in open('ecma-opcodes.txt'):
    v, name, opnd, size = line.rstrip('\n').split('\t')
    ecma[name] = (int(v, 16) & 0xFFFF, opnd)
bad = 0; checked = 0
for rec in d['!instanceof']['MSILInst']:
    r = d[rec]
    v = sum(1 << i for i, b in enumerate(r['Inst']) if b)
    if v == 0xFFFF: continue
    asm = r['AsmString'].replace('${flags}', '').split()[0]
    e = ecma.get(asm)
    checked += 1
    if not e or e[0] != v:
        bad += 1; print('MISMATCH', rec, asm, hex(v), e)
print('checked', checked, 'TableGen records with a CIL opcode; mismatches', bad)

"""Prototype of the MSIL MCCodeEmitter over the Phase-1 ILAsm subset.

Encodes every method body of the backend's -S output (one CIL opcode per
line, labels, 'switch (...)' lists) into CIL bytes, with 4-byte zero
placeholders for metadata tokens (the fixups an MC code emitter would
record), resolves branch and switch offsets as MCAssembler would resolve
PC-relative fixups, and compares the result with the IL bytes that ilasm
produced for the same text (tokens compared by kind and name).

Usage: python encode-check.py <ecma-opcodes.txt> <corpus dir> <bodies dir>
"""
import os
import re
import struct
import sys
from collections import Counter

ecma_file, corpus, bodies_dir = sys.argv[1:4]
OPS = {}
for line in open(ecma_file):
    v, name, opnd, size = line.rstrip('\n').split('\t')
    OPS[name] = (int(v, 16) & 0xFFFF, opnd)

METHOD_RE = re.compile(r"^\.method .*?'((?:[^'\\]|\\.)*)'\(|^\.method .* (\.cctor)\(")
LABEL_RE = re.compile(r'^(@[\w.$@<>]+):')


def opcode_bytes(v):
    return bytes([v >> 8, v & 0xFF]) if v > 0xFF else bytes([v])


def quoted_name(text):
    m = re.search(r"'((?:[^'\\]|\\.)*)'\s*(?:\(|$)", text)
    return m.group(1) if m else None


class Fixup:
    def __init__(self, off, kind, target, base=None):
        self.off, self.kind, self.target, self.base = off, kind, target, base


def encode_method(lines):
    code = bytearray()
    labels = {}
    fixups = []
    tokens = []  # (offset, operand text)
    for raw in lines:
        s = raw.split('//')[0].strip() if not raw.strip().startswith('.data') else raw.strip()
        if not s or s.startswith('.maxstack') or s.startswith('.locals') or s == '.entrypoint':
            continue
        m = LABEL_RE.match(s)
        if m:
            labels[m.group(1)] = len(code)
            continue
        parts = s.split(None, 1)
        mnem, rest = parts[0], (parts[1] if len(parts) > 1 else '')
        v, opnd = OPS[mnem]
        code += opcode_bytes(v)
        if opnd == 'InlineNone':
            pass
        elif opnd in ('ShortInlineVar',):
            code += struct.pack('<B', int(rest))
        elif opnd == 'ShortInlineI':
            code += struct.pack('<b', int(rest)) if mnem == 'ldc.i4.s' else struct.pack('<B', int(rest))
        elif opnd == 'InlineVar':
            code += struct.pack('<H', int(rest))
        elif opnd == 'InlineI':
            code += struct.pack('<i', int(rest))
        elif opnd == 'InlineI8':
            code += struct.pack('<q', int(rest))
        elif opnd == 'ShortInlineR':
            code += struct.pack('<I', int(re.search(r'0x([0-9A-Fa-f]+)', rest).group(1), 16))
        elif opnd == 'InlineR':
            code += struct.pack('<Q', int(re.search(r'0x([0-9A-Fa-f]+)', rest).group(1), 16))
        elif opnd == 'InlineBrTarget':
            # PC-relative to the next instruction: the operand is the last
            # 4 bytes, so value = target - (fixup offset + 4).
            fixups.append(Fixup(len(code), 'br32', rest.strip(), base=len(code) + 4))
            code += b'\0\0\0\0'
        elif opnd == 'InlineSwitch':
            targets = [t.strip() for t in rest.strip()[1:-1].split(',')]
            code += struct.pack('<I', len(targets))
            end = len(code) + 4 * len(targets)
            for t in targets:
                # Relative to the end of the whole instruction: as a PC-relative
                # fixup at offset o, value = target - o + (o - end).
                fixups.append(Fixup(len(code), 'switch32', t, base=end))
                code += b'\0\0\0\0'
        elif opnd in ('InlineMethod', 'InlineField', 'InlineSig', 'InlineTok', 'InlineType'):
            tokens.append((len(code), mnem, rest))
            code += b'\0\0\0\0'
        else:
            raise ValueError('operand type ' + opnd + ' in ' + s)
    for f in fixups:
        code[f.off:f.off + 4] = struct.pack('<i', labels[f.target] - f.base)
    return bytes(code), tokens


def token_matches(mnem, text, desc):
    kind = desc.split(' ', 1)[0]
    if mnem == 'calli':
        return kind == 'StandAloneSig'
    if '::' in text:  # BCL member
        name = text.split('::')[1].split('(')[0]
        return kind == 'MemberRef' and desc.split('::')[-1] == name
    name = quoted_name(text)
    if name is None and mnem == 'call':
        name = text.split('(')[0].split()[-1]
    if mnem in ('ldsfld', 'stsfld', 'ldsflda'):
        return kind == 'FieldDef' and desc.split(' ', 1)[1].split(' [')[0] == name.replace("\\'", "'")
    return kind == 'MethodDef' and desc.split(' ', 1)[1].split(' [')[0] == name.replace("\\'", "'")


stats = Counter()
diffs = []
for fn in sorted(os.listdir(corpus)):
    if not fn.endswith('.il'):
        continue
    text = open(os.path.join(corpus, fn), encoding='utf-8-sig').read().split('\n')
    methods = []
    i = 0
    while i < len(text):
        line = text[i]
        if line.startswith('.method') and 'pinvokeimpl' not in line:
            j = i + 1
            while not text[j].startswith('} // end of method'):
                j += 1
            methods.append(text[i + 2:j])
            i = j
        i += 1
    ref = [l.split('\t') for l in open(os.path.join(bodies_dir, fn[:-3] + '.txt'), encoding='utf-8').read().splitlines()]
    if len(ref) != len(methods):
        stats['method count mismatch'] += 1
        continue
    for body, (name, maxstack, hexcode, toks) in zip(methods, ref):
        code, tokens = encode_method(body)
        ilasm = bytes.fromhex(hexcode)
        stats['methods'] += 1
        stats['bytes'] += len(code)
        # Compare with the token slots masked out.
        ours = bytearray(code)
        theirs = bytearray(ilasm)
        for off, _, _ in tokens:
            ours[off:off + 4] = b'\0\0\0\0'
            if off + 4 <= len(theirs):
                theirs[off:off + 4] = b'\0\0\0\0'
        ref_toks = dict((int(t.split('=', 1)[0]), t.split('=', 1)[1]) for t in toks.split('|') if t)
        tok_ok = all(off in ref_toks and token_matches(m, t, ref_toks[off]) for off, m, t in tokens)
        stats['token slots'] += len(tokens)
        if ours == theirs and tok_ok:
            stats['identical'] += 1
        else:
            stats['different'] += 1
            # first differing byte
            k = next((x for x in range(min(len(ours), len(theirs))) if ours[x] != theirs[x]), min(len(ours), len(theirs)))
            diffs.append((fn, name, len(ours), len(theirs), k, ours[k:k + 8].hex(), theirs[k:k + 8].hex(), tok_ok))
print(dict(stats))
for d in diffs[:20]:
    print('DIFF', d)

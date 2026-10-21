#!/usr/bin/env python3
"""Append a COFF symbol table to a PE/ECMA-335 module (a 'B' object) so that
llvm-nm and llvm-ar see its C symbols.  Feasibility experiment only.

usage: coffsym.py in.o out.o SPEC...
  SPEC = kind:name[:size]
  kind: T (global func) D (global data) t (local) W (weak def) U (undef)
        w (weak undef) C (common, size)
"""
import struct, sys

IMAGE_SYM_CLASS_EXTERNAL = 2
IMAGE_SYM_CLASS_STATIC = 3
IMAGE_SYM_CLASS_WEAK_EXTERNAL = 105
DTYPE_FUNCTION = 0x20

def main():
    src, dst, specs = sys.argv[1], sys.argv[2], sys.argv[3:]
    img = bytearray(open(src, 'rb').read())
    pe = struct.unpack_from('<I', img, 0x3C)[0]
    assert img[pe:pe+4] == b'PE\0\0'
    coff = pe + 4
    machine, nsec, ts, ptrsym, nsym, optsz, chars = struct.unpack_from('<HHIIIHH', img, coff)
    assert ptrsym == 0 and nsym == 0, 'already has a symbol table'
    sechdr = coff + 20 + optsz
    secs = []
    for i in range(nsec):
        name = bytes(img[sechdr + 40*i: sechdr + 40*i + 8]).rstrip(b'\0').decode()
        ch = struct.unpack_from('<I', img, sechdr + 40*i + 36)[0]
        secs.append((name, ch))
    text = 1 + [n for n, _ in secs].index('.text')
    # data symbols point at a data section if there is one, else .text
    data = next((1 + i for i, (n, ch) in enumerate(secs) if ch & 0x40 and not ch & 0x20 and n != '.reloc'), text)

    strtab = bytearray()
    recs = []
    def name_field(n):
        b = n.encode()
        if len(b) <= 8:
            return b.ljust(8, b'\0')
        off = 4 + len(strtab)
        strtab.extend(b + b'\0')
        return struct.pack('<II', 0, off)
    def sym(n, value, secnum, typ, cls, naux=0):
        recs.append(name_field(n) + struct.pack('<IhHBB', value, secnum, typ, cls, naux))
    def aux_weak(tag, chars=3):   # IMAGE_WEAK_EXTERN_SEARCH_ALIAS
        recs.append(struct.pack('<II', tag, chars) + b'\0' * 10)

    for spec in specs:
        parts = spec.split(':')
        kind, n = parts[0], parts[1]
        if kind == 'T': sym(n, 0, text, DTYPE_FUNCTION, IMAGE_SYM_CLASS_EXTERNAL)
        elif kind == 'D': sym(n, 0, data, 0, IMAGE_SYM_CLASS_EXTERNAL)
        elif kind == 't': sym(n, 0, text, DTYPE_FUNCTION, IMAGE_SYM_CLASS_STATIC)
        elif kind == 'U': sym(n, 0, 0, 0, IMAGE_SYM_CLASS_EXTERNAL)
        elif kind == 'C': sym(n, int(parts[2]), 0, 0, IMAGE_SYM_CLASS_EXTERNAL)
        elif kind == 'W':   # weak definition: MinGW-style .weak.<n>.default + weak external
            dflt = len(recs)
            sym('.weak.%s.default' % n, 0, text, DTYPE_FUNCTION, IMAGE_SYM_CLASS_EXTERNAL)
            sym(n, 0, 0, DTYPE_FUNCTION, IMAGE_SYM_CLASS_WEAK_EXTERNAL, 1)
            aux_weak(dflt)
        elif kind == 'w':   # weak undefined: weak external whose default is an undefined null
            dflt = len(recs)
            sym('.weak.%s.null' % n, 0, 0, 0, IMAGE_SYM_CLASS_EXTERNAL)
            sym(n, 0, 0, 0, IMAGE_SYM_CLASS_WEAK_EXTERNAL, 1)
            aux_weak(dflt)
        else:
            raise SystemExit('bad spec ' + spec)

    # symbol table goes at the end of the file, 4-aligned
    while len(img) % 4: img.append(0)
    ptr = len(img)
    for r in recs:
        assert len(r) == 18
        img.extend(r)
    img.extend(struct.pack('<I', 4 + len(strtab)) + strtab)
    struct.pack_into('<II', img, coff + 8, ptr, len(recs))
    open(dst, 'wb').write(img)
    print('%s: %d symbol records at 0x%x, string table %d bytes' % (dst, len(recs), ptr, 4 + len(strtab)))

main()

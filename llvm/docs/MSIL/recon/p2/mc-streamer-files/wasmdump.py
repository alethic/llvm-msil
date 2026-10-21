"""Minimal dump of a wasm relocatable object: sections, the 'linking' symbol
table and the reloc.* sections, to show how MC fixups became relocations."""
import sys

RELOC = {0: 'R_WASM_FUNCTION_INDEX_LEB', 1: 'R_WASM_TABLE_INDEX_SLEB', 2: 'R_WASM_TABLE_INDEX_I32',
         3: 'R_WASM_MEMORY_ADDR_LEB', 4: 'R_WASM_MEMORY_ADDR_SLEB', 5: 'R_WASM_MEMORY_ADDR_I32',
         6: 'R_WASM_TYPE_INDEX_LEB', 7: 'R_WASM_GLOBAL_INDEX_LEB', 8: 'R_WASM_FUNCTION_OFFSET_I32',
         9: 'R_WASM_SECTION_OFFSET_I32', 10: 'R_WASM_TAG_INDEX_LEB', 13: 'R_WASM_GLOBAL_INDEX_I32',
         20: 'R_WASM_TABLE_NUMBER_LEB'}
ADDEND = {3, 4, 5, 8, 9}
SECT = {0: 'custom', 1: 'type', 2: 'import', 3: 'function', 4: 'table', 5: 'memory', 6: 'global',
        7: 'export', 8: 'start', 9: 'elem', 10: 'code', 11: 'data', 12: 'datacount'}
SYMKIND = {0: 'FUNCTION', 1: 'DATA', 2: 'GLOBAL', 3: 'SECTION', 4: 'TAG', 5: 'TABLE'}


class R:
    def __init__(self, b, p=0):
        self.b, self.p = b, p

    def u8(self):
        v = self.b[self.p]; self.p += 1; return v

    def uleb(self):
        r = s = 0
        while True:
            x = self.u8(); r |= (x & 0x7F) << s; s += 7
            if not x & 0x80:
                return r

    def sleb(self):
        r = s = 0
        while True:
            x = self.u8(); r |= (x & 0x7F) << s; s += 7
            if not x & 0x80:
                if x & 0x40:
                    r -= 1 << s
                return r

    def name(self):
        n = self.uleb(); v = self.b[self.p:self.p + n].decode(); self.p += n; return v


data = open(sys.argv[1], 'rb').read()
r = R(data, 8)
idx = 0
while r.p < len(data):
    sid = r.u8(); size = r.uleb(); start = r.p
    if sid == 0:
        nm = r.name()
        print(f'section {idx}: custom "{nm}" ({size} bytes)')
        if nm == 'linking':
            ver = r.uleb()
            while r.p < start + size:
                sub = r.u8(); ssz = r.uleb(); send = r.p + ssz
                if sub == 8:  # WASM_SYMBOL_TABLE
                    for i in range(r.uleb()):
                        kind = r.u8(); flags = r.uleb()
                        undefined = flags & 0x10
                        explicit = flags & 0x40
                        if kind in (0, 2, 4, 5):
                            index = r.uleb()
                            n = r.name() if (not undefined or explicit) else '(import name)'
                            print(f'   sym {i}: {SYMKIND[kind]:8} flags=0x{flags:02x} index={index} {n}')
                        elif kind == 1:
                            n = r.name()
                            if not undefined:
                                seg = r.uleb(); off = r.uleb(); sz = r.uleb()
                                print(f'   sym {i}: DATA     flags=0x{flags:02x} {n} seg={seg} off={off} size={sz}')
                            else:
                                print(f'   sym {i}: DATA     flags=0x{flags:02x} {n} (undefined)')
                        else:
                            sec = r.uleb(); print(f'   sym {i}: SECTION  section={sec}')
                else:
                    print(f'   linking subsection {sub} ({ssz} bytes)')
                r.p = send
        elif nm.startswith('reloc.'):
            target = r.uleb(); n = r.uleb()
            print(f'   relocations for section {target}: {n}')
            for _ in range(n):
                t = r.u8(); off = r.uleb(); sym = r.uleb()
                add = f' addend={r.sleb()}' if t in ADDEND else ''
                print(f'     {RELOC.get(t, t):28} offset=0x{off:x} symbol/type={sym}{add}')
    else:
        print(f'section {idx}: {SECT.get(sid, sid)} ({size} bytes)')
    r.p = start + size
    idx += 1

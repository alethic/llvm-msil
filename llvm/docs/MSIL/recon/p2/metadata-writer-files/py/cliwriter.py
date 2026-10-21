"""Prototype ECMA-335 metadata, CIL body and IL-only PE writer (stdlib only).

This is the design prototype behind llvm/docs/MSIL/recon/p2/metadata-writer.md:
the same layering a C++ library in LLVM would have (heaps -> tables -> #~ ->
metadata root; IL encoder -> method bodies; PE envelope), written byte by
byte from ECMA-335 6th edition Partition II sections 22-25 and Partition III,
without ilasm or System.Reflection.Metadata.
"""
import hashlib
import struct

# ----------------------------------------------------------------------------
# II.23.2 compressed integers
# ----------------------------------------------------------------------------


def compress_u(n):
    if n < 0x80:
        return bytes([n])
    if n < 0x4000:
        return bytes([0x80 | (n >> 8), n & 0xFF])
    if n < 0x20000000:
        return bytes([0xC0 | (n >> 24), (n >> 16) & 0xFF, (n >> 8) & 0xFF, n & 0xFF])
    raise ValueError("compressed integer out of range")


def align(n, a):
    return (n + a - 1) & ~(a - 1)


# ----------------------------------------------------------------------------
# II.24.2.2-24.2.5 heaps
# ----------------------------------------------------------------------------


class Heaps:
    def __init__(self):
        self.strings = bytearray(b"\0")
        self._str = {"": 0}
        self.us = bytearray(b"\0")
        self._us = {}
        self.blob = bytearray(b"\0")
        self._blob = {b"": 0}
        self.guid = bytearray()

    def string(self, s):
        if s is None:
            return 0
        if s not in self._str:
            self._str[s] = len(self.strings)
            self.strings += s.encode("utf-8") + b"\0"
        return self._str[s]

    def user_string(self, s):
        if s not in self._us:
            data = s.encode("utf-16-le")
            # II.24.2.4: the trailing byte is 1 if any UTF-16 code unit has a
            # non-zero high byte or a low byte in 01-08, 0E-1F, 27, 2D, 7F.
            special = any((ord(c) > 0xFF) or (ord(c) in range(0x01, 0x09)) or
                          (ord(c) in range(0x0E, 0x20)) or ord(c) in (0x27, 0x2D, 0x7F)
                          for c in s)
            data += bytes([1 if special else 0])
            self._us[s] = len(self.us)
            self.us += compress_u(len(data)) + data
        return self._us[s]

    def blob_(self, b):
        b = bytes(b)
        if b not in self._blob:
            self._blob[b] = len(self.blob)
            self.blob += compress_u(len(b)) + b
        return self._blob[b]

    def guid_(self, g):
        self.guid += g
        return len(self.guid) // 16  # 1-based index


# ----------------------------------------------------------------------------
# II.22 tables and II.24.2.6 coded indexes
# ----------------------------------------------------------------------------

T = dict(Module=0x00, TypeRef=0x01, TypeDef=0x02, FieldPtr=0x03, Field=0x04, MethodPtr=0x05,
         MethodDef=0x06, ParamPtr=0x07, Param=0x08, InterfaceImpl=0x09, MemberRef=0x0A,
         Constant=0x0B, CustomAttribute=0x0C, FieldMarshal=0x0D, DeclSecurity=0x0E,
         ClassLayout=0x0F, FieldLayout=0x10, StandAloneSig=0x11, EventMap=0x12, EventPtr=0x13,
         Event=0x14, PropertyMap=0x15, PropertyPtr=0x16, Property=0x17, MethodSemantics=0x18,
         MethodImpl=0x19, ModuleRef=0x1A, TypeSpec=0x1B, ImplMap=0x1C, FieldRVA=0x1D,
         ENCLog=0x1E, ENCMap=0x1F, Assembly=0x20, AssemblyProcessor=0x21, AssemblyOS=0x22,
         AssemblyRef=0x23, AssemblyRefProcessor=0x24, AssemblyRefOS=0x25, File=0x26,
         ExportedType=0x27, ManifestResource=0x28, NestedClass=0x29, GenericParam=0x2A,
         MethodSpec=0x2B, GenericParamConstraint=0x2C)

CODED = {
    "TypeDefOrRef": (2, ["TypeDef", "TypeRef", "TypeSpec"]),
    "HasConstant": (2, ["Field", "Param", "Property"]),
    "HasCustomAttribute": (5, ["MethodDef", "Field", "TypeRef", "TypeDef", "Param", "InterfaceImpl",
                               "MemberRef", "Module", "DeclSecurity", "Property", "Event",
                               "StandAloneSig", "ModuleRef", "TypeSpec", "Assembly", "AssemblyRef",
                               "File", "ExportedType", "ManifestResource", "GenericParam",
                               "GenericParamConstraint", "MethodSpec"]),
    "HasFieldMarshal": (1, ["Field", "Param"]),
    "HasDeclSecurity": (2, ["TypeDef", "MethodDef", "Assembly"]),
    "MemberRefParent": (3, ["TypeDef", "TypeRef", "ModuleRef", "MethodDef", "TypeSpec"]),
    "HasSemantics": (1, ["Event", "Property"]),
    "MethodDefOrRef": (1, ["MethodDef", "MemberRef"]),
    "MemberForwarded": (1, ["Field", "MethodDef"]),
    "Implementation": (2, ["File", "AssemblyRef", "ExportedType"]),
    # Tags 0, 1 and 4 are unused.
    "CustomAttributeType": (3, [None, None, "MethodDef", "MemberRef", None]),
    "ResolutionScope": (2, ["Module", "ModuleRef", "AssemblyRef", "TypeRef"]),
    "TypeOrMethodDef": (1, ["TypeDef", "MethodDef"]),
}

# Column kinds: 2/4 = fixed u16/u32, 1 = u8+pad (Constant.Type is "a 1-byte
# constant, followed by a 1-byte padding zero"), 'S' #Strings, 'G' #GUID,
# 'B' #Blob, ('T', table) simple index, ('C', coded) coded index.
SCHEMA = {
    "Module": [2, "S", "G", "G", "G"],
    "TypeRef": [("C", "ResolutionScope"), "S", "S"],
    "TypeDef": [4, "S", "S", ("C", "TypeDefOrRef"), ("T", "Field"), ("T", "MethodDef")],
    "Field": [2, "S", "B"],
    "MethodDef": [4, 2, 2, "S", "B", ("T", "Param")],
    "Param": [2, 2, "S"],
    "InterfaceImpl": [("T", "TypeDef"), ("C", "TypeDefOrRef")],
    "MemberRef": [("C", "MemberRefParent"), "S", "B"],
    "Constant": [2, ("C", "HasConstant"), "B"],
    "CustomAttribute": [("C", "HasCustomAttribute"), ("C", "CustomAttributeType"), "B"],
    "FieldMarshal": [("C", "HasFieldMarshal"), "B"],
    "DeclSecurity": [2, ("C", "HasDeclSecurity"), "B"],
    "ClassLayout": [2, 4, ("T", "TypeDef")],
    "FieldLayout": [4, ("T", "Field")],
    "StandAloneSig": ["B"],
    "ModuleRef": ["S"],
    "TypeSpec": ["B"],
    "ImplMap": [2, ("C", "MemberForwarded"), "S", ("T", "ModuleRef")],
    "FieldRVA": [4, ("T", "Field")],
    "Assembly": [4, 2, 2, 2, 2, 4, "B", "S", "S"],
    "AssemblyRef": [2, 2, 2, 2, 4, "B", "S", "S", "B"],
    "NestedClass": [("T", "TypeDef"), ("T", "TypeDef")],
    "GenericParam": [2, 2, ("C", "TypeOrMethodDef"), "S"],
    "MethodSpec": [("C", "MethodDefOrRef"), "B"],
    "GenericParamConstraint": [("T", "GenericParam"), ("C", "TypeDefOrRef")],
    # The rest of II.22 (a reader must know every row size to find the tables
    # after them; this writer never emits these).
    "FieldPtr": [("T", "Field")],
    "MethodPtr": [("T", "MethodDef")],
    "ParamPtr": [("T", "Param")],
    "EventMap": [("T", "TypeDef"), ("T", "Event")],
    "EventPtr": [("T", "Event")],
    "Event": [2, "S", ("C", "TypeDefOrRef")],
    "PropertyMap": [("T", "TypeDef"), ("T", "Property")],
    "PropertyPtr": [("T", "Property")],
    "Property": [2, "S", "B"],
    "MethodSemantics": [2, ("T", "MethodDef"), ("C", "HasSemantics")],
    "MethodImpl": [("T", "TypeDef"), ("C", "MethodDefOrRef"), ("C", "MethodDefOrRef")],
    "ENCLog": [4, 4],
    "ENCMap": [4],
    "AssemblyProcessor": [4],
    "AssemblyOS": [4, 4, 4],
    "AssemblyRefProcessor": [4, ("T", "AssemblyRef")],
    "AssemblyRefOS": [4, 4, 4, ("T", "AssemblyRef")],
    "File": [4, "S", "B"],
    "ExportedType": [4, 4, "S", "S", ("C", "Implementation")],
    "ManifestResource": [4, 4, "S", ("C", "Implementation")],
}

# II.22: the tables that must be sorted, and their primary key column (then a
# secondary key where the standard gives one). The 'Sorted' bit vector of #~
# is exactly these tables (Roslyn writes 0x000016003301FA00).
SORTED_KEYS = {
    "InterfaceImpl": (0, 1), "Constant": (1,), "CustomAttribute": (0,), "FieldMarshal": (0,),
    "DeclSecurity": (1,), "ClassLayout": (2,), "FieldLayout": (1,), "MethodSemantics": (2,),
    "MethodImpl": (0,), "ImplMap": (1,), "FieldRVA": (1,), "NestedClass": (0,),
    "GenericParam": (2, 0), "GenericParamConstraint": (0,),
}
SORTED_MASK = sum(1 << T[n] for n in SORTED_KEYS)


class Ref:
    """A row reference: (table name, 1-based row)."""
    __slots__ = ("table", "row")

    def __init__(self, table, row):
        self.table, self.row = table, row

    @property
    def token(self):
        return (T[self.table] << 24) | self.row

    def __repr__(self):
        return "%s#%d" % (self.table, self.row)


class Metadata:
    def __init__(self):
        self.h = Heaps()
        self.rows = {name: [] for name in SCHEMA}

    def add(self, table, *values):
        assert len(values) == len(SCHEMA[table]), table
        self.rows[table].append(list(values))
        return Ref(table, len(self.rows[table]))

    # -- index sizes (II.24.2.6) --------------------------------------------
    def _count(self, t):
        return len(self.rows.get(t, []))

    def _simple_size(self, t):
        return 2 if self._count(t) < (1 << 16) else 4

    def _coded_size(self, c):
        bits, tables = CODED[c]
        m = max(self._count(t) for t in tables if t)
        return 2 if m < (1 << (16 - bits)) else 4

    def _coded_value(self, c, ref):
        if ref is None:
            return 0
        bits, tables = CODED[c]
        return (ref.row << bits) | tables.index(ref.table)

    def serialize_tables(self):
        """#~ stream (II.24.2.6)."""
        h = self.h
        # Pad the heaps first: their final sizes decide the heap index sizes.
        for heap in (h.strings, h.us, h.blob):
            while len(heap) % 4:
                heap.append(0)
        heap_sizes = ((1 if len(h.strings) >= 1 << 16 else 0) |
                      (2 if len(h.guid) // 16 >= 1 << 16 else 0) |
                      (4 if len(h.blob) >= 1 << 16 else 0))
        size = {"S": 4 if heap_sizes & 1 else 2, "G": 4 if heap_sizes & 2 else 2,
                "B": 4 if heap_sizes & 4 else 2}
        # Sort the sorted tables; nothing in this prototype refers to their
        # rows by index (GenericParam would need a remap).
        for t, keys in SORTED_KEYS.items():
            if t in self.rows and self.rows[t]:
                col = SCHEMA[t]

                def key(r, col=col, keys=keys):
                    out = []
                    for k in keys:
                        v = r[k]
                        if isinstance(col[k], tuple) and col[k][0] == "C":
                            v = self._coded_value(col[k][1], v)
                        elif isinstance(v, Ref):
                            v = v.row
                        out.append(v)
                    return out
                self.rows[t].sort(key=key)
        valid = 0
        for t, r in self.rows.items():
            if r:
                valid |= 1 << T[t]
        out = bytearray()
        out += struct.pack("<IBBBBQQ", 0, 2, 0, heap_sizes, 1, valid, SORTED_MASK)
        present = sorted((T[t], t) for t, r in self.rows.items() if r)
        for _, t in present:
            out += struct.pack("<I", len(self.rows[t]))
        for _, t in present:
            for r in self.rows[t]:
                for kind, v in zip(SCHEMA[t], r):
                    if kind == 1 or kind == 2:
                        out += struct.pack("<H", v)
                    elif kind == 4:
                        out += struct.pack("<I", v)
                    elif kind in ("S", "G", "B"):
                        out += struct.pack("<H" if size[kind] == 2 else "<I", v)
                    elif kind[0] == "T":
                        n = v.row if isinstance(v, Ref) else v
                        out += struct.pack("<H" if self._simple_size(kind[1]) == 2 else "<I", n)
                    else:
                        n = self._coded_value(kind[1], v)
                        out += struct.pack("<H" if self._coded_size(kind[1]) == 2 else "<I", n)
        while len(out) % 4:
            out.append(0)
        return bytes(out)

    def serialize(self, version="v4.0.30319"):
        """Metadata root (II.24.2.1) with #~, #Strings, #US, #GUID, #Blob."""
        tables = self.serialize_tables()
        h = self.h
        streams = [("#~", tables), ("#Strings", bytes(h.strings)), ("#US", bytes(h.us)),
                   ("#GUID", bytes(h.guid)), ("#Blob", bytes(h.blob))]
        ver = version.encode() + b"\0"
        ver += b"\0" * (align(len(ver), 4) - len(ver))
        hdr = struct.pack("<IHHII", 0x424A5342, 1, 1, 0, len(ver)) + ver
        hdr += struct.pack("<HH", 0, len(streams))
        names = []
        for name, _ in streams:
            n = name.encode() + b"\0"
            names.append(n + b"\0" * (align(len(n), 4) - len(n)))
        off = len(hdr) + sum(8 + len(n) for n in names)
        body = bytearray()
        sh = bytearray()
        for (name, data), n in zip(streams, names):
            sh += struct.pack("<II", off + len(body), len(data)) + n
            body += data
        out = hdr + sh + body
        self.guid_offset = len(hdr) + len(sh) + len(tables) + len(h.strings) + len(h.us)
        return bytes(out)


# ----------------------------------------------------------------------------
# II.23.2 signatures
# ----------------------------------------------------------------------------

ELEMENT = dict(void=0x01, bool=0x02, char=0x03, int8=0x04, uint8=0x05, int16=0x06, uint16=0x07,
               int32=0x08, uint32=0x09, int64=0x0A, uint64=0x0B, float32=0x0C, float64=0x0D,
               string=0x0E, ptr=0x0F, byref=0x10, valuetype=0x11, class_=0x12, var=0x13,
               array=0x14, genericinst=0x15, typedbyref=0x16, native_int=0x18,
               native_uint=0x19, fnptr=0x1B, object=0x1C, szarray=0x1D, mvar=0x1E,
               cmod_reqd=0x1F, cmod_opt=0x20, sentinel=0x41, pinned=0x45)

CC_DEFAULT, CC_C, CC_STDCALL, CC_THISCALL, CC_FASTCALL, CC_VARARG, CC_UNMANAGED = 0, 1, 2, 3, 4, 5, 9
CC_FIELD, CC_LOCAL, CC_PROPERTY, CC_GENERICINST = 0x06, 0x07, 0x08, 0x0A
CC_HASTHIS, CC_EXPLICITTHIS, CC_GENERIC = 0x20, 0x40, 0x10


def type_def_or_ref_encoded(ref):
    tag = {"TypeDef": 0, "TypeRef": 1, "TypeSpec": 2}[ref.table]
    return compress_u((ref.row << 2) | tag)


def ty(t):
    """A type: an element-type name, ('valuetype', ref), ('class', ref),
    ('szarray', t), ('ptr', t), ('fnptr', methodsig bytes)."""
    if isinstance(t, str):
        return bytes([ELEMENT[t]])
    kind = t[0]
    if kind == "valuetype":
        return bytes([0x11]) + type_def_or_ref_encoded(t[1])
    if kind == "class":
        return bytes([0x12]) + type_def_or_ref_encoded(t[1])
    if kind in ("szarray", "ptr", "byref", "pinned"):
        return bytes([ELEMENT[kind]]) + ty(t[1])
    if kind == "fnptr":
        return bytes([0x1B]) + t[1]
    raise ValueError(t)


def method_sig(ret, params, cc=CC_DEFAULT):
    return bytes([cc]) + compress_u(len(params)) + ty(ret) + b"".join(ty(p) for p in params)


def field_sig(t):
    return bytes([CC_FIELD]) + ty(t)


def locals_sig(types):
    return bytes([CC_LOCAL]) + compress_u(len(types)) + b"".join(ty(t) for t in types)


def ser_string(s):
    if s is None:
        return b"\xFF"
    b = s.encode("utf-8")
    return compress_u(len(b)) + b


# ----------------------------------------------------------------------------
# III CIL encoder with branch relaxation, and II.25.4 method bodies
# ----------------------------------------------------------------------------

from cilopcodes import BY_NAME, SHORT_BRANCH, OPERAND_SIZE, opcode_size  # noqa: E402
import cilopcodes as K  # noqa: E402


class Label:
    pass


class IL:
    """A list of instructions with symbolic labels. Branches are written in
    the long form and relaxed to the short form when every offset fits."""

    def __init__(self):
        self.items = []  # ('op', name, operand) or ('label', Label)

    def label(self):
        return Label()

    def mark(self, lab):
        self.items.append(("label", lab))

    def __call__(self, name, operand=None):
        assert name in BY_NAME, name
        self.items.append(("op", name, operand))
        return self

    def _layout(self, short):
        """Offsets of every item given the set of branches in short form."""
        pos, offs, labs = 0, [], {}
        for i, it in enumerate(self.items):
            offs.append(pos)
            if it[0] == "label":
                labs[it[1]] = pos
                continue
            name = short.get(i, it[1])
            v, k = BY_NAME[name]
            pos += opcode_size(v)
            pos += 4 + 4 * len(it[2]) if k == K.SWITCH else OPERAND_SIZE[k]
        return offs, labs, pos

    def assemble(self, relax=True):
        # Start with every relaxable branch short; widen the ones that do not
        # fit and repeat (widening only grows code, so this terminates).
        short = {i: SHORT_BRANCH[it[1]] for i, it in enumerate(self.items)
                 if relax and it[0] == "op" and it[1] in SHORT_BRANCH}
        while True:
            offs, labs, end = self._layout(short)
            bad = []
            for i, s in short.items():
                nxt = offs[i] + 2
                d = labs[self.items[i][2]] - nxt
                if not -128 <= d <= 127:
                    bad.append(i)
            if not bad:
                break
            for i in bad:
                del short[i]
        offs, labs, end = self._layout(short)
        out = bytearray()
        for i, it in enumerate(self.items):
            if it[0] == "label":
                continue
            name = short.get(i, it[1])
            v, k = BY_NAME[name]
            out += bytes([v >> 8, v & 0xFF]) if v >= 0xFE00 else bytes([v])
            opnd = it[2]
            if k == K.NONE:
                pass
            elif k in (K.VAR8,):
                out += struct.pack("<B", opnd)
            elif k == K.I8:
                out += struct.pack("<b" if name != "unaligned." and name != "no." else "<B", opnd)
            elif k == K.VAR16:
                out += struct.pack("<H", opnd)
            elif k == K.I32:
                out += struct.pack("<i", opnd)
            elif k == K.I64:
                out += struct.pack("<q", opnd)
            elif k == K.R4:
                out += struct.pack("<I", opnd)  # raw bits: exact NaN payloads
            elif k == K.R8:
                out += struct.pack("<Q", opnd)
            elif k in (K.BR8, K.BR32):
                size = 1 if k == K.BR8 else 4
                nxt = len(out) + size
                out += struct.pack("<b" if size == 1 else "<i", labs[opnd] - nxt)
            elif k == K.SWITCH:
                nxt = len(out) + 4 + 4 * len(opnd)
                out += struct.pack("<I", len(opnd))
                for lab in opnd:
                    out += struct.pack("<i", labs[lab] - nxt)
            else:  # tokens
                tok = opnd.token if isinstance(opnd, Ref) else opnd
                out += struct.pack("<I", tok)
        assert len(out) == end
        self.labels = labs
        return bytes(out)


EH_CATCH, EH_FILTER, EH_FINALLY, EH_FAULT = 0, 1, 2, 4


def method_body(code, max_stack, local_sig_token=0, init_locals=False, eh=(), force_fat_eh=False):
    """II.25.4: a tiny header when allowed, else a fat header (4-aligned by
    the caller) followed by the code and the 4-aligned EH data sections.
    eh: (kind, try_off, try_len, h_off, h_len, class_token_or_filter_off)."""
    if len(code) < 64 and max_stack <= 8 and not local_sig_token and not eh:
        return bytes([(len(code) << 2) | 0x2]) + code, False
    flags = 0x3 | (0x8 if eh else 0) | (0x10 if init_locals else 0)
    out = bytearray(struct.pack("<HHII", flags | (3 << 12), max_stack, len(code),
                                local_sig_token))
    out += code
    if eh:
        while len(out) % 4:
            out.append(0)
        small = not force_fat_eh and (len(eh) * 12 + 4 <= 0xFF and
                 all(t <= 0xFFFF and tl <= 0xFF and h <= 0xFFFF and hl <= 0xFF
                     for _, t, tl, h, hl, _ in eh))
        if small:
            out += struct.pack("<BBH", 0x01, len(eh) * 12 + 4, 0)
            for k, t, tl, h, hl, x in eh:
                out += struct.pack("<HHBHBI", k, t, tl, h, hl, x)
        else:
            n = len(eh) * 24 + 4
            out += struct.pack("<I", 0x41 | (n << 8))
            for k, t, tl, h, hl, x in eh:
                out += struct.pack("<IIIIII", k, t, tl, h, hl, x)
    return bytes(out), True


# ----------------------------------------------------------------------------
# II.25 PE envelope
# ----------------------------------------------------------------------------

IMAGE_FILE_MACHINE_I386, IMAGE_FILE_MACHINE_AMD64, IMAGE_FILE_MACHINE_ARM64 = 0x14C, 0x8664, 0xAA64
COMIMAGE_FLAGS_ILONLY, COMIMAGE_FLAGS_32BITREQUIRED, COMIMAGE_FLAGS_32BITPREFERRED = 0x1, 0x2, 0x20000

DOS_STUB = bytes.fromhex(
    "4d5a90000300000004000000ffff0000b8000000000000004000000000000000"
    "0000000000000000000000000000000000000000000000000000000080000000"
    "0e1fba0e00b409cd21b8014ccd21546869732070726f6772616d2063616e6e6f"
    "742062652072756e20696e20444f53206d6f64652e0d0d0a2400000000000000")
assert len(DOS_STUB) == 128


class PEOptions:
    def __init__(self, machine=IMAGE_FILE_MACHINE_AMD64, dll=True, cor_flags=COMIMAGE_FLAGS_ILONLY,
                 mscoree_stub=False, section_align=0x2000, file_align=0x200,
                 image_base=None, deterministic=True, timestamp=0, rva_data_in_text=True):
        self.machine, self.dll, self.cor_flags = machine, dll, cor_flags
        self.mscoree_stub = mscoree_stub
        self.section_align, self.file_align = section_align, file_align
        self.pe32plus = machine != IMAGE_FILE_MACHINE_I386
        if image_base is None:
            image_base = (0x180000000 if dll else 0x140000000) if self.pe32plus else 0x400000
        self.image_base = image_base
        self.deterministic, self.timestamp = deterministic, timestamp
        self.rva_data_in_text = rva_data_in_text


class Image:
    """Lays out .text (CLI header, IL bodies, FieldRVA data, metadata, and
    optionally the mscoree import and entry stub), an optional .sdata section
    for FieldRVA data and an optional .reloc, then writes the PE file."""

    TEXT_RVA = 0x2000

    def __init__(self, md, opts):
        self.md, self.opts = md, opts
        self.bodies = bytearray()      # IL method bodies, offsets relative to the IL stream
        self.rva_data = bytearray()    # FieldRVA blobs

    def add_body(self, body, fat):
        if fat:
            while len(self.bodies) % 4:
                self.bodies.append(0)
        off = len(self.bodies)
        self.bodies += body
        return off

    def add_rva_data(self, data, alignment=8):
        while len(self.rva_data) % alignment:
            self.rva_data.append(0)
        off = len(self.rva_data)
        self.rva_data += data
        return off

    def write(self, path, entry_token, body_rva_fixups, field_rva_fixups, module_name):
        """body_rva_fixups: [(MethodDef row index into md.rows, body offset)];
        field_rva_fixups: [(FieldRVA row list, data offset)]."""
        o = self.opts
        ptr = 8 if o.pe32plus else 4
        text = bytearray()
        iat_off = None
        if o.mscoree_stub:
            iat_off = 0
            text += b"\0" * (2 * ptr)      # IAT: one entry + terminator
        cli_off = len(text)
        text += b"\0" * 72                  # IMAGE_COR20_HEADER, filled below
        while len(text) % 4:
            text.append(0)
        il_off = len(text)
        text += self.bodies
        rva_off = None
        if o.rva_data_in_text and self.rva_data:
            while len(text) % 8:
                text.append(0)
            rva_off = len(text)
            text += self.rva_data
        while len(text) % 4:
            text.append(0)
        T0 = self.TEXT_RVA
        for row, off in body_rva_fixups:
            row[0] = T0 + il_off + off
        md_off = len(text)
        md_bytes = self.md.serialize()          # FieldRVA rows may still be placeholders
        imp_off = stub_off = None
        reloc = None
        imp = bytearray()
        if o.mscoree_stub:
            # Import directory, ILT, hint/name, DLL name and the entry stub,
            # laid out after the metadata (ilasm puts them at the end of .text).
            imp_off = md_off + len(md_bytes)
            assert imp_off % 4 == 0
            ilt_off = imp_off + 40
            hint_off = ilt_off + 2 * ptr
            name = b"_CorDllMain\0" if o.dll else b"_CorExeMain\0"
            dll_off = hint_off + 2 + len(name)
            dll_off += dll_off & 1
            stub_off = align(dll_off + 12, 4) + 2   # the stub's operand is 4-aligned
            end_off = stub_off + (12 if o.pe32plus else 6)
            imp = bytearray(end_off - imp_off)
            def put(fmt, at, *v):
                struct.pack_into(fmt, imp, at - imp_off, *v)
            put("<IIIII", imp_off, T0 + ilt_off, 0, 0, T0 + dll_off, T0 + iat_off)
            put("<Q" if o.pe32plus else "<I", ilt_off, T0 + hint_off)
            struct.pack_into("<Q" if o.pe32plus else "<I", text, iat_off, T0 + hint_off)
            imp[hint_off + 2 - imp_off:hint_off + 2 + len(name) - imp_off] = name
            imp[dll_off - imp_off:dll_off + 12 - imp_off] = b"mscoree.dll\0"
            iat_va = o.image_base + T0 + iat_off
            if o.pe32plus:   # mov rax, [abs64 IAT]; jmp rax (what ilasm -pe64 -x64 writes)
                imp[stub_off - imp_off:stub_off - imp_off + 12] = bytes([0x48, 0xA1]) + struct.pack("<Q", iat_va) + bytes([0xFF, 0xE0])
                reloc_at, reloc_type = T0 + stub_off + 2, 10      # IMAGE_REL_BASED_DIR64
            else:            # jmp [abs32 IAT]
                imp[stub_off - imp_off:stub_off - imp_off + 6] = bytes([0xFF, 0x25]) + struct.pack("<I", iat_va)
                reloc_at, reloc_type = T0 + stub_off + 2, 3       # IMAGE_REL_BASED_HIGHLOW
            page = reloc_at & ~0xFFF
            reloc = struct.pack("<IIHH", page, 12, (reloc_type << 12) | (reloc_at - page), 0)
        text_len = md_off + len(md_bytes) + len(imp)
        sdata = None
        if self.rva_data and not o.rva_data_in_text:
            base = align(T0 + text_len, o.section_align)
            sdata = bytes(self.rva_data)
        else:
            base = T0 + (rva_off or 0)
        for row, off in field_rva_fixups:
            row[0] = base + off
        md2 = self.md.serialize()
        assert len(md2) == len(md_bytes)
        md_bytes = md2
        text += md_bytes
        text += imp
        # CLI header.
        struct.pack_into("<IHHIIII", text, cli_off, 72, 2, 5, T0 + md_off, len(md_bytes),
                         o.cor_flags, entry_token)
        # Sections.
        sections = [(".text", bytes(text), 0x60000020)]
        if sdata is not None:
            sections.append((".sdata", sdata, 0xC0000040))
        if reloc is not None:
            sections.append((".reloc", reloc, 0x42000040))
        opt_size = 240 if o.pe32plus else 224
        hdr_size = align(128 + 4 + 20 + opt_size + 40 * len(sections), o.file_align)
        rva, fptr, layout = T0, hdr_size, []
        for name, data, ch in sections:
            layout.append((name, data, ch, rva, fptr, align(len(data), o.file_align)))
            rva = align(rva + len(data), o.section_align)
            fptr += align(len(data), o.file_align)
        if sdata is not None:
            assert layout[1][3] == base, "FieldRVA base mismatch"
        size_of_image = rva
        out = bytearray(fptr)
        out[0:128] = DOS_STUB
        p = 128
        out[p:p + 4] = b"PE\0\0"
        p += 4
        chars = 0x0002 | 0x0020 | (0x2000 if o.dll else 0)  # EXECUTABLE_IMAGE, LARGE_ADDRESS_AWARE, DLL
        if not o.pe32plus:
            chars = 0x0002 | 0x0100 | (0x2000 if o.dll else 0)  # + 32BIT_MACHINE
        struct.pack_into("<HHIIIHH", out, p, o.machine, len(sections), o.timestamp, 0, 0, opt_size, chars)
        p += 20
        code_size = layout[0][5]
        init_size = sum(l[5] for l in layout[1:])
        entry_rva = T0 + stub_off if stub_off is not None else 0
        dllchars = 0x8540  # DYNAMIC_BASE | NX_COMPAT | NO_SEH | TERMINAL_SERVER_AWARE
        if o.pe32plus:
            struct.pack_into("<HBBIIIII", out, p, 0x20B, 11, 0, code_size, init_size, 0, entry_rva, T0)
            q = p + 24
            struct.pack_into("<QIIHHHHHHIIIIHHQQQQII", out, q, o.image_base, o.section_align, o.file_align,
                             4, 0, 0, 0, 4, 0, 0, size_of_image, hdr_size, 0, 3, dllchars,
                             0x400000, 0x4000, 0x100000, 0x2000, 0, 16)
            dirs = p + 112
        else:
            struct.pack_into("<HBBIIIIII", out, p, 0x10B, 11, 0, code_size, init_size, 0, entry_rva, T0, 0)
            q = p + 28
            struct.pack_into("<IIIHHHHHHIIIIHHIIIIII", out, q, o.image_base, o.section_align, o.file_align,
                             4, 0, 0, 0, 4, 0, 0, size_of_image, hdr_size, 0, 3, dllchars,
                             0x100000, 0x1000, 0x100000, 0x1000, 0, 16)
            dirs = p + 96
        if imp_off is not None:
            struct.pack_into("<II", out, dirs + 1 * 8, T0 + imp_off, 0x53)
            struct.pack_into("<II", out, dirs + 12 * 8, T0 + iat_off, 2 * ptr)
        if reloc is not None:
            struct.pack_into("<II", out, dirs + 5 * 8, layout[-1][3], len(reloc))
        struct.pack_into("<II", out, dirs + 14 * 8, T0 + cli_off, 72)
        sp = p + opt_size
        for name, data, ch, srva, sptr, sraw in layout:
            struct.pack_into("<8sIIIIIIHHI", out, sp, name.encode(), len(data), srva, sraw, sptr,
                             0, 0, 0, 0, ch)
            sp += 40
            out[sptr:sptr + len(data)] = data
        # Deterministic MVID and timestamp (Roslyn's BlobContentId.FromHash
        # scheme): hash the image with both zero, then patch them in.
        mvid_pos = layout[0][4] + md_off + self.md.guid_offset  # first #GUID entry = MVID
        assert bytes(out[mvid_pos:mvid_pos + 16]) == b"\0" * 16
        if o.deterministic:
            dig = hashlib.sha256(bytes(out)).digest()
            g = bytearray(dig[:16])
            g[7] = (g[7] & 0x0F) | 0x40     # version 4 (Guid byte 7 = high byte of Data3)
            g[8] = (g[8] & 0x3F) | 0x80     # RFC 4122 variant
            out[mvid_pos:mvid_pos + 16] = g
            stamp = struct.unpack("<I", dig[16:20])[0] | 0x80000000
            struct.pack_into("<I", out, 128 + 4 + 4, stamp)
        with open(path, "wb") as f:
            f.write(out)
        return dict(text_size=len(text), md_size=len(md_bytes), il_size=len(self.bodies),
                    file_size=len(out), mvid_file_offset=mvid_pos)

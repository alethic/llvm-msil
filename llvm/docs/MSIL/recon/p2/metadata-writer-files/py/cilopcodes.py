"""ECMA-335 Partition III opcode table (6th edition, June 2012), typed in from
the standard: name, value, operand kind. Two-byte opcodes are 0xFEnn. The
experiment checks this table against System.Reflection.Emit.OpCodes and
against ilasm-assembled bytes (check_opcodes.py)."""

# Operand kinds (ECMA-335 III.1.2 / VI.C.4 names).
NONE = "InlineNone"
VAR8 = "ShortInlineVar"      # uint8 arg/local index
VAR16 = "InlineVar"          # uint16 arg/local index
I8 = "ShortInlineI"          # int8 (unaligned. and no. take an uint8)
I32 = "InlineI"
I64 = "InlineI8"
R4 = "ShortInlineR"
R8 = "InlineR"
BR8 = "ShortInlineBrTarget"  # int8 offset from the next instruction
BR32 = "InlineBrTarget"      # int32 offset from the next instruction
METHOD = "InlineMethod"      # MethodDef, MemberRef or MethodSpec token
FIELD = "InlineField"        # Field or MemberRef token
TYPE = "InlineType"          # TypeDef, TypeRef or TypeSpec token
TOK = "InlineTok"            # any of the three above
SIG = "InlineSig"            # StandAloneSig token
STRING = "InlineString"      # #US token (0x70xxxxxx)
SWITCH = "InlineSwitch"      # uint32 n, then n int32 offsets from the end

OPCODES = [
    ("nop", 0x00, NONE), ("break", 0x01, NONE),
    ("ldarg.0", 0x02, NONE), ("ldarg.1", 0x03, NONE), ("ldarg.2", 0x04, NONE), ("ldarg.3", 0x05, NONE),
    ("ldloc.0", 0x06, NONE), ("ldloc.1", 0x07, NONE), ("ldloc.2", 0x08, NONE), ("ldloc.3", 0x09, NONE),
    ("stloc.0", 0x0A, NONE), ("stloc.1", 0x0B, NONE), ("stloc.2", 0x0C, NONE), ("stloc.3", 0x0D, NONE),
    ("ldarg.s", 0x0E, VAR8), ("ldarga.s", 0x0F, VAR8), ("starg.s", 0x10, VAR8),
    ("ldloc.s", 0x11, VAR8), ("ldloca.s", 0x12, VAR8), ("stloc.s", 0x13, VAR8),
    ("ldnull", 0x14, NONE),
    ("ldc.i4.m1", 0x15, NONE), ("ldc.i4.0", 0x16, NONE), ("ldc.i4.1", 0x17, NONE), ("ldc.i4.2", 0x18, NONE),
    ("ldc.i4.3", 0x19, NONE), ("ldc.i4.4", 0x1A, NONE), ("ldc.i4.5", 0x1B, NONE), ("ldc.i4.6", 0x1C, NONE),
    ("ldc.i4.7", 0x1D, NONE), ("ldc.i4.8", 0x1E, NONE), ("ldc.i4.s", 0x1F, I8),
    ("ldc.i4", 0x20, I32), ("ldc.i8", 0x21, I64), ("ldc.r4", 0x22, R4), ("ldc.r8", 0x23, R8),
    ("dup", 0x25, NONE), ("pop", 0x26, NONE), ("jmp", 0x27, METHOD), ("call", 0x28, METHOD),
    ("calli", 0x29, SIG), ("ret", 0x2A, NONE),
    ("br.s", 0x2B, BR8), ("brfalse.s", 0x2C, BR8), ("brtrue.s", 0x2D, BR8), ("beq.s", 0x2E, BR8),
    ("bge.s", 0x2F, BR8), ("bgt.s", 0x30, BR8), ("ble.s", 0x31, BR8), ("blt.s", 0x32, BR8),
    ("bne.un.s", 0x33, BR8), ("bge.un.s", 0x34, BR8), ("bgt.un.s", 0x35, BR8), ("ble.un.s", 0x36, BR8),
    ("blt.un.s", 0x37, BR8),
    ("br", 0x38, BR32), ("brfalse", 0x39, BR32), ("brtrue", 0x3A, BR32), ("beq", 0x3B, BR32),
    ("bge", 0x3C, BR32), ("bgt", 0x3D, BR32), ("ble", 0x3E, BR32), ("blt", 0x3F, BR32),
    ("bne.un", 0x40, BR32), ("bge.un", 0x41, BR32), ("bgt.un", 0x42, BR32), ("ble.un", 0x43, BR32),
    ("blt.un", 0x44, BR32), ("switch", 0x45, SWITCH),
    ("ldind.i1", 0x46, NONE), ("ldind.u1", 0x47, NONE), ("ldind.i2", 0x48, NONE), ("ldind.u2", 0x49, NONE),
    ("ldind.i4", 0x4A, NONE), ("ldind.u4", 0x4B, NONE), ("ldind.i8", 0x4C, NONE), ("ldind.i", 0x4D, NONE),
    ("ldind.r4", 0x4E, NONE), ("ldind.r8", 0x4F, NONE), ("ldind.ref", 0x50, NONE), ("stind.ref", 0x51, NONE),
    ("stind.i1", 0x52, NONE), ("stind.i2", 0x53, NONE), ("stind.i4", 0x54, NONE), ("stind.i8", 0x55, NONE),
    ("stind.r4", 0x56, NONE), ("stind.r8", 0x57, NONE),
    ("add", 0x58, NONE), ("sub", 0x59, NONE), ("mul", 0x5A, NONE), ("div", 0x5B, NONE), ("div.un", 0x5C, NONE),
    ("rem", 0x5D, NONE), ("rem.un", 0x5E, NONE), ("and", 0x5F, NONE), ("or", 0x60, NONE), ("xor", 0x61, NONE),
    ("shl", 0x62, NONE), ("shr", 0x63, NONE), ("shr.un", 0x64, NONE), ("neg", 0x65, NONE), ("not", 0x66, NONE),
    ("conv.i1", 0x67, NONE), ("conv.i2", 0x68, NONE), ("conv.i4", 0x69, NONE), ("conv.i8", 0x6A, NONE),
    ("conv.r4", 0x6B, NONE), ("conv.r8", 0x6C, NONE), ("conv.u4", 0x6D, NONE), ("conv.u8", 0x6E, NONE),
    ("callvirt", 0x6F, METHOD), ("cpobj", 0x70, TYPE), ("ldobj", 0x71, TYPE), ("ldstr", 0x72, STRING),
    ("newobj", 0x73, METHOD), ("castclass", 0x74, TYPE), ("isinst", 0x75, TYPE), ("conv.r.un", 0x76, NONE),
    ("unbox", 0x79, TYPE), ("throw", 0x7A, NONE),
    ("ldfld", 0x7B, FIELD), ("ldflda", 0x7C, FIELD), ("stfld", 0x7D, FIELD),
    ("ldsfld", 0x7E, FIELD), ("ldsflda", 0x7F, FIELD), ("stsfld", 0x80, FIELD), ("stobj", 0x81, TYPE),
    ("conv.ovf.i1.un", 0x82, NONE), ("conv.ovf.i2.un", 0x83, NONE), ("conv.ovf.i4.un", 0x84, NONE),
    ("conv.ovf.i8.un", 0x85, NONE), ("conv.ovf.u1.un", 0x86, NONE), ("conv.ovf.u2.un", 0x87, NONE),
    ("conv.ovf.u4.un", 0x88, NONE), ("conv.ovf.u8.un", 0x89, NONE), ("conv.ovf.i.un", 0x8A, NONE),
    ("conv.ovf.u.un", 0x8B, NONE),
    ("box", 0x8C, TYPE), ("newarr", 0x8D, TYPE), ("ldlen", 0x8E, NONE), ("ldelema", 0x8F, TYPE),
    ("ldelem.i1", 0x90, NONE), ("ldelem.u1", 0x91, NONE), ("ldelem.i2", 0x92, NONE), ("ldelem.u2", 0x93, NONE),
    ("ldelem.i4", 0x94, NONE), ("ldelem.u4", 0x95, NONE), ("ldelem.i8", 0x96, NONE), ("ldelem.i", 0x97, NONE),
    ("ldelem.r4", 0x98, NONE), ("ldelem.r8", 0x99, NONE), ("ldelem.ref", 0x9A, NONE),
    ("stelem.i", 0x9B, NONE), ("stelem.i1", 0x9C, NONE), ("stelem.i2", 0x9D, NONE), ("stelem.i4", 0x9E, NONE),
    ("stelem.i8", 0x9F, NONE), ("stelem.r4", 0xA0, NONE), ("stelem.r8", 0xA1, NONE), ("stelem.ref", 0xA2, NONE),
    ("ldelem", 0xA3, TYPE), ("stelem", 0xA4, TYPE), ("unbox.any", 0xA5, TYPE),
    ("conv.ovf.i1", 0xB3, NONE), ("conv.ovf.u1", 0xB4, NONE), ("conv.ovf.i2", 0xB5, NONE),
    ("conv.ovf.u2", 0xB6, NONE), ("conv.ovf.i4", 0xB7, NONE), ("conv.ovf.u4", 0xB8, NONE),
    ("conv.ovf.i8", 0xB9, NONE), ("conv.ovf.u8", 0xBA, NONE),
    ("refanyval", 0xC2, TYPE), ("ckfinite", 0xC3, NONE), ("mkrefany", 0xC6, TYPE), ("ldtoken", 0xD0, TOK),
    ("conv.u2", 0xD1, NONE), ("conv.u1", 0xD2, NONE), ("conv.i", 0xD3, NONE), ("conv.ovf.i", 0xD4, NONE),
    ("conv.ovf.u", 0xD5, NONE), ("add.ovf", 0xD6, NONE), ("add.ovf.un", 0xD7, NONE), ("mul.ovf", 0xD8, NONE),
    ("mul.ovf.un", 0xD9, NONE), ("sub.ovf", 0xDA, NONE), ("sub.ovf.un", 0xDB, NONE),
    ("endfinally", 0xDC, NONE), ("leave", 0xDD, BR32), ("leave.s", 0xDE, BR8), ("stind.i", 0xDF, NONE),
    ("conv.u", 0xE0, NONE),
    # Two-byte opcodes (0xFE prefix).
    ("arglist", 0xFE00, NONE), ("ceq", 0xFE01, NONE), ("cgt", 0xFE02, NONE), ("cgt.un", 0xFE03, NONE),
    ("clt", 0xFE04, NONE), ("clt.un", 0xFE05, NONE), ("ldftn", 0xFE06, METHOD), ("ldvirtftn", 0xFE07, METHOD),
    ("ldarg", 0xFE09, VAR16), ("ldarga", 0xFE0A, VAR16), ("starg", 0xFE0B, VAR16),
    ("ldloc", 0xFE0C, VAR16), ("ldloca", 0xFE0D, VAR16), ("stloc", 0xFE0E, VAR16),
    ("localloc", 0xFE0F, NONE), ("endfilter", 0xFE11, NONE), ("unaligned.", 0xFE12, I8),
    ("volatile.", 0xFE13, NONE), ("tail.", 0xFE14, NONE), ("initobj", 0xFE15, TYPE),
    ("constrained.", 0xFE16, TYPE), ("cpblk", 0xFE17, NONE), ("initblk", 0xFE18, NONE),
    ("no.", 0xFE19, I8), ("rethrow", 0xFE1A, NONE), ("sizeof", 0xFE1C, TYPE), ("refanytype", 0xFE1D, NONE),
    ("readonly.", 0xFE1E, NONE),
]

BY_NAME = {n: (v, k) for n, v, k in OPCODES}

# Short/long branch pairs (relaxation chooses the short form when it fits).
SHORT_BRANCH = {
    "br": "br.s", "brfalse": "brfalse.s", "brtrue": "brtrue.s", "beq": "beq.s", "bge": "bge.s",
    "bgt": "bgt.s", "ble": "ble.s", "blt": "blt.s", "bne.un": "bne.un.s", "bge.un": "bge.un.s",
    "bgt.un": "bgt.un.s", "ble.un": "ble.un.s", "blt.un": "blt.un.s", "leave": "leave.s",
}


def opcode_size(value):
    return 2 if value >= 0xFE00 else 1


OPERAND_SIZE = {NONE: 0, VAR8: 1, VAR16: 2, I8: 1, I32: 4, I64: 8, R4: 4, R8: 8, BR8: 1, BR32: 4,
                METHOD: 4, FIELD: 4, TYPE: 4, TOK: 4, SIG: 4, STRING: 4}

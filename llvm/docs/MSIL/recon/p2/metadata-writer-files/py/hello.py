"""Writes a Phase-1-shaped hello-world assembly byte by byte with cliwriter.py
(no ilasm): a data image in FieldRVA memory copied by <Module>::.cctor into
AllocHGlobal memory, a P/Invoke of puts, a #US string, a calli through an
address field set with ldftn, an exception filter, a loop with short
branches, a switch, a long branch, a method with a 2000-byte name, a
float32 signaling-NaN constant and an assembly-level custom attribute.
main returns a bit mask of passed checks (127 = all)."""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cliwriter import (Metadata, Image, PEOptions, Ref, IL, method_body, method_sig, field_sig,  # noqa
                       locals_sig, ser_string, EH_FILTER, IMAGE_FILE_MACHINE_AMD64,
                       IMAGE_FILE_MACHINE_I386, COMIMAGE_FLAGS_ILONLY, COMIMAGE_FLAGS_32BITREQUIRED,
                       COMIMAGE_FLAGS_32BITPREFERRED)

ap = argparse.ArgumentParser()
ap.add_argument("-o", required=True)
ap.add_argument("--os", default="win", choices=["win", "linux"])
ap.add_argument("--exe", action="store_true")
ap.add_argument("--stub", action="store_true", help="mscoree import + entry stub + .reloc")
ap.add_argument("--pe32", action="store_true", help="PE32, machine I386")
ap.add_argument("--req32", action="store_true", help="COMIMAGE_FLAGS_32BITREQUIRED")
ap.add_argument("--pref32", action="store_true", help="COMIMAGE_FLAGS_32BITPREFERRED")
ap.add_argument("--sdata", action="store_true", help="FieldRVA data in a writable .sdata section")
ap.add_argument("--rvawrite", action="store_true", help="also write into the RVA field (crash test)")
ap.add_argument("--nondeterministic", action="store_true")
ap.add_argument("--many-fields", type=int, default=0, help="extra static fields (forces 4-byte indexes)")
ap.add_argument("--split-fields", action="store_true", help="put the extra fields on a separate class G")
ap.add_argument("--many-methods", type=int, default=0, help="extra methods on <Module> sharing one body")
ap.add_argument("--longlen", type=int, default=2000, help="length of the long method name")
ap.add_argument("--fat-eh", action="store_true", help="force the fat EH section format")
ap.add_argument("--snan-probe", choices=["const", "move", "raw"], help="main returns the stored float32 bits")
a = ap.parse_args()

name = os.path.splitext(os.path.basename(a.o))[0]
md = Metadata()
h = md.h
mvid = h.guid_(b"\0" * 16)
md.add("Module", 0, h.string(os.path.basename(a.o)), mvid, 0, 0)

mscorlib = md.add("AssemblyRef", 4, 0, 0, 0, 0, h.blob_(bytes.fromhex("B77A5C561934E089")),
                  h.string("mscorlib"), 0, 0)
tr_valuetype = md.add("TypeRef", mscorlib, h.string("ValueType"), h.string("System"))
tr_marshal = md.add("TypeRef", mscorlib, h.string("Marshal"), h.string("System.Runtime.InteropServices"))
tr_asmmeta = md.add("TypeRef", mscorlib, h.string("AssemblyMetadataAttribute"), h.string("System.Reflection"))

# Data image (D12): the initialized bytes the .cctor copies.
message = b"hello, world from FieldRVA data, written by a Python metadata writer\0"
image = message + b"\0" * ((-len(message)) % 8)

# TypeDefs: <Module> owns every field and method; '<msil>image_t' owns none.
N_FIELDS, N_METHODS = 4 + (0 if a.split_fields else a.many_fields), 7 + a.many_methods
td_module = md.add("TypeDef", 0, h.string("<Module>"), 0, None, Ref("Field", 1), Ref("MethodDef", 1))
td_image = md.add("TypeDef", 0x110, h.string("<msil>image_t"), 0, tr_valuetype,
                  Ref("Field", N_FIELDS + 1), Ref("MethodDef", N_METHODS + 1))
md.add("ClassLayout", 1, len(image), td_image)
if a.split_fields:
    tr_object = md.add("TypeRef", mscorlib, h.string("Object"), h.string("System"))
    td_g = md.add("TypeDef", 0x180, h.string("G"), 0, tr_object, Ref("Field", 5), Ref("MethodDef", N_METHODS + 1))
    td_h = md.add("TypeDef", 0x180, h.string("H"), 0, tr_object, Ref("Field", 5 + a.many_fields // 2),
                  Ref("MethodDef", N_METHODS + 1))

f_image = md.add("Field", 0x0111, h.string("<msil>image"), h.blob_(field_sig(("valuetype", td_image))))
f_data = md.add("Field", 0x0031, h.string("<msil>data"), h.blob_(field_sig("native_int")))
f_msg = md.add("Field", 0x0036, h.string("msg"), h.blob_(field_sig("native_int")))
f_fn = md.add("Field", 0x0033, h.string("<msil>fn_square"), h.blob_(field_sig("native_int")))
for i in range(a.many_fields):
    md.add("Field", 0x0013, h.string("extra_field_%06d_padding_to_grow_the_strings_heap" % i),
           h.blob_(field_sig("native_int")))
fieldrva_row = md.add("FieldRVA", 0, f_image)
fieldrva_list = md.rows["FieldRVA"][fieldrva_row.row - 1]

LONG = "long_" + "x" * (a.longlen - 5)   # default 2000 bytes; both ilasms reject identifiers over 1023 bytes

sig_i32 = h.blob_(method_sig("int32", []))
sig_i32_i32 = h.blob_(method_sig("int32", ["int32"]))
sig_void = h.blob_(method_sig("void", []))
sig_puts = h.blob_(method_sig("int32", ["native_int"]))
sig_main = h.blob_(method_sig("int32", [("szarray", "string")]))
m_main = md.add("MethodDef", 0, 0, 0x0016, h.string("main"), sig_i32, Ref("Param", 1))
m_square = md.add("MethodDef", 0, 0, 0x0013, h.string("square"), sig_i32_i32, Ref("Param", 1))
m_thrower = md.add("MethodDef", 0, 0, 0x0013, h.string("thrower"), sig_void, Ref("Param", 1))
m_long = md.add("MethodDef", 0, 0, 0x0013, h.string(LONG), sig_i32, Ref("Param", 1))
m_puts = md.add("MethodDef", 0, 0x0080, 0x2096, h.string("puts"), sig_puts, Ref("Param", 1))
m_cctor = md.add("MethodDef", 0, 0, 0x1891, h.string(".cctor"), sig_void, Ref("Param", 1))
m_entry = md.add("MethodDef", 0, 0, 0x0091, h.string("<msil>Main"), sig_main, Ref("Param", 1))
extra_methods = [md.add("MethodDef", 0, 0, 0x0013, h.string("extra_method_%06d" % i), sig_i32_i32, Ref("Param", 1))
                 for i in range(a.many_methods)]
assert md._count("MethodDef") == N_METHODS

lib = "ucrtbase" if a.os == "win" else "libc"
modref = md.add("ModuleRef", h.string(lib))
md.add("ImplMap", 0x0200, m_puts, h.string("puts"), modref)   # CallConvCdecl

mr_alloc = md.add("MemberRef", tr_marshal, h.string("AllocHGlobal"),
                  h.blob_(method_sig("native_int", ["native_int"])))
mr_strtoh = md.add("MemberRef", tr_marshal, h.string("StringToHGlobalAnsi"),
                   h.blob_(method_sig("native_int", ["string"])))
mr_asmmeta = md.add("MemberRef", tr_asmmeta, h.string(".ctor"),
                    h.blob_(bytes([0x20]) + method_sig("void", ["string", "string"])[1:]))

triple = "x86_64-pc-windows-msvc-msil" if a.os == "win" else "x86_64-unknown-linux-gnu-msil"
asm = md.add("Assembly", 0x8004, 0, 0, 0, 0, 0, 0, h.string(name), 0)
md.add("CustomAttribute", asm, mr_asmmeta,
       h.blob_(b"\x01\x00" + ser_string("msil.triple") + ser_string(triple) + b"\x00\x00"))

calli_sig = md.add("StandAloneSig", h.blob_(method_sig("int32", ["int32"])))
main_locals = md.add("StandAloneSig", h.blob_(locals_sig(["int32", "native_int", "int32", "int32"])))

img = Image(md, PEOptions(
    machine=IMAGE_FILE_MACHINE_I386 if a.pe32 else IMAGE_FILE_MACHINE_AMD64, dll=not a.exe,
    cor_flags=COMIMAGE_FLAGS_ILONLY | (COMIMAGE_FLAGS_32BITREQUIRED if a.req32 else 0) |
    (COMIMAGE_FLAGS_32BITPREFERRED if a.pref32 else 0),
    mscoree_stub=a.stub, deterministic=not a.nondeterministic, rva_data_in_text=not a.sdata))
body_fixups = []


def define(mref, il, max_stack, locals_tok=0, eh=()):
    code = il.assemble()
    eh_rows = [(k, il.labels[t0], il.labels[t1] - il.labels[t0], il.labels[h0],
                il.labels[h1] - il.labels[h0], il.labels[x] if isinstance(x, object) and x in il.labels else x)
               for k, t0, t1, h0, h1, x in eh]
    body, fat = method_body(code, max_stack, locals_tok, False, eh_rows, force_fat_eh=a.fat_eh)
    off = img.add_body(body, fat)
    body_fixups.append((md.rows["MethodDef"][mref.row - 1], off))


# main
il = IL()
L = {k: il.label() for k in ["nocalli", "try0", "try1", "filter", "h0", "h1", "after", "loop", "noloop",
                             "c0", "c1", "c2", "swend", "far", "nolong", "nosnan"]}
il("ldsfld", f_msg)("call", m_puts)("pop")
if a.split_fields and a.many_fields:
    il("ldsfld", Ref("Field", 5))("pop")("ldsfld", Ref("Field", 4 + a.many_fields))("pop")
il("ldstr", 0x70000000 | h.user_string("hello from the #US heap: ldstr + Marshal.StringToHGlobalAnsi"))
il("call", mr_strtoh)("call", m_puts)("pop")
il("ldc.i4.0")("stloc.0")
il("ldc.i4.7")("ldsfld", f_fn)("calli", calli_sig)("ldc.i4.s", 49)("bne.un", L["nocalli"])
il("ldloc.0")("ldc.i4.1")("or")("stloc.0")
il.mark(L["nocalli"])
il.mark(L["try0"])
il("call", m_thrower)("leave", L["after"])
il.mark(L["try1"])
il.mark(L["filter"])
il("pop")("ldc.i4.1")("endfilter")
il.mark(L["h0"])
il("pop")("ldloc.0")("ldc.i4.2")("or")("stloc.0")("leave", L["after"])
il.mark(L["h1"])
il.mark(L["after"])
il("ldc.i4.0")("stloc.3")("ldc.i4.1")("stloc.2")
il.mark(L["loop"])
il("ldloc.3")("ldloc.2")("add")("stloc.3")("ldloc.2")("ldc.i4.1")("add")("stloc.2")
il("ldloc.2")("ldc.i4.s", 10)("ble", L["loop"])
il("ldloc.3")("ldc.i4.s", 55)("bne.un", L["noloop"])
il("ldloc.0")("ldc.i4.4")("or")("stloc.0")
il.mark(L["noloop"])
il("ldc.i4.2")("switch", [L["c0"], L["c1"], L["c2"]])("br", L["swend"])
il.mark(L["c0"])
il("br", L["swend"])
il.mark(L["c1"])
il("br", L["swend"])
il.mark(L["c2"])
il("ldloc.0")("ldc.i4.8")("or")("stloc.0")
il.mark(L["swend"])
il("br", L["far"])
for _ in range(200):
    il("nop")
il("ldnull")("throw")
il.mark(L["far"])
il("ldloc.0")("ldc.i4.s", 16)("or")("stloc.0")
il("call", m_long)("brfalse", L["nolong"])
il("ldloc.0")("ldc.i4.s", 32)("or")("stloc.0")
il.mark(L["nolong"])
il("ldc.i4.8")("localloc")("stloc.1")
il("ldloc.1")("ldc.r4", 0x7F800001)("stind.r4")
# The probes return bits >> 20: 0x7F8 (2040, low byte 248) if the stored
# float32 is still the signaling NaN 0x7F800001, 0x7FC (2044, low byte 252)
# if it was quieted.
if a.snan_probe == "raw":  # the raw bits (needs a 32-bit exit code: Windows only)
    il("ldloc.1")("ldind.i4")("ret")
elif a.snan_probe == "const":
    il("ldloc.1")("ldind.i4")("ldc.i4.s", 20)("shr.un")("ret")
elif a.snan_probe == "move":
    # float32 sNaN bits stored as int32, moved through a float32 load/store.
    il("ldloc.1")("ldc.i4", 0x7F800001)("stind.i4")
    il("ldloc.1")("ldc.i4.4")("add")("ldloc.1")("ldind.r4")("stind.r4")
    il("ldloc.1")("ldc.i4.4")("add")("ldind.i4")("ldc.i4.s", 20)("shr.un")("ret")
il("ldloc.1")("ldind.i4")("ldc.i4", 0x7F800001)("bne.un", L["nosnan"])
il("ldloc.0")("ldc.i4.s", 64)("or")("stloc.0")
il.mark(L["nosnan"])
if a.rvawrite:
    il("ldsflda", f_image)("ldc.i4.s", 0x7F)("stind.i1")
il("ldloc.0")("ret")
define(m_main, il, 3, main_locals.token,
       eh=[(EH_FILTER, L["try0"], L["try1"], L["h0"], L["h1"], L["filter"])])

il = IL()
il("ldarg.0")("ldarg.0")("mul")("ret")
define(m_square, il, 2)
for mref in extra_methods:   # every extra method shares square's body
    body_fixups.append((md.rows["MethodDef"][mref.row - 1], body_fixups[-1][1]))
il = IL()
il("ldnull")("throw")
define(m_thrower, il, 1)
il = IL()
il("ldc.i4.1")("ret")
define(m_long, il, 1)
il = IL()
il("ldc.i4", len(image))("conv.i")("call", mr_alloc)("stsfld", f_data)
il("ldsfld", f_data)("ldsflda", f_image)("ldc.i4", len(image))("cpblk")
il("ldsfld", f_data)("stsfld", f_msg)
il("ldftn", m_square)("stsfld", f_fn)
il("ret")
define(m_cctor, il, 3)
il = IL()
il("call", m_main)("ret")
define(m_entry, il, 1)

data_off = img.add_rva_data(image)
info = img.write(a.o, m_entry.token, body_fixups, [(fieldrva_list, data_off)], os.path.basename(a.o))
print(a.o, info)

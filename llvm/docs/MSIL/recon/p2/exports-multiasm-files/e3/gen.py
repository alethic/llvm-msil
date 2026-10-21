"""E3: generate a library with N exported functions and D exported data symbols,
with the P2-C symbol table in several encodings, as ILAsm text.

Variants (argv[1]):
  base     export class only (no symbol table)
  attr-a   assembly attribute, self-contained binary table (names + CIL signatures)
  attr-b   assembly attribute, compact table that refers to MethodDef/Property rows
  attr-t   assembly attribute, the same information as UTF-8 text
  res-a    the self-contained table as an embedded manifest resource
  fnacc    base + one lazy '<msil>addr_<f>' accessor per exported function
  fnfld    base + one public initonly '<msil>fn_<f>' field per exported function, set in .cctor
Writes symlib-<variant>.il (and msil.symtab for res-a, <variant>.table.bin for attr-*).
"""
import random, struct, sys

N = int(sys.argv[2]) if len(sys.argv) > 2 else 1000
D = int(sys.argv[3]) if len(sys.argv) > 3 else 50
TRIPLE = "x86_64-pc-windows-msvc-msil"
random.seed(42)
WORDS = ("deflate inflate crc adler compress uncompress gz stream state window buffer init end reset "
         "params dictionary bound copy prime tune header get set read write open close flush seek tell "
         "error version table block hash tree code len dist").split()


def cname(i):
    k = random.choice((1, 2, 2, 3))
    return "_".join(random.choice(WORDS) for _ in range(k)) + f"_{i}"


FN = [cname(i) for i in range(N)]
DN = [cname(10000 + i) for i in range(D)]
VT = {"i": ("int32", 0x08), "p": ("native int", 0x18), "d": ("float64", 0x0D)}
SIGS = []
for i in range(N):
    np_ = random.choice((0, 1, 2, 2, 3, 3, 4, 5))
    SIGS.append((random.choice("iipd"), "".join(random.choice("iipd") for _ in range(np_))))
DSZ = [(random.choice((4, 8, 16, 64, 256, 1024)), random.choice((4, 8, 16))) for _ in range(D)]


def cu(n):  # ECMA-335 II.23.2 compressed unsigned integer
    if n < 0x80:
        return bytes([n])
    if n < 0x4000:
        return bytes([0x80 | (n >> 8), n & 0xFF])
    return bytes([0xC0 | (n >> 24), (n >> 16) & 0xFF, (n >> 8) & 0xFF, n & 0xFF])


def sstr(s):
    b = s.encode()
    return cu(len(b)) + b


def cilsig(ret, params):  # MethodDefSig: DEFAULT, param count, ret, params
    return bytes([0x00]) + cu(len(params)) + bytes([VT[ret][1]]) + bytes(VT[p][1] for p in params)


KIND_FUNC, KIND_DATA = 0, 1


def header():
    # magic, format version, flags, toolchain ABI version, triple
    return b"MSYM" + bytes([1, 0]) + struct.pack("<H", 1) + sstr(TRIPLE)


def table_a():  # self-contained
    out = bytearray(header())
    out += cu(N + D)
    for n, (r, p) in zip(FN, SIGS):
        s = cilsig(r, p)
        out += bytes([KIND_FUNC]) + sstr(n) + cu(len(s)) + s
    for n, (sz, al) in zip(DN, DSZ):
        out += bytes([KIND_DATA]) + sstr(n) + cu(sz) + bytes([al.bit_length() - 1])
    return bytes(out)


def table_b():  # refers to metadata rows: export methods are MethodDef rows 2..N+1 (row 1 = attribute .ctor)
    out = bytearray(header())
    out += cu(N + D)
    for i in range(N):
        out += bytes([KIND_FUNC]) + cu(3 + i)  # MethodDef row of the export (1: <Module>::.cctor, 2: the attribute .ctor)
    for i, (sz, al) in enumerate(DSZ):
        out += bytes([KIND_DATA]) + cu(1 + i) + cu(sz) + bytes([al.bit_length() - 1])  # Property row
    return bytes(out)


def table_n():  # by name: functions are implied by the export class, data listed with size/align
    out = bytearray(header())
    out += cu(1) + sstr("Contoso.Big.Native")          # export classes
    out += cu(D)
    for n, (sz, al) in zip(DN, DSZ):
        out += sstr(n) + cu(sz) + bytes([al.bit_length() - 1, 0])  # name, size, log2 align, flags
    out += cu(0)                                        # functions with non-default flags (variadic, ...)
    return bytes(out)


def table_t():
    lines = [f"msil-symtab 1 abi=1 triple={TRIPLE}"]
    for n, (r, p) in zip(FN, SIGS):
        lines.append(f"F {n} {VT[r][0]}({','.join(VT[x][0] for x in p)})")
    for n, (sz, al) in zip(DN, DSZ):
        lines.append(f"D {n} {sz} {al}")
    return ("\n".join(lines) + "\n").encode()


def bytearray_il(b):
    rows = []
    for i in range(0, len(b), 32):
        rows.append("    " + " ".join(f"{x:02X}" for x in b[i:i + 32]))
    return "(\n" + "\n".join(rows) + " )"


def emit(variant):
    o = []
    o.append(".assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }")
    o.append(".assembly 'symlib'\n{")
    if variant == "proposed":
        t = table_n()
        blob = bytes([1, 0]) + struct.pack("<I", len(t)) + t + bytes([0, 0])
        o.append("  .custom instance void '<msil>SymbolTable'::.ctor(uint8[]) = " + bytearray_il(blob))
    if variant.startswith("attr"):
        t = {"attr-a": table_a, "attr-b": table_b, "attr-t": table_t, "attr-n": table_n}[variant]()
        # CustomAttrib blob: prolog 0x0001, SZARRAY<u1>: int32 count + bytes, NumNamed = 0
        blob = b"\x01\x00" + struct.pack("<I", len(t)) + t + b"\x00\x00"
        o.append("  .custom instance void '<msil>SymbolTable'::.ctor(uint8[]) = " + bytearray_il(blob))
        open(f"{variant}.table.bin", "wb").write(t)
    o.append("  .ver 1:0:0:0\n}")
    if variant == "res-a":
        open("msil.symtab", "wb").write(table_a())
        o.append(".mresource private 'msil.symtab' { }")
    if variant.startswith("attr") or variant == "proposed":
        o.append(""".class private auto ansi sealed beforefieldinit '<msil>SymbolTable' extends [mscorlib]System.Attribute
{
  .method public hidebysig specialname rtspecialname instance void .ctor(uint8[] table) cil managed
  { .maxstack 1 ldarg.0 call instance void [mscorlib]System.Attribute::.ctor() ret }
}""")
    o.append(".class public abstract sealed auto ansi 'Contoso.Big.Native' extends [mscorlib]System.Object\n{")
    for n, (r, p) in zip(FN, SIGS):
        params = ", ".join(VT[x][0] for x in p)
        body = {"i": "ldc.i4.0", "p": "ldc.i4.0 conv.i", "d": "ldc.r8 0.0"}[r]
        o.append(f"  .method public static {VT[r][0]} '{n}'({params}) cil managed {{ .maxstack 1 {body} ret }}")
    for n in DN:
        o.append(f"  .method public hidebysig specialname static native int '<msil>addr_{n}'() cil managed {{ .maxstack 1 ldsfld native int '{n}' ret }}")
        o.append(f"  .property native int '{n}'() {{ .get native int 'Contoso.Big.Native'::'<msil>addr_{n}'() }}")
    if variant == "fnacc":
        for n, (r, p) in zip(FN, SIGS):
            params = ", ".join(VT[x][0] for x in p)
            o.append(f"  .field private static native int '<msil>fn_{n}'")
            o.append(f"""  .method public static native int '<msil>addr_{n}'() cil managed
  {{ .maxstack 3
    ldsfld native int 'Contoso.Big.Native'::'<msil>fn_{n}' dup brtrue DONE pop
    ldsflda native int 'Contoso.Big.Native'::'<msil>fn_{n}' ldftn {VT[r][0]} 'Contoso.Big.Native'::'{n}'({params}) ldc.i4.0 conv.i
    call native int [mscorlib]System.Threading.Interlocked::CompareExchange(native int&, native int, native int) pop
    ldsfld native int 'Contoso.Big.Native'::'<msil>fn_{n}'
  DONE: ret }}""")
    if variant == "fnfld":
        for n in FN:
            o.append(f"  .field public static initonly native int '<msil>fn_{n}'")
    if variant == "proposed":
        for n in FN:
            o.append(f"  .field assembly static initonly native int '<msil>fn_{n}'")
            o.append(f"  .method public static native int '<msil>addr_{n}'() cil managed {{ .maxstack 1 ldsfld native int 'Contoso.Big.Native'::'<msil>fn_{n}' ret }}")
    o.append("}")
    # data segment (one address field per exported variable) and module initializer
    total = 0
    offs = []
    for sz, al in DSZ:
        total = (total + al - 1) // al * al
        offs.append(total)
        total += sz
    for n in DN:
        o.append(f".field assembly static initonly native int '{n}'")
    o.append(".field assembly static initonly native int '<msil>data'")
    o.append(".method private hidebysig specialname rtspecialname static void .cctor() cil managed\n{\n  .maxstack 3")
    o.append(f"  ldc.i4 {total + 15} conv.i call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int) stsfld native int '<msil>data'")
    for n, off in zip(DN, offs):
        o.append(f"  ldsfld native int '<msil>data' ldc.i4 {off} add stsfld native int '{n}'")
    if variant in ("fnfld", "proposed"):
        for n, (r, p) in zip(FN, SIGS):
            params = ", ".join(VT[x][0] for x in p)
            o.append(f"  ldftn {VT[r][0]} 'Contoso.Big.Native'::'{n}'({params}) stsfld native int 'Contoso.Big.Native'::'<msil>fn_{n}'")
    o.append("  ret\n}")
    return "\n".join(o) + "\n"


v = sys.argv[1]
open(f"symlib-{v}.il", "w").write(emit(v))

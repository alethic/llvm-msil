"""ILAsm text equivalent of hello.py (for the ilasm comparison). The long
method name is 1000 bytes here, because both ilasms reject names over 1023
bytes; branches are written in the forms hello.py's relaxation picks."""
import sys

LONG = "long_" + "x" * 995
out = sys.argv[1]
lib = sys.argv[2] if len(sys.argv) > 2 else "ucrtbase"
name = out.rsplit("/", 1)[-1].rsplit(".", 1)[0]
image = b"hello, world from FieldRVA data, written by a Python metadata writer\0"
image += b"\0" * ((-len(image)) % 8)
hexbytes = " ".join("%02X" % b for b in image)
nops = "\n".join("  nop" for _ in range(200))
text = f""".assembly extern mscorlib {{ .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }}
.assembly '{name}'
{{
  .custom instance void [mscorlib]System.Reflection.AssemblyMetadataAttribute::.ctor(string, string) = ( 01 00 0B 6D 73 69 6C 2E 74 72 69 70 6C 65 1B 78 38 36 5F 36 34 2D 70 63 2D 77 69 6E 64 6F 77 73 2D 6D 73 76 63 2D 6D 73 69 6C 00 00 )
  .ver 0:0:0:0
}}
.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType
{{
  .pack 1
  .size {len(image)}
}}
.data cil '<msil>image_d' = bytearray ( {hexbytes} )
.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'
.field private static initonly native int '<msil>data'
.field public static initonly native int 'msg'
.field assembly static initonly native int '<msil>fn_square'

.method public static int32 'main'() cil managed
{{
  .maxstack 3
  .locals (int32, native int, int32, int32)
  ldsfld native int 'msg'
  call int32 'puts'(native int)
  pop
  ldstr "hello from the #US heap: ldstr + Marshal.StringToHGlobalAnsi"
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::StringToHGlobalAnsi(string)
  call int32 'puts'(native int)
  pop
  ldc.i4.0
  stloc.0
  ldc.i4.7
  ldsfld native int '<msil>fn_square'
  calli int32(int32)
  ldc.i4.s 49
  bne.un.s NOCALLI
  ldloc.0
  ldc.i4.1
  or
  stloc.0
NOCALLI:
  .try
  {{
    call void 'thrower'()
    leave.s AFTER
  }}
  filter
  {{
    pop
    ldc.i4.1
    endfilter
  }}
  {{
    pop
    ldloc.0
    ldc.i4.2
    or
    stloc.0
    leave.s AFTER
  }}
AFTER:
  ldc.i4.0
  stloc.3
  ldc.i4.1
  stloc.2
LOOP:
  ldloc.3
  ldloc.2
  add
  stloc.3
  ldloc.2
  ldc.i4.1
  add
  stloc.2
  ldloc.2
  ldc.i4.s 10
  ble.s LOOP
  ldloc.3
  ldc.i4.s 55
  bne.un.s NOLOOP
  ldloc.0
  ldc.i4.4
  or
  stloc.0
NOLOOP:
  ldc.i4.2
  switch (C0, C1, C2)
  br.s SWEND
C0:
  br.s SWEND
C1:
  br.s SWEND
C2:
  ldloc.0
  ldc.i4.8
  or
  stloc.0
SWEND:
  br FAR
{nops}
  ldnull
  throw
FAR:
  ldloc.0
  ldc.i4.s 16
  or
  stloc.0
  call int32 '{LONG}'()
  brfalse.s NOLONG
  ldloc.0
  ldc.i4.s 32
  or
  stloc.0
NOLONG:
  ldc.i4.8
  localloc
  stloc.1
  ldloc.1
  ldc.r4 float32(0x7F800001)
  stind.r4
  ldloc.1
  ldind.i4
  ldc.i4 0x7F800001
  bne.un.s NOSNAN
  ldloc.0
  ldc.i4.s 64
  or
  stloc.0
NOSNAN:
  ldloc.0
  ret
}}
.method assembly static int32 'square'(int32) cil managed
{{
  .maxstack 2
  ldarg.0
  ldarg.0
  mul
  ret
}}
.method assembly static void 'thrower'() cil managed
{{
  .maxstack 1
  ldnull
  throw
}}
.method assembly static int32 '{LONG}'() cil managed
{{
  .maxstack 1
  ldc.i4.1
  ret
}}
.method public hidebysig static pinvokeimpl("{lib}" cdecl) int32 'puts'(native int) cil managed preservesig {{}}
.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{{
  .maxstack 3
  ldc.i4 {len(image)}
  conv.i
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  stsfld native int '<msil>data'
  ldsfld native int '<msil>data'
  ldsflda valuetype '<msil>image_t' '<msil>image'
  ldc.i4 {len(image)}
  cpblk
  ldsfld native int '<msil>data'
  stsfld native int 'msg'
  ldftn int32 'square'(int32)
  stsfld native int '<msil>fn_square'
  ret
}}
.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{{
  .entrypoint
  .maxstack 1
  call int32 'main'()
  ret
}}
"""
open(out, "w", newline="\n").write(text)

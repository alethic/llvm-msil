// d12-image.il with one error that msil-verify.py must reject: native int +
// int64 ('ldsfld native int '<msil>data'; ldc.i8 1; add', plan D2) right
// after the image copy in <Module>::.cctor. PEVerify stops at the image
// ldsflda in front of it and accepts the method's errors only because it
// contains ldftn, so the error is found by msil-verify.py's neutralized pass
// (pass 2), which rewrites the ldsflda and the ldftn. Not run.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'd12-image-mix' { .ver 0:0:0:0 }

// get(p) reads one byte at p; 'fn' holds its address.
.method assembly static int32 'get'(native int) cil managed
{
  .maxstack 1
  ldarg.0
  ldind.u1
  ret
} // end of method 'get'

// fn(table) + *ptr + bss = 42 + 7 + 0
.method public static int32 'main'() cil managed
{
  .maxstack 2
  ldsfld native int 'table'
  ldsfld native int 'fn'
  ldind.i
  calli int32(native int)
  ldsfld native int 'ptr'
  ldind.i
  ldind.i4
  add
  ldsfld native int 'bss'
  ldind.i4
  add
  ret
} // end of method 'main'

// ---- data segment: 28 bytes (24 initialized + 4 zero), align 8 ----
.class private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType
{
  .pack 1
  .size 24
}
.data cil '<msil>image_d' = bytearray (
  2A 00 00 00 07 00 00 00 00 00 00 00 00 00 00 00   // 0x0000
  00 00 00 00 00 00 00 00 )                         // 0x0010
.field private static valuetype '<msil>image_t' '<msil>image' at '<msil>image_d'

.field private static initonly native int '<msil>data'
.field public static initonly native int 'table'      // +0, int[2] = {42, 7}
.field public static initonly native int 'ptr'        // +8, &table[1]
.field public static initonly native int 'fn'         // +16, get
.field public static initonly native int 'bss'        // +24 (zero-initialized)

.method private hidebysig specialname rtspecialname static void .cctor() cil managed
{
  .maxstack 3
  ldc.i4.s 35                     // 28 + (8 - 1)
  conv.i
  call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
  ldc.i4.7
  conv.i
  add
  ldc.i4.s -8
  conv.i
  and
  stsfld native int '<msil>data'
  ldsfld native int '<msil>data'  // copy the initialized image
  ldsflda valuetype '<msil>image_t' '<msil>image'
  ldc.i4.s 24
  cpblk
  ldsfld native int '<msil>data'  // the error: native int + int64
  ldc.i8 1
  add
  pop
  ldsfld native int '<msil>data'  // zero-fill [24, 28)
  ldc.i4.s 24
  add
  ldc.i4.0
  ldc.i4.4
  initblk
  ldsfld native int '<msil>data'  // symbol addresses
  stsfld native int 'table'
  ldsfld native int '<msil>data'
  ldc.i4.8
  add
  stsfld native int 'ptr'
  ldsfld native int '<msil>data'
  ldc.i4.s 16
  add
  stsfld native int 'fn'
  ldsfld native int '<msil>data'
  ldc.i4.s 24
  add
  stsfld native int 'bss'
  ldsfld native int '<msil>data'  // reloc @8: ptr = &table[1]
  ldc.i4.8
  add
  ldsfld native int 'table'
  ldc.i4.4
  add
  stind.i
  ldsfld native int '<msil>data'  // reloc @16: fn = get
  ldc.i4.s 16
  add
  ldftn int32 'get'(native int)
  stind.i
  ret
} // end of method '.cctor'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  call int32 'main'()
  ret
} // end of method '<msil>Main'

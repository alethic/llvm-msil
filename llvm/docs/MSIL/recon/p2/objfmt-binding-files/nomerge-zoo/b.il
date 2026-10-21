.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern 'msil.extern' { .ver 0:0:0:0 }
.assembly 'b' { .ver 0:0:0:0 }
.class public abstract sealed auto ansi '<sym>add3' extends [mscorlib]System.Object
{
  .field public static native int '<msil>fn'
.method public static int32 'add3'(int32) cil managed
{                                       // -- Begin function add3
                                        // @add3
  .maxstack 3
  .locals (int32, int32)
// %bb.0:                               // %entry
  ldsfld native int [msil.extern]'<sym>??_C@_04JCHODAIH@add3?$AA@'::'??_C@_04JCHODAIH@add3?$AA@'
  call int32 '<msil>b'::'puts'(native int)
  pop
  ldsfld native int [msil.extern]'<sym>counter'::'counter'
  ldsfld native int [msil.extern]'<sym>counter'::'counter'
  ldind.i4
  ldc.i4.1
  add
  dup
  stloc.0
  stind.i4
  ldsfld native int '<msil>b'::'local_g'
  ldsfld native int '<msil>b'::'local_g'
  ldind.i4
  ldc.i4.1
  add
  dup
  stloc.1
  stind.i4
  ldloc.1
  ldloc.0
  add
  ldarg.0
  call int32 '<msil>b'::'helper'(int32)
  add
  ret
} // end of method 'add3'
}
.class public abstract sealed auto ansi '<sym>twice' extends [mscorlib]System.Object
{
  .field public static native int '<msil>fn'
.method public static int32 'twice'(int32) cil managed
{                                       // -- Begin function twice
                                        // @twice
  .maxstack 2
// %bb.0:                               // %entry
  ldarg.0
  ldc.i4.1
  shl
  ret
} // end of method 'twice'
}
.class public abstract sealed auto ansi '<sym>weakfn' extends [mscorlib]System.Object
{
  .field public static native int '<msil>fn'
.method public static int32 'weakfn'() cil managed
{                                       // -- Begin function weakfn
                                        // @weakfn
  .maxstack 1
// %bb.0:                               // %entry
  ldc.i4.2
  ret
} // end of method 'weakfn'
}
.class public abstract sealed auto ansi '<sym>b_extra' extends [mscorlib]System.Object
{
  .field public static native int '<msil>fn'
.method public static int32 'b_extra'() cil managed
{                                       // -- Begin function b_extra
                                        // @b_extra
  .maxstack 4
// %bb.0:                               // %entry
  ldsfld native int [msil.extern]'<sym>common_c'::'common_c'
  ldind.i4
  ldc.i4 1000
  mul
  ldsfld native int '<msil>b'::'idx'
  volatile.
  ldind.i4
  conv.i
  ldsfld native int [msil.extern]'<sym>??_C@_05CJBACGMB@hello?$AA@'::'??_C@_05CJBACGMB@hello?$AA@'
  add
  ldind.i1
  add
  ret
} // end of method 'b_extra'
}
.class public abstract sealed auto ansi '<sym>alias_twice' extends [mscorlib]System.Object
{
  .field public static native int '<msil>fn'
.method public static int32 'alias_twice'(int32) cil managed
{
  jmp int32 [msil.extern]'<sym>twice'::'twice'(int32)
}
}
.class public abstract sealed auto ansi '<sym>counter' extends [mscorlib]System.Object
{
  .field public static native int 'counter'
}
.class public abstract sealed auto ansi '<sym>??_C@_05CJBACGMB@hello?$AA@' extends [mscorlib]System.Object
{
  .field public static native int '??_C@_05CJBACGMB@hello?$AA@'
}
.class public abstract sealed auto ansi '<sym>msg_b' extends [mscorlib]System.Object
{
  .field public static native int 'msg_b'
}
.class public abstract sealed auto ansi '<sym>??_C@_04JCHODAIH@add3?$AA@' extends [mscorlib]System.Object
{
  .field public static native int '??_C@_04JCHODAIH@add3?$AA@'
}
.class public abstract sealed auto ansi '<sym>common_c' extends [mscorlib]System.Object
{
  .field public static native int 'common_c'
}
.class public abstract sealed auto ansi '<msil>b' extends [mscorlib]System.Object
{
  .field private static native int '<msil>data'
  .field public static native int 'local_g'
  .field public static native int 'vb'
  .field public static native int 'idx'
.method public static int32 'helper'(int32) cil managed
{                                       // -- Begin function helper
                                        // @helper
  .maxstack 2
// %bb.0:                               // %entry
  ldarg.0
  ldc.i4.3
  mul
  ret
} // end of method 'helper'
.method public static void 'ctor_b'() cil managed
{                                       // -- Begin function ctor_b
                                        // @ctor_b
  .maxstack 4
// %bb.0:                               // %entry
  ldsfld native int [msil.extern]'<sym>counter'::'counter'
  ldsfld native int '<msil>b'::'vb'
  volatile.
  ldind.i4
  ldsfld native int [msil.extern]'<sym>counter'::'counter'
  ldind.i4
  mul
  stind.i4
  ret
} // end of method 'ctor_b'
.method public hidebysig static pinvokeimpl("ucrtbase" cdecl) int32 'puts'(native int) cil managed preservesig {}
  .method public static void init1() cil managed
  {
    .maxstack 4
    ldc.i4 63 conv.i call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
    ldc.i4.s 15 conv.i add ldc.i4.s -16 conv.i and stsfld native int '<msil>b'::'<msil>data'
    ldsfld native int '<msil>b'::'<msil>data' ldsflda valuetype '<msil>b'/'<msil>image_t' '<msil>b'::'<msil>image' ldc.i4 44 cpblk
    ldsfld native int '<msil>b'::'<msil>data' ldc.i4 44 add ldc.i4.0 ldc.i4 4 initblk
    ldsfld native int '<msil>b'::'<msil>data' ldc.i4 0 add stsfld native int '<sym>counter'::'counter'
    ldsfld native int '<msil>b'::'<msil>data' ldc.i4 16 add stsfld native int '<sym>msg_b'::'msg_b'
    ldsfld native int '<msil>b'::'<msil>data' ldc.i4 24 add stsfld native int '<sym>??_C@_04JCHODAIH@add3?$AA@'::'??_C@_04JCHODAIH@add3?$AA@'
    ldsfld native int '<msil>b'::'<msil>data' ldc.i4 32 add stsfld native int '<msil>b'::'local_g'
    ldsfld native int '<msil>b'::'<msil>data' ldc.i4 36 add stsfld native int '<msil>b'::'vb'
    ldsfld native int '<msil>b'::'<msil>data' ldc.i4 40 add stsfld native int '<msil>b'::'idx'
    ldftn int32 '<sym>add3'::'add3'(int32) stsfld native int '<sym>add3'::'<msil>fn'
    ldftn int32 '<sym>twice'::'twice'(int32) stsfld native int '<sym>twice'::'<msil>fn'
    ldftn int32 '<sym>weakfn'::'weakfn'() stsfld native int '<sym>weakfn'::'<msil>fn'
    ldftn int32 '<sym>b_extra'::'b_extra'() stsfld native int '<sym>b_extra'::'<msil>fn'
    ldsfld native int [msil.extern]'<sym>twice'::'<msil>fn' stsfld native int '<sym>alias_twice'::'<msil>fn'
    ret
  }
  .method public static void init2() cil managed
  {
    .maxstack 4
    ldsfld native int '<msil>b'::'<msil>data' ldc.i4 16 add ldsfld native int [msil.extern]'<sym>??_C@_05CJBACGMB@hello?$AA@'::'??_C@_05CJBACGMB@hello?$AA@' stind.i
    ret
  }
  .class nested private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType { .pack 1 .size 44 }
  .field private static valuetype '<msil>b'/'<msil>image_t' '<msil>image' at D_b
}
.data D_b = bytearray (05 00 00 00 68 65 6C 6C 6F 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 61 64 64 33 00 00 00 00 0B 00 00 00 02 00 00 00 04 00 00 00)

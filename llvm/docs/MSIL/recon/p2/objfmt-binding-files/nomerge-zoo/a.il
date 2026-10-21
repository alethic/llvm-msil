.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern 'msil.extern' { .ver 0:0:0:0 }
.assembly 'a' { .ver 0:0:0:0 }
.class public abstract sealed auto ansi '<sym>weakfn' extends [mscorlib]System.Object
{
  .field public static native int '<msil>fn'
.method public static int32 'weakfn'() cil managed
{                                       // -- Begin function weakfn
                                        // @weakfn
  .maxstack 1
// %bb.0:                               // %entry
  ldc.i4.1
  ret
} // end of method 'weakfn'
}
.class public abstract sealed auto ansi '<sym>main' extends [mscorlib]System.Object
{
  .field public static native int '<msil>fn'
.method public static int32 'main'() cil managed
{                                       // -- Begin function main
                                        // @main
  .maxstack 4
  .locals (int32, int32)
// %bb.0:                               // %entry
  ldsfld native int [msil.extern]'<sym>??_C@_04GHJNJNPO@main?$AA@'::'??_C@_04GHJNJNPO@main?$AA@'
  call int32 '<msil>a'::'puts'(native int)
  pop
  ldsfld native int '<msil>a'::'local_g'
  ldsfld native int '<msil>a'::'local_g'
  ldind.i4
  ldc.i4.1
  add
  stind.i4
  ldc.i4.1
  ldsfld native int [msil.extern]'<sym>table'::'table'
  ldind.i
  calli int32(int32)
  ldc.i4.2
  ldsfld native int [msil.extern]'<sym>table'::'table'
  ldc.i4 8
  add
  ldind.i
  calli int32(int32)
  add
  ldc.i4.3
  ldsfld native int [msil.extern]'<sym>table'::'table'
  ldc.i4 16
  add
  ldind.i
  calli int32(int32)
  add
  ldsfld native int [msil.extern]'<sym>pcounter'::'pcounter'
  ldind.i
  ldind.i4
  add
  ldsfld native int '<msil>a'::'local_g'
  ldind.i4
  call int32 '<msil>a'::'helper'(int32)
  add
  call int32 [msil.extern]'<sym>weakfn'::'weakfn'()
  ldc.i4 1000
  mul
  add
  stloc.0
  ldsfld native int [msil.extern]'<sym>maybe'::'<msil>fn'
  ldc.i4 0
  conv.i
  ceq
  brtrue @BB2_2
// %bb.1:                               // %entry
  ldloc.0
  ldc.i4 100000
  add
  stloc.0
@BB2_2:                                 // %entry
  ldsfld native int [msil.extern]'<sym>common_c'::'common_c'
  ldsfld native int [msil.extern]'<sym>common_c'::'common_c'
  ldind.i4
  ldc.i4.1
  add
  dup
  stloc.1
  stind.i4
  ldloc.0
  ldloc.1
  add
  stloc.1
  ldc.i4.0
  stloc.0
  ldsfld native int [msil.extern]'<sym>table'::'table'
  ldc.i4 8
  add
  ldind.i
  ldsfld native int [msil.extern]'<sym>table'::'table'
  ldc.i4 16
  add
  ldind.i
  ceq
  brtrue @BB2_4
// %bb.3:                               // %entry
  ldc.i4.7
  stloc.0
@BB2_4:                                 // %entry
  ldloc.1
  ldloc.0
  add
  ldsfld native int [msil.extern]'<sym>msg'::'msg'
  ldind.i
  ldind.u1
  add
  call int32 [msil.extern]'<sym>b_extra'::'b_extra'()
  add
  conv.u1
  ret
} // end of method 'main'
}
.class public abstract sealed auto ansi '<sym>table' extends [mscorlib]System.Object
{
  .field public static native int 'table'
}
.class public abstract sealed auto ansi '<sym>pcounter' extends [mscorlib]System.Object
{
  .field public static native int 'pcounter'
}
.class public abstract sealed auto ansi '<sym>??_C@_05CJBACGMB@hello?$AA@' extends [mscorlib]System.Object
{
  .field public static native int '??_C@_05CJBACGMB@hello?$AA@'
}
.class public abstract sealed auto ansi '<sym>msg' extends [mscorlib]System.Object
{
  .field public static native int 'msg'
}
.class public abstract sealed auto ansi '<sym>??_C@_04GHJNJNPO@main?$AA@' extends [mscorlib]System.Object
{
  .field public static native int '??_C@_04GHJNJNPO@main?$AA@'
}
.class public abstract sealed auto ansi '<sym>common_c' extends [mscorlib]System.Object
{
  .field public static native int 'common_c'
}
.class public abstract sealed auto ansi '<msil>a' extends [mscorlib]System.Object
{
  .field private static native int '<msil>data'
  .field public static native int 'va'
  .field public static native int 'local_g'
.method public static void 'ctor_a'() cil managed
{                                       // -- Begin function ctor_a
                                        // @ctor_a
  .maxstack 4
// %bb.0:                               // %entry
  ldsfld native int [msil.extern]'<sym>counter'::'counter'
  ldsfld native int '<msil>a'::'va'
  volatile.
  ldind.i4
  ldsfld native int [msil.extern]'<sym>counter'::'counter'
  ldind.i4
  add
  stind.i4
  ret
} // end of method 'ctor_a'
.method public static int32 'helper'(int32) cil managed
{                                       // -- Begin function helper
                                        // @helper
  .maxstack 2
// %bb.0:                               // %entry
  ldarg.0
  ldc.i4.s 100
  add
  ret
} // end of method 'helper'
.method public hidebysig static pinvokeimpl("ucrtbase" cdecl) int32 'puts'(native int) cil managed preservesig {}
  .method public static void init1() cil managed
  {
    .maxstack 4
    ldc.i4 87 conv.i call native int [mscorlib]System.Runtime.InteropServices.Marshal::AllocHGlobal(native int)
    ldc.i4.s 15 conv.i add ldc.i4.s -16 conv.i and stsfld native int '<msil>a'::'<msil>data'
    ldsfld native int '<msil>a'::'<msil>data' ldsflda valuetype '<msil>a'/'<msil>image_t' '<msil>a'::'<msil>image' ldc.i4 64 cpblk
    ldsfld native int '<msil>a'::'<msil>data' ldc.i4 64 add ldc.i4.0 ldc.i4 8 initblk
    ldsfld native int '<msil>a'::'<msil>data' ldc.i4 0 add stsfld native int '<sym>table'::'table'
    ldsfld native int '<msil>a'::'<msil>data' ldc.i4 24 add stsfld native int '<sym>pcounter'::'pcounter'
    ldsfld native int '<msil>a'::'<msil>data' ldc.i4 32 add stsfld native int '<sym>??_C@_05CJBACGMB@hello?$AA@'::'??_C@_05CJBACGMB@hello?$AA@'
    ldsfld native int '<msil>a'::'<msil>data' ldc.i4 40 add stsfld native int '<sym>msg'::'msg'
    ldsfld native int '<msil>a'::'<msil>data' ldc.i4 48 add stsfld native int '<msil>a'::'va'
    ldsfld native int '<msil>a'::'<msil>data' ldc.i4 52 add stsfld native int '<sym>??_C@_04GHJNJNPO@main?$AA@'::'??_C@_04GHJNJNPO@main?$AA@'
    ldsfld native int '<msil>a'::'<msil>data' ldc.i4 60 add stsfld native int '<msil>a'::'local_g'
    ldsfld native int '<msil>a'::'<msil>data' ldc.i4 68 add stsfld native int '<sym>common_c'::'common_c'
    ldftn int32 '<sym>main'::'main'() stsfld native int '<sym>main'::'<msil>fn'
    ret
  }
  .method public static void init2() cil managed
  {
    .maxstack 4
    ldsfld native int '<msil>a'::'<msil>data' ldc.i4 0 add ldsfld native int [msil.extern]'<sym>add3'::'<msil>fn' stind.i
    ldsfld native int '<msil>a'::'<msil>data' ldc.i4 8 add ldsfld native int [msil.extern]'<sym>twice'::'<msil>fn' stind.i
    ldsfld native int '<msil>a'::'<msil>data' ldc.i4 16 add ldsfld native int [msil.extern]'<sym>alias_twice'::'<msil>fn' stind.i
    ldsfld native int '<msil>a'::'<msil>data' ldc.i4 24 add ldsfld native int [msil.extern]'<sym>counter'::'counter' stind.i
    ldsfld native int '<msil>a'::'<msil>data' ldc.i4 40 add ldsfld native int [msil.extern]'<sym>??_C@_05CJBACGMB@hello?$AA@'::'??_C@_05CJBACGMB@hello?$AA@' stind.i
    ret
  }
  .class nested private explicit ansi sealed '<msil>image_t' extends [mscorlib]System.ValueType { .pack 1 .size 64 }
  .field private static valuetype '<msil>a'/'<msil>image_t' '<msil>image' at D_a
}
.data D_a = bytearray (00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 68 65 6C 6C 6F 00 00 00 00 00 00 00 00 00 00 00 0A 00 00 00 6D 61 69 6E 00 00 00 00 07 00 00 00)

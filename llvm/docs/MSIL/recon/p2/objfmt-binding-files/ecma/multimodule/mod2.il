.module mod2.netmodule
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.field public static int32 'counter'
.method public static int32 'G'(int32 x) cil managed
{
  .maxstack 2
  ldarg.0
  ldc.i4.2
  mul
  ldsfld int32 'counter'
  add
  ret
}
.class public auto ansi sealed abstract Lib extends [mscorlib]System.Object
{
  .method public static int32 F() cil managed { .maxstack 1 ldc.i4.s 40 ret }
}
.method private specialname rtspecialname static void .cctor() cil managed
{ .maxstack 1 ldc.i4.1 stsfld int32 'counter' ret }

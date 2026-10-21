.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly b { .ver 0:0:0:0 }
.class public abstract sealed auto ansi 'add3' extends [mscorlib]System.Object
{
  .field public static int32 'counter'
  .method public static int32 'add3'(int32) cil managed { .maxstack 2 ldarg.0 ldc.i4.3 add ldsfld int32 'add3'::'counter' add ret }
  .method private specialname rtspecialname static void .cctor() cil managed { .maxstack 1 ldc.i4.s 100 stsfld int32 'add3'::'counter' ret }
}

.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern x { .ver 0:0:0:0 }
.assembly y { .ver 0:0:0:0 }
.class public abstract sealed auto ansi Y extends [mscorlib]System.Object
{
  .field public static native int addr
  .field public static native int slot
  .method private specialname rtspecialname static void .cctor() cil managed
  { .maxstack 1 ldc.i4 2000 conv.i stsfld native int Y::addr
    ldsfld native int [x]X::addr stsfld native int Y::slot ret }
}

// x's type initializer stores y's address field into x's data (a data relocation into another
// assembly); y's type initializer does the same with x's field: a cycle of lazy initializers.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern y { .ver 0:0:0:0 }
.assembly x { .ver 0:0:0:0 }
.class public abstract sealed auto ansi X extends [mscorlib]System.Object
{
  .field public static native int addr      // the address field of a global in x
  .field public static native int slot      // a pointer in x's data that holds y's address
  .method private specialname rtspecialname static void .cctor() cil managed
  { .maxstack 1 ldc.i4 1000 conv.i stsfld native int X::addr
    ldsfld native int [y]Y::addr stsfld native int X::slot ret }
}

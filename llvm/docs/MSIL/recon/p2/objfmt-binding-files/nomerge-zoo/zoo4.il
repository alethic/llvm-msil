.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern 'msil.extern' { .ver 0:0:0:0 }
.assembly extern 'a' { .ver 0:0:0:0 }
.assembly extern 'b' { .ver 0:0:0:0 }
.assembly 'zoo4' { .ver 0:0:0:0 }
.class public abstract sealed auto ansi '<sym>maybe' extends [mscorlib]System.Object
{
  .field public static native int '<msil>fn'
  .field public static native int 'maybe'
}
.method private static int32 Main(string[] a) cil managed
{
  .entrypoint
  .maxstack 1
  call void ['a']'<msil>a'::init1()
  call void ['b']'<msil>b'::init1()
  call void ['a']'<msil>a'::init2()
  call void ['b']'<msil>b'::init2()
  call void ['b']'<msil>b'::'ctor_b'()
  call void ['a']'<msil>a'::'ctor_a'()
  call int32 [msil.extern]'<sym>main'::'main'()
  ret
}

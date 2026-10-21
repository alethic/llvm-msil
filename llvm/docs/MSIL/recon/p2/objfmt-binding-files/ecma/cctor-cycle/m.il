.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern x { .ver 0:0:0:0 }
.assembly extern y { .ver 0:0:0:0 }
.assembly m { .ver 0:0:0:0 }
.method private static int32 Main(string[] a) cil managed
{
  .entrypoint
  .maxstack 2
  ldsfld native int [x]X::slot call void [mscorlib]System.Console::WriteLine(int64)   // expect 2000
  ldsfld native int [y]Y::slot call void [mscorlib]System.Console::WriteLine(int64)   // expect 1000
  ldc.i4.0 ret
}

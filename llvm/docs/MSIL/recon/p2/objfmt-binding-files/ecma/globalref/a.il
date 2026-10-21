.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern b { .ver 0:0:0:0 }
.assembly a { .ver 0:0:0:0 }
.method private static int32 Main(string[] x) cil managed
{
  .entrypoint
  .maxstack 2
  ldc.i4.1
  call int32 [b]'<Module>'::'g'(int32)
  ret
}

.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern 'msil.extern' { .ver 0:0:0:0 }
.assembly a { .ver 0:0:0:0 }
.method private static int32 Main(string[] x) cil managed
{
  .entrypoint
  .maxstack 2
  ldc.i4.1
  call int32 [msil.extern]'add3'::'add3'(int32)
  ret
}

.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'multi' { .ver 0:0:0:0 }
.module extern 'two.netmodule'
.file 'two.netmodule'
.method private static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldc.i4.s 20
  call int32 [.module 'two.netmodule']::'twice'(int32)
  ret
}

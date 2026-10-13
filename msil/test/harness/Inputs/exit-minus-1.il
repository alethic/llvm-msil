// 'main' returns -1: Windows reports it as -1 (0xFFFFFFFF), Linux as 255, so
// msil-run.py must write '[exit -1: outside 0..255]' (plan D37).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'exit-minus-1' { .ver 0:0:0:0 }

.method public static int32 'main'() cil managed
{
  .maxstack 1
  ldc.i4.m1
  ret
} // end of method 'main'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  call int32 'main'()
  ret
} // end of method '<msil>Main'

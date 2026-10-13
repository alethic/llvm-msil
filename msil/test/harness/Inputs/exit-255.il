// 'main' returns 255, the largest exit status that every OS reports unchanged
// (plan D37): the transcript is '[exit 255]' and it passes.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'exit-255' { .ver 0:0:0:0 }

.method public static int32 'main'() cil managed
{
  .maxstack 1
  ldc.i4 255
  ret
} // end of method 'main'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  call int32 'main'()
  ret
} // end of method '<msil>Main'

// 'main' returns 300. Windows reports the full 32-bit status, Linux keeps only
// the low 8 bits (300 & 0xff = 44), so a test like this would pass on one OS
// and not the other. msil-run.py must report it as '[exit 300: outside 0..255]'
// (plan D37).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'exit-300' { .ver 0:0:0:0 }

.method public static int32 'main'() cil managed
{
  .maxstack 1
  ldc.i4 300
  ret
} // end of method 'main'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  call int32 'main'()
  ret
} // end of method '<msil>Main'

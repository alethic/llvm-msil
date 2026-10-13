// Assembly 'same', exit 11. same-name-b.il has the same assembly name and
// exits with 22 (asmprinter recon 0.9: with both in one directory, 'dotnet'
// can silently run the other file's code).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'same' { .ver 0:0:0:0 }

.method public static int32 'main'() cil managed
{
  .maxstack 1
  ldc.i4.s 11
  ret
} // end of method 'main'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  call int32 'main'()
  ret
} // end of method '<msil>Main'

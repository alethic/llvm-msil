// The program of check-inputs.test and check-defaults.test: its exit status
// is 10 times the number of program arguments plus the digit that the first
// byte of stdin holds, or plus 9 when stdin is empty. Not run by itself.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'args-stdin' { .ver 0:0:0:0 }

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 3
  .locals init (int32)
  call int32 [mscorlib]System.Console::Read()
  stloc.0
  ldloc.0
  ldc.i4.0
  bge.s HAVE
  ldc.i4.s 57
  stloc.0
HAVE:
  ldarg.0
  ldlen
  conv.i4
  ldc.i4.s 10
  mul
  ldloc.0
  ldc.i4.s 48
  sub
  add
  ret
} // end of method '<msil>Main'

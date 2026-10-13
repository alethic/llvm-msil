// 'add_two' declares .maxstack 1 but pushes two values. ILAsm accepts it; the
// JIT rejects it with InvalidProgramException, and msil-jitall.py must report
// it without executing anything: if 'main' ever ran, it would print the marker
// line below before failing.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'jitall-maxstack' { .ver 0:0:0:0 }

.method public static int32 'add_two'() cil managed
{
  .maxstack 1
  ldc.i4.1
  ldc.i4.2
  add
  ret
} // end of method 'add_two'

.method public static int32 'main'() cil managed
{
  .maxstack 1
  ldstr "MSIL-HARNESS: main was executed"
  call void [mscorlib]System.Console::WriteLine(string)
  call int32 'add_two'()
  ret
} // end of method 'main'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldstr "MSIL-HARNESS: <msil>Main was executed"
  call void [mscorlib]System.Console::WriteLine(string)
  call int32 'main'()
  ret
} // end of method '<msil>Main'

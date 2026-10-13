// 'no_ret' falls off its end when its argument is non-zero: the block at
// @BB0_2 has no 'ret'. ILAsm accepts it; the JIT rejects it with
// InvalidProgramException, and msil-jitall.py must report it without executing
// anything: if 'main' ever ran, it would print the marker line below.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'jitall-falloff' { .ver 0:0:0:0 }

.method public static void 'no_ret'(int32) cil managed
{
  .maxstack 1
  ldarg.0
  brtrue @BB0_2
  ret
@BB0_2:
  nop
} // end of method 'no_ret'

.method public static int32 'main'() cil managed
{
  .maxstack 1
  ldstr "MSIL-HARNESS: main was executed"
  call void [mscorlib]System.Console::WriteLine(string)
  ldc.i4.1
  call void 'no_ret'(int32)
  ldc.i4.0
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

// The control for masked-join-calli.il and calli-wrong-arg.il: a managed
// calli with an int32 argument for its int32 parameter, followed by a join
// with one value on each path. msil-verify.py must accept it, and it runs:
// 'main' returns id(5) + 3 = 8 (the argument 5 takes the brtrue path).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'calli-ok' { .ver 0:0:0:0 }

.method public static int32 'id'(int32) cil managed
{
  .maxstack 1
  ldarg.0
  ret
} // end of method 'id'

.method public static int32 'join'(native int, int32) cil managed
{
  .maxstack 2
  ldarg.1
  ldftn int32 'id'(int32)
  calli int32(int32)
  ldarg.1
  brtrue @BB0_3
  ldc.i4.2
  br @BB0_4
@BB0_3:
  ldc.i4.3
@BB0_4:
  add
  ret
} // end of method 'join'

.method public static int32 'main'() cil managed
{
  .maxstack 2
  ldc.i4.0
  conv.i
  ldc.i4.5
  call int32 'join'(native int, int32)
  ret
} // end of method 'main'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  call int32 'main'()
  ret
} // end of method '<msil>Main'

// The control for masked-mix-*.il: the same three allowed first errors (a
// D13 frame, a .locals without init, a load through a native int), each
// followed by the corrected construct ('conv.i' before the 'add').
// msil-verify.py must accept it. Not run.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'masked-mix-ok' { .ver 0:0:0:0 }

.method public static native int 'mix_frame'(native int, int64) cil managed
{
  .maxstack 2
  .locals (native int)
  ldc.i4.s 16
  localloc
  stloc.0
  br @BB0_1
@BB0_1:
  ldarg.0
  ldarg.1
  conv.i
  add
  ret
} // end of method 'mix_frame'

.method public static native int 'mix_local'(native int, int64) cil managed
{
  .maxstack 2
  .locals (int32)
  ldloc.0
  pop
  br @BB0_1
@BB0_1:
  ldarg.0
  ldarg.1
  conv.i
  add
  ret
} // end of method 'mix_local'

.method public static native int 'mix_ldind'(native int, int64) cil managed
{
  .maxstack 2
  ldarg.0
  ldind.i4
  pop
  ldarg.0
  ldarg.1
  conv.i
  add
  ret
} // end of method 'mix_ldind'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldc.i4.0
  ret
} // end of method '<msil>Main'

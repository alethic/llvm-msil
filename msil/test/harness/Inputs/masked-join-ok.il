// The control for masked-join-*.il: the same three allowed first errors (a
// D13 frame, a .locals without init, a load through a native int), each
// followed by the corrected construct (one value on each path).
// msil-verify.py must accept it. Not run.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'masked-join-ok' { .ver 0:0:0:0 }

.method public static int32 'join_frame'(native int, int32) cil managed
{
  .maxstack 2
  .locals (native int)
  ldc.i4.s 16
  localloc
  stloc.0
  br @BB0_1
@BB0_1:
  ldarg.1
  brtrue @BB0_3
  ldc.i4.2
  br @BB0_4
@BB0_3:
  ldc.i4.3
@BB0_4:
  ret
} // end of method 'join_frame'

.method public static int32 'join_local'(native int, int32) cil managed
{
  .maxstack 2
  .locals (int32)
  ldloc.0
  pop
  br @BB0_1
@BB0_1:
  ldarg.1
  brtrue @BB0_3
  ldc.i4.2
  br @BB0_4
@BB0_3:
  ldc.i4.3
@BB0_4:
  ret
} // end of method 'join_local'

.method public static int32 'join_ldind'(native int, int32) cil managed
{
  .maxstack 2
  ldarg.0
  ldind.i4
  pop
  ldarg.1
  brtrue @BB0_3
  ldc.i4.2
  br @BB0_4
@BB0_3:
  ldc.i4.3
@BB0_4:
  ret
} // end of method 'join_ldind'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldc.i4.0
  ret
} // end of method '<msil>Main'

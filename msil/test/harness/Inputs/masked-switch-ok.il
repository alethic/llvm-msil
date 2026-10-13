// The control for masked-switch-*.il: the same three allowed first errors (a
// D13 frame, a .locals without init, a load through a native int), each
// followed by the corrected construct ('conv.i4' before the 'switch').
// msil-verify.py must accept it. Not run.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'masked-switch-ok' { .ver 0:0:0:0 }

.method public static int32 'select_frame'(native int) cil managed
{
  .maxstack 2
  .locals (native int)
  ldc.i4.s 16
  localloc
  stloc.0
  br @BB0_1
@BB0_1:
  ldarg.0
  conv.i4
  switch (@BB0_3, @BB0_4)
  br @BB0_5
@BB0_3:
  ldc.i4.s 10
  ret
@BB0_4:
  ldc.i4.s 11
  ret
@BB0_5:
  ldc.i4.s 99
  ret
} // end of method 'select_frame'

.method public static int32 'select_local'(native int) cil managed
{
  .maxstack 2
  .locals (int32)
  ldloc.0
  pop
  br @BB0_1
@BB0_1:
  ldarg.0
  conv.i4
  switch (@BB0_3, @BB0_4)
  br @BB0_5
@BB0_3:
  ldc.i4.s 10
  ret
@BB0_4:
  ldc.i4.s 11
  ret
@BB0_5:
  ldc.i4.s 99
  ret
} // end of method 'select_local'

.method public static int32 'select_ldind'(native int) cil managed
{
  .maxstack 2
  ldarg.0
  ldind.i4
  pop
  ldarg.0
  conv.i4
  switch (@BB0_3, @BB0_4)
  br @BB0_5
@BB0_3:
  ldc.i4.s 10
  ret
@BB0_4:
  ldc.i4.s 11
  ret
@BB0_5:
  ldc.i4.s 99
  ret
} // end of method 'select_ldind'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  ldc.i4.0
  ret
} // end of method '<msil>Main'

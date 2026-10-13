// Unverifiable by design, as the backend emits it (Design.md section 1,
// plan D2/D13/D15/D21), and accepted by msil-verify.py. ILVerify and PEVerify
// often stop at the first error of a method, so each allowed construct is the
// first error of a method of its own:
//   'load', 'store'  ldind/stind through a native int argument, no frame
//                    (ILVerify StackByRef, PEVerify 'Expected ByRef')
//   'uninit'         .locals without init and no localloc (ILVerify
//                    InitLocals, PEVerify 'initlocals must be set')
//   'copy', 'fill'   cpblk, initblk (ILVerify Unverifiable, PEVerify
//                    'Instruction cannot be verified')
//   'scratch'        localloc (the same two)
//   'indirect'       a managed calli (ILVerify cannot verify it: 'ImportCalli
//                    not implemented' is a note; PEVerify 'Instruction cannot
//                    be verified')
// 'frame' combines them the way a D13 frame does. The module also runs:
// 'main' returns 5 + 0 + 1 + 4 + 9 = 19.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'unverifiable-ok' { .ver 0:0:0:0 }

.method public static int32 'load'(native int) cil managed
{
  .maxstack 1
  ldarg.0
  ldind.i4
  ret
} // end of method 'load'

.method public static void 'store'(native int, int32) cil managed
{
  .maxstack 2
  ldarg.0
  ldarg.1
  stind.i4
  ret
} // end of method 'store'

.method public static int32 'uninit'(int32) cil managed
{
  .maxstack 1
  .locals (int32)
  ldarg.0
  stloc.0
  ldloc.0
  ret
} // end of method 'uninit'

.method public static void 'copy'(native int, native int, int32) cil managed
{
  .maxstack 3
  ldarg.0
  ldarg.1
  ldarg.2
  cpblk
  ret
} // end of method 'copy'

.method public static void 'fill'(native int, int32, int32) cil managed
{
  .maxstack 3
  ldarg.0
  ldarg.1
  ldarg.2
  initblk
  ret
} // end of method 'fill'

.method public static int32 'scratch'(int32) cil managed
{
  .maxstack 3
  ldc.i4.8
  localloc
  dup
  ldarg.0
  stind.i4
  ldind.i4
  ret
} // end of method 'scratch'

// A D13 frame: localloc into a native int local of a .locals without init,
// then initblk, stind, cpblk and ldind through it. Returns arg * arg.
.method public static int32 'frame'(int32) cil managed
{
  .maxstack 4
  .locals (native int)
  ldc.i4.s 32
  localloc
  stloc.0
  ldloc.0
  ldc.i4.s 16
  add
  ldc.i4.0
  ldc.i4.s 16
  initblk
  ldloc.0
  ldarg.0
  stind.i4
  ldloc.0
  ldc.i4.8
  add
  ldloc.0
  ldc.i4.4
  cpblk
  ldloc.0
  ldc.i4.8
  add
  ldind.i4
  ldloc.0
  ldc.i4.s 16
  add
  ldind.i4
  add
  ldarg.0
  mul
  ret
} // end of method 'frame'

.method public static int32 'indirect'(int32) cil managed
{
  .maxstack 2
  ldarg.0
  ldftn int32 'frame'(int32)
  calli int32(int32)
  ret
} // end of method 'indirect'

// [fp] = 5; [fp + 4] = 77, then zeroed by 'fill'; [fp + 8] = [fp] by 'copy'.
.method public static int32 'main'() cil managed
{
  .maxstack 3
  .locals (native int)
  ldc.i4.s 16
  localloc
  stloc.0
  ldloc.0
  ldc.i4.5
  call void 'store'(native int, int32)
  ldloc.0
  ldc.i4.4
  add
  ldc.i4.s 77
  call void 'store'(native int, int32)
  ldloc.0
  ldc.i4.4
  add
  ldc.i4.0
  ldc.i4.s 12
  call void 'fill'(native int, int32, int32)
  ldloc.0
  ldc.i4.8
  add
  ldloc.0
  ldc.i4.4
  call void 'copy'(native int, native int, int32)
  ldloc.0
  ldc.i4.8
  add
  call int32 'load'(native int)
  ldloc.0
  ldc.i4.4
  add
  call int32 'load'(native int)
  add
  ldc.i4.1
  call int32 'uninit'(int32)
  add
  ldc.i4.4
  call int32 'scratch'(int32)
  add
  ldc.i4.3
  call int32 'indirect'(int32)
  add
  ret
} // end of method 'main'

.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 1
  call int32 'main'()
  ret
} // end of method '<msil>Main'

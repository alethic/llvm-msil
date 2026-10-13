.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly ilv3 {}
// locals without init, then switch on native int
.method public static int32 'swl'(native int) cil managed
{ .maxstack 2 .locals (int32) ldc.i4.0 stloc.0 ldarg.0 switch (L0) br LD
L0: ldloc.0 ret
LD: ldc.i4.1 ret }
// localloc frame first (O0 shape), then switch on native int
.method public static int32 'swf'(native int) cil managed
{ .maxstack 2 .locals (native int) ldc.i4 16 localloc stloc.0 ldarg.0 switch (L0) br LD
L0: ldc.i4.0 ret
LD: ldc.i4.1 ret }
// load through native int first, then switch on native int
.method public static int32 'swd'(native int) cil managed
{ .maxstack 2 ldarg.0 ldind.i switch (L0) br LD
L0: ldc.i4.0 ret
LD: ldc.i4.1 ret }
// MemoryCopy after a global load (ldsfld) only
.field public static initonly native int 'g'
.method public static void 'mmg'(native int) cil managed
{ .maxstack 4 ldsfld native int 'g' ldc.i4.1 add ldsfld native int 'g' ldarg.0 conv.i8 dup call void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64) ret }

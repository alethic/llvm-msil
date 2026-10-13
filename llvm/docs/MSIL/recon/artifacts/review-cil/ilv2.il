.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly ilv2 {}
.field public static initonly native int 'g'
// D15 MEMMOVE record: src dst len -> conv.i8 dup call MemoryCopy
.method public static void 'mm'(native int, native int, native int) cil managed
{ .maxstack 4  ldarg.1 ldarg.0 ldarg.2 conv.i8 dup call void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64) ret }
// GLOBAL_ADDR with offset
.method public static native int 'ga'() cil managed
{ .maxstack 2  ldsfld native int 'g' ldc.i4 20 add ret }
// switch after conv.u4 (proposed fix)
.method public static int32 'sw32'(native int) cil managed
{ .maxstack 1 ldarg.0 conv.u4 switch (L0) br LD
L0: ldc.i4.1 ret
LD: ldc.i4.0 ret }
// uitofp i32->f32: conv.r.un conv.r4
.method public static float32 'u2f'(int32) cil managed
{ .maxstack 1 ldarg.0 conv.r.un conv.r4 ret }
// fused fp branch
.method public static int32 'fb'(float64, float64) cil managed
{ .maxstack 2 ldarg.0 ldarg.1 bge.un L ldc.i4.0 ret
L: ldc.i4.1 ret }
// zext i32 -> i64 and compare native int
.method public static int32 'zx'(int32, native int) cil managed
{ .maxstack 2 ldarg.0 conv.u ldarg.1 clt.un ret }
// CONST_I64 large
.method public static native int 'k'() cil managed
{ .maxstack 1 ldc.i8 0x123456789 conv.i ret }
// TRAP
.method public static void 't'() cil managed
{ .maxstack 1 ldnull throw }

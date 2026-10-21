.module 'obj.o'
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.module extern 'msil.unresolved'
.method public static int32 'twice'(int32 x) cil managed
{
  .maxstack 2
  .line 3,3 : 1,20 'obj.c'
  ldarg.0
  ldc.i4.2
  .line 4,4 : 1,20 'obj.c'
  mul
  ret
}
.method public static pinvokeimpl("msil.unresolved" cdecl) int32 'ext'(int32) cil managed preservesig {}

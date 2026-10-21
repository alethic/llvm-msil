// Object whose C link semantics are typed custom attributes on its rows.
// The attribute types live in a placeholder assembly that never exists.
.assembly extern 'msil.link' { .ver 0:0:0:0 }
.module 'obj.o'
.custom instance void [msil.link]MSIL.Link.ObjectAttribute::.ctor(string, int32) = ( 01 00 1B 78 38 36 5F 36 34 2D 70 63 2D 77 69 6E 64 6F 77 73 2D 6D 73 76 63 2D 6D 73 69 6C 01 00 00 00 00 00 )
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.module extern 'msil.unresolved'
.method public static int32 'weakfn'() cil managed
{
  .custom instance void [msil.link]MSIL.Link.WeakAttribute::.ctor() = ( 01 00 00 00 )
  .maxstack 1
  ldc.i4.1
  ret
}
.method public static int32 'inl'() cil managed
{
  .custom instance void [msil.link]MSIL.Link.ComdatAttribute::.ctor(string) = ( 01 00 03 69 6E 6C 00 00 )
  .maxstack 1
  ldc.i4.2
  ret
}
.field public static initonly native int 'common_c'
.custom instance void [msil.link]MSIL.Link.CommonAttribute::.ctor(int32, int32) = ( 01 00 04 00 00 00 04 00 00 00 00 00 )
.method public static pinvokeimpl("msil.unresolved" cdecl) int32 'twice'(int32) cil managed preservesig {}

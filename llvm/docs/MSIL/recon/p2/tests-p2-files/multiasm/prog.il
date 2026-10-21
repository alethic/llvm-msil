.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern 'mylib' { .ver 0:0:0:0 }
.assembly 'prog' { .ver 0:0:0:0 }
.method public hidebysig static pinvokeimpl("native" cdecl) int32 'add3'(int32, int32, int32) cil managed preservesig {}
.method private hidebysig static int32 '<msil>Main'(string[] args) cil managed
{
  .entrypoint
  .maxstack 3
  ldc.i4.s 40
  ldc.i4.1
  call int32 [mylib]'Contoso.Native'::'add'(int32, int32)
  ldc.i4.1
  ldc.i4.0
  call int32 'add3'(int32, int32, int32)
  ret
}

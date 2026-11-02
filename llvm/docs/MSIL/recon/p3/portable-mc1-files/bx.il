.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly bx { .ver 0:0:0:0 }
.module bx.exe
// stub shape without typed locals: handle -> object through box/unbox of the GCHandle value
.method public static object '<mc>get'(native int h) cil managed
{
  .maxstack 1
  ldarg.0
  call valuetype [mscorlib]System.Runtime.InteropServices.GCHandle [mscorlib]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int)
  box [mscorlib]System.Runtime.InteropServices.GCHandle
  unbox [mscorlib]System.Runtime.InteropServices.GCHandle
  call instance object [mscorlib]System.Runtime.InteropServices.GCHandle::get_Target()
  ret
}
.method public static void '<mc>free'(native int h) cil managed
{
  .maxstack 1
  ldarg.0
  call valuetype [mscorlib]System.Runtime.InteropServices.GCHandle [mscorlib]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int)
  box [mscorlib]System.Runtime.InteropServices.GCHandle
  unbox [mscorlib]System.Runtime.InteropServices.GCHandle
  call instance void [mscorlib]System.Runtime.InteropServices.GCHandle::Free()
  ret
}
// a boxed DateTime handle used as 'this' of an instance member (DateTime::get_Ticks)
.method public static int64 ticks(native int h) cil managed
{
  .maxstack 1
  ldarg.0
  call object '<mc>get'(native int)
  unbox [mscorlib]System.DateTime
  call instance int64 [mscorlib]System.DateTime::get_Ticks()
  ret
}
.method public static int32 main() cil managed
{
  .entrypoint
  .maxstack 3
  .locals init (native int h, native int d)
  ldstr "target through box/unbox: ok"
  call valuetype [mscorlib]System.Runtime.InteropServices.GCHandle [mscorlib]System.Runtime.InteropServices.GCHandle::Alloc(object)
  call native int [mscorlib]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype [mscorlib]System.Runtime.InteropServices.GCHandle)
  stloc.0
  ldloc.0
  call object '<mc>get'(native int)
  castclass [mscorlib]System.String
  call void [mscorlib]System.Console::WriteLine(string)
  ldloc.0
  call void '<mc>free'(native int)
  call valuetype [mscorlib]System.DateTime [mscorlib]System.DateTime::get_UtcNow()
  box [mscorlib]System.DateTime
  call valuetype [mscorlib]System.Runtime.InteropServices.GCHandle [mscorlib]System.Runtime.InteropServices.GCHandle::Alloc(object)
  call native int [mscorlib]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype [mscorlib]System.Runtime.InteropServices.GCHandle)
  stloc.1
  ldloc.1
  call int64 ticks(native int)
  ldc.i8 630000000000000000
  cgt
  call void [mscorlib]System.Console::WriteLine(bool)
  ldloc.1
  call void '<mc>free'(native int)
  // use after free: loud?
  ldloc.0
  call object '<mc>get'(native int)
  call void [mscorlib]System.Console::WriteLine(object)
  ldc.i4.s 42
  ret
}

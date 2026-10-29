// The same stack case with System.Int128 (.NET 8+, System.Runtime): the runtime knows its 16-byte alignment.
.assembly extern System.Runtime { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A ) .ver 8:0:0:0 }
.assembly extern System.Console { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A ) .ver 8:0:0:0 }
.assembly i128n {}
.module i128n.dll
.method public static pinvokeimpl("libnat128.so" nomangle cdecl) valuetype [System.Runtime]System.Int128 stk7(int64, int64, int64, int64, int64, int64, int64, valuetype [System.Runtime]System.Int128) cil managed preservesig {}
.method public static pinvokeimpl("libnat128.so" nomangle cdecl) valuetype [System.Runtime]System.Int128 mul3p1(valuetype [System.Runtime]System.Int128) cil managed preservesig {}
.method public static int32 main() cil managed
{ .entrypoint
  .maxstack 10
  .locals init (valuetype [System.Runtime]System.Int128 v, valuetype [System.Runtime]System.Int128 r)
  ldloca.s v
  ldc.i8 1
  ldc.i8 5
  call instance void [System.Runtime]System.Int128::.ctor(uint64, uint64)
  ldstr "Int128 mul3p1 "
  call void [System.Console]System.Console::Write(string)
  ldloc.0
  call valuetype [System.Runtime]System.Int128 mul3p1(valuetype [System.Runtime]System.Int128)
  box [System.Runtime]System.Int128
  call void [System.Console]System.Console::Write(object)
  ldstr " stk7 "
  call void [System.Console]System.Console::Write(string)
  ldc.i8 1
  ldc.i8 2
  ldc.i8 3
  ldc.i8 4
  ldc.i8 5
  ldc.i8 6
  ldc.i8 7
  ldloc.0
  call valuetype [System.Runtime]System.Int128 stk7(int64, int64, int64, int64, int64, int64, int64, valuetype [System.Runtime]System.Int128)
  box [System.Runtime]System.Int128
  call void [System.Console]System.Console::WriteLine(object)
  ldc.i4.0
  ret }

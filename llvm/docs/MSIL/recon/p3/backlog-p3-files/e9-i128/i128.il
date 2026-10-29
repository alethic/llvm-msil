// SysV __int128 by value through P/Invokes. V8: explicit 16-byte value type, two int64 fields, .pack 8.
// V16: the same with .pack 16. Each check prints 1 (right) or 0 (wrong).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly i128 {}
.module i128.exe
.class private explicit ansi sealed V8 extends [mscorlib]System.ValueType
{ .pack 8 .size 16
  .field [0] public int64 lo
  .field [8] public int64 hi }
.class private explicit ansi sealed V16 extends [mscorlib]System.ValueType
{ .pack 16 .size 16
  .field [0] public int64 lo
  .field [8] public int64 hi }
.method public static pinvokeimpl("libnat128.so" nomangle cdecl) valuetype V8 mul3p1(valuetype V8) cil managed preservesig {}
.method public static pinvokeimpl("libnat128.so" nomangle cdecl) valuetype V8 split5(int64, int64, int64, int64, int64, valuetype V8) cil managed preservesig {}
.method public static pinvokeimpl("libnat128.so" nomangle cdecl) valuetype V8 stk7(int64, int64, int64, int64, int64, int64, int64, valuetype V8) cil managed preservesig {}
.method public static pinvokeimpl("libnat128.so" nomangle cdecl) valuetype V8 stk7s(int64, int64, int64, int64, int64, int64, int64, valuetype V8) cil managed preservesig {}
.method public static pinvokeimpl("libnat128.so" nomangle cdecl) valuetype V16 stk7(int64, int64, int64, int64, int64, int64, int64, valuetype V16) cil managed preservesig {}
.method static void check(valuetype V8 r, int64 lo, int64 hi) cil managed
{ .maxstack 3
  ldarga.s r
  ldfld int64 V8::lo
  ldarg.1
  ceq
  ldarga.s r
  ldfld int64 V8::hi
  ldarg.2
  ceq
  and
  call void [mscorlib]System.Console::Write(int32)
  ret }
.method public static int32 main() cil managed
{ .entrypoint
  .maxstack 10
  .locals init (valuetype V8 v, valuetype V16 w, valuetype V16 rw)
  // v = 2^64 + 5  (lo 5, hi 1)
  ldloca.s v
  ldc.i8 5
  stfld int64 V8::lo
  ldloca.s v
  ldc.i8 1
  stfld int64 V8::hi
  ldloca.s w
  ldc.i8 5
  stfld int64 V16::lo
  ldloca.s w
  ldc.i8 1
  stfld int64 V16::hi
  ldstr "mul3p1 "
  call void [mscorlib]System.Console::Write(string)
  ldloc.0
  call valuetype V8 mul3p1(valuetype V8)
  ldc.i8 16
  ldc.i8 3
  call void check(valuetype V8, int64, int64)
  ldstr " split5 "
  call void [mscorlib]System.Console::Write(string)
  ldc.i8 1
  ldc.i8 2
  ldc.i8 3
  ldc.i8 4
  ldc.i8 5
  ldloc.0
  call valuetype V8 split5(int64, int64, int64, int64, int64, valuetype V8)
  ldc.i8 20
  ldc.i8 1
  call void check(valuetype V8, int64, int64)
  ldstr " stk7s(struct) "
  call void [mscorlib]System.Console::Write(string)
  ldc.i8 1
  ldc.i8 2
  ldc.i8 3
  ldc.i8 4
  ldc.i8 5
  ldc.i8 6
  ldc.i8 7
  ldloc.0
  call valuetype V8 stk7s(int64, int64, int64, int64, int64, int64, int64, valuetype V8)
  ldc.i8 12
  ldc.i8 1
  call void check(valuetype V8, int64, int64)
  ldstr " stk7(i128,pack8) "
  call void [mscorlib]System.Console::Write(string)
  ldc.i8 1
  ldc.i8 2
  ldc.i8 3
  ldc.i8 4
  ldc.i8 5
  ldc.i8 6
  ldc.i8 7
  ldloc.0
  call valuetype V8 stk7(int64, int64, int64, int64, int64, int64, int64, valuetype V8)
  ldc.i8 12
  ldc.i8 1
  call void check(valuetype V8, int64, int64)
  ldstr " stk7(i128,pack16) "
  call void [mscorlib]System.Console::Write(string)
  ldc.i8 1
  ldc.i8 2
  ldc.i8 3
  ldc.i8 4
  ldc.i8 5
  ldc.i8 6
  ldc.i8 7
  ldloc.1
  call valuetype V16 stk7(int64, int64, int64, int64, int64, int64, int64, valuetype V16)
  stloc.2
  ldloca.s rw
  ldfld int64 V16::lo
  ldc.i8 12
  ceq
  ldloca.s rw
  ldfld int64 V16::hi
  ldc.i8 1
  ceq
  and
  call void [mscorlib]System.Console::Write(int32)
  call void [mscorlib]System.Console::WriteLine()
  ldc.i4.0
  ret }

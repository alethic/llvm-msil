// The stack case of i128.il with an explicit 8-byte pad parameter before the value type, as msil-ld
// could emit it from the SysV classification (the value type then starts at a 16-byte stack offset).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly i128pad {}
.module i128pad.exe
.class private explicit ansi sealed V8 extends [mscorlib]System.ValueType
{ .pack 8 .size 16
  .field [0] public int64 lo
  .field [8] public int64 hi }
.method public static pinvokeimpl("libnat128.so" as "stk7" nomangle cdecl) valuetype V8 stk7pad(int64, int64, int64, int64, int64, int64, int64, int64, valuetype V8) cil managed preservesig {}
.method public static int32 main() cil managed
{ .entrypoint
  .maxstack 10
  .locals init (valuetype V8 v, valuetype V8 r)
  ldloca.s v
  ldc.i8 5
  stfld int64 V8::lo
  ldloca.s v
  ldc.i8 1
  stfld int64 V8::hi
  ldc.i8 1
  ldc.i8 2
  ldc.i8 3
  ldc.i8 4
  ldc.i8 5
  ldc.i8 6
  ldc.i8 7
  ldc.i8 0        // pad
  ldloc.0
  call valuetype V8 stk7pad(int64, int64, int64, int64, int64, int64, int64, int64, valuetype V8)
  stloc.1
  ldstr "stk7(i128,pad) lo="
  call void [mscorlib]System.Console::Write(string)
  ldloca.s r
  ldfld int64 V8::lo
  call void [mscorlib]System.Console::Write(int64)
  ldstr " hi="
  call void [mscorlib]System.Console::Write(string)
  ldloca.s r
  ldfld int64 V8::hi
  call void [mscorlib]System.Console::WriteLine(int64)
  ldloca.s r
  ldfld int64 V8::lo
  ldc.i8 12
  bne.un FAIL
  ldloca.s r
  ldfld int64 V8::hi
  ldc.i8 1
  bne.un FAIL
  ldc.i4.s 42
  ret
FAIL:
  ldc.i4.1
  ret }

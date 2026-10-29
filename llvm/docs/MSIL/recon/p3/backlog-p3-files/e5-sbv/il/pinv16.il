// A SysV two-register struct result ({i64, i64} in RAX:RDX) and a 24-byte MEMORY-class
// argument, through P/Invokes with explicit-layout value types (Design.md 2.3 shape).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly pinv16 {}
.module pinv16.exe
.class private explicit ansi sealed S16 extends [mscorlib]System.ValueType
{ .pack 1 .size 16
  .field [0] public int64 a
  .field [8] public int64 b }
.class private explicit ansi sealed S24 extends [mscorlib]System.ValueType
{ .pack 1 .size 24 }
.method public static pinvokeimpl("libc.so.6" nomangle cdecl) valuetype S16 lldiv(native int, native int) cil managed preservesig {}
.method public static pinvokeimpl("libnat24.so" nomangle cdecl) int64 sum24(valuetype S24) cil managed preservesig {}
.method public static int32 main() cil managed
{ .entrypoint
  .maxstack 3
  .locals init (valuetype S16 r, valuetype S24 s)
  ldc.i8 1000000000007
  conv.i
  ldc.i4.s 10
  conv.i
  call valuetype S16 lldiv(native int, native int)
  stloc.0
  ldloca.s s
  ldc.i8 5
  stind.i8
  ldloca.s s
  ldc.i4.8
  add
  ldc.i8 6
  stind.i8
  ldloca.s s
  ldc.i4.s 16
  add
  ldc.i8 7
  stind.i8
  ldloc.1
  call int64 sum24(valuetype S24)
  ldc.i8 18
  bne.un FAIL
  ldloca.s r
  ldfld int64 S16::b
  ldc.i8 7
  bne.un FAIL
  ldloca.s r
  ldfld int64 S16::a
  ldc.i8 100000000000
  bne.un FAIL
  ldc.i4.s 42
  ret
FAIL:
  ldc.i4.1
  ret }

// SysV x86-64: mk returns a 24-byte struct through a hidden pointer (IR sret),
// passed here as a plain first 'native int'; sum takes the struct by value (IR
// byval), passed here as an explicit-layout value type.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly abi { .ver 0:0:0:0 }
.class public explicit ansi sealed big extends [mscorlib]System.ValueType { .pack 8 .size 24 }
.method public hidebysig static pinvokeimpl("libbig.so.1" as "mk" nomangle cdecl) void 'mk'(native int, int64) cil managed preservesig {}
.method public hidebysig static pinvokeimpl("libbig.so.1" as "sum" nomangle cdecl) int64 'sum'(valuetype big) cil managed preservesig {}
.method public static int32 main() cil managed {
  .entrypoint
  .maxstack 4
  .locals init (native int p)
  ldc.i4.s 24
  conv.i
  localloc
  stloc.0
  ldloc.0
  ldc.i8 7
  call void 'mk'(native int, int64)
  ldloc.0
  ldc.i4.s 16
  add
  ldind.i8
  call void [mscorlib]System.Console::WriteLine(int64)
  ldloc.0
  ldobj big
  call int64 'sum'(valuetype big)
  call void [mscorlib]System.Console::WriteLine(int64)
  ldc.i4.0
  ret
}

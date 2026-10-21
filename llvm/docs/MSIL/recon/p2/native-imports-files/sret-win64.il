// lldiv returns lldiv_t (16 bytes) on Win64 through a hidden pointer in RCX:
// declared here as a plain first 'native int' parameter, as IR 'sret' would be.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly sret { .ver 0:0:0:0 }
.method public hidebysig static pinvokeimpl("api-ms-win-crt-utility-l1-1-0.dll" as "lldiv" nomangle cdecl) void 'lldiv'(native int, int64, int64) cil managed preservesig {}
.method public static int32 main() cil managed {
  .entrypoint
  .maxstack 4
  .locals init (native int p)
  ldc.i4.s 16
  conv.i
  localloc
  stloc.0
  ldloc.0
  ldc.i8 47
  ldc.i8 5
  call void 'lldiv'(native int, int64, int64)
  ldloc.0
  ldind.i8
  call void [mscorlib]System.Console::WriteLine(int64)
  ldloc.0
  ldc.i4.8
  add
  ldind.i8
  call void [mscorlib]System.Console::WriteLine(int64)
  ldc.i4.0
  ret
}

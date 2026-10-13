// native int + int64: legal for the x64 JIT, a stack-type error per ECMA-335.
// ILVerify 10.0.12 does not report it; PEVerify does (StackUnexpected).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly mix { }
.method public static native int mix(native int p, int64 o) cil managed {
  .maxstack 2
  ldarg.0
  ldarg.1
  add
  ret
}
.method public static int32 Main(string[] args) cil managed {
  .entrypoint
  .maxstack 2
  ldc.i4.0
  conv.i
  ldc.i8 5
  call native int mix(native int, int64)
  conv.i4
  ret
}

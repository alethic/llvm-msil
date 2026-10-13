// Unverifiable by design (localloc frame, native int addresses, no .locals
// init, calli): msil-verify must accept it.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89) .ver 4:0:0:0 }
.assembly unverifiable { }
.method public static int32 sq(int32 x) cil managed { .maxstack 2 ldarg.0 ldarg.0 mul ret }
.method public static int32 frame(int32 a) cil managed {
  .maxstack 3
  .locals (native int fp)
  ldc.i4 32
  localloc
  stloc fp
  ldloc fp
  ldarg a
  stind.i4
  ldloc fp
  ldind.i4
  ldftn int32 sq(int32)
  calli int32(int32)
  ret
}
.method public static int32 Main(string[] args) cil managed {
  .entrypoint
  .maxstack 1
  ldc.i4.3
  call int32 frame(int32)
  ret
}

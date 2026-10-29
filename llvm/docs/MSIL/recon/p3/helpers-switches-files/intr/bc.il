// f32 -> i32 bitcast: today's localloc slot vs an address-taken typed local vs BitConverter (net core only).
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly extern System.Runtime { .publickeytoken = (B0 3F 5F 7F 11 D5 0A 3A ) .ver 8:0:0:0 }
.assembly bc { .ver 0:0:0:0 }
.method public static int32 f2u_localloc(float32) cil managed noinlining {
  .maxstack 3
  .locals (native int)
  ldc.i4.s 16
  localloc
  stloc.0
  ldloc.0
  ldarg.0
  stind.r4
  ldloc.0
  ldind.i4
  ret
}
.method public static int32 f2u_ldloca(float32) cil managed noinlining {
  .maxstack 2
  .locals (float32)
  ldarg.0
  stloc.0
  ldloca.s 0
  ldind.i4
  ret
}
.method public static int32 f2u_ldarga(float32) cil managed noinlining {
  .maxstack 1
  ldarga.s 0
  ldind.i4
  ret
}
.method public static int32 f2u_bcl(float32) cil managed noinlining {
  .maxstack 1
  ldarg.0
  call int32 [System.Runtime]System.BitConverter::SingleToInt32Bits(float32)
  ret
}
.method public static int32 sum(int32 n, int32 which) cil managed {
  .maxstack 4
  .locals (int32 i, int32 s, float32 f)
  ldc.i4.0
  stloc.0
  ldc.i4.0
  stloc.1
loop:
  ldloc.0
  ldarg.0
  bge done
  ldloc.0
  conv.r4
  ldc.r4 0.37
  mul
  stloc.2
  ldarg.1
  switch (l0, l1, l2, l3)
l0: ldloc.2
  call int32 f2u_localloc(float32)
  br acc
l1: ldloc.2
  call int32 f2u_ldloca(float32)
  br acc
l2: ldloc.2
  call int32 f2u_ldarga(float32)
  br acc
l3: ldloc.2
  call int32 f2u_bcl(float32)
acc:
  ldloc.1
  add
  stloc.1
  ldloc.0
  ldc.i4.1
  add
  stloc.0
  br loop
done:
  ldloc.1
  ret
}
.method public static int32 main() cil managed {
  .entrypoint
  .maxstack 6
  ldc.i4 1000
  ldc.i4.0
  call int32 sum(int32, int32)
  ldc.i4 1000
  ldc.i4.1
  call int32 sum(int32, int32)
  sub
  ldc.i4 1000
  ldc.i4.2
  call int32 sum(int32, int32)
  ldc.i4 1000
  ldc.i4.3
  call int32 sum(int32, int32)
  sub
  or
  ret
}

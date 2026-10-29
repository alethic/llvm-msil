.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly bc3 { .ver 0:0:0:0 }
.class private explicit sealed '<msil>f32bits' extends [mscorlib]System.ValueType {
  .pack 0
  .size 4
  .field [0] public float32 f
  .field [0] public int32 i
}
.method public static int32 f2u_union(float32) cil managed noinlining {
  .maxstack 2
  .locals init (valuetype '<msil>f32bits' u)
  ldloca.s u
  ldarg.0
  stfld float32 '<msil>f32bits'::f
  ldloca.s u
  ldfld int32 '<msil>f32bits'::i
  ret
}
.method public static float32 u2f_union(int32) cil managed noinlining {
  .maxstack 2
  .locals init (valuetype '<msil>f32bits' u)
  ldloca.s u
  ldarg.0
  stfld int32 '<msil>f32bits'::i
  ldloca.s u
  ldfld float32 '<msil>f32bits'::f
  ret
}
.method public static int32 main() cil managed {
  .entrypoint
  .maxstack 4
  ldc.r4 1.5
  call int32 f2u_union(float32)
  ldc.i4 0x3fc00000
  sub
  ldc.i4 0x40490fdb
  call float32 u2f_union(int32)
  ldc.r4 3.14159274
  sub
  conv.i4
  or
  ret
}

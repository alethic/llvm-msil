// Input of roundtrip-netfx-ildasm-inf.test: a module whose only method loads
// +infinity the way the backend writes it, 'ldc.r8 float64(0x...)'. The .NET
// Framework 4.8 ildasm prints that operand back as 'inf', which ilasm cannot
// parse (recon cil-runtime.md 15). Nothing runs it.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'roundtrip-inf' { .ver 0:0:0:0 }

.method public static float64 'pos_inf'() cil managed
{
  .maxstack 1
  ldc.r8 float64(0x7FF0000000000000)
  ret
} // end of method 'pos_inf'

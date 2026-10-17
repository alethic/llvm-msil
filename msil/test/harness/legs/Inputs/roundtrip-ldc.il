// Input of the round-trip self-tests (roundtrip-mismatch.test,
// roundtrip-ilasm-error.test): a module in the Phase-1 output shape (plan
// section 5.1) with one 'ldc.i4.0', which the stand-in ilasm fake-ilasm.py
// rewrites in the listing before the real ilasm reassembles it. Nothing runs
// it.
.assembly extern mscorlib { .publickeytoken = (B7 7A 5C 56 19 34 E0 89 ) .ver 4:0:0:0 }
.assembly 'roundtrip-ldc' { .ver 0:0:0:0 }

.method public static int32 'main'() cil managed
{
  .maxstack 1
  ldc.i4.0
  ret
} // end of method 'main'

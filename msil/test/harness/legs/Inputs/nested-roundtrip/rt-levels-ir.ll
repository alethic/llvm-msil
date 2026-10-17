; The IR input of ../../roundtrip-composites.test, run only by a nested lit
; with -a: IR that %{msil_ll} builds, verifies and runs at every llc level,
; and with --param msil_roundtrip=1 also round-trips through ildasm and ilasm
; and runs again. The stand-in compiler copies rt-levels-ir.il.in.
; RUN: %{msil_ll}

define i32 @main() {
  ret i32 12
}

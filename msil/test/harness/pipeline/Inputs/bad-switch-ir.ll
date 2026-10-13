; The negative case of verify-runs.test for %{msil_ll}, run only by a nested
; lit: IR whose stand-in output (bad-switch-ir.il.in) runs correctly but
; switches on a native int, which breaks plan D35. msil-verify rejects it
; while the JIT and the runtime accept it, so the run fails only if the
; composite really runs %msil_verify.
; RUN: %{msil_ll}

define i32 @main() {
  ret i32 7
}

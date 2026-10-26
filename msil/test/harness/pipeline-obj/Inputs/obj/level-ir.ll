; Run in a nested lit by ../../levels.test with --param msil_pipeline=obj:
; the per-level composite %{msil_ll_O3}, whatever msil_opt_levels says.
; RUN: %{msil_ll_O3}
; prog-value: 6

define i32 @main() {
  ret i32 6
}

; Smoke test (Milestone-3 goal): 'define i32 @main() { ret i32 42 }' through llc
; at -O0 and -O2 exits with status 42.
; RUN: %{msil_ll}

define i32 @main() {
  ret i32 42
}

; The positive case of jitall-runs.test for %{msil_ll}, run only by a nested
; lit with -a: IR that verifies, force-JITs and runs at every llc level of
; %{msil_ll}, so that the nested output shows the msil-verify command and the
; jitall summary of each level.
; RUN: %{msil_ll}

define i32 @main() {
  ret i32 9
}

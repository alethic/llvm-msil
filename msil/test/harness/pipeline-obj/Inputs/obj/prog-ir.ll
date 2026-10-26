; Run in a nested lit by ../../ll.test with --param msil_pipeline=obj:
; %{msil_ll} at every llc level of msil_opt_levels, the IR compiled by the
; stand-in llc of ../../../prog/Inputs/prog-cc.py (llc -filetype=obj; it reads
; the value below) and linked by its msil-ld.
; RUN: %{msil_ll}
; prog-value: 9

define i32 @main() {
  ret i32 9
}

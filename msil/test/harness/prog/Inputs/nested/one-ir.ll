; Run in a nested lit by ../../pipelines.test: %{msil_ll}, which stays on
; llc text under msil_pipeline=asm and lto (plan 5.12.1):
; ../../../Inputs/pipeline-cc.py stands in for llc and 'compiles' this file
; into one-ir.il.in, then ilasm -dll; msil-prog.py never runs.
; RUN: %{msil_ll}

define i32 @main() {
  ret i32 9
}

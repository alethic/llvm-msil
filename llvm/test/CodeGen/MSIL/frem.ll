; frem (ImplementationPlan.md M5.a, plan 5.4 'FP' REM_{F32,F64}; recon
; isel.md 3.2 and 5.6): FREM is Legal and selects the CIL 'rem' opcode, whose
; floating-point result is C fmod's (verified on every runtime, isel.md 3.2:
; the result has the dividend's sign, rem(-0.0, 1) is -0.0, rem(x, inf) is x,
; rem(inf, y) and rem(x, 0) are NaN). So frem is never a call to fmod or fmodf,
; and float32 operands are not widened to float64 first. The divisor and the
; dividend keep their IR order. The results are checked by M5.d's frem.ll.
;
; A single frem of arguments and at most one constant compiles to the same CIL
; at -O0 and -O2 (CHECK); the rest is checked exactly at -O2 (O2) and for its
; opcodes at -O0 (O0). The MIR lines check the record names and the operand
; order after instruction selection.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=conv \
; DEFINE:   --implicit-check-not=call --implicit-check-not=fmod --implicit-check-not=localloc \
; DEFINE:   --implicit-check-not="{{^ b}}"
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=MIR
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "frem.c"

; CHECK-LABEL: .method {{.*}}'frem_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  rem{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'frem_f32'{{$}}
; MIR-LABEL:   name: frem_f32{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:     [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR:         {{%[0-9]+}}:f32 = {{([a-z]+ )*}}REM_F32 [[A]], [[B]],
define float @frem_f32(float %a, float %b) {
  %r = frem float %a, %b
  ret float %r
}

; CHECK-LABEL: .method {{.*}}'frem_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  rem{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'frem_f64'{{$}}
; MIR-LABEL:   name: frem_f64{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:     [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR:         {{%[0-9]+}}:f64 = {{([a-z]+ )*}}REM_F64 [[A]], [[B]],
define double @frem_f64(double %a, double %b) {
  %r = frem double %a, %b
  ret double %r
}

; The operands are not swapped: the divisor is the second operand.
; CHECK-LABEL: .method {{.*}}'frem_f64_swapped'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  rem{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'frem_f64_swapped'{{$}}
define double @frem_f64_swapped(double %a, double %b) {
  %r = frem double %b, %a
  ret double %r
}

; A constant divisor: 3.0.
; CHECK-LABEL: .method {{.*}}'frem_f64_by_three'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r8 float64(0x4008000000000000){{$}}
; CHECK-NEXT:  {{^}}  rem{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'frem_f64_by_three'{{$}}
define double @frem_f64_by_three(double %x) {
  %r = frem double %x, 3.0
  ret double %r
}

; A power-of-two divisor stays a rem as well (no fmul/ftrunc rewrite: rem is
; legal).
; CHECK-LABEL: .method {{.*}}'frem_f32_by_two'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r4 float32(0x40000000){{$}}
; CHECK-NEXT:  {{^}}  rem{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'frem_f32_by_two'{{$}}
define float @frem_f32_by_two(float %x) {
  %r = frem float %x, 2.0
  ret float %r
}

; A constant dividend: 1e10 is exact in float32 (0x501502F9).
; CHECK-LABEL: .method {{.*}}'frem_f32_const_dividend'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldc.r4 float32(0x501502F9){{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  rem{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'frem_f32_const_dividend'{{$}}
define float @frem_f32_const_dividend(float %y) {
  %r = frem float 1.0e10, %y
  ret float %r
}

; -0.0 as the dividend keeps its sign bit.
; CHECK-LABEL: .method {{.*}}'frem_f64_neg_zero_dividend'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldc.r8 float64(0x8000000000000000){{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  rem{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'frem_f64_neg_zero_dividend'{{$}}
define double @frem_f64_neg_zero_dividend(double %y) {
  %r = frem double -0.0, %y
  ret double %r
}

; An infinite divisor (the result is the dividend for finite dividends).
; CHECK-LABEL: .method {{.*}}'frem_f64_by_inf'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r8 float64(0x7FF0000000000000){{$}}
; CHECK-NEXT:  {{^}}  rem{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'frem_f64_by_inf'{{$}}
define double @frem_f64_by_inf(double %x) {
  %r = frem double %x, 0x7FF0000000000000
  ret double %r
}

; frem in a float32 expression: no widening, no rounding steps.
; CHECK-LABEL: .method {{.*}}'frem_of_sum_f32'(
; CHECK-SAME:  float32, float32, float32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  rem{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  add{{$}}
; O0:          {{^}}  rem{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'frem_of_sum_f32'{{$}}
define float @frem_of_sum_f32(float %a, float %b, float %c) {
  %s = fadd float %a, %b
  %r = frem float %s, %c
  ret float %r
}

; A remainder used as a divisor of another remainder: .maxstack 3.
; CHECK-LABEL: .method {{.*}}'frem_nested_f64'(
; CHECK-SAME:  float64, float64, float64) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  rem{{$}}
; O2-NEXT:     {{^}}  rem{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  rem{{$}}
; O0:          {{^}}  rem{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'frem_nested_f64'{{$}}
define double @frem_nested_f64(double %a, double %b, double %c) {
  %d = frem double %b, %c
  %r = frem double %a, %d
  ret double %r
}

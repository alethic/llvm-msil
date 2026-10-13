; .maxstack is exact (ImplementationPlan.md D20, M3.b/M3.d; recon passes-frame.md
; 8): a value that is too small makes the JIT throw InvalidProgramException,
; and ilasm would default the directive to 8 if it were missing, so every method
; states the peak evaluation-stack depth of its body, computed by MSILStackCheck
; at every optimization level. The peak counts the extra slot of instructions
; that print as several opcodes (StackPeakExtra): ldnull; throw needs one,
; ldc + conv.i needs none. A method without stack traffic states .maxstack 0.
; Deeper stacks need calls (M4); the tee case (dup; stloc) is in
; reg-stackify-basic.mir. Every method has exactly one .maxstack, placed right
; after its opening brace.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=.maxstack
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O3 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil < %s -o %t.il \
; RUN:   && %ilasm-verify -output=%t.dll %t.il %}

source_filename = "maxstack.c"

; CHECK-LABEL: .method {{.*}}'empty_void'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
define void @empty_void() {
  ret void
}

; CHECK-LABEL: .method {{.*}}'unused_args'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
define void @unused_args(i32 %a, ptr %b, double %c) {
  ret void
}

; CHECK-LABEL: .method {{.*}}'const_i32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
define i32 @const_i32() {
  ret i32 1000
}

; CHECK-LABEL: .method {{.*}}'const_i64_small'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
define i64 @const_i64_small() {
  ret i64 1000
}

; CHECK-LABEL: .method {{.*}}'const_i64_large'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
define i64 @const_i64_large() {
  ret i64 -9223372036854775808
}

; CHECK-LABEL: .method {{.*}}'const_ptr'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
define ptr @const_ptr() {
  ret ptr null
}

; CHECK-LABEL: .method {{.*}}'const_f32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
define float @const_f32() {
  ret float 2.5
}

; CHECK-LABEL: .method {{.*}}'const_f64'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
define double @const_f64() {
  ret double 2.5
}

; CHECK-LABEL: .method {{.*}}'arg_i32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
define i32 @arg_i32(i32 %a, i32 %b) {
  ret i32 %b
}

; CHECK-LABEL: .method {{.*}}'arg_f64'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
define double @arg_f64(ptr %p, double %d) {
  ret double %d
}

; ldnull; throw: one slot (the null) beyond the empty stack.
; CHECK-LABEL: .method {{.*}}'trap_void'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
define void @trap_void() {
  unreachable
}

; CHECK-LABEL: .method {{.*}}'trap_i64'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
define i64 @trap_i64(i64 %x) {
  unreachable
}

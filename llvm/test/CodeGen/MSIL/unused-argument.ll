; Ported from llvm/test/CodeGen/WebAssembly/unused-argument.ll: argument
; indices are correct even if some arguments are unused. The call case of the
; WebAssembly test (a dropped call result) needs calls and is added by M4.d.
; In CIL an unused argument costs nothing: it is never read, popped or copied.

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --implicit-check-not=pop --implicit-check-not=.locals
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --implicit-check-not=pop --implicit-check-not=.locals
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil < %s -o %t.il \
; RUN:   && %ilasm-verify -output=%t.dll %t.il %}

source_filename = "unused-argument.c"

; CHECK-LABEL: .method {{.*}}'unused_first'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'unused_first'{{$}}
define i32 @unused_first(i32 %x, i32 %y) {
  ret i32 %y
}

; CHECK-LABEL: .method {{.*}}'unused_second'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'unused_second'{{$}}
define i32 @unused_second(i32 %x, i32 %y) {
  ret i32 %x
}

; The same with unused arguments of every CIL type before and after the one
; that is used.
; CHECK-LABEL: .method {{.*}}'unused_mixed'(
; CHECK-SAME:  native int, float32, float64, int32, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'unused_mixed'{{$}}
define double @unused_mixed(ptr %p, float %f, double %d, i32 %i, i64 %l) {
  ret double %d
}

; No argument is used at all.
; CHECK-LABEL: .method {{.*}}'unused_all'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'unused_all'{{$}}
define i32 @unused_all(i32 %x, i32 %y) {
  ret i32 0
}

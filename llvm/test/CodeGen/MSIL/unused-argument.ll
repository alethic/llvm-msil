; Ported from llvm/test/CodeGen/WebAssembly/unused-argument.ll: argument
; indices are correct even if some arguments are unused. In CIL an unused
; argument costs nothing: it is never read, popped or copied. The call case of
; the WebAssembly test (M4.d): a call result that is not used is popped right
; after the call (WebAssembly's '$drop'), and an argument that the callee
; ignores is still passed (the call token must match the callee's signature,
; M4.c). The only 'pop's allowed are the ones checked explicitly, and no
; function needs a local. Every function here compiles to the same CIL at -O0
; and -O2.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=pop --implicit-check-not=.locals
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil < %s -o %t.il \
; RUN:   && %ilasm-verify -output=%t.dll %t.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

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

; The WebAssembly call case: the unused result of a call is popped.
; CHECK-LABEL: .method {{.*}}'return_something'(
; CHECK-SAME:  ) cil managed{{$}}
define i32 @return_something() {
  ret i32 7
}

; CHECK-LABEL: .method {{.*}}'call_something'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  call int32 'return_something'(){{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_something'{{$}}
define void @call_something() {
  call i32 @return_something()
  ret void
}

; Arguments that the callee does not use are passed all the same.
; CHECK-LABEL: .method {{.*}}'call_unused_first'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'unused_first'(int32, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_unused_first'{{$}}
define i32 @call_unused_first(i32 %a, i32 %b) {
  %r = call i32 @unused_first(i32 %b, i32 %a)
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'call_unused_mixed'(
; CHECK-SAME:  native int, float32, float64, int32, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 5{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 4{{$}}
; CHECK-NEXT:  {{^}}  call float64 'unused_mixed'(native int, float32, float64, int32, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_unused_mixed'{{$}}
define double @call_unused_mixed(ptr %p, float %f, double %d, i32 %i, i64 %l) {
  %r = call double @unused_mixed(ptr %p, float %f, double %d, i32 %i, i64 %l)
  ret double %r
}

; A caller whose own arguments are unused calls a function whose result is
; unused: no ldarg, only the call and its pop.
; CHECK-LABEL: .method {{.*}}'unused_args_and_result'(
; CHECK-SAME:  int32, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  call int32 'return_something'(){{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'unused_args_and_result'{{$}}
define void @unused_args_and_result(i32 %x, ptr %p) {
  %r = call i32 @return_something()
  ret void
}

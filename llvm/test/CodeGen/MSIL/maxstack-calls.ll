; .maxstack with calls (ImplementationPlan.md D20, M3.b MSILStackCheck, M4.a;
; plan 5.4 stack effect contract: a CALL pops one slot per argument and pushes
; at most one result, with no extra peak; recon isel.md 9): the peak depth of
; a call is its argument count, or 1 for a call with no arguments and a
; result, or 0 for 'call void f()'. Values pushed before the call (earlier
; arguments of an outer call) add to it, and an argument computed by an
; expression peaks above the arguments already pushed. A value that is too
; small makes the JIT throw InvalidProgramException, so the directive is
; exact. Every method has exactly one .maxstack.
;
; Calls of the caller's own parameters give the same .maxstack at -O0 and -O2
; (CHECK); constants and expressions as arguments are stackified only at -O2
; (O2).

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "maxstack-calls.c"

; CHECK-LABEL: .method {{.*}}'callee12'(
; CHECK-SAME:  int32, int32, int32, int32, int32, int32, int32, int32, int32, int32, int32, int32) cil managed{{$}}
define i32 @callee12(i32 %a0, i32 %a1, i32 %a2, i32 %a3, i32 %a4, i32 %a5,
                     i32 %a6, i32 %a7, i32 %a8, i32 %a9, i32 %a10, i32 %a11) {
  ret i32 %a11
}

; CHECK-LABEL: .method {{.*}}'callee12_mixed'(
; CHECK-SAME:  int32, native int, int32, native int, int32, native int, int32, native int, int32, native int, int32, native int) cil managed{{$}}
define i64 @callee12_mixed(i32 %a0, i64 %a1, i32 %a2, i64 %a3, i32 %a4, i64 %a5,
                           i32 %a6, i64 %a7, i32 %a8, i64 %a9, i32 %a10, i64 %a11) {
  ret i64 %a11
}

define i32 @callee2(i32 %a, i32 %b) {
  ret i32 %a
}

define void @void_callee() {
  ret void
}

define i32 @i32_callee() {
  ret i32 3
}

; Twelve arguments: .maxstack 12.
; CHECK-LABEL: .method {{.*}}'call12'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 12{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 4{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 5{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 6{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 7{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 8{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 9{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 10{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 11{{$}}
; CHECK-NEXT:  {{^}}  call int32 'callee12'(int32, int32, int32, int32, int32, int32, int32, int32, int32, int32, int32, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call12'{{$}}
define i32 @call12(i32 %a0, i32 %a1, i32 %a2, i32 %a3, i32 %a4, i32 %a5,
                   i32 %a6, i32 %a7, i32 %a8, i32 %a9, i32 %a10, i32 %a11) {
  %r = call i32 @callee12(i32 %a0, i32 %a1, i32 %a2, i32 %a3, i32 %a4, i32 %a5,
                          i32 %a6, i32 %a7, i32 %a8, i32 %a9, i32 %a10, i32 %a11)
  ret i32 %r
}

; Twelve mixed int32 and native int arguments, passed in reverse order.
; CHECK-LABEL: .method {{.*}}'call12_mixed_reversed'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 12{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 11{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call native int 'callee12_mixed'(int32, native int, int32, native int, int32, native int, int32, native int, int32, native int, int32, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @call12_mixed_reversed(i64 %b11, i32 %b10, i64 %b9, i32 %b8, i64 %b7, i32 %b6,
                                  i64 %b5, i32 %b4, i64 %b3, i32 %b2, i64 %b1, i32 %b0) {
  %r = call i64 @callee12_mixed(i32 %b0, i64 %b1, i32 %b2, i64 %b3, i32 %b4, i64 %b5,
                                i32 %b6, i64 %b7, i32 %b8, i64 %b9, i32 %b10, i64 %b11)
  ret i64 %r
}

; A nested call as the last argument: eleven arguments wait on the stack while
; the inner call's two arguments are pushed, 13 in all.
; CHECK-LABEL: .method {{.*}}'call12_nested_last'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 13{{$}}
; CHECK:       {{^}}  ldarg.s 10{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 11{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 12{{$}}
; CHECK-NEXT:  {{^}}  call int32 'callee2'(int32, int32){{$}}
; CHECK-NEXT:  {{^}}  call int32 'callee12'(int32, int32, int32, int32, int32, int32, int32, int32, int32, int32, int32, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call12_nested_last(i32 %a0, i32 %a1, i32 %a2, i32 %a3, i32 %a4, i32 %a5,
                               i32 %a6, i32 %a7, i32 %a8, i32 %a9, i32 %a10, i32 %a11,
                               i32 %a12) {
  %x = call i32 @callee2(i32 %a11, i32 %a12)
  %r = call i32 @callee12(i32 %a0, i32 %a1, i32 %a2, i32 %a3, i32 %a4, i32 %a5,
                          i32 %a6, i32 %a7, i32 %a8, i32 %a9, i32 %a10, i32 %x)
  ret i32 %r
}

; An expression as the last argument also peaks at 13.
; CHECK-LABEL: .method {{.*}}'call12_expr_last'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 13{{$}}
; O2:          {{^}}  ldarg.s 11{{$}}
; O2-NEXT:     {{^}}  ldarg.s 12{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  call int32 'callee12'(int32, int32, int32, int32, int32, int32, int32, int32, int32, int32, int32, int32){{$}}
; O0:          .maxstack {{1[23]}}{{$}}
; CHECK:       {{^}}} // end of method 'call12_expr_last'{{$}}
define i32 @call12_expr_last(i32 %a0, i32 %a1, i32 %a2, i32 %a3, i32 %a4, i32 %a5,
                             i32 %a6, i32 %a7, i32 %a8, i32 %a9, i32 %a10, i32 %a11,
                             i32 %a12) {
  %x = add i32 %a11, %a12
  %r = call i32 @callee12(i32 %a0, i32 %a1, i32 %a2, i32 %a3, i32 %a4, i32 %a5,
                          i32 %a6, i32 %a7, i32 %a8, i32 %a9, i32 %a10, i32 %x)
  ret i32 %r
}

; Twelve constants of both integer classes (ldc + conv.i has no extra peak).
; CHECK-LABEL: .method {{.*}}'call12_consts'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 12{{$}}
; CHECK:       {{^}}  call native int 'callee12_mixed'(int32, native int, int32, native int, int32, native int, int32, native int, int32, native int, int32, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @call12_consts() {
  %r = call i64 @callee12_mixed(i32 0, i64 1, i32 2, i64 3, i32 4, i64 5,
                                i32 6, i64 7, i32 8, i64 9, i32 10, i64 11)
  ret i64 %r
}

; 'call void f()' neither pops nor pushes.
; CHECK-LABEL: .method {{.*}}'call_void'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
; CHECK-NEXT:  {{^}}  call void 'void_callee'(){{$}}
; CHECK-NEXT:  {{^}}  call void 'void_callee'(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @call_void() {
  call void @void_callee()
  call void @void_callee()
  ret void
}

; A result that is dropped still needs one slot.
; CHECK-LABEL: .method {{.*}}'call_drop'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  call int32 'i32_callee'(){{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @call_drop() {
  %r = call i32 @i32_callee()
  ret void
}

; A result used together with an argument: 2.
; CHECK-LABEL: .method {{.*}}'call_result_plus_arg'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  call int32 'i32_callee'(){{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_result_plus_arg(i32 %a) {
  %r = call i32 @i32_callee()
  %s = add i32 %r, %a
  ret i32 %s
}

; Calls in sequence do not add up: the deepest one counts.
; CHECK-LABEL: .method {{.*}}'calls_in_sequence'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 12{{$}}
; CHECK:       {{^}}  call int32 'callee2'(int32, int32){{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK:       {{^}}  call int32 'callee12'(int32, int32, int32, int32, int32, int32, int32, int32, int32, int32, int32, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @calls_in_sequence(i32 %a0, i32 %a1, i32 %a2, i32 %a3, i32 %a4, i32 %a5,
                              i32 %a6, i32 %a7, i32 %a8, i32 %a9, i32 %a10, i32 %a11) {
  %x = call i32 @callee2(i32 %a0, i32 %a1)
  %r = call i32 @callee12(i32 %a0, i32 %a1, i32 %a2, i32 %a3, i32 %a4, i32 %a5,
                          i32 %a6, i32 %a7, i32 %a8, i32 %a9, i32 %a10, i32 %a11)
  ret i32 %r
}

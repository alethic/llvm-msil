; Order of call arguments (ImplementationPlan.md M4.a, plan 5.4 'CALL' operand
; list: defs, callee, then the argument uses; recon isel.md 0 decision 3 and 9:
; the stack order is the MI operand order): CIL pops the arguments of 'call'
; with the last one on top, so the arguments are pushed in IR order, whatever
; order the caller's own parameters, constants or call results come in. Calls
; are never reordered: when a call result is needed after a later call, it
; waits in a local. The two halves of an i128 argument are pushed low half
; first.
;
; Arguments taken from the caller's parameters and call results that are used
; in order compile to the same CIL at -O0 and -O2 (CHECK); constants are exact
; at -O2 (O2).

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "call-args-order.c"

define i32 @sub2(i32 %a, i32 %b) {
  %r = sub i32 %a, %b
  ret i32 %r
}
define i32 @three(i32 %a, i32 %b, i32 %c) {
  ret i32 %c
}
define void @mixed5(double %a, i64 %b, float %c, i32 %d, ptr %e) {
  ret void
}
define i32 @get_a() {
  ret i32 1
}
define i32 @get_b() {
  ret i32 2
}
define i64 @take_i128(i128 %v) {
  %t = trunc i128 %v to i64
  ret i64 %t
}

; Parameters passed in reverse order.
; CHECK-LABEL: .method {{.*}}'swap'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'sub2'(int32, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @swap(i32 %a, i32 %b) {
  %r = call i32 @sub2(i32 %b, i32 %a)
  ret i32 %r
}

; The same parameter twice.
; CHECK-LABEL: .method {{.*}}'twice'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'sub2'(int32, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @twice(i32 %a) {
  %r = call i32 @sub2(i32 %a, i32 %a)
  ret i32 %r
}

; Parameters of every CIL type, permuted.
; CHECK-LABEL: .method {{.*}}'permute5'(
; CHECK-SAME:  native int, int32, float32, native int, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 5{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 4{{$}}
; CHECK-NEXT:  {{^}}  ldarg.3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call void 'mixed5'(float64, native int, float32, int32, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @permute5(ptr %e, i32 %d, float %c, i64 %b, double %a) {
  call void @mixed5(double %a, i64 %b, float %c, i32 %d, ptr %e)
  ret void
}

; A parameter, a call result and a parameter: the inner call runs between the
; pushes of the first and the last argument.
; CHECK-LABEL: .method {{.*}}'nested_middle'(
; CHECK-SAME:  int32, int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  call int32 'sub2'(int32, int32){{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  call int32 'three'(int32, int32, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @nested_middle(i32 %a, i32 %b, i32 %c) {
  %x = call i32 @sub2(i32 %b, i32 %c)
  %r = call i32 @three(i32 %a, i32 %x, i32 %c)
  ret i32 %r
}

; Call results used in the order the calls were made.
; CHECK-LABEL: .method {{.*}}'results_in_order'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  call int32 'get_a'(){{$}}
; CHECK-NEXT:  {{^}}  call int32 'get_b'(){{$}}
; CHECK-NEXT:  {{^}}  call int32 'sub2'(int32, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @results_in_order() {
  %a = call i32 @get_a()
  %b = call i32 @get_b()
  %r = call i32 @sub2(i32 %a, i32 %b)
  ret i32 %r
}

; Call results used in the opposite order: get_b still runs first, and its
; result waits in a local while get_a runs.
; CHECK-LABEL: .method {{.*}}'results_reversed'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  .locals (int32){{$}}
; CHECK-NEXT:  {{^}}  call int32 'get_b'(){{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'get_a'(){{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'sub2'(int32, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @results_reversed() {
  %b = call i32 @get_b()
  %a = call i32 @get_a()
  %r = call i32 @sub2(i32 %a, i32 %b)
  ret i32 %r
}

; Constants and parameters mixed.
; CHECK-LABEL: .method {{.*}}'consts_and_params'(
; CHECK-SAME:  int32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldc.i4.s 10{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4.s -20{{$}}
; O2-NEXT:     {{^}}  call int32 'three'(int32, int32, int32){{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  call int32 'three'(int32, int32, int32){{$}}
; CHECK:       {{^}}} // end of method 'consts_and_params'{{$}}
define i32 @consts_and_params(i32 %x) {
  %r = call i32 @three(i32 10, i32 %x, i32 -20)
  ret i32 %r
}

; Expressions as arguments are evaluated in order.
; CHECK-LABEL: .method {{.*}}'exprs'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  mul{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  xor{{$}}
; O2-NEXT:     {{^}}  call int32 'sub2'(int32, int32){{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0-DAG:      {{^}}  mul{{$}}
; O0-DAG:      {{^}}  xor{{$}}
; O0:          {{^}}  call int32 'sub2'(int32, int32){{$}}
; CHECK:       {{^}}} // end of method 'exprs'{{$}}
define i32 @exprs(i32 %a, i32 %b) {
  %m = mul i32 %a, %b
  %x = xor i32 %a, %b
  %r = call i32 @sub2(i32 %m, i32 %x)
  ret i32 %r
}

; The halves of an i128 built from two native ints: low half first.
; CHECK-LABEL: .method {{.*}}'i128_halves'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  call native int 'take_i128'(native int, native int){{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  call native int 'take_i128'(native int, native int){{$}}
; CHECK:       {{^}}} // end of method 'i128_halves'{{$}}
define i64 @i128_halves(i64 %lo, i64 %hi) {
  %l = zext i64 %lo to i128
  %h = zext i64 %hi to i128
  %hs = shl i128 %h, 64
  %v = or i128 %l, %hs
  %r = call i64 @take_i128(i128 %v)
  ret i64 %r
}

; The same with the halves coming from the parameters in the other order.
; CHECK-LABEL: .method {{.*}}'i128_halves_swapped'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  call native int 'take_i128'(native int, native int){{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  call native int 'take_i128'(native int, native int){{$}}
; CHECK:       {{^}}} // end of method 'i128_halves_swapped'{{$}}
define i64 @i128_halves_swapped(i64 %hi, i64 %lo) {
  %l = zext i64 %lo to i128
  %h = zext i64 %hi to i128
  %hs = shl i128 %h, 64
  %v = or i128 %l, %hs
  %r = call i64 @take_i128(i128 %v)
  ret i64 %r
}

; An i128 constant: low half 1, high half 2.
; CHECK-LABEL: .method {{.*}}'i128_const'(
; CHECK-SAME:  ) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.1|ldc\.i4 1}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.2|ldc\.i4 2}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  call native int 'take_i128'(native int, native int){{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  call native int 'take_i128'(native int, native int){{$}}
; CHECK:       {{^}}} // end of method 'i128_const'{{$}}
define i64 @i128_const() {
  %r = call i64 @take_i128(i128 36893488147419103233)
  ret i64 %r
}

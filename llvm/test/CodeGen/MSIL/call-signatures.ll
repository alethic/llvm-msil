; Call signatures (ImplementationPlan.md M4.a/M4.c, decision D2; plan 5.1 and
; 5.3 MSIL::Signature; recon isel.md 4 and 8.2, asmprinter.md 5.3): the
; '.method' header of a definition and the member reference of every call to
; it come from the same computeSignatureVTs mapping, so their text is equal.
; i1, i8 and i16 parameters and results are int32 (with or without zeroext or
; signext), i32 is int32, i64 and pointers are native int (never int64), float
; and double are float32 and float64, a void result is void, and an i128 is
; two native int parameters (low half first). Each callee's header is captured
; here and every call to it must print the same text after 'call'.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=int8 \
; DEFINE:   --implicit-check-not=int16 --implicit-check-not=bool
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "call-signatures.c"

; ---- callees -----------------------------------------------------------------

; CHECK: {{^}}.method public static [[SIG_VOID:void 'sig_void'\(\)]] cil managed{{$}}
define void @sig_void() {
  ret void
}

; CHECK: {{^}}.method public static [[SIG_I1:int32 'sig_i1'\(int32\)]] cil managed{{$}}
define zeroext i1 @sig_i1(i1 zeroext %a) {
  ret i1 %a
}

; CHECK: {{^}}.method public static [[SIG_I1_PLAIN:int32 'sig_i1_plain'\(int32, int32\)]] cil managed{{$}}
define i1 @sig_i1_plain(i1 %a, i1 signext %b) {
  ret i1 %b
}

; CHECK: {{^}}.method public static [[SIG_I8:int32 'sig_i8'\(int32, int32\)]] cil managed{{$}}
define signext i8 @sig_i8(i8 signext %a, i8 %b) {
  ret i8 %a
}

; CHECK: {{^}}.method public static [[SIG_I16:int32 'sig_i16'\(int32, int32\)]] cil managed{{$}}
define zeroext i16 @sig_i16(i16 zeroext %a, i16 signext %b) {
  ret i16 %a
}

; CHECK: {{^}}.method public static [[SIG_I32:int32 'sig_i32'\(int32\)]] cil managed{{$}}
define i32 @sig_i32(i32 %a) {
  ret i32 %a
}

; CHECK: {{^}}.method public static [[SIG_I64:native int 'sig_i64'\(native int\)]] cil managed{{$}}
define i64 @sig_i64(i64 %a) {
  ret i64 %a
}

; CHECK: {{^}}.method public static [[SIG_PTR:native int 'sig_ptr'\(native int, native int\)]] cil managed{{$}}
define ptr @sig_ptr(ptr %a, ptr %b) {
  ret ptr %b
}

; CHECK: {{^}}.method public static [[SIG_F32:float32 'sig_f32'\(float32\)]] cil managed{{$}}
define float @sig_f32(float %a) {
  ret float %a
}

; CHECK: {{^}}.method public static [[SIG_F64:float64 'sig_f64'\(float64\)]] cil managed{{$}}
define double @sig_f64(double %a) {
  ret double %a
}

; CHECK: {{^}}.method public static [[SIG_I128:void 'sig_i128'\(native int, native int\)]] cil managed{{$}}
define void @sig_i128(i128 %a) {
  ret void
}

; CHECK: {{^}}.method public static [[SIG_MIXED:float64 'sig_mixed'\(int32, int32, int32, native int, native int, float32, float64, int32\)]] cil managed{{$}}
define double @sig_mixed(i8 %a, i16 %b, i32 %c, i64 %d, ptr %e, float %f, double %g, i1 %h) {
  ret double %g
}

; CHECK: {{^}}.method assembly static [[SIG_INTERNAL:float32 'sig_internal'\(native int, int32\)]] cil managed{{$}}
define internal float @sig_internal(ptr %p, i8 zeroext %c) {
  ret float 0.0
}

; CHECK: {{^}}.method assembly static [[SIG_PRIVATE:int32 'sig_private'\(\)]] cil managed{{$}}
define private i16 @sig_private() {
  ret i16 7
}

; ---- callers -----------------------------------------------------------------

; CHECK-LABEL: .method {{.*}}'call_sig_void'(
; CHECK:       {{^}}  call [[SIG_VOID]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @call_sig_void() {
  call void @sig_void()
  ret void
}

; CHECK-LABEL: .method {{.*}}'call_sig_i1'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK:       {{^}}  call [[SIG_I1]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define zeroext i1 @call_sig_i1(i1 zeroext %a) {
  %r = call zeroext i1 @sig_i1(i1 zeroext %a)
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'call_sig_i1_plain'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK:       {{^}}  call [[SIG_I1_PLAIN]]{{$}}
; CHECK:       {{^}}  ret{{$}}
define i1 @call_sig_i1_plain(i1 %a, i1 signext %b) {
  %r = call i1 @sig_i1_plain(i1 %a, i1 signext %b)
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'call_sig_i8'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK:       {{^}}  call [[SIG_I8]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define signext i8 @call_sig_i8(i8 signext %a, i8 %b) {
  %r = call signext i8 @sig_i8(i8 signext %a, i8 %b)
  ret i8 %r
}

; CHECK-LABEL: .method {{.*}}'call_sig_i16'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK:       {{^}}  call [[SIG_I16]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define zeroext i16 @call_sig_i16(i16 zeroext %a, i16 signext %b) {
  %r = call zeroext i16 @sig_i16(i16 zeroext %a, i16 signext %b)
  ret i16 %r
}

; CHECK-LABEL: .method {{.*}}'call_sig_i32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call [[SIG_I32]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_sig_i32(i32 %a) {
  %r = call i32 @sig_i32(i32 %a)
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'call_sig_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call [[SIG_I64]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @call_sig_i64(i64 %a) {
  %r = call i64 @sig_i64(i64 %a)
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'call_sig_ptr'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call [[SIG_PTR]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @call_sig_ptr(ptr %a, ptr %b) {
  %r = call ptr @sig_ptr(ptr %a, ptr %b)
  ret ptr %r
}

; CHECK-LABEL: .method {{.*}}'call_sig_f32'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call [[SIG_F32]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define float @call_sig_f32(float %a) {
  %r = call float @sig_f32(float %a)
  ret float %r
}

; CHECK-LABEL: .method {{.*}}'call_sig_f64'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call [[SIG_F64]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @call_sig_f64(double %a) {
  %r = call double @sig_f64(double %a)
  ret double %r
}

; The two halves of an i128 are two arguments, low half first.
; CHECK-LABEL: .method {{.*}}'call_sig_i128'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call [[SIG_I128]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @call_sig_i128(i128 %a) {
  call void @sig_i128(i128 %a)
  ret void
}

; CHECK-LABEL: .method {{.*}}'call_sig_mixed'(
; CHECK-SAME:  int32, int32, int32, native int, native int, float32, float64, int32) cil managed{{$}}
; CHECK:       {{^}}  call [[SIG_MIXED]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @call_sig_mixed(i8 %a, i16 %b, i32 %c, i64 %d, ptr %e, float %f, double %g, i1 %h) {
  %r = call double @sig_mixed(i8 %a, i16 %b, i32 %c, i64 %d, ptr %e, float %f, double %g, i1 %h)
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'call_sig_internal'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call [[SIG_INTERNAL]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define float @call_sig_internal(ptr %p, i8 zeroext %c) {
  %r = call float @sig_internal(ptr %p, i8 zeroext %c)
  ret float %r
}

; CHECK-LABEL: .method {{.*}}'call_sig_private'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  call [[SIG_PRIVATE]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i16 @call_sig_private() {
  %r = call i16 @sig_private()
  ret i16 %r
}

; One callee called from two places with different argument sources: the
; token is the same.
; CHECK-LABEL: .method {{.*}}'call_sig_mixed_twice'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK:       {{^}}  call [[SIG_MIXED]]{{$}}
; CHECK:       {{^}}  call [[SIG_MIXED]]{{$}}
; CHECK:       {{^}}  ret{{$}}
define double @call_sig_mixed_twice(ptr %p) {
  %x = call double @sig_mixed(i8 1, i16 2, i32 3, i64 4, ptr %p, float 5.0, double 6.0, i1 true)
  %y = call double @sig_mixed(i8 -1, i16 -2, i32 -3, i64 -4, ptr null, float -5.0, double %x, i1 false)
  ret double %y
}

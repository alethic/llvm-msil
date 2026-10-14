; Integer width conversions (ImplementationPlan.md M4.b; plan 5.4
; 'Conversions' CONV_I_I32, CONV_U_I32, CONV_I4_I64, CONV_I1_I32, CONV_I2_I32,
; CONV_U1_I32, CONV_U2_I32; recon isel.md 3.1, 5.1 and 5.4): trunc from native
; int to int32 is conv.i4, sext from int32 to native int is conv.i, zext from
; int32 is conv.u. Pointers and i64 are both native int (decision D2), so
; ptrtoint and inttoptr at 64 bits print nothing, and at 32 bits they are a
; trunc (conv.i4) or a zext (conv.u). An int32 sign-extended in register from
; 8 or 16 bits is conv.i1 or conv.i2; a native int sign-extended in register
; from 32 bits is conv.i4; conv.i (from 8 or 16 bits: conv.i1 or conv.i2, then
; conv.i). Zero extension in register of an int32 is conv.u1 or conv.u2 (the
; peephole patterns of isel.md 5.1); a native int zero-extended from its low 32
; bits is 'conv.i4; conv.u' (plan section 12, row 'Plan 5.4 records added,
; M4.b'), and an 8-bit value widened to native int is masked with a native int
; 255 ('ldc.i4 255; conv.i; and'). Each of these sequences is checked line by
; line, so a dropped conversion or a wrong mask operation fails. int64 never
; appears: no conv.i8, conv.u8 or ldc.i8 without conv.i. Narrow arguments and
; returns are in narrow-ints.ll.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=conv.i8 \
; DEFINE:   --implicit-check-not=conv.u8 --implicit-check-not=conv.ovf
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "int-conv.c"

; CHECK-LABEL: .method {{.*}}'trunc_i64_i32'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'trunc_i64_i32'{{$}}
define i32 @trunc_i64_i32(i64 %x) {
  %r = trunc i64 %x to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sext_i32_i64'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @sext_i32_i64(i32 %x) {
  %r = sext i32 %x to i64
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'zext_i32_i64'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @zext_i32_i64(i32 %x) {
  %r = zext i32 %x to i64
  ret i64 %r
}

; ---- pointers ----------------------------------------------------------------

; CHECK-LABEL: .method {{.*}}'ptrtoint_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @ptrtoint_i64(ptr %p) {
  %r = ptrtoint ptr %p to i64
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'inttoptr_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @inttoptr_i64(i64 %x) {
  %r = inttoptr i64 %x to ptr
  ret ptr %r
}

; CHECK-LABEL: .method {{.*}}'ptrtoint_i32'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @ptrtoint_i32(ptr %p) {
  %r = ptrtoint ptr %p to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'inttoptr_i32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @inttoptr_i32(i32 %x) {
  %r = inttoptr i32 %x to ptr
  ret ptr %r
}

; ---- sign extension in register ---------------------------------------------

; CHECK-LABEL: .method {{.*}}'sext_inreg_i8_i32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @sext_inreg_i8_i32(i32 %x) {
  %t = trunc i32 %x to i8
  %r = sext i8 %t to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sext_inreg_i16_i32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @sext_inreg_i16_i32(i32 %x) {
  %t = trunc i32 %x to i16
  %r = sext i16 %t to i32
  ret i32 %r
}

; From native int to an 8-bit value sign-extended to int32: truncate, then
; sign-extend in register.
; CHECK-LABEL: .method {{.*}}'sext_trunc_i64_i8_i32'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  conv.i1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @sext_trunc_i64_i8_i32(i64 %x) {
  %t = trunc i64 %x to i8
  %r = sext i8 %t to i32
  ret i32 %r
}

; A native int sign-extended from its low 32 bits.
; CHECK-LABEL: .method {{.*}}'sext_inreg_i32_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @sext_inreg_i32_i64(i64 %x) {
  %t = trunc i64 %x to i32
  %r = sext i32 %t to i64
  ret i64 %r
}

; From 8 and 16 bits to native int: conv.i1 or conv.i2, later conv.i.
; CHECK-LABEL: .method {{.*}}'sext_inreg_i8_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK:       {{^}}  conv.i1{{$}}
; CHECK:       {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_inreg_i8_i64'{{$}}
define i64 @sext_inreg_i8_i64(i64 %x) {
  %t = trunc i64 %x to i8
  %r = sext i8 %t to i64
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'sext_inreg_i16_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK:       {{^}}  conv.i2{{$}}
; CHECK:       {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_inreg_i16_i64'{{$}}
define i64 @sext_inreg_i16_i64(i64 %x) {
  %t = trunc i64 %x to i16
  %r = sext i16 %t to i64
  ret i64 %r
}

; An int32 that is an 8-bit value sign-extended to native int.
; CHECK-LABEL: .method {{.*}}'sext_i8_in_i32_to_i64'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK:       {{^}}  conv.i1{{$}}
; CHECK:       {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_i8_in_i32_to_i64'{{$}}
define i64 @sext_i8_in_i32_to_i64(i32 %x) {
  %t = trunc i32 %x to i8
  %r = sext i8 %t to i64
  ret i64 %r
}

; ---- zero extension in register ---------------------------------------------

; CHECK-LABEL: .method {{.*}}'zext_inreg_i8_i32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'zext_inreg_i8_i32'{{$}}
define i32 @zext_inreg_i8_i32(i32 %x) {
  %t = trunc i32 %x to i8
  %r = zext i8 %t to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'zext_inreg_i16_i32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'zext_inreg_i16_i32'{{$}}
define i32 @zext_inreg_i16_i32(i32 %x) {
  %t = trunc i32 %x to i16
  %r = zext i16 %t to i32
  ret i32 %r
}

; trunc to i1 then zext: 'ldc.i4.1; and'.
; CHECK-LABEL: .method {{.*}}'zext_inreg_i1_i32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @zext_inreg_i1_i32(i32 %x) {
  %t = trunc i32 %x to i1
  %r = zext i1 %t to i32
  ret i32 %r
}

; A native int zero-extended from its low 32 bits: conv.u of the truncated
; value (row 'Plan 5.4 records added, M4.b'). Without the conv.i4, conv.u
; would keep the upper 32 bits.
; CHECK-LABEL: .method {{.*}}'zext_inreg_i32_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'zext_inreg_i32_i64'{{$}}
define i64 @zext_inreg_i32_i64(i64 %x) {
  %t = trunc i64 %x to i32
  %r = zext i32 %t to i64
  ret i64 %r
}

; An 8-bit value zero-extended to native int: the int32 is widened with
; conv.u, then masked with a native int 255.
; CHECK-LABEL: .method {{.*}}'zext_i8_in_i32_to_i64'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 255{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'zext_i8_in_i32_to_i64'{{$}}
define i64 @zext_i8_in_i32_to_i64(i32 %x) {
  %t = trunc i32 %x to i8
  %r = zext i8 %t to i64
  ret i64 %r
}

; ---- chains ----------------------------------------------------------------

; Truncating twice is one conv.i4 (the second trunc is free in an int32).
; CHECK-LABEL: .method {{.*}}'trunc_i64_i16_anyext'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i16 @trunc_i64_i16_anyext(i64 %x) {
  %r = trunc i64 %x to i16
  ret i16 %r
}

; sext then trunc back is the identity.
; CHECK-LABEL: .method {{.*}}'sext_trunc_identity'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @sext_trunc_identity(i32 %x) {
  %e = sext i32 %x to i64
  %r = trunc i64 %e to i32
  ret i32 %r
}

; A pointer rebuilt from an int32 offset: sext, then native int add.
; CHECK-LABEL: .method {{.*}}'ptr_plus_sext'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @ptr_plus_sext(ptr %p, i32 %off) {
  %i = ptrtoint ptr %p to i64
  %o = sext i32 %off to i64
  %s = add i64 %i, %o
  %r = inttoptr i64 %s to ptr
  ret ptr %r
}

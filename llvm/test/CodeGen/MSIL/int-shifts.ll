; Integer shifts (ImplementationPlan.md M4.b; plan 5.4 'Integer'; recon isel.md
; 3.1, 5.1 and the getScalarShiftAmountTy row of 6): shl, ashr and lshr are CIL
; shl, shr and shr.un, and the shift amount is always an int32, for int32 and
; native int values alike (getScalarShiftAmountTy is i32). A native int amount
; is truncated with conv.i4 first, a constant amount is an int32 constant, and
; an amount that is a zero-extended int32 is used as it is. CIL leaves shifts
; by the operand width or more unspecified (they mask like the hardware) and
; LLVM makes them poison, so the explicit masks that C front ends emit
; ('x << (n & 31)') are kept: unlike WebAssembly, no pattern strips them.
;
; A shift of arguments by an argument or one constant compiles to the same CIL
; at -O0 and -O2 (CHECK); masked shifts are exact at -O2 (O2).

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

source_filename = "int-shifts.c"

; ---- int32 ---------------------------------------------------------------

; CHECK-LABEL: .method {{.*}}'shl_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  shl{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'shl_i32'{{$}}
define i32 @shl_i32(i32 %x, i32 %n) {
  %r = shl i32 %x, %n
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'ashr_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  shr{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @ashr_i32(i32 %x, i32 %n) {
  %r = ashr i32 %x, %n
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'lshr_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  shr.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @lshr_i32(i32 %x, i32 %n) {
  %r = lshr i32 %x, %n
  ret i32 %r
}

; The amount is the first operand here.
; CHECK-LABEL: .method {{.*}}'shl_rev_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  shl{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @shl_rev_i32(i32 %n, i32 %x) {
  %r = shl i32 %x, %n
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'shl_imm_i32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.5{{$}}
; CHECK-NEXT:  {{^}}  shl{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @shl_imm_i32(i32 %x) {
  %r = shl i32 %x, 5
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'ashr_imm_i32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 31{{$}}
; CHECK-NEXT:  {{^}}  shr{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @ashr_imm_i32(i32 %x) {
  %r = ashr i32 %x, 31
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'lshr_imm_i32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 12{{$}}
; CHECK-NEXT:  {{^}}  shr.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @lshr_imm_i32(i32 %x) {
  %r = lshr i32 %x, 12
  ret i32 %r
}

; A constant shifted by a variable amount.
; CHECK-LABEL: .method {{.*}}'shl_one_i32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  shl{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @shl_one_i32(i32 %n) {
  %r = shl i32 1, %n
  ret i32 %r
}

; ---- native int: the amount is an int32 -----------------------------------

; CHECK-LABEL: .method {{.*}}'shl_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  shl{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @shl_i64(i64 %x, i64 %n) {
  %r = shl i64 %x, %n
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'ashr_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  shr{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @ashr_i64(i64 %x, i64 %n) {
  %r = ashr i64 %x, %n
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'lshr_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  shr.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @lshr_i64(i64 %x, i64 %n) {
  %r = lshr i64 %x, %n
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'shl_imm_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 40{{$}}
; CHECK-NEXT:  {{^}}  shl{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @shl_imm_i64(i64 %x) {
  %r = shl i64 %x, 40
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'ashr_imm_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 63{{$}}
; CHECK-NEXT:  {{^}}  shr{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @ashr_imm_i64(i64 %x) {
  %r = ashr i64 %x, 63
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'lshr_imm_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 32{{$}}
; CHECK-NEXT:  {{^}}  shr.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @lshr_imm_i64(i64 %x) {
  %r = lshr i64 %x, 32
  ret i64 %r
}

; A zero-extended int32 amount needs no conversion at all.
; CHECK-LABEL: .method {{.*}}'shl_i64_by_i32'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  shl{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @shl_i64_by_i32(i64 %x, i32 %n) {
  %a = zext i32 %n to i64
  %r = shl i64 %x, %a
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'lshr_i64_by_i32'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  shr.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @lshr_i64_by_i32(i64 %x, i32 %n) {
  %a = zext i32 %n to i64
  %r = lshr i64 %x, %a
  ret i64 %r
}

; ---- masked amounts are kept --------------------------------------------

; CHECK-LABEL: .method {{.*}}'shl_masked_i32'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldc.i4.s 31{{$}}
; O2-NEXT:     {{^}}  and{{$}}
; O2-NEXT:     {{^}}  shl{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldc.i4.s 31{{$}}
; O0:          {{^}}  and{{$}}
; O0:          {{^}}  shl{{$}}
; CHECK:       {{^}}} // end of method 'shl_masked_i32'{{$}}
define i32 @shl_masked_i32(i32 %x, i32 %n) {
  %m = and i32 %n, 31
  %r = shl i32 %x, %m
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'ashr_masked_i32'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldc.i4.s 31{{$}}
; O2-NEXT:     {{^}}  and{{$}}
; O2-NEXT:     {{^}}  shr{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldc.i4.s 31{{$}}
; O0:          {{^}}  and{{$}}
; O0:          {{^}}  shr{{$}}
; CHECK:       {{^}}} // end of method 'ashr_masked_i32'{{$}}
define i32 @ashr_masked_i32(i32 %x, i32 %n) {
  %m = and i32 %n, 31
  %r = ashr i32 %x, %m
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'lshr_masked_i32'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldc.i4.s 31{{$}}
; O2-NEXT:     {{^}}  and{{$}}
; O2-NEXT:     {{^}}  shr.un{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldc.i4.s 31{{$}}
; O0:          {{^}}  and{{$}}
; O0:          {{^}}  shr.un{{$}}
; CHECK:       {{^}}} // end of method 'lshr_masked_i32'{{$}}
define i32 @lshr_masked_i32(i32 %x, i32 %n) {
  %m = and i32 %n, 31
  %r = lshr i32 %x, %m
  ret i32 %r
}

; The native int amount is truncated, then masked as an int32.
; CHECK-LABEL: .method {{.*}}'shl_masked_i64'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  conv.i4{{$}}
; O2-NEXT:     {{^}}  ldc.i4.s 63{{$}}
; O2-NEXT:     {{^}}  and{{$}}
; O2-NEXT:     {{^}}  shl{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  and{{$}}
; O0:          {{^}}  shl{{$}}
; CHECK:       {{^}}} // end of method 'shl_masked_i64'{{$}}
define i64 @shl_masked_i64(i64 %x, i64 %n) {
  %m = and i64 %n, 63
  %r = shl i64 %x, %m
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'ashr_masked_i64'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  conv.i4{{$}}
; O2-NEXT:     {{^}}  ldc.i4.s 63{{$}}
; O2-NEXT:     {{^}}  and{{$}}
; O2-NEXT:     {{^}}  shr{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  and{{$}}
; O0:          {{^}}  shr{{$}}
; CHECK:       {{^}}} // end of method 'ashr_masked_i64'{{$}}
define i64 @ashr_masked_i64(i64 %x, i64 %n) {
  %m = and i64 %n, 63
  %r = ashr i64 %x, %m
  ret i64 %r
}

; An int32 amount masked to 63 for a native int shift.
; CHECK-LABEL: .method {{.*}}'lshr_masked_i64_by_i32'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldc.i4.s 63{{$}}
; O2-NEXT:     {{^}}  and{{$}}
; O2-NEXT:     {{^}}  shr.un{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  and{{$}}
; O0:          {{^}}  shr.un{{$}}
; CHECK:       {{^}}} // end of method 'lshr_masked_i64_by_i32'{{$}}
define i64 @lshr_masked_i64_by_i32(i64 %x, i32 %n) {
  %m = and i32 %n, 63
  %a = zext i32 %m to i64
  %r = lshr i64 %x, %a
  ret i64 %r
}

; A mask narrower than the width is ordinary arithmetic and stays as well.
; CHECK-LABEL: .method {{.*}}'shl_mask15_i32'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldc.i4.s 15{{$}}
; O2-NEXT:     {{^}}  and{{$}}
; O2-NEXT:     {{^}}  shl{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldc.i4.s 15{{$}}
; O0:          {{^}}  and{{$}}
; O0:          {{^}}  shl{{$}}
; CHECK:       {{^}}} // end of method 'shl_mask15_i32'{{$}}
define i32 @shl_mask15_i32(i32 %x, i32 %n) {
  %m = and i32 %n, 15
  %r = shl i32 %x, %m
  ret i32 %r
}

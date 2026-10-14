; Division and remainder (ImplementationPlan.md M4.b; plan 5.4 'Integer'; recon
; isel.md 3.1, 5.1 and the isIntDivCheap row of 6): sdiv, udiv, srem and urem
; are CIL div, div.un, rem and rem.un on int32 and native int. They throw on a
; zero divisor and on INT_MIN / -1, which is undefined behaviour in LLVM, so
; the records have side effects (nothing speculates them) but are marked
; TrapsOnlyOnUB for MSILRegStackify (reg-stackify.ll). isIntDivCheap is true up
; to 64 bits (RyuJIT does the magic-number division itself), so division by a
; constant, including a signed division by a power of two, stays one div or rem
; with no multiply-high sequence; only the unsigned power-of-two cases become a
; shift or a mask, as on every target. i128 division and remainder by a power
; of two, and exact i128 division by any constant, expand inline (i128.ll);
; every other i128 division or remainder needs a runtime helper and is an error
; (call-errors.ll).
;
; Every function is one operation on arguments and at most one constant, so
; the CIL is the same at -O0 and -O2.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=mul \
; DEFINE:   --implicit-check-not=call
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "div-rem.c"

; ---- int32 ---------------------------------------------------------------

; CHECK-LABEL: .method {{.*}}'sdiv_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  div{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sdiv_i32'{{$}}
define i32 @sdiv_i32(i32 %x, i32 %y) {
  %r = sdiv i32 %x, %y
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'udiv_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  div.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @udiv_i32(i32 %x, i32 %y) {
  %r = udiv i32 %x, %y
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'srem_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  rem{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @srem_i32(i32 %x, i32 %y) {
  %r = srem i32 %x, %y
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'urem_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  rem.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @urem_i32(i32 %x, i32 %y) {
  %r = urem i32 %x, %y
  ret i32 %r
}

; The divisor is the second operand, whatever the argument order.
; CHECK-LABEL: .method {{.*}}'sdiv_rev_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  div{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @sdiv_rev_i32(i32 %y, i32 %x) {
  %r = sdiv i32 %x, %y
  ret i32 %r
}

; Constant divisors: one div or rem, no multiply.
; CHECK-LABEL: .method {{.*}}'sdiv_7_i32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.7{{$}}
; CHECK-NEXT:  {{^}}  div{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @sdiv_7_i32(i32 %x) {
  %r = sdiv i32 %x, 7
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'udiv_7_i32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.7{{$}}
; CHECK-NEXT:  {{^}}  div.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @udiv_7_i32(i32 %x) {
  %r = udiv i32 %x, 7
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'srem_7_i32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.7{{$}}
; CHECK-NEXT:  {{^}}  rem{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @srem_7_i32(i32 %x) {
  %r = srem i32 %x, 7
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'urem_7_i32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.7{{$}}
; CHECK-NEXT:  {{^}}  rem.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @urem_7_i32(i32 %x) {
  %r = urem i32 %x, 7
  ret i32 %r
}

; Signed division and remainder by a power of two stay div and rem (no
; shift-and-adjust sequence), because division is cheap.
; CHECK-LABEL: .method {{.*}}'sdiv_8_i32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.8{{$}}
; CHECK-NEXT:  {{^}}  div{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @sdiv_8_i32(i32 %x) {
  %r = sdiv i32 %x, 8
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'srem_8_i32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.8{{$}}
; CHECK-NEXT:  {{^}}  rem{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @srem_8_i32(i32 %x) {
  %r = srem i32 %x, 8
  ret i32 %r
}

; Unsigned power-of-two division is a logical shift, the remainder a mask.
; CHECK-LABEL: .method {{.*}}'udiv_8_i32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.3{{$}}
; CHECK-NEXT:  {{^}}  shr.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @udiv_8_i32(i32 %x) {
  %r = udiv i32 %x, 8
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'urem_8_i32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.7{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @urem_8_i32(i32 %x) {
  %r = urem i32 %x, 8
  ret i32 %r
}

; A constant dividend.
; CHECK-LABEL: .method {{.*}}'sdiv_from_imm_i32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 1000{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  div{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @sdiv_from_imm_i32(i32 %x) {
  %r = sdiv i32 1000, %x
  ret i32 %r
}

; ---- native int ----------------------------------------------------------

; CHECK-LABEL: .method {{.*}}'sdiv_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  div{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @sdiv_i64(i64 %x, i64 %y) {
  %r = sdiv i64 %x, %y
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'udiv_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  div.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @udiv_i64(i64 %x, i64 %y) {
  %r = udiv i64 %x, %y
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'srem_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  rem{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @srem_i64(i64 %x, i64 %y) {
  %r = srem i64 %x, %y
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'urem_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  rem.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @urem_i64(i64 %x, i64 %y) {
  %r = urem i64 %x, %y
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'sdiv_7_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.7|ldc\.i4 7}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  div{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @sdiv_7_i64(i64 %x) {
  %r = sdiv i64 %x, 7
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'urem_10_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.s 10|ldc\.i4 10}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  rem.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @urem_10_i64(i64 %x) {
  %r = urem i64 %x, 10
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'sdiv_large_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i8 10000000000{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  div{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @sdiv_large_i64(i64 %x) {
  %r = sdiv i64 %x, 10000000000
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'udiv_16_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.4{{$}}
; CHECK-NEXT:  {{^}}  shr.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @udiv_16_i64(i64 %x) {
  %r = udiv i64 %x, 16
  ret i64 %r
}

; A native int divided by a zero-extended int32.
; CHECK-LABEL: .method {{.*}}'udiv_i64_by_u32'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  div.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @udiv_i64_by_u32(i64 %x, i32 %y) {
  %e = zext i32 %y to i64
  %r = udiv i64 %x, %e
  ret i64 %r
}

; Pointer difference divided by the element size: an exact signed division by
; a power of two may become an arithmetic shift.
; CHECK-LABEL: .method {{.*}}'ptrdiff_i32_elems'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK:       {{^}}  sub{{$}}
; CHECK:       {{^}}  {{div|shr}}{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @ptrdiff_i32_elems(ptr %a, ptr %b) {
  %ia = ptrtoint ptr %a to i64
  %ib = ptrtoint ptr %b to i64
  %d = sub i64 %ia, %ib
  %r = sdiv exact i64 %d, 4
  ret i64 %r
}

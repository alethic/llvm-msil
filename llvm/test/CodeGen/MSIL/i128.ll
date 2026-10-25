; i128 arithmetic without runtime helpers (ImplementationPlan.md M4.b, isel
; decision 10; recon isel.md 2.13, 6 and 7.1): i128 is expanded into two
; native int halves (low half first), so an i128 parameter is two native int
; parameters. Addition and subtraction carry through an unsigned compare
; ('clt.un', then 'conv.u'); multiplication expands inline because
; MSILSubtarget::initLibcallLoweringInfo marks MUL_I128 unsupported (no
; __multi3), as are MULO_I64 and MULO_I128 for the overflow intrinsics (no
; __mulodi4 or __muloti4); shifts by a constant become shifts and an 'or' of
; the halves, and shifts by a variable amount are SHL_PARTS/SRA_PARTS/
; SRL_PARTS, custom-lowered through expandShiftParts (no __ashlti3, __lshrti3
; or __ashrti3), which selects between the two halves with brtrue triangles
; (decision D34). Every result is passed to @use128, because an i128 return
; value would be demoted to a hidden pointer, and stores arrive in M7; the
; divisions return one native int half instead. sdiv and srem by a power of
; two expand inline into shifts and adds, as on native x86-64, with the sign
; of the high half ('ldc.i4.s 63; shr') as the rounding bias (no __divti3 or
; __modti3), and udiv and urem by a power of two become a shift and a mask.
; Until M12 the DAGCombiner did this (isIntDivCheap is false above 64 bits,
; plan section 12, row 'Plan 5.3 amended (D33 query hook), M4.b'), an exact
; sdiv or udiv by any constant became a shift and a multiply by the inverse,
; and every other i128 division or remainder needed a helper and was an error.
; Since M12 (Q36; section 12, 'D14/D33 note and P3-B text') MSILTargetLowering
; sets setMaxDivRemBitWidthSupported(64), so ExpandIRInsts rewrites every
; i128 division and remainder in IR before instruction selection: by a power
; of two into the same shifts, adds and masks, and by anything else, an exact
; division by a constant included, into the generic shift-and-subtract
; expansion, with no call and no 'div' or 'rem' (i128-divrem.ll has the
; divisions by a variable).
;
; llc must succeed: any helper call would be a 'runtime library call ... is
; not supported' error (exit 1).

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=__multi3 \
; DEFINE:   --implicit-check-not=__ashlti3 --implicit-check-not=__lshrti3 \
; DEFINE:   --implicit-check-not=__ashrti3 --implicit-check-not=__mulodi4 \
; DEFINE:   --implicit-check-not=__muloti4 --implicit-check-not=__divti3 \
; DEFINE:   --implicit-check-not=__modti3 --implicit-check-not=__udivti3 \
; DEFINE:   --implicit-check-not=__umodti3
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

source_filename = "i128.c"

; CHECK-LABEL: .method {{.*}}'use128'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
define void @use128(i128 %v) {
  ret void
}

; ---- add and sub: carry or borrow through clt.un --------------------------

; The low sum is used twice (argument and carry compare), so it goes through
; dup and a local or argument slot.
; CHECK-LABEL: .method {{.*}}'add128'(
; CHECK-SAME:  native int, native int, native int, native int) cil managed{{$}}
; CHECK-NOT:   {{brtrue|brfalse}}
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{brtrue|brfalse}}
; O2:          {{^}}  clt.un{{$}}
; O2-NEXT:     {{^}}  conv.u{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O0:          {{^}}  clt.un{{$}}
; O0:          {{^}}  conv.u{{$}}
; CHECK-NOT:   {{brtrue|brfalse}}
; CHECK:       {{^}}  call void 'use128'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @add128(i128 %a, i128 %b) {
  %r = add i128 %a, %b
  call void @use128(i128 %r)
  ret void
}

; O2-LABEL:    .method {{.*}}'sub128'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 4{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  sub{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldarg.3{{$}}
; O2-NEXT:     {{^}}  sub{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  clt.un{{$}}
; O2-NEXT:     {{^}}  conv.u{{$}}
; O2-NEXT:     {{^}}  sub{{$}}
; O2-NEXT:     {{^}}  call void 'use128'(native int, native int){{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O2-NEXT:     {{^}}} // end of method 'sub128'{{$}}
; O0-LABEL:    .method {{.*}}'sub128'(
; O0-NOT:      {{brtrue|brfalse}}
; O0:          {{^}}  clt.un{{$}}
; O0:          {{^}}  call void 'use128'(native int, native int){{$}}
define void @sub128(i128 %a, i128 %b) {
  %r = sub i128 %a, %b
  call void @use128(i128 %r)
  ret void
}

; Adding a small constant.
; CHECK-LABEL: .method {{.*}}'inc128'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NOT:   {{brtrue|brfalse}}
; CHECK:       {{^}}  call void 'use128'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @inc128(i128 %a) {
  %r = add i128 %a, 1
  call void @use128(i128 %r)
  ret void
}

; ---- mul: inline, no __multi3 -------------------------------------------------

; CHECK-LABEL: .method {{.*}}'mul128'(
; CHECK-SAME:  native int, native int, native int, native int) cil managed{{$}}
; CHECK:       {{^}}  mul{{$}}
; CHECK:       {{^}}  call void 'use128'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'mul128'{{$}}
define void @mul128(i128 %a, i128 %b) {
  %r = mul i128 %a, %b
  call void @use128(i128 %r)
  ret void
}

; A full 64 x 64 -> 128 bit product.
; CHECK-LABEL: .method {{.*}}'mul64_wide'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK:       {{^}}  mul{{$}}
; CHECK:       {{^}}  call void 'use128'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @mul64_wide(i64 %a, i64 %b) {
  %wa = zext i64 %a to i128
  %wb = zext i64 %b to i128
  %r = mul i128 %wa, %wb
  call void @use128(i128 %r)
  ret void
}

; The overflow intrinsics expand inline too.
; CHECK-LABEL: .method {{.*}}'umulo64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK:       {{^}}  mul{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'umulo64'{{$}}
define i32 @umulo64(i64 %a, i64 %b) {
  %p = call { i64, i1 } @llvm.umul.with.overflow.i64(i64 %a, i64 %b)
  %o = extractvalue { i64, i1 } %p, 1
  %r = zext i1 %o to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'smulo64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK:       {{^}}  mul{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'smulo64'{{$}}
define i32 @smulo64(i64 %a, i64 %b) {
  %p = call { i64, i1 } @llvm.smul.with.overflow.i64(i64 %a, i64 %b)
  %o = extractvalue { i64, i1 } %p, 1
  %r = zext i1 %o to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'smulo128'(
; CHECK-SAME:  native int, native int, native int, native int) cil managed{{$}}
; CHECK:       {{^}}  mul{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'smulo128'{{$}}
define i32 @smulo128(i128 %a, i128 %b) {
  %p = call { i128, i1 } @llvm.smul.with.overflow.i128(i128 %a, i128 %b)
  %o = extractvalue { i128, i1 } %p, 1
  %r = zext i1 %o to i32
  ret i32 %r
}

; ---- shifts by a constant: no branch ------------------------------------------

; lo << 3, (hi << 3) | (lo >> 61).
; O2-LABEL:    .method {{.*}}'shl128_3'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 4{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4.3{{$}}
; O2-NEXT:     {{^}}  shl{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldc.i4.3{{$}}
; O2-NEXT:     {{^}}  shl{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4.s 61{{$}}
; O2-NEXT:     {{^}}  shr.un{{$}}
; O2-NEXT:     {{^}}  or{{$}}
; O2-NEXT:     {{^}}  call void 'use128'(native int, native int){{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O2-NEXT:     {{^}}} // end of method 'shl128_3'{{$}}
; O0-LABEL:    .method {{.*}}'shl128_3'(
; O0-NOT:      {{brtrue|brfalse}}
; O0:          {{^}}  shr.un{{$}}
; O0-NOT:      {{brtrue|brfalse}}
; O0:          {{^}}} // end of method 'shl128_3'{{$}}
define void @shl128_3(i128 %a) {
  %r = shl i128 %a, 3
  call void @use128(i128 %r)
  ret void
}

; By 64 or more only one half is shifted.
; CHECK-LABEL: .method {{.*}}'lshr128_70'(
; CHECK-NOT:   {{brtrue|brfalse}}
; CHECK:       {{^}}  ldc.i4.6{{$}}
; CHECK-NEXT:  {{^}}  shr.un{{$}}
; CHECK-NOT:   {{brtrue|brfalse}}
; CHECK:       {{^}}} // end of method 'lshr128_70'{{$}}
define void @lshr128_70(i128 %a) {
  %r = lshr i128 %a, 70
  call void @use128(i128 %r)
  ret void
}

; CHECK-LABEL: .method {{.*}}'ashr128_100'(
; CHECK-NOT:   {{brtrue|brfalse}}
; CHECK-DAG:   {{^}}  ldc.i4.s 36{{$}}
; CHECK-DAG:   {{^}}  ldc.i4.s 63{{$}}
; CHECK-NOT:   {{brtrue|brfalse}}
; CHECK:       {{^}}} // end of method 'ashr128_100'{{$}}
define void @ashr128_100(i128 %a) {
  %r = ashr i128 %a, 100
  call void @use128(i128 %r)
  ret void
}

; Shifting by exactly 64 moves a half.
; CHECK-LABEL: .method {{.*}}'shl128_64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.0|ldc\.i4 0}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call void 'use128'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @shl128_64(i128 %a) {
  %r = shl i128 %a, 64
  call void @use128(i128 %r)
  ret void
}

; ---- shifts by a variable amount: expandShiftParts with select triangles ----

; CHECK-LABEL: .method {{.*}}'shl128_var'(
; CHECK-SAME:  native int, native int, int32) cil managed{{$}}
; CHECK:       {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK:       {{^}}  call void 'use128'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'shl128_var'{{$}}
define void @shl128_var(i128 %a, i32 %n) {
  %z = zext i32 %n to i128
  %r = shl i128 %a, %z
  call void @use128(i128 %r)
  ret void
}

; CHECK-LABEL: .method {{.*}}'lshr128_var'(
; CHECK-SAME:  native int, native int, int32) cil managed{{$}}
; CHECK:       {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK:       {{^}}  call void 'use128'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'lshr128_var'{{$}}
define void @lshr128_var(i128 %a, i32 %n) {
  %z = zext i32 %n to i128
  %r = lshr i128 %a, %z
  call void @use128(i128 %r)
  ret void
}

; CHECK-LABEL: .method {{.*}}'ashr128_var'(
; CHECK-SAME:  native int, native int, int32) cil managed{{$}}
; CHECK:       {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK:       {{^}}  call void 'use128'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ashr128_var'{{$}}
define void @ashr128_var(i128 %a, i32 %n) {
  %z = zext i32 %n to i128
  %r = ashr i128 %a, %z
  call void @use128(i128 %r)
  ret void
}

; An i128 shift amount: only its low half matters.
; CHECK-LABEL: .method {{.*}}'shl128_var128'(
; CHECK-SAME:  native int, native int, native int, native int) cil managed{{$}}
; CHECK:       {{^}}  conv.i4{{$}}
; CHECK:       {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK:       {{^}}  call void 'use128'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'shl128_var128'{{$}}
define void @shl128_var128(i128 %a, i128 %n) {
  %r = shl i128 %a, %n
  call void @use128(i128 %r)
  ret void
}

; ---- sdiv and srem by a power of two: inline, no __divti3 or __modti3 ------

; The i128 is built from two native int arguments (low half first) and one
; half of the result is returned. q = (a + ((a >> 127) >>u 126)) >> 2: the
; high half's sign ('shr' by 63) gives the bias, and the high half of the
; quotient is an arithmetic shift as well.
; CHECK-LABEL: .method {{.*}}'sdiv128_by_4'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NOT:   call
; CHECK:       {{^}}  ldc.i4.s 63{{$}}
; CHECK-NEXT:  {{^}}  shr{{$}}
; CHECK-NOT:   call
; CHECK:       {{^}}  shr{{$}}
; CHECK-NOT:   call
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sdiv128_by_4'{{$}}
define i64 @sdiv128_by_4(i64 %lo, i64 %hi) {
  %l = zext i64 %lo to i128
  %h = zext i64 %hi to i128
  %hs = shl i128 %h, 64
  %a = or i128 %hs, %l
  %q = sdiv i128 %a, 4
  %qh = lshr i128 %q, 64
  %r = trunc i128 %qh to i64
  ret i64 %r
}

; A negative power of two divides by 4 and negates the quotient.
; CHECK-LABEL: .method {{.*}}'sdiv128_by_minus_4'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NOT:   call
; CHECK:       {{^}}  ldc.i4.s 63{{$}}
; CHECK-NEXT:  {{^}}  shr{{$}}
; CHECK-NOT:   call
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sdiv128_by_minus_4'{{$}}
define i64 @sdiv128_by_minus_4(i64 %lo, i64 %hi) {
  %l = zext i64 %lo to i128
  %h = zext i64 %hi to i128
  %hs = shl i128 %h, 64
  %a = or i128 %hs, %l
  %q = sdiv i128 %a, -4
  %r = trunc i128 %q to i64
  ret i64 %r
}

; r = a - ((a + bias) & -4), with the same bias.
; CHECK-LABEL: .method {{.*}}'srem128_by_4'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NOT:   call
; CHECK:       {{^}}  ldc.i4.s 63{{$}}
; CHECK-NEXT:  {{^}}  shr{{$}}
; CHECK-NOT:   call
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'srem128_by_4'{{$}}
define i64 @srem128_by_4(i64 %lo, i64 %hi) {
  %l = zext i64 %lo to i128
  %h = zext i64 %hi to i128
  %hs = shl i128 %h, 64
  %a = or i128 %hs, %l
  %q = srem i128 %a, 4
  %r = trunc i128 %q to i64
  ret i64 %r
}

; ---- udiv and urem by a power of two, exact division by a constant ---------

; udiv by 16 shifts: the low half of the quotient is (lo >>u 4) | (hi << 60),
; the high half is hi >>u 4; both halves are combined with xor and returned.
; CHECK-LABEL: .method {{.*}}'udiv128_by_16'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NOT:   {{^}}  {{call|div|rem}}
; CHECK:       {{^}}  ldc.i4.s 60{{$}}
; CHECK-NEXT:  {{^}}  shl{{$}}
; CHECK-NOT:   {{^}}  {{call|div|rem}}
; CHECK:       {{^}}  shr.un{{$}}
; CHECK-NOT:   {{^}}  {{call|div|rem}}
; CHECK:       {{^}}  xor{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'udiv128_by_16'{{$}}
define i64 @udiv128_by_16(i64 %lo, i64 %hi) {
  %l = zext i64 %lo to i128
  %h = zext i64 %hi to i128
  %hs = shl i128 %h, 64
  %a = or i128 %hs, %l
  %q = udiv i128 %a, 16
  %qh = lshr i128 %q, 64
  %x = trunc i128 %q to i64
  %y = trunc i128 %qh to i64
  %r = xor i64 %x, %y
  ret i64 %r
}

; urem by 16 masks the low half.
; CHECK-LABEL: .method {{.*}}'urem128_by_16'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.s 15|ldc\.i4 15}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'urem128_by_16'{{$}}
define i64 @urem128_by_16(i64 %lo, i64 %hi) {
  %l = zext i64 %lo to i128
  %h = zext i64 %hi to i128
  %hs = shl i128 %h, 64
  %a = or i128 %hs, %l
  %q = urem i128 %a, 16
  %r = trunc i128 %q to i64
  ret i64 %r
}

; An exact division by 10 shifted right by one and multiplied by the inverse
; of 5 modulo 2^128 until M12 (ldc.i8 -3689348814741910323, the low half of
; 0xCCCC...CCCD). Since M12 ExpandIRInsts expands it like any other i128
; division by a constant that is not a power of two, before the DAGCombiner
; sees it: the generic expansion, which still calls nothing and uses no 'div'
; or 'rem', but no longer multiplies by the inverse.
; CHECK-LABEL: .method {{.*}}'sdiv128_exact_by_10'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NOT:   {{^}}  {{call|div|rem|ldc.i8 -3689348814741910323}}
; CHECK:       {{^}}} // end of method 'sdiv128_exact_by_10'{{$}}
define i64 @sdiv128_exact_by_10(i64 %lo, i64 %hi) {
  %l = zext i64 %lo to i128
  %h = zext i64 %hi to i128
  %hs = shl i128 %h, 64
  %a = or i128 %hs, %l
  %q = sdiv exact i128 %a, 10
  %qh = lshr i128 %q, 64
  %x = trunc i128 %q to i64
  %y = trunc i128 %qh to i64
  %r = xor i64 %x, %y
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'udiv128_exact_by_10'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NOT:   {{^}}  {{call|div|rem|ldc.i8 -3689348814741910323}}
; CHECK:       {{^}}} // end of method 'udiv128_exact_by_10'{{$}}
define i64 @udiv128_exact_by_10(i64 %lo, i64 %hi) {
  %l = zext i64 %lo to i128
  %h = zext i64 %hi to i128
  %hs = shl i128 %h, 64
  %a = or i128 %hs, %l
  %q = udiv exact i128 %a, 10
  %qh = lshr i128 %q, 64
  %x = trunc i128 %q to i64
  %y = trunc i128 %qh to i64
  %r = xor i64 %x, %y
  ret i64 %r
}

declare { i64, i1 } @llvm.umul.with.overflow.i64(i64, i64)
declare { i64, i1 } @llvm.smul.with.overflow.i64(i64, i64)
declare { i128, i1 } @llvm.smul.with.overflow.i128(i128, i128)

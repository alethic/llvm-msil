; Integer arithmetic (ImplementationPlan.md M4.b/M4.c, plan 5.4 'Integer';
; recon isel.md 5.1 and 9): add, sub, mul, and, or and xor on int32 (class I32)
; and on native int (class I64: i64 and pointers, decision D2, so int64 never
; appears) are one CIL opcode each, which pops its operands in IR operand order.
; 64-bit constants are 'ldc.i4 N; conv.i' or 'ldc.i8 N; conv.i' (M3); there are
; no mixed int32/native int instructions in Phase 1, so an int32 operand of a
; native int operation is widened with conv.i (sext) or conv.u (zext) first.
; Pointer arithmetic (getelementptr) is native int arithmetic. ctpop, ctlz,
; cttz, bswap, rotates and abs use LLVM's generic expansions (decision D16: no
; BCL intrinsics in Phase 1): straight-line arithmetic without a call, a branch
; or a memory access. The one exception is ctlz or cttz whose is_zero_poison
; operand is false: isCheapToSpeculateCtlz/Cttz keep the default false in
; Phase 1 (D33, plan section 12 'D16 clarified'), so at -O2 CodeGenPrepare
; wraps the expansion in a zero test ('ceq; brtrue' to a join that returns the
; width 32 or 64, then 'br' to the expansion, which is the next block:
; SelectionDAG always emits the false branch, and branch folding cannot remove
; it while analyzeBranch fails, until M6); at -O0, and with is_zero_poison
; true, they stay straight-line. These two functions pin the printing of BR
; and BR_IF: always the long forms 'br' and 'brtrue' with '@BB<f>_<b>' labels
; (M4.c; plan 5.1), never 'br.s', 'brtrue.s' or 'brfalse.s' (implicit checks).
; 0 - x is 'neg' and x ^ -1 is 'not' (NEG_{I32,I64} and NOT_{I32,I64}, plan
; section 12 row 'Plan 5.4 records added, M4.b'). Division and remainder are
; in div-rem.ll, shifts in int-shifts.ll, compares in int-compare.ll and
; narrow types in narrow-ints.ll.
;
; Functions whose body is a single operation on arguments and at most one
; constant compile to the same CIL at -O0 and -O2 (CHECK); the rest is checked
; exactly at -O2 (O2) and only for its key opcodes at -O0 (O0), where
; MSILRegStackify moves nothing and the ISel order decides what is stackified.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 \
; DEFINE:   --implicit-check-not="{{br(true|false)?\.s }}"
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

source_filename = "int-arith.c"

; ---- int32 ---------------------------------------------------------------

; CHECK-LABEL: .method {{.*}}'add_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'add_i32'{{$}}
define i32 @add_i32(i32 %x, i32 %y) {
  %r = add i32 %x, %y
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sub_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @sub_i32(i32 %x, i32 %y) {
  %r = sub i32 %x, %y
  ret i32 %r
}

; The operands are pushed in IR order, whatever the argument order.
; CHECK-LABEL: .method {{.*}}'sub_rev_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @sub_rev_i32(i32 %x, i32 %y) {
  %r = sub i32 %y, %x
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'mul_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @mul_i32(i32 %x, i32 %y) {
  %r = mul i32 %x, %y
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'and_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @and_i32(i32 %x, i32 %y) {
  %r = and i32 %x, %y
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'or_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  or{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @or_i32(i32 %x, i32 %y) {
  %r = or i32 %x, %y
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'xor_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  xor{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @xor_i32(i32 %x, i32 %y) {
  %r = xor i32 %x, %y
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'add_imm_i32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.7{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @add_imm_i32(i32 %x) {
  %r = add i32 %x, 7
  ret i32 %r
}

; x - C is canonicalized to x + -C.
; CHECK-LABEL: .method {{.*}}'sub_imm_i32'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4.s -7{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  {{add|sub}}{{$}}
define i32 @sub_imm_i32(i32 %x) {
  %r = sub i32 %x, 7
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sub_from_imm_i32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 100{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @sub_from_imm_i32(i32 %x) {
  %r = sub i32 100, %x
  ret i32 %r
}

; Multiplication by a constant stays a mul (no shift-and-add decomposition);
; a power of two becomes a left shift by an int32 amount.
; CHECK-LABEL: .method {{.*}}'mul_imm_i32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 10{{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @mul_imm_i32(i32 %x) {
  %r = mul i32 %x, 10
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'mul_pow2_i32'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4.3{{$}}
; O2-NEXT:     {{^}}  shl{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  {{shl|mul}}{{$}}
define i32 @mul_pow2_i32(i32 %x) {
  %r = mul i32 %x, 8
  ret i32 %r
}

; 0 - x and x ^ -1 are 'neg' and 'not' (NEG_I32, NOT_I32).
; CHECK-LABEL: .method {{.*}}'neg_i32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'neg_i32'{{$}}
define i32 @neg_i32(i32 %x) {
  %r = sub i32 0, %x
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'not_i32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  not{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'not_i32'{{$}}
define i32 @not_i32(i32 %x) {
  %r = xor i32 %x, -1
  ret i32 %r
}

; A longer expression is one tree on the evaluation stack at -O2.
; O2-LABEL:    .method {{.*}}'tree_i32'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  mul{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ldarg.3{{$}}
; O2-NEXT:     {{^}}  xor{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O2-NEXT:     {{^}}} // end of method 'tree_i32'{{$}}
define i32 @tree_i32(i32 %a, i32 %b, i32 %c, i32 %d) {
  %m = mul i32 %b, %c
  %s = add i32 %a, %m
  %x = xor i32 %s, %d
  ret i32 %x
}

; ---- native int ----------------------------------------------------------

; CHECK-LABEL: .method {{.*}}'add_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @add_i64(i64 %x, i64 %y) {
  %r = add i64 %x, %y
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'sub_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @sub_i64(i64 %x, i64 %y) {
  %r = sub i64 %x, %y
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'mul_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @mul_i64(i64 %x, i64 %y) {
  %r = mul i64 %x, %y
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'and_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @and_i64(i64 %x, i64 %y) {
  %r = and i64 %x, %y
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'or_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  or{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @or_i64(i64 %x, i64 %y) {
  %r = or i64 %x, %y
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'xor_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  xor{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @xor_i64(i64 %x, i64 %y) {
  %r = xor i64 %x, %y
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'sub_rev_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @sub_rev_i64(i64 %x, i64 %y) {
  %r = sub i64 %y, %x
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'add_imm_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.7|ldc\.i4 7}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @add_imm_i64(i64 %x) {
  %r = add i64 %x, 7
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'and_imm_i64_large'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i8 81985529216486895{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @and_imm_i64_large(i64 %x) {
  %r = and i64 %x, 81985529216486895
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'mul_imm_i64'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 1000{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @mul_imm_i64(i64 %x) {
  %r = mul i64 %x, 1000
  ret i64 %r
}

; NEG_I64 and NOT_I64 on native int.
; CHECK-LABEL: .method {{.*}}'neg_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'neg_i64'{{$}}
define i64 @neg_i64(i64 %x) {
  %r = sub i64 0, %x
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'not_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  not{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'not_i64'{{$}}
define i64 @not_i64(i64 %x) {
  %r = xor i64 %x, -1
  ret i64 %r
}

; An int32 operand of a native int operation is widened first: sext is conv.i,
; zext is conv.u.
; CHECK-LABEL: .method {{.*}}'add_sext_i64'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @add_sext_i64(i64 %x, i32 %y) {
  %e = sext i32 %y to i64
  %r = add i64 %x, %e
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'add_zext_i64'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @add_zext_i64(i64 %x, i32 %y) {
  %e = zext i32 %y to i64
  %r = add i64 %x, %e
  ret i64 %r
}

; ---- pointer arithmetic --------------------------------------------------

; CHECK-LABEL: .method {{.*}}'gep_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @gep_i64(ptr %p, i64 %n) {
  %q = getelementptr i8, ptr %p, i64 %n
  ret ptr %q
}

; CHECK-LABEL: .method {{.*}}'gep_i32_index'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @gep_i32_index(ptr %p, i32 %n) {
  %q = getelementptr i8, ptr %p, i32 %n
  ret ptr %q
}

; The element size scales the index with a shift by an int32 amount.
; CHECK-LABEL: .method {{.*}}'gep_scaled'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldc.i4.2{{$}}
; O2-NEXT:     {{^}}  shl{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  shl{{$}}
; O0:          {{^}}  add{{$}}
define ptr @gep_scaled(ptr %p, i64 %n) {
  %q = getelementptr i32, ptr %p, i64 %n
  ret ptr %q
}

; CHECK-LABEL: .method {{.*}}'gep_const'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.s 40|ldc\.i4 40}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @gep_const(ptr %p) {
  %q = getelementptr i32, ptr %p, i64 10
  ret ptr %q
}

; ---- generic bit-operation expansions (decision D16) ----------------------

; CHECK-LABEL: .method {{.*}}'ctpop_i32'(
; CHECK-NOT:   {{call|brtrue|brfalse|switch|ldind|stind|localloc}}
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{call|brtrue|brfalse|switch|ldind|stind|localloc}}
; CHECK:       {{^}}} // end of method 'ctpop_i32'{{$}}
define i32 @ctpop_i32(i32 %x) {
  %r = call i32 @llvm.ctpop.i32(i32 %x)
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'ctpop_i64'(
; CHECK-NOT:   {{call|brtrue|brfalse|switch|ldind|stind|localloc}}
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{call|brtrue|brfalse|switch|ldind|stind|localloc}}
; CHECK:       {{^}}} // end of method 'ctpop_i64'{{$}}
define i64 @ctpop_i64(i64 %x) {
  %r = call i64 @llvm.ctpop.i64(i64 %x)
  ret i64 %r
}

; With is_zero_poison false the zero input must give the width. -O0 keeps the
; generic expansion straight-line; at -O2 CodeGenPrepare puts it behind a zero
; test (isCheapToSpeculateCtlz is false, see the header): the entry block
; stores the width, compares x with 0 and branches to the join that returns it,
; then branches with the long-form 'br' to the expansion in the next block.
; CHECK-LABEL: .method {{.*}}'ctlz_i32'(
; O0-NOT:      {{call|brtrue|brfalse|switch|ldind|stind|localloc}}
; O2-NOT:      {{call|brfalse|switch|ldind|stind|localloc}}
; O2:          {{^}}  ldc.i4{{(\.s)?}} 32{{$}}
; O2-NOT:      {{call|brfalse|switch|ldind|stind|localloc}}
; O2:          {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  brtrue @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; O2-NEXT:     {{^}}  br @BB[[F]]_[[NEXT:[0-9]+]]{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[NEXT]]:{{$}}
; CHECK-NOT:   {{call|brtrue|brfalse|switch|ldind|stind|localloc}}
; CHECK:       {{^}}  shr.un{{$}}
; CHECK-NOT:   {{call|brtrue|brfalse|switch|ldind|stind|localloc}}
; O2:          {{^}}@BB[[F]]_[[B]]:{{$}}
; O2-NOT:      {{call|brtrue|brfalse|switch|ldind|stind|localloc}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ctlz_i32'{{$}}
define i32 @ctlz_i32(i32 %x) {
  %r = call i32 @llvm.ctlz.i32(i32 %x, i1 false)
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'cttz_i64'(
; O0-NOT:      {{call|brtrue|brfalse|switch|ldind|stind|localloc}}
; O2-NOT:      {{call|brfalse|switch|ldind|stind|localloc}}
; O2:          {{^}}  ldc.i4{{(\.s)?}} 64{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NOT:      {{call|brfalse|switch|ldind|stind|localloc}}
; O2:          {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  brtrue @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; O2-NEXT:     {{^}}  br @BB[[F]]_[[NEXT:[0-9]+]]{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[NEXT]]:{{$}}
; CHECK-NOT:   {{call|brtrue|brfalse|switch|ldind|stind|localloc}}
; CHECK:       {{^}}  shr.un{{$}}
; CHECK-NOT:   {{call|brtrue|brfalse|switch|ldind|stind|localloc}}
; O2:          {{^}}@BB[[F]]_[[B]]:{{$}}
; O2-NOT:      {{call|brtrue|brfalse|switch|ldind|stind|localloc}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'cttz_i64'{{$}}
define i64 @cttz_i64(i64 %x) {
  %r = call i64 @llvm.cttz.i64(i64 %x, i1 false)
  ret i64 %r
}

; With is_zero_poison true there is no zero test: branch-free at -O0 and -O2.
; CHECK-LABEL: .method {{.*}}'ctlz_zero_poison_i32'(
; CHECK-NOT:   {{call|brtrue|brfalse|switch|ldind|stind|localloc|br }}
; CHECK:       {{^}}  shr.un{{$}}
; CHECK-NOT:   {{call|brtrue|brfalse|switch|ldind|stind|localloc|br }}
; CHECK:       {{^}}} // end of method 'ctlz_zero_poison_i32'{{$}}
define i32 @ctlz_zero_poison_i32(i32 %x) {
  %r = call i32 @llvm.ctlz.i32(i32 %x, i1 true)
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'cttz_zero_poison_i64'(
; CHECK-NOT:   {{call|brtrue|brfalse|switch|ldind|stind|localloc|br }}
; CHECK:       {{^}}  shr.un{{$}}
; CHECK-NOT:   {{call|brtrue|brfalse|switch|ldind|stind|localloc|br }}
; CHECK:       {{^}}} // end of method 'cttz_zero_poison_i64'{{$}}
define i64 @cttz_zero_poison_i64(i64 %x) {
  %r = call i64 @llvm.cttz.i64(i64 %x, i1 true)
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'bswap_i32'(
; CHECK-NOT:   {{call|brtrue|brfalse|switch|ldind|stind|localloc}}
; CHECK:       {{^}}  shl{{$}}
; CHECK-NOT:   {{call|brtrue|brfalse|switch|ldind|stind|localloc}}
; CHECK:       {{^}}} // end of method 'bswap_i32'{{$}}
define i32 @bswap_i32(i32 %x) {
  %r = call i32 @llvm.bswap.i32(i32 %x)
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'bswap_i64'(
; CHECK-NOT:   {{call|brtrue|brfalse|switch|ldind|stind|localloc}}
; CHECK:       {{^}}} // end of method 'bswap_i64'{{$}}
define i64 @bswap_i64(i64 %x) {
  %r = call i64 @llvm.bswap.i64(i64 %x)
  ret i64 %r
}

; A rotate by a variable amount: two shifts and an or, with LLVM's masks.
; CHECK-LABEL: .method {{.*}}'rotl_i32'(
; CHECK-NOT:   {{call|brtrue|brfalse|switch|ldind|stind|localloc}}
; CHECK:       {{^}}  or{{$}}
; CHECK-NOT:   {{call|brtrue|brfalse|switch|ldind|stind|localloc}}
; CHECK:       {{^}}} // end of method 'rotl_i32'{{$}}
define i32 @rotl_i32(i32 %x, i32 %n) {
  %r = call i32 @llvm.fshl.i32(i32 %x, i32 %x, i32 %n)
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'rotr_i64'(
; CHECK-NOT:   {{call|brtrue|brfalse|switch|ldind|stind|localloc}}
; CHECK:       {{^}}  or{{$}}
; CHECK-NOT:   {{call|brtrue|brfalse|switch|ldind|stind|localloc}}
; CHECK:       {{^}}} // end of method 'rotr_i64'{{$}}
define i64 @rotr_i64(i64 %x, i64 %n) {
  %r = call i64 @llvm.fshr.i64(i64 %x, i64 %x, i64 %n)
  ret i64 %r
}

; abs(x) = (x ^ (x >> 31)) - (x >> 31), no select.
; CHECK-LABEL: .method {{.*}}'abs_i32'(
; CHECK-NOT:   {{call|brtrue|brfalse|switch|ldind|stind|localloc}}
; CHECK:       {{^}}  shr{{$}}
; CHECK-NOT:   {{call|brtrue|brfalse|switch|ldind|stind|localloc}}
; CHECK:       {{^}}} // end of method 'abs_i32'{{$}}
define i32 @abs_i32(i32 %x) {
  %r = call i32 @llvm.abs.i32(i32 %x, i1 false)
  ret i32 %r
}

declare i32 @llvm.ctpop.i32(i32)
declare i64 @llvm.ctpop.i64(i64)
declare i32 @llvm.ctlz.i32(i32, i1)
declare i64 @llvm.cttz.i64(i64, i1)
declare i32 @llvm.bswap.i32(i32)
declare i64 @llvm.bswap.i64(i64)
declare i32 @llvm.fshl.i32(i32, i32, i32)
declare i64 @llvm.fshr.i64(i64, i64, i64)
declare i32 @llvm.abs.i32(i32, i1)

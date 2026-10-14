; i1, i8 and i16 values (ImplementationPlan.md M4.a/M4.b; recon isel.md 4, 5.1,
; 5.4 and 8.2; M3.e left the extension cases to this file): narrow integers are
; promoted to int32, so every narrow parameter, result and call operand is an
; int32 in the signature (no int8/int16/bool CIL types), and extension follows
; LLVM's zeroext/signext discipline. A parameter or call result that carries
; zeroext or signext is already extended and is used as it is; one without the
; attribute has undefined upper bits, so a sext of it is conv.i1 or conv.i2 and
; a zext of it is conv.u1 or conv.u2 (the peephole patterns of isel.md 5.1;
; each is checked as the whole sequence, so a wrong mask operation fails). A
; narrow result with zeroext or signext is extended before 'ret';
; without an attribute it is returned as it is. At a call, the caller extends
; an operand whose parameter carries zeroext or signext. Compares and
; divisions of narrow values compare and divide the extended int32 values; an
; i1 that is not known to be 0 or 1 is masked with 'ldc.i4.1; and'.
;
; Single-operation functions compile to the same CIL at -O0 and -O2 (CHECK);
; the rest is exact at -O2 (O2) and checked by opcode at -O0 (O0).

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=int8 \
; DEFINE:   --implicit-check-not=int16
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

source_filename = "narrow-ints.c"

; ---- extension of narrow parameters ---------------------------------------

; CHECK-LABEL: .method {{.*}}'sext_i8_param'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_i8_param'{{$}}
define i32 @sext_i8_param(i8 %x) {
  %r = sext i8 %x to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sext_i16_param'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @sext_i16_param(i16 %x) {
  %r = sext i16 %x to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'zext_i8_param'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'zext_i8_param'{{$}}
define i32 @zext_i8_param(i8 %x) {
  %r = zext i8 %x to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'zext_i16_param'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'zext_i16_param'{{$}}
define i32 @zext_i16_param(i16 %x) {
  %r = zext i16 %x to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'zext_i1_param'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @zext_i1_param(i1 %b) {
  %r = zext i1 %b to i32
  ret i32 %r
}

; A sign-extended i1 is 0 or -1, computed without a branch.
; CHECK-LABEL: .method {{.*}}'sext_i1_param'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NOT:   {{brtrue|brfalse|call}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_i1_param'{{$}}
define i32 @sext_i1_param(i1 %b) {
  %r = sext i1 %b to i32
  ret i32 %r
}

; An attribute of the other kind does not help: the value is extended again.
; CHECK-LABEL: .method {{.*}}'sext_of_zeroext_param'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @sext_of_zeroext_param(i8 zeroext %x) {
  %r = sext i8 %x to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'zext_of_signext_param'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'zext_of_signext_param'{{$}}
define i32 @zext_of_signext_param(i16 signext %x) {
  %r = zext i16 %x to i32
  ret i32 %r
}

; A matching attribute makes the extension free (also for i1).
; CHECK-LABEL: .method {{.*}}'zext_of_zeroext_i1'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @zext_of_zeroext_i1(i1 zeroext %b) {
  %r = zext i1 %b to i32
  ret i32 %r
}

; ---- narrow results ---------------------------------------------------------

; CHECK-LABEL: .method {{.*}}'add_s8'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  conv.i1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define signext i8 @add_s8(i8 signext %a, i8 signext %b) {
  %s = add i8 %a, %b
  ret i8 %s
}

; CHECK-LABEL: .method {{.*}}'mul_s16'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  conv.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define signext i16 @mul_s16(i16 %a, i16 %b) {
  %s = mul i16 %a, %b
  ret i16 %s
}

; CHECK-LABEL: .method {{.*}}'add_u16'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  conv.u2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'add_u16'{{$}}
define zeroext i16 @add_u16(i16 zeroext %a, i16 zeroext %b) {
  %s = add i16 %a, %b
  ret i16 %s
}

; CHECK-LABEL: .method {{.*}}'sub_u8'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK-NEXT:  {{^}}  conv.u1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sub_u8'{{$}}
define zeroext i8 @sub_u8(i8 %a, i8 %b) {
  %s = sub i8 %a, %b
  ret i8 %s
}

; No attribute on the result: no extension.
; CHECK-LABEL: .method {{.*}}'add_i8_anyext'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i8 @add_i8_anyext(i8 %a, i8 %b) {
  %s = add i8 %a, %b
  ret i8 %s
}

; A compare is already 0 or 1, so a zeroext i1 result needs no mask ...
; CHECK-LABEL: .method {{.*}}'ret_bool_cmp'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define zeroext i1 @ret_bool_cmp(i32 %a, i32 %b) {
  %c = icmp slt i32 %a, %b
  ret i1 %c
}

; ... but a truncated int32 does.
; CHECK-LABEL: .method {{.*}}'ret_bool_trunc'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define zeroext i1 @ret_bool_trunc(i32 %x) {
  %t = trunc i32 %x to i1
  ret i1 %t
}

; CHECK-LABEL: .method {{.*}}'ret_bool_anyext'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i1 @ret_bool_anyext(i32 %x) {
  %t = trunc i32 %x to i1
  ret i1 %t
}

; A narrow value from a native int.
; CHECK-LABEL: .method {{.*}}'ret_s8_from_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  conv.i1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define signext i8 @ret_s8_from_i64(i64 %x) {
  %t = trunc i64 %x to i8
  ret i8 %t
}

; ---- compares, divisions and shifts of narrow values -----------------------

; CHECK-LABEL: .method {{.*}}'slt_i8'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  conv.i1{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  conv.i1{{$}}
; O2-NEXT:     {{^}}  clt{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0-DAG:      {{^}}  conv.i1{{$}}
; O0-DAG:      {{^}}  conv.i1{{$}}
; O0:          {{^}}  clt{{$}}
; CHECK:       {{^}}} // end of method 'slt_i8'{{$}}
define i32 @slt_i8(i8 %a, i8 %b) {
  %c = icmp slt i8 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'ult_i16'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  conv.u2{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  conv.u2{{$}}
; O2-NEXT:     {{^}}  clt.un{{$}}
; O0:          {{^}}  ldarg.{{[01]}}{{$}}
; O0-NEXT:     {{^}}  conv.u2{{$}}
; O0:          {{^}}  ldarg.{{[01]}}{{$}}
; O0-NEXT:     {{^}}  conv.u2{{$}}
; O0:          {{^}}  clt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ult_i16'{{$}}
define i32 @ult_i16(i16 %a, i16 %b) {
  %c = icmp ult i16 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; Extended parameters are compared directly.
; CHECK-LABEL: .method {{.*}}'slt_s8_params'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @slt_s8_params(i8 signext %a, i8 signext %b) {
  %c = icmp slt i8 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'ugt_u16_params'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @ugt_u16_params(i16 zeroext %a, i16 zeroext %b) {
  %c = icmp ugt i16 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sdiv_i8'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  conv.i1{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  conv.i1{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0-DAG:      {{^}}  conv.i1{{$}}
; O0-DAG:      {{^}}  conv.i1{{$}}
; O0:          {{^}}  div{{$}}
; CHECK:       {{^}}} // end of method 'sdiv_i8'{{$}}
define i8 @sdiv_i8(i8 %a, i8 %b) {
  %q = sdiv i8 %a, %b
  ret i8 %q
}

; The remainder of two zero-extended values needs no mask for its zeroext
; result.
; CHECK-LABEL: .method {{.*}}'urem_u16_params'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  rem.un{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  rem.un{{$}}
; CHECK:       {{^}}} // end of method 'urem_u16_params'{{$}}
define zeroext i16 @urem_u16_params(i16 zeroext %a, i16 zeroext %b) {
  %q = urem i16 %a, %b
  ret i16 %q
}

; A logical right shift of an i8 shifts its zero-extended value.
; CHECK-LABEL: .method {{.*}}'lshr_i8'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  conv.u1{{$}}
; CHECK-NEXT:  {{^}}  shr.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'lshr_i8'{{$}}
define i8 @lshr_i8(i8 %x, i8 %n) {
  %r = lshr i8 %x, %n
  ret i8 %r
}

; An arithmetic right shift of an i16 shifts its sign-extended value.
; CHECK-LABEL: .method {{.*}}'ashr_i16'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK:       {{^}}  conv.i2{{$}}
; CHECK:       {{^}}  shr{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ashr_i16'{{$}}
define i16 @ashr_i16(i16 %x, i16 %n) {
  %r = ashr i16 %x, %n
  ret i16 %r
}

; ---- narrow values at call sites ----------------------------------------------

define void @take_s8(i8 signext %x) {
  ret void
}
define void @take_u16(i16 zeroext %x) {
  ret void
}
define void @take_bool(i1 zeroext %x) {
  ret void
}
define void @take_i8(i8 %x) {
  ret void
}
define signext i8 @get_s8() {
  ret i8 -5
}
define i8 @get_i8() {
  ret i8 -5
}

; The callees take int32.
; CHECK:       {{^}}.method public static void 'take_s8'(int32) cil managed{{$}}
; CHECK:       {{^}}.method public static void 'take_u16'(int32) cil managed{{$}}
; CHECK:       {{^}}.method public static void 'take_bool'(int32) cil managed{{$}}
; CHECK:       {{^}}.method public static void 'take_i8'(int32) cil managed{{$}}
; CHECK:       {{^}}.method public static int32 'get_s8'() cil managed{{$}}
; CHECK:       {{^}}.method public static int32 'get_i8'() cil managed{{$}}

; The caller sign-extends for a signext parameter ...
; CHECK-LABEL: .method {{.*}}'pass_s8'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i1{{$}}
; CHECK-NEXT:  {{^}}  call void 'take_s8'(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @pass_s8(i32 %x) {
  %t = trunc i32 %x to i8
  call void @take_s8(i8 signext %t)
  ret void
}

; ... zero-extends for a zeroext one ...
; CHECK-LABEL: .method {{.*}}'pass_u16'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u2{{$}}
; CHECK-NEXT:  {{^}}  call void 'take_u16'(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @pass_u16(i32 %x) {
  %t = trunc i32 %x to i16
  call void @take_u16(i16 zeroext %t)
  ret void
}

; ... passes a compare as it is ...
; CHECK-LABEL: .method {{.*}}'pass_bool_cmp'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  call void 'take_bool'(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @pass_bool_cmp(i32 %a, i32 %b) {
  %c = icmp eq i32 %a, %b
  call void @take_bool(i1 zeroext %c)
  ret void
}

; ... and does not extend for a parameter without an attribute.
; CHECK-LABEL: .method {{.*}}'pass_i8_anyext'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call void 'take_i8'(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @pass_i8_anyext(i32 %x) {
  %t = trunc i32 %x to i8
  call void @take_i8(i8 %t)
  ret void
}

; A signext result is used as it is ...
; CHECK-LABEL: .method {{.*}}'use_s8_result'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  call int32 'get_s8'(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @use_s8_result() {
  %v = call signext i8 @get_s8()
  %e = sext i8 %v to i32
  ret i32 %e
}

; ... a result without an attribute is extended by the caller.
; CHECK-LABEL: .method {{.*}}'use_i8_result'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  call int32 'get_i8'(){{$}}
; CHECK-NEXT:  {{^}}  conv.i1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @use_i8_result() {
  %v = call i8 @get_i8()
  %e = sext i8 %v to i32
  ret i32 %e
}

; A signext result zero-extended still needs the mask.
; CHECK-LABEL: .method {{.*}}'zext_s8_result'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  call int32 'get_s8'(){{$}}
; CHECK-NEXT:  {{^}}  conv.u1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'zext_s8_result'{{$}}
define i32 @zext_s8_result() {
  %v = call signext i8 @get_s8()
  %e = zext i8 %v to i32
  ret i32 %e
}

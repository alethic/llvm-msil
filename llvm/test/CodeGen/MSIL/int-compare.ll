; Integer compares as values (ImplementationPlan.md M4.b, plan 5.4 'Integer';
; recon isel.md 5.2 and decision 2): a setcc produces an int32 0 or 1 through
; ceq, cgt, cgt.un, clt or clt.un on int32 and native int operands alike. The
; negated predicates are built from single-opcode instructions as the compare
; followed by 'ldc.i4.0; ceq': ne is ceq, sle is cgt, sge is clt, ule is cgt.un
; and uge is clt.un, each negated. Every one of the ten predicates is checked
; for int32, native int and pointers. Because the result is already 0 or 1, a
; zext of a compare adds nothing to int32 and only conv.u to native int; a sext
; of a compare stays arithmetic (convertSelectOfConstantsToMath, decision D34):
; no select triangle, so no brtrue, and the compare opcode itself is still
; there (the inverse predicate uses the same base opcode). Compares against a
; constant may be canonicalized (x <= 10 becomes x < 11), and 'x != 0' may use
; the shorter 'ldc.i4.0; cgt.un'.
;
; A compare of two arguments compiles to the same CIL at -O0 and -O2 (CHECK);
; the order of the negating 'ldc.i4.0' relative to the compare is only fixed
; at -O2 (O2), so -O0 checks the opcodes (O0).

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=brtrue \
; DEFINE:   --implicit-check-not=brfalse
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

source_filename = "int-compare.c"

; ---- int32 ---------------------------------------------------------------

; CHECK-LABEL: .method {{.*}}'eq_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'eq_i32'{{$}}
define i32 @eq_i32(i32 %a, i32 %b) {
  %c = icmp eq i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'ne_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ceq{{$}}
; O0:          {{^}}  ceq{{$}}
; CHECK:       {{^}}} // end of method 'ne_i32'{{$}}
define i32 @ne_i32(i32 %a, i32 %b) {
  %c = icmp ne i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'slt_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @slt_i32(i32 %a, i32 %b) {
  %c = icmp slt i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sle_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  cgt{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  cgt{{$}}
; O0:          {{^}}  ceq{{$}}
; CHECK:       {{^}}} // end of method 'sle_i32'{{$}}
define i32 @sle_i32(i32 %a, i32 %b) {
  %c = icmp sle i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sgt_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @sgt_i32(i32 %a, i32 %b) {
  %c = icmp sgt i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sge_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  clt{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  clt{{$}}
; O0:          {{^}}  ceq{{$}}
; CHECK:       {{^}}} // end of method 'sge_i32'{{$}}
define i32 @sge_i32(i32 %a, i32 %b) {
  %c = icmp sge i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'ult_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @ult_i32(i32 %a, i32 %b) {
  %c = icmp ult i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'ule_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  cgt.un{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  cgt.un{{$}}
; O0:          {{^}}  ceq{{$}}
; CHECK:       {{^}}} // end of method 'ule_i32'{{$}}
define i32 @ule_i32(i32 %a, i32 %b) {
  %c = icmp ule i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'ugt_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @ugt_i32(i32 %a, i32 %b) {
  %c = icmp ugt i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'uge_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  clt.un{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  clt.un{{$}}
; O0:          {{^}}  ceq{{$}}
; CHECK:       {{^}}} // end of method 'uge_i32'{{$}}
define i32 @uge_i32(i32 %a, i32 %b) {
  %c = icmp uge i32 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; ---- native int ----------------------------------------------------------

; CHECK-LABEL: .method {{.*}}'eq_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @eq_i64(i64 %a, i64 %b) {
  %c = icmp eq i64 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'ne_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ceq{{$}}
; O0:          {{^}}  ceq{{$}}
; CHECK:       {{^}}} // end of method 'ne_i64'{{$}}
define i32 @ne_i64(i64 %a, i64 %b) {
  %c = icmp ne i64 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'slt_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @slt_i64(i64 %a, i64 %b) {
  %c = icmp slt i64 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sle_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  cgt{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  cgt{{$}}
; O0:          {{^}}  ceq{{$}}
; CHECK:       {{^}}} // end of method 'sle_i64'{{$}}
define i32 @sle_i64(i64 %a, i64 %b) {
  %c = icmp sle i64 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sgt_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @sgt_i64(i64 %a, i64 %b) {
  %c = icmp sgt i64 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sge_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  clt{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  clt{{$}}
; O0:          {{^}}  ceq{{$}}
; CHECK:       {{^}}} // end of method 'sge_i64'{{$}}
define i32 @sge_i64(i64 %a, i64 %b) {
  %c = icmp sge i64 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'ult_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @ult_i64(i64 %a, i64 %b) {
  %c = icmp ult i64 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'ule_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  cgt.un{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  cgt.un{{$}}
; O0:          {{^}}  ceq{{$}}
; CHECK:       {{^}}} // end of method 'ule_i64'{{$}}
define i32 @ule_i64(i64 %a, i64 %b) {
  %c = icmp ule i64 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'ugt_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @ugt_i64(i64 %a, i64 %b) {
  %c = icmp ugt i64 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'uge_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  clt.un{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  clt.un{{$}}
; O0:          {{^}}  ceq{{$}}
; CHECK:       {{^}}} // end of method 'uge_i64'{{$}}
define i32 @uge_i64(i64 %a, i64 %b) {
  %c = icmp uge i64 %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; ---- pointers --------------------------------------------------------------

; CHECK-LABEL: .method {{.*}}'eq_ptr'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @eq_ptr(ptr %a, ptr %b) {
  %c = icmp eq ptr %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'ult_ptr'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @ult_ptr(ptr %a, ptr %b) {
  %c = icmp ult ptr %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'uge_ptr'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  clt.un{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  clt.un{{$}}
; O0:          {{^}}  ceq{{$}}
; CHECK:       {{^}}} // end of method 'uge_ptr'{{$}}
define i32 @uge_ptr(ptr %a, ptr %b) {
  %c = icmp uge ptr %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; ---- constants ---------------------------------------------------------------

; CHECK-LABEL: .method {{.*}}'slt_imm_i32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 10{{$}}
; CHECK-NEXT:  {{^}}  clt{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @slt_imm_i32(i32 %a) {
  %c = icmp slt i32 %a, 10
  %r = zext i1 %c to i32
  ret i32 %r
}

; x <= 10 is x < 11: one compare, no negation.
; CHECK-LABEL: .method {{.*}}'sle_imm_i32'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4.s 11{{$}}
; O2-NEXT:     {{^}}  clt{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  {{clt|cgt}}{{$}}
; CHECK:       {{^}}} // end of method 'sle_imm_i32'{{$}}
define i32 @sle_imm_i32(i32 %a) {
  %c = icmp sle i32 %a, 10
  %r = zext i1 %c to i32
  ret i32 %r
}

; x >=u 10 is x >u 9.
; CHECK-LABEL: .method {{.*}}'uge_imm_i32'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4.s 9{{$}}
; O2-NEXT:     {{^}}  cgt.un{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  {{clt|cgt}}.un{{$}}
; CHECK:       {{^}}} // end of method 'uge_imm_i32'{{$}}
define i32 @uge_imm_i32(i32 %a) {
  %c = icmp uge i32 %a, 10
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'eq_zero_i32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @eq_zero_i32(i32 %a) {
  %c = icmp eq i32 %a, 0
  %r = zext i1 %c to i32
  ret i32 %r
}

; 'ldc.i4.0; cgt.un' or the general 'ldc.i4.0; ceq; ldc.i4.0; ceq' (at -O0 a
; zero shared by both compares may live in a local).
; CHECK-LABEL: .method {{.*}}'ne_zero_i32'(
; O2:          {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  {{ceq|cgt\.un}}{{$}}
; O0:          {{^}}  {{ceq|cgt\.un}}{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ne_zero_i32'{{$}}
define i32 @ne_zero_i32(i32 %a) {
  %c = icmp ne i32 %a, 0
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'eq_imm_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.5|ldc\.i4 5}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @eq_imm_i64(i64 %a) {
  %c = icmp eq i64 %a, 5
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'eq_null'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.0|ldc\.i4 0}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @eq_null(ptr %p) {
  %c = icmp eq ptr %p, null
  %r = zext i1 %c to i32
  ret i32 %r
}

; ---- zext and sext of a compare ----------------------------------------------

; To native int: the 0/1 int32 is zero-extended with conv.u.
; CHECK-LABEL: .method {{.*}}'zext_ult_to_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt.un{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @zext_ult_to_i64(i64 %a, i64 %b) {
  %c = icmp ult i64 %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'zext_eq_i32_to_i64'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @zext_eq_i32_to_i64(i32 %a, i32 %b) {
  %c = icmp eq i32 %a, %b
  %r = zext i1 %c to i64
  ret i64 %r
}

; sext gives 0 or -1 by arithmetic on the compare result (no brtrue: see the
; implicit checks): the 0 or 1 of the compare, widened with conv.u for native
; int, then 'neg'. A negated predicate first inverts the compare with
; 'ldc.i4.0; ceq', so every sequence is checked line by line: dropping the
; inversion or the neg changes the value.
; CHECK-LABEL: .method {{.*}}'sext_eq_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_eq_i32'{{$}}
define i32 @sext_eq_i32(i32 %a, i32 %b) {
  %c = icmp eq i32 %a, %b
  %r = sext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sext_ne_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_ne_i32'{{$}}
define i32 @sext_ne_i32(i32 %a, i32 %b) {
  %c = icmp ne i32 %a, %b
  %r = sext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sext_slt_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_slt_i32'{{$}}
define i32 @sext_slt_i32(i32 %a, i32 %b) {
  %c = icmp slt i32 %a, %b
  %r = sext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sext_sle_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_sle_i32'{{$}}
define i32 @sext_sle_i32(i32 %a, i32 %b) {
  %c = icmp sle i32 %a, %b
  %r = sext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sext_sgt_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_sgt_i32'{{$}}
define i32 @sext_sgt_i32(i32 %a, i32 %b) {
  %c = icmp sgt i32 %a, %b
  %r = sext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sext_sge_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_sge_i32'{{$}}
define i32 @sext_sge_i32(i32 %a, i32 %b) {
  %c = icmp sge i32 %a, %b
  %r = sext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sext_ult_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt.un{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_ult_i32'{{$}}
define i32 @sext_ult_i32(i32 %a, i32 %b) {
  %c = icmp ult i32 %a, %b
  %r = sext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sext_ule_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_ule_i32'{{$}}
define i32 @sext_ule_i32(i32 %a, i32 %b) {
  %c = icmp ule i32 %a, %b
  %r = sext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sext_ugt_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_ugt_i32'{{$}}
define i32 @sext_ugt_i32(i32 %a, i32 %b) {
  %c = icmp ugt i32 %a, %b
  %r = sext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sext_uge_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt.un{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_uge_i32'{{$}}
define i32 @sext_uge_i32(i32 %a, i32 %b) {
  %c = icmp uge i32 %a, %b
  %r = sext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sext_ne_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_ne_i64'{{$}}
define i64 @sext_ne_i64(i64 %a, i64 %b) {
  %c = icmp ne i64 %a, %b
  %r = sext i1 %c to i64
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'sext_sle_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_sle_i64'{{$}}
define i64 @sext_sle_i64(i64 %a, i64 %b) {
  %c = icmp sle i64 %a, %b
  %r = sext i1 %c to i64
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'sext_sgt_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_sgt_i64'{{$}}
define i64 @sext_sgt_i64(i64 %a, i64 %b) {
  %c = icmp sgt i64 %a, %b
  %r = sext i1 %c to i64
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'sext_sge_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_sge_i64'{{$}}
define i64 @sext_sge_i64(i64 %a, i64 %b) {
  %c = icmp sge i64 %a, %b
  %r = sext i1 %c to i64
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'sext_ult_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt.un{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_ult_i64'{{$}}
define i64 @sext_ult_i64(i64 %a, i64 %b) {
  %c = icmp ult i64 %a, %b
  %r = sext i1 %c to i64
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'sext_ule_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_ule_i64'{{$}}
define i64 @sext_ule_i64(i64 %a, i64 %b) {
  %c = icmp ule i64 %a, %b
  %r = sext i1 %c to i64
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'sext_uge_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt.un{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_uge_i64'{{$}}
define i64 @sext_uge_i64(i64 %a, i64 %b) {
  %c = icmp uge i64 %a, %b
  %r = sext i1 %c to i64
  ret i64 %r
}

; Two compares combined with i1 logic stay arithmetic too.
; CHECK-LABEL: .method {{.*}}'and_of_compares'(
; CHECK-DAG:   {{^}}  clt{{$}}
; CHECK-DAG:   {{^}}  cgt{{$}}
; CHECK:       {{^}}  and{{$}}
; CHECK:       {{^}}} // end of method 'and_of_compares'{{$}}
define i32 @and_of_compares(i32 %a, i32 %b, i32 %c, i32 %d) {
  %x = icmp slt i32 %a, %b
  %y = icmp sgt i32 %c, %d
  %z = and i1 %x, %y
  %r = zext i1 %z to i32
  ret i32 %r
}

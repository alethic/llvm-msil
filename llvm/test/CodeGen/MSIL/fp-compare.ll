; Floating-point compares as values (ImplementationPlan.md M5.a, plan 5.4
; 'FP' CEQ/CGT/CGT_UN/CLT/CLT_UN_{F32,F64}; recon isel.md 3.2 and 5.2): every
; fcmp predicate on float32 and float64 produces an int32 0 or 1 with exact
; NaN semantics. CIL ceq, cgt and clt are false when an operand is NaN (oeq,
; ogt, olt), cgt.un and clt.un are true (ugt, ult). The other predicates are
; the inverse of one of these, built from single-opcode instructions as the
; compare followed by 'ldc.i4.0; ceq' (isel.md 5.2): une = !oeq, ole = !ugt
; (cgt.un), ule = !ogt (cgt), oge = !ult (clt.un), uge = !olt (clt), so an
; ordered '<=' or '>=' uses the unordered opcode and vice versa. ord, uno, one
; and ueq are Expand (setCondCodeAction): LLVM's legalizer builds ord as
; (a oeq a) and (b oeq b), uno as (a une a) or (b une b), one as (a ogt b) or
; (a olt b), and ueq as the logical not of one. false and true are constants.
; With nnan the predicates lose their ordered/unordered flavor (SETEQ, SETNE,
; SETLT, ...) and select the same opcodes (the relational ones may use either
; flavor, since no operand is NaN). A sext of a compare stays arithmetic
; (convertSelectOfConstantsToMath, decision D34). Nothing in cmp.ll branches,
; converts or calls, so float32 operands are never widened. A select on a
; floating-point compare (select.ll) compiles to the compare and a select
; triangle (D34); M5.d's select-fp.ll checks its results. The constrained
; compares of a strictfp function (llvm.experimental.constrained.fcmp and
; .fcmps, strict.ll) compile to the opcodes of the plain predicates: CIL has
; no floating-point exceptions, so the quiet and the signaling forms are the
; same compare (plan section 12, row 'M5.a extended (constrained floating
; point and rounding mode)').
;
; Single compares of two arguments compile to the same CIL at -O0 and -O2
; (CHECK). The expanded predicates are checked exactly at -O2 (O2) and only for
; their opcodes at -O0 (O0), where MSILRegStackify moves nothing. The MIR lines
; check the record names and operand order of plan 5.4 after instruction
; selection, including the negated forms (CEQ_I32 of the compare and a
; CONST_I32 0).

; The opcodes that must never appear are checked over the whole output by a
; second FileCheck run (BAN) whose only directive is the '.assembly' line,
; because implicit negative checks do not look between CHECK-DAG matches.
; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=conv \
; DEFINE:   --implicit-check-not=call --implicit-check-not=localloc \
; DEFINE:   --implicit-check-not="{{^ b}}"
; DEFINE: %{ban} = FileCheck %s --check-prefix=BAN --implicit-check-not=int64 \
; DEFINE:   --implicit-check-not=conv --implicit-check-not=call --implicit-check-not=localloc
; RUN: split-file %s %t
; BAN: {{^}}.assembly '
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/cmp.ll \
; RUN:   -o %t/cmp.lnx.o2.il
; RUN: %{check} --check-prefixes=CHECK,O2 --input-file=%t/cmp.lnx.o2.il
; RUN: %{ban} --implicit-check-not="{{^ b}}" --input-file=%t/cmp.lnx.o2.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/cmp.ll \
; RUN:   -o %t/cmp.win.o2.il
; RUN: %{check} --check-prefixes=CHECK,O2 --input-file=%t/cmp.win.o2.il
; RUN: %{ban} --implicit-check-not="{{^ b}}" --input-file=%t/cmp.win.o2.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/cmp.ll \
; RUN:   -o %t/cmp.lnx.o0.il
; RUN: %{check} --check-prefixes=CHECK,O0 --input-file=%t/cmp.lnx.o0.il
; RUN: %{ban} --implicit-check-not="{{^ b}}" --input-file=%t/cmp.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/cmp.ll \
; RUN:   -o %t/cmp.win.o0.il
; RUN: %{check} --check-prefixes=CHECK,O0 --input-file=%t/cmp.win.o0.il
; RUN: %{ban} --implicit-check-not="{{^ b}}" --input-file=%t/cmp.win.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -stop-after=finalize-isel < %t/cmp.ll \
; RUN:   | FileCheck %s --check-prefix=MIR
; RUN: %if ilasm %{ %ilasm-verify -output=%t/cmp.lnx.o2.dll %t/cmp.lnx.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/cmp.win.o0.dll %t/cmp.win.o0.il %}

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/select.ll \
; RUN:   -o %t/select.lnx.o2.il
; RUN: FileCheck %s --check-prefix=SEL --input-file=%t/select.lnx.o2.il
; RUN: %{ban} --input-file=%t/select.lnx.o2.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/select.ll \
; RUN:   -o %t/select.win.o2.il
; RUN: FileCheck %s --check-prefix=SEL --input-file=%t/select.win.o2.il
; RUN: %{ban} --input-file=%t/select.win.o2.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/select.ll \
; RUN:   -o %t/select.lnx.o0.il
; RUN: FileCheck %s --check-prefix=SEL --input-file=%t/select.lnx.o0.il
; RUN: %{ban} --input-file=%t/select.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/select.ll \
; RUN:   -o %t/select.win.o0.il
; RUN: FileCheck %s --check-prefix=SEL --input-file=%t/select.win.o0.il
; RUN: %{ban} --input-file=%t/select.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/select.win.o2.dll %t/select.win.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/select.lnx.o0.dll %t/select.lnx.o0.il %}

; Constrained compares (strictfp).
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/strict.ll \
; RUN:   -o %t/strict.lnx.o2.il
; RUN: %{check} --check-prefixes=STRICT,STRICT-O2 --input-file=%t/strict.lnx.o2.il
; RUN: %{ban} --implicit-check-not="{{^ b}}" --input-file=%t/strict.lnx.o2.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/strict.ll \
; RUN:   -o %t/strict.win.o2.il
; RUN: %{check} --check-prefixes=STRICT,STRICT-O2 --input-file=%t/strict.win.o2.il
; RUN: %{ban} --implicit-check-not="{{^ b}}" --input-file=%t/strict.win.o2.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/strict.ll \
; RUN:   -o %t/strict.lnx.o0.il
; RUN: %{check} --check-prefixes=STRICT,STRICT-O0 --input-file=%t/strict.lnx.o0.il
; RUN: %{ban} --implicit-check-not="{{^ b}}" --input-file=%t/strict.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/strict.ll \
; RUN:   -o %t/strict.win.o0.il
; RUN: %{check} --check-prefixes=STRICT,STRICT-O0 --input-file=%t/strict.win.o0.il
; RUN: %{ban} --implicit-check-not="{{^ b}}" --input-file=%t/strict.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/strict.win.o2.dll %t/strict.win.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/strict.lnx.o0.dll %t/strict.lnx.o0.il %}

;--- cmp.ll
source_filename = "fp-compare.c"

; ---- float32 ------------------------------------------------------------------

; fcmp false and fcmp true are constants.
; CHECK-LABEL: .method {{.*}}'false_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'false_f32'{{$}}
define i32 @false_f32(float %a, float %b) {
  %c = fcmp false float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; oeq is ceq: false when either operand is NaN.
; CHECK-LABEL: .method {{.*}}'oeq_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'oeq_f32'{{$}}
; MIR-LABEL:   name: oeq_f32{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:     [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR:         {{%[0-9]+}}:i32 = {{([a-z]+ )*}}CEQ_F32 [[A]], [[B]],
define i32 @oeq_f32(float %a, float %b) {
  %c = fcmp oeq float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; ogt is cgt (false on NaN).
; CHECK-LABEL: .method {{.*}}'ogt_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ogt_f32'{{$}}
; MIR-LABEL:   name: ogt_f32{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:     [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR:         {{%[0-9]+}}:i32 = {{([a-z]+ )*}}CGT_F32 [[A]], [[B]],
define i32 @ogt_f32(float %a, float %b) {
  %c = fcmp ogt float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; oge is !ult: clt.un, then ldc.i4.0; ceq (false on NaN).
; CHECK-LABEL: .method {{.*}}'oge_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt.un{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'oge_f32'{{$}}
; MIR-LABEL:   name: oge_f32{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:     [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR-DAG:     [[Z:%[0-9]+]]:i32 = CONST_I32 0,
; MIR-DAG:     [[C:%[0-9]+]]:i32 = {{([a-z]+ )*}}CLT_UN_F32 [[A]], [[B]],
; MIR:         {{%[0-9]+}}:i32 = CEQ_I32 [[C]], [[Z]],
define i32 @oge_f32(float %a, float %b) {
  %c = fcmp oge float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; olt is clt (false on NaN).
; CHECK-LABEL: .method {{.*}}'olt_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'olt_f32'{{$}}
; MIR-LABEL:   name: olt_f32{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:     [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR:         {{%[0-9]+}}:i32 = {{([a-z]+ )*}}CLT_F32 [[A]], [[B]],
define i32 @olt_f32(float %a, float %b) {
  %c = fcmp olt float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; ole is !ugt: cgt.un, then ldc.i4.0; ceq (false on NaN).
; CHECK-LABEL: .method {{.*}}'ole_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ole_f32'{{$}}
; MIR-LABEL:   name: ole_f32{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:     [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR-DAG:     [[Z:%[0-9]+]]:i32 = CONST_I32 0,
; MIR-DAG:     [[C:%[0-9]+]]:i32 = {{([a-z]+ )*}}CGT_UN_F32 [[A]], [[B]],
; MIR:         {{%[0-9]+}}:i32 = CEQ_I32 [[C]], [[Z]],
define i32 @ole_f32(float %a, float %b) {
  %c = fcmp ole float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; one is Expand: (a ogt b) or (a olt b).
; CHECK-LABEL: .method {{.*}}'one_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  cgt{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  clt{{$}}
; O2-NEXT:     {{^}}  or{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0-DAG:      {{^}}  cgt{{$}}
; O0-DAG:      {{^}}  clt{{$}}
; O0:          {{^}}  or{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'one_f32'{{$}}
define i32 @one_f32(float %a, float %b) {
  %c = fcmp one float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; ord is Expand: (a oeq a) and (b oeq b); x oeq x is false only for NaN.
; CHECK-LABEL: .method {{.*}}'ord_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  and{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0-DAG:      {{^}}  ceq{{$}}
; O0-DAG:      {{^}}  ceq{{$}}
; O0:          {{^}}  and{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ord_f32'{{$}}
define i32 @ord_f32(float %a, float %b) {
  %c = fcmp ord float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; ueq is Expand: the logical not of one.
; CHECK-LABEL: .method {{.*}}'ueq_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  cgt{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  clt{{$}}
; O2-NEXT:     {{^}}  or{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.1|ldc\.i4\.0}}{{$}}
; O2-NEXT:     {{^}}  {{xor|ceq}}{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0-DAG:      {{^}}  cgt{{$}}
; O0-DAG:      {{^}}  clt{{$}}
; O0:          {{^}}  or{{$}}
; O0:          {{^}}  {{xor|ceq}}{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ueq_f32'{{$}}
define i32 @ueq_f32(float %a, float %b) {
  %c = fcmp ueq float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; ugt is cgt.un (true on NaN).
; CHECK-LABEL: .method {{.*}}'ugt_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ugt_f32'{{$}}
; MIR-LABEL:   name: ugt_f32{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:     [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR:         {{%[0-9]+}}:i32 = {{([a-z]+ )*}}CGT_UN_F32 [[A]], [[B]],
define i32 @ugt_f32(float %a, float %b) {
  %c = fcmp ugt float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; uge is !olt: clt, then ldc.i4.0; ceq (true on NaN).
; CHECK-LABEL: .method {{.*}}'uge_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'uge_f32'{{$}}
; MIR-LABEL:   name: uge_f32{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:     [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR-DAG:     [[Z:%[0-9]+]]:i32 = CONST_I32 0,
; MIR-DAG:     [[C:%[0-9]+]]:i32 = {{([a-z]+ )*}}CLT_F32 [[A]], [[B]],
; MIR:         {{%[0-9]+}}:i32 = CEQ_I32 [[C]], [[Z]],
define i32 @uge_f32(float %a, float %b) {
  %c = fcmp uge float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; ult is clt.un (true on NaN).
; CHECK-LABEL: .method {{.*}}'ult_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ult_f32'{{$}}
; MIR-LABEL:   name: ult_f32{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:     [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR:         {{%[0-9]+}}:i32 = {{([a-z]+ )*}}CLT_UN_F32 [[A]], [[B]],
define i32 @ult_f32(float %a, float %b) {
  %c = fcmp ult float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; ule is !ogt: cgt, then ldc.i4.0; ceq (true on NaN).
; CHECK-LABEL: .method {{.*}}'ule_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ule_f32'{{$}}
; MIR-LABEL:   name: ule_f32{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:     [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR-DAG:     [[Z:%[0-9]+]]:i32 = CONST_I32 0,
; MIR-DAG:     [[C:%[0-9]+]]:i32 = {{([a-z]+ )*}}CGT_F32 [[A]], [[B]],
; MIR:         {{%[0-9]+}}:i32 = CEQ_I32 [[C]], [[Z]],
define i32 @ule_f32(float %a, float %b) {
  %c = fcmp ule float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; une is !oeq: ceq, then ldc.i4.0; ceq (true on NaN).
; CHECK-LABEL: .method {{.*}}'une_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'une_f32'{{$}}
; MIR-LABEL:   name: une_f32{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:     [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR-DAG:     [[Z:%[0-9]+]]:i32 = CONST_I32 0,
; MIR-DAG:     [[C:%[0-9]+]]:i32 = {{([a-z]+ )*}}CEQ_F32 [[A]], [[B]],
; MIR:         {{%[0-9]+}}:i32 = CEQ_I32 [[C]], [[Z]],
define i32 @une_f32(float %a, float %b) {
  %c = fcmp une float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; uno is Expand: (a une a) or (b une b).
; CHECK-LABEL: .method {{.*}}'uno_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  or{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0-DAG:      {{^}}  ceq{{$}}
; O0-DAG:      {{^}}  ceq{{$}}
; O0-DAG:      {{^}}  ceq{{$}}
; O0-DAG:      {{^}}  ceq{{$}}
; O0:          {{^}}  or{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'uno_f32'{{$}}
define i32 @uno_f32(float %a, float %b) {
  %c = fcmp uno float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'true_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'true_f32'{{$}}
define i32 @true_f32(float %a, float %b) {
  %c = fcmp true float %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; ---- float64 ------------------------------------------------------------------

; CHECK-LABEL: .method {{.*}}'false_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'false_f64'{{$}}
define i32 @false_f64(double %a, double %b) {
  %c = fcmp false double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'oeq_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'oeq_f64'{{$}}
; MIR-LABEL:   name: oeq_f64{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:     [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR:         {{%[0-9]+}}:i32 = {{([a-z]+ )*}}CEQ_F64 [[A]], [[B]],
define i32 @oeq_f64(double %a, double %b) {
  %c = fcmp oeq double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'ogt_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ogt_f64'{{$}}
; MIR-LABEL:   name: ogt_f64{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:     [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR:         {{%[0-9]+}}:i32 = {{([a-z]+ )*}}CGT_F64 [[A]], [[B]],
define i32 @ogt_f64(double %a, double %b) {
  %c = fcmp ogt double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'oge_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt.un{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'oge_f64'{{$}}
; MIR-LABEL:   name: oge_f64{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:     [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR-DAG:     [[Z:%[0-9]+]]:i32 = CONST_I32 0,
; MIR-DAG:     [[C:%[0-9]+]]:i32 = {{([a-z]+ )*}}CLT_UN_F64 [[A]], [[B]],
; MIR:         {{%[0-9]+}}:i32 = CEQ_I32 [[C]], [[Z]],
define i32 @oge_f64(double %a, double %b) {
  %c = fcmp oge double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'olt_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'olt_f64'{{$}}
; MIR-LABEL:   name: olt_f64{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:     [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR:         {{%[0-9]+}}:i32 = {{([a-z]+ )*}}CLT_F64 [[A]], [[B]],
define i32 @olt_f64(double %a, double %b) {
  %c = fcmp olt double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'ole_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ole_f64'{{$}}
; MIR-LABEL:   name: ole_f64{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:     [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR-DAG:     [[Z:%[0-9]+]]:i32 = CONST_I32 0,
; MIR-DAG:     [[C:%[0-9]+]]:i32 = {{([a-z]+ )*}}CGT_UN_F64 [[A]], [[B]],
; MIR:         {{%[0-9]+}}:i32 = CEQ_I32 [[C]], [[Z]],
define i32 @ole_f64(double %a, double %b) {
  %c = fcmp ole double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'one_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  cgt{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  clt{{$}}
; O2-NEXT:     {{^}}  or{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0-DAG:      {{^}}  cgt{{$}}
; O0-DAG:      {{^}}  clt{{$}}
; O0:          {{^}}  or{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'one_f64'{{$}}
define i32 @one_f64(double %a, double %b) {
  %c = fcmp one double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'ord_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  and{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0-DAG:      {{^}}  ceq{{$}}
; O0-DAG:      {{^}}  ceq{{$}}
; O0:          {{^}}  and{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ord_f64'{{$}}
define i32 @ord_f64(double %a, double %b) {
  %c = fcmp ord double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'ueq_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  cgt{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  clt{{$}}
; O2-NEXT:     {{^}}  or{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.1|ldc\.i4\.0}}{{$}}
; O2-NEXT:     {{^}}  {{xor|ceq}}{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0-DAG:      {{^}}  cgt{{$}}
; O0-DAG:      {{^}}  clt{{$}}
; O0:          {{^}}  or{{$}}
; O0:          {{^}}  {{xor|ceq}}{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ueq_f64'{{$}}
define i32 @ueq_f64(double %a, double %b) {
  %c = fcmp ueq double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'ugt_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ugt_f64'{{$}}
; MIR-LABEL:   name: ugt_f64{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:     [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR:         {{%[0-9]+}}:i32 = {{([a-z]+ )*}}CGT_UN_F64 [[A]], [[B]],
define i32 @ugt_f64(double %a, double %b) {
  %c = fcmp ugt double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'uge_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'uge_f64'{{$}}
; MIR-LABEL:   name: uge_f64{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:     [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR-DAG:     [[Z:%[0-9]+]]:i32 = CONST_I32 0,
; MIR-DAG:     [[C:%[0-9]+]]:i32 = {{([a-z]+ )*}}CLT_F64 [[A]], [[B]],
; MIR:         {{%[0-9]+}}:i32 = CEQ_I32 [[C]], [[Z]],
define i32 @uge_f64(double %a, double %b) {
  %c = fcmp uge double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'ult_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ult_f64'{{$}}
; MIR-LABEL:   name: ult_f64{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:     [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR:         {{%[0-9]+}}:i32 = {{([a-z]+ )*}}CLT_UN_F64 [[A]], [[B]],
define i32 @ult_f64(double %a, double %b) {
  %c = fcmp ult double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'ule_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ule_f64'{{$}}
; MIR-LABEL:   name: ule_f64{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:     [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR-DAG:     [[Z:%[0-9]+]]:i32 = CONST_I32 0,
; MIR-DAG:     [[C:%[0-9]+]]:i32 = {{([a-z]+ )*}}CGT_F64 [[A]], [[B]],
; MIR:         {{%[0-9]+}}:i32 = CEQ_I32 [[C]], [[Z]],
define i32 @ule_f64(double %a, double %b) {
  %c = fcmp ule double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'une_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'une_f64'{{$}}
; MIR-LABEL:   name: une_f64{{$}}
; MIR-DAG:     [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:     [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR-DAG:     [[Z:%[0-9]+]]:i32 = CONST_I32 0,
; MIR-DAG:     [[C:%[0-9]+]]:i32 = {{([a-z]+ )*}}CEQ_F64 [[A]], [[B]],
; MIR:         {{%[0-9]+}}:i32 = CEQ_I32 [[C]], [[Z]],
define i32 @une_f64(double %a, double %b) {
  %c = fcmp une double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'uno_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  or{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0-DAG:      {{^}}  ceq{{$}}
; O0-DAG:      {{^}}  ceq{{$}}
; O0-DAG:      {{^}}  ceq{{$}}
; O0-DAG:      {{^}}  ceq{{$}}
; O0:          {{^}}  or{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'uno_f64'{{$}}
define i32 @uno_f64(double %a, double %b) {
  %c = fcmp uno double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'true_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'true_f64'{{$}}
define i32 @true_f64(double %a, double %b) {
  %c = fcmp true double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; ---- nnan: no NaN operands, so the ordered and unordered forms agree -----

; one and ueq are no longer expanded: with nnan they are une and oeq.
; CHECK-LABEL: .method {{.*}}'nnan_oeq_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'nnan_oeq_f64'{{$}}
define i32 @nnan_oeq_f64(double %a, double %b) {
  %c = fcmp nnan oeq double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'nnan_ueq_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'nnan_ueq_f64'{{$}}
define i32 @nnan_ueq_f64(double %a, double %b) {
  %c = fcmp nnan ueq double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'nnan_one_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'nnan_one_f64'{{$}}
define i32 @nnan_one_f64(double %a, double %b) {
  %c = fcmp nnan one double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'nnan_une_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'nnan_une_f64'{{$}}
define i32 @nnan_une_f64(double %a, double %b) {
  %c = fcmp nnan une double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'nnan_olt_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt{{(\.un)?}}{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'nnan_olt_f64'{{$}}
define i32 @nnan_olt_f64(double %a, double %b) {
  %c = fcmp nnan olt double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'nnan_ult_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt{{(\.un)?}}{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'nnan_ult_f64'{{$}}
define i32 @nnan_ult_f64(double %a, double %b) {
  %c = fcmp nnan ult double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'nnan_ogt_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt{{(\.un)?}}{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'nnan_ogt_f64'{{$}}
define i32 @nnan_ogt_f64(double %a, double %b) {
  %c = fcmp nnan ogt double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'nnan_ugt_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt{{(\.un)?}}{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'nnan_ugt_f64'{{$}}
define i32 @nnan_ugt_f64(double %a, double %b) {
  %c = fcmp nnan ugt double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'nnan_ole_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt{{(\.un)?}}{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'nnan_ole_f64'{{$}}
define i32 @nnan_ole_f64(double %a, double %b) {
  %c = fcmp nnan ole double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'nnan_ule_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  cgt{{(\.un)?}}{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'nnan_ule_f64'{{$}}
define i32 @nnan_ule_f64(double %a, double %b) {
  %c = fcmp nnan ule double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'nnan_oge_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt{{(\.un)?}}{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'nnan_oge_f64'{{$}}
define i32 @nnan_oge_f64(double %a, double %b) {
  %c = fcmp nnan oge double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'nnan_uge_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  clt{{(\.un)?}}{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'nnan_uge_f64'{{$}}
define i32 @nnan_uge_f64(double %a, double %b) {
  %c = fcmp nnan uge double %a, %b
  %r = zext i1 %c to i32
  ret i32 %r
}

; ---- sext of a compare and compares of computed values --------------------

; sext of an fcmp is -1 or 0 without a select triangle: the compare, then
; arithmetic.
; CHECK-LABEL: .method {{.*}}'sext_olt_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK:       {{^}}  clt{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_olt_f32'{{$}}
define i32 @sext_olt_f32(float %a, float %b) {
  %c = fcmp olt float %a, %b
  %r = sext i1 %c to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'sext_une_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK:       {{^}}  ceq{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_une_f64'{{$}}
define i32 @sext_une_f64(double %a, double %b) {
  %c = fcmp une double %a, %b
  %r = sext i1 %c to i32
  ret i32 %r
}

; A compare of a float32 sum stays float32.
; CHECK-LABEL: .method {{.*}}'olt_of_sum_f32'(
; CHECK-SAME:  float32, float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  clt{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'olt_of_sum_f32'{{$}}
define i32 @olt_of_sum_f32(float %a, float %b, float %c) {
  %s = fadd float %a, %b
  %t = fcmp olt float %s, %c
  %r = zext i1 %t to i32
  ret i32 %r
}

;--- select.ll
; A select whose condition is a floating-point compare: the compare produces
; the int32 condition of the select triangle (D34), for each value type and
; with expanded predicates as conditions.
source_filename = "fp-compare-select.c"

; SEL-LABEL: .method {{.*}}'sel_olt_f64'(
; SEL-SAME:  float64, float64, float64, float64) cil managed{{$}}
; SEL:       {{^}}  clt{{$}}
; SEL:       {{^}}  ret{{$}}
; SEL-NEXT:  {{^}}} // end of method 'sel_olt_f64'{{$}}
define double @sel_olt_f64(double %a, double %b, double %t, double %f) {
  %c = fcmp olt double %a, %b
  %r = select i1 %c, double %t, double %f
  ret double %r
}

; SEL-LABEL: .method {{.*}}'sel_ugt_f32'(
; SEL-SAME:  float32, float32, float32, float32) cil managed{{$}}
; SEL:       {{^}}  cgt.un{{$}}
; SEL:       {{^}}  ret{{$}}
; SEL-NEXT:  {{^}}} // end of method 'sel_ugt_f32'{{$}}
define float @sel_ugt_f32(float %a, float %b, float %t, float %f) {
  %c = fcmp ugt float %a, %b
  %r = select i1 %c, float %t, float %f
  ret float %r
}

; SEL-LABEL: .method {{.*}}'sel_une_i32'(
; SEL-SAME:  float32, float32, int32, int32) cil managed{{$}}
; SEL:       {{^}}  ceq{{$}}
; SEL:       {{^}}  ret{{$}}
; SEL-NEXT:  {{^}}} // end of method 'sel_une_i32'{{$}}
define i32 @sel_une_i32(float %a, float %b, i32 %t, i32 %f) {
  %c = fcmp une float %a, %b
  %r = select i1 %c, i32 %t, i32 %f
  ret i32 %r
}

; SEL-LABEL: .method {{.*}}'sel_uno_i64'(
; SEL-SAME:  float64, float64, native int, native int) cil managed{{$}}
; SEL:       {{^}}  ceq{{$}}
; SEL:       {{^}}  or{{$}}
; SEL:       {{^}}  ret{{$}}
; SEL-NEXT:  {{^}}} // end of method 'sel_uno_i64'{{$}}
define i64 @sel_uno_i64(double %a, double %b, i64 %t, i64 %f) {
  %c = fcmp uno double %a, %b
  %r = select i1 %c, i64 %t, i64 %f
  ret i64 %r
}

; SEL-LABEL: .method {{.*}}'sel_one_f64'(
; SEL-SAME:  float64, float64, float64, float64) cil managed{{$}}
; SEL-DAG:   {{^}}  cgt{{$}}
; SEL-DAG:   {{^}}  clt{{$}}
; SEL:       {{^}}  ret{{$}}
; SEL-NEXT:  {{^}}} // end of method 'sel_one_f64'{{$}}
define double @sel_one_f64(double %a, double %b, double %t, double %f) {
  %c = fcmp one double %a, %b
  %r = select i1 %c, double %t, double %f
  ret double %r
}

;--- strict.ll
; The constrained compares of a strictfp function (clang -ffp-model=strict):
; llvm.experimental.constrained.fcmp (quiet) and .fcmps (signaling) with every
; predicate. CIL compares raise no floating-point exceptions, so LowerStrictFP
; turns STRICT_FSETCC and STRICT_FSETCCS into the plain SETCC, and each one
; compiles to the opcodes of the plain predicate in cmp.ll (exactly at -O2;
; at -O0 the single compares exactly and the expanded predicates by their
; opcodes).
source_filename = "fp-compare-strict.c"

; ---- float32 ----------------------------------------------------------------

; STRICT-LABEL:    .method {{.*}}'fcmp_oeq_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_oeq_f32'{{$}}
define i32 @fcmp_oeq_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f32(float %a, float %b, metadata !"oeq", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_ogt_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  cgt{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_ogt_f32'{{$}}
define i32 @fcmp_ogt_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f32(float %a, float %b, metadata !"ogt", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_oge_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  clt.un{{$}}
; STRICT-NEXT:     {{^}}  ldc.i4.0{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_oge_f32'{{$}}
define i32 @fcmp_oge_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f32(float %a, float %b, metadata !"oge", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_olt_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  clt{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_olt_f32'{{$}}
define i32 @fcmp_olt_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f32(float %a, float %b, metadata !"olt", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_ole_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  cgt.un{{$}}
; STRICT-NEXT:     {{^}}  ldc.i4.0{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_ole_f32'{{$}}
define i32 @fcmp_ole_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f32(float %a, float %b, metadata !"ole", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_one_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-O2-NEXT:  {{^\{$}}
; STRICT-O2-NEXT:  {{^}}  .maxstack 3{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  cgt{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  clt{{$}}
; STRICT-O2-NEXT:  {{^}}  or{{$}}
; STRICT-O2-NEXT:  {{^}}  ret{{$}}
; STRICT-O0-DAG:   {{^}}  cgt{{$}}
; STRICT-O0-DAG:   {{^}}  clt{{$}}
; STRICT-O0:       {{^}}  or{{$}}
; STRICT-O0-NEXT:  {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_one_f32'{{$}}
define i32 @fcmp_one_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f32(float %a, float %b, metadata !"one", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_ord_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-O2-NEXT:  {{^\{$}}
; STRICT-O2-NEXT:  {{^}}  .maxstack 3{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  and{{$}}
; STRICT-O2-NEXT:  {{^}}  ret{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0:       {{^}}  and{{$}}
; STRICT-O0-NEXT:  {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_ord_f32'{{$}}
define i32 @fcmp_ord_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f32(float %a, float %b, metadata !"ord", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_ueq_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-O2-NEXT:  {{^\{$}}
; STRICT-O2-NEXT:  {{^}}  .maxstack 3{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  cgt{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  clt{{$}}
; STRICT-O2-NEXT:  {{^}}  or{{$}}
; STRICT-O2-NEXT:  {{^}}  {{ldc\.i4\.1|ldc\.i4\.0}}{{$}}
; STRICT-O2-NEXT:  {{^}}  {{xor|ceq}}{{$}}
; STRICT-O2-NEXT:  {{^}}  ret{{$}}
; STRICT-O0-DAG:   {{^}}  cgt{{$}}
; STRICT-O0-DAG:   {{^}}  clt{{$}}
; STRICT-O0:       {{^}}  or{{$}}
; STRICT-O0:       {{^}}  {{xor|ceq}}{{$}}
; STRICT-O0-NEXT:  {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_ueq_f32'{{$}}
define i32 @fcmp_ueq_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f32(float %a, float %b, metadata !"ueq", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_ugt_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  cgt.un{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_ugt_f32'{{$}}
define i32 @fcmp_ugt_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f32(float %a, float %b, metadata !"ugt", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_uge_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  clt{{$}}
; STRICT-NEXT:     {{^}}  ldc.i4.0{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_uge_f32'{{$}}
define i32 @fcmp_uge_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f32(float %a, float %b, metadata !"uge", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_ult_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  clt.un{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_ult_f32'{{$}}
define i32 @fcmp_ult_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f32(float %a, float %b, metadata !"ult", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_ule_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  cgt{{$}}
; STRICT-NEXT:     {{^}}  ldc.i4.0{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_ule_f32'{{$}}
define i32 @fcmp_ule_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f32(float %a, float %b, metadata !"ule", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_une_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ldc.i4.0{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_une_f32'{{$}}
define i32 @fcmp_une_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f32(float %a, float %b, metadata !"une", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_uno_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-O2-NEXT:  {{^\{$}}
; STRICT-O2-NEXT:  {{^}}  .maxstack 3{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  ldc.i4.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  ldc.i4.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  or{{$}}
; STRICT-O2-NEXT:  {{^}}  ret{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0:       {{^}}  or{{$}}
; STRICT-O0-NEXT:  {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_uno_f32'{{$}}
define i32 @fcmp_uno_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f32(float %a, float %b, metadata !"uno", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_oeq_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_oeq_f32'{{$}}
define i32 @fcmps_oeq_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f32(float %a, float %b, metadata !"oeq", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_ogt_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  cgt{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_ogt_f32'{{$}}
define i32 @fcmps_ogt_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f32(float %a, float %b, metadata !"ogt", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_oge_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  clt.un{{$}}
; STRICT-NEXT:     {{^}}  ldc.i4.0{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_oge_f32'{{$}}
define i32 @fcmps_oge_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f32(float %a, float %b, metadata !"oge", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_olt_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  clt{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_olt_f32'{{$}}
define i32 @fcmps_olt_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f32(float %a, float %b, metadata !"olt", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_ole_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  cgt.un{{$}}
; STRICT-NEXT:     {{^}}  ldc.i4.0{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_ole_f32'{{$}}
define i32 @fcmps_ole_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f32(float %a, float %b, metadata !"ole", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_one_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-O2-NEXT:  {{^\{$}}
; STRICT-O2-NEXT:  {{^}}  .maxstack 3{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  cgt{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  clt{{$}}
; STRICT-O2-NEXT:  {{^}}  or{{$}}
; STRICT-O2-NEXT:  {{^}}  ret{{$}}
; STRICT-O0-DAG:   {{^}}  cgt{{$}}
; STRICT-O0-DAG:   {{^}}  clt{{$}}
; STRICT-O0:       {{^}}  or{{$}}
; STRICT-O0-NEXT:  {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_one_f32'{{$}}
define i32 @fcmps_one_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f32(float %a, float %b, metadata !"one", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_ord_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-O2-NEXT:  {{^\{$}}
; STRICT-O2-NEXT:  {{^}}  .maxstack 3{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  and{{$}}
; STRICT-O2-NEXT:  {{^}}  ret{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0:       {{^}}  and{{$}}
; STRICT-O0-NEXT:  {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_ord_f32'{{$}}
define i32 @fcmps_ord_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f32(float %a, float %b, metadata !"ord", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_ueq_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-O2-NEXT:  {{^\{$}}
; STRICT-O2-NEXT:  {{^}}  .maxstack 3{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  cgt{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  clt{{$}}
; STRICT-O2-NEXT:  {{^}}  or{{$}}
; STRICT-O2-NEXT:  {{^}}  {{ldc\.i4\.1|ldc\.i4\.0}}{{$}}
; STRICT-O2-NEXT:  {{^}}  {{xor|ceq}}{{$}}
; STRICT-O2-NEXT:  {{^}}  ret{{$}}
; STRICT-O0-DAG:   {{^}}  cgt{{$}}
; STRICT-O0-DAG:   {{^}}  clt{{$}}
; STRICT-O0:       {{^}}  or{{$}}
; STRICT-O0:       {{^}}  {{xor|ceq}}{{$}}
; STRICT-O0-NEXT:  {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_ueq_f32'{{$}}
define i32 @fcmps_ueq_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f32(float %a, float %b, metadata !"ueq", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_ugt_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  cgt.un{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_ugt_f32'{{$}}
define i32 @fcmps_ugt_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f32(float %a, float %b, metadata !"ugt", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_uge_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  clt{{$}}
; STRICT-NEXT:     {{^}}  ldc.i4.0{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_uge_f32'{{$}}
define i32 @fcmps_uge_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f32(float %a, float %b, metadata !"uge", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_ult_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  clt.un{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_ult_f32'{{$}}
define i32 @fcmps_ult_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f32(float %a, float %b, metadata !"ult", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_ule_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  cgt{{$}}
; STRICT-NEXT:     {{^}}  ldc.i4.0{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_ule_f32'{{$}}
define i32 @fcmps_ule_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f32(float %a, float %b, metadata !"ule", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_une_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ldc.i4.0{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_une_f32'{{$}}
define i32 @fcmps_une_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f32(float %a, float %b, metadata !"une", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_uno_f32'(
; STRICT-SAME:     float32, float32) cil managed{{$}}
; STRICT-O2-NEXT:  {{^\{$}}
; STRICT-O2-NEXT:  {{^}}  .maxstack 3{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  ldc.i4.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  ldc.i4.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  or{{$}}
; STRICT-O2-NEXT:  {{^}}  ret{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0:       {{^}}  or{{$}}
; STRICT-O0-NEXT:  {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_uno_f32'{{$}}
define i32 @fcmps_uno_f32(float %a, float %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f32(float %a, float %b, metadata !"uno", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; ---- float64 ----------------------------------------------------------------

; STRICT-LABEL:    .method {{.*}}'fcmp_oeq_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_oeq_f64'{{$}}
define i32 @fcmp_oeq_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f64(double %a, double %b, metadata !"oeq", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_ogt_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  cgt{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_ogt_f64'{{$}}
define i32 @fcmp_ogt_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f64(double %a, double %b, metadata !"ogt", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_oge_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  clt.un{{$}}
; STRICT-NEXT:     {{^}}  ldc.i4.0{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_oge_f64'{{$}}
define i32 @fcmp_oge_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f64(double %a, double %b, metadata !"oge", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_olt_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  clt{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_olt_f64'{{$}}
define i32 @fcmp_olt_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f64(double %a, double %b, metadata !"olt", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_ole_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  cgt.un{{$}}
; STRICT-NEXT:     {{^}}  ldc.i4.0{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_ole_f64'{{$}}
define i32 @fcmp_ole_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f64(double %a, double %b, metadata !"ole", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_one_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-O2-NEXT:  {{^\{$}}
; STRICT-O2-NEXT:  {{^}}  .maxstack 3{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  cgt{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  clt{{$}}
; STRICT-O2-NEXT:  {{^}}  or{{$}}
; STRICT-O2-NEXT:  {{^}}  ret{{$}}
; STRICT-O0-DAG:   {{^}}  cgt{{$}}
; STRICT-O0-DAG:   {{^}}  clt{{$}}
; STRICT-O0:       {{^}}  or{{$}}
; STRICT-O0-NEXT:  {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_one_f64'{{$}}
define i32 @fcmp_one_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f64(double %a, double %b, metadata !"one", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_ord_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-O2-NEXT:  {{^\{$}}
; STRICT-O2-NEXT:  {{^}}  .maxstack 3{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  and{{$}}
; STRICT-O2-NEXT:  {{^}}  ret{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0:       {{^}}  and{{$}}
; STRICT-O0-NEXT:  {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_ord_f64'{{$}}
define i32 @fcmp_ord_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f64(double %a, double %b, metadata !"ord", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_ueq_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-O2-NEXT:  {{^\{$}}
; STRICT-O2-NEXT:  {{^}}  .maxstack 3{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  cgt{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  clt{{$}}
; STRICT-O2-NEXT:  {{^}}  or{{$}}
; STRICT-O2-NEXT:  {{^}}  {{ldc\.i4\.1|ldc\.i4\.0}}{{$}}
; STRICT-O2-NEXT:  {{^}}  {{xor|ceq}}{{$}}
; STRICT-O2-NEXT:  {{^}}  ret{{$}}
; STRICT-O0-DAG:   {{^}}  cgt{{$}}
; STRICT-O0-DAG:   {{^}}  clt{{$}}
; STRICT-O0:       {{^}}  or{{$}}
; STRICT-O0:       {{^}}  {{xor|ceq}}{{$}}
; STRICT-O0-NEXT:  {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_ueq_f64'{{$}}
define i32 @fcmp_ueq_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f64(double %a, double %b, metadata !"ueq", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_ugt_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  cgt.un{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_ugt_f64'{{$}}
define i32 @fcmp_ugt_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f64(double %a, double %b, metadata !"ugt", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_uge_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  clt{{$}}
; STRICT-NEXT:     {{^}}  ldc.i4.0{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_uge_f64'{{$}}
define i32 @fcmp_uge_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f64(double %a, double %b, metadata !"uge", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_ult_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  clt.un{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_ult_f64'{{$}}
define i32 @fcmp_ult_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f64(double %a, double %b, metadata !"ult", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_ule_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  cgt{{$}}
; STRICT-NEXT:     {{^}}  ldc.i4.0{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_ule_f64'{{$}}
define i32 @fcmp_ule_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f64(double %a, double %b, metadata !"ule", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_une_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ldc.i4.0{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_une_f64'{{$}}
define i32 @fcmp_une_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f64(double %a, double %b, metadata !"une", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmp_uno_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-O2-NEXT:  {{^\{$}}
; STRICT-O2-NEXT:  {{^}}  .maxstack 3{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  ldc.i4.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  ldc.i4.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  or{{$}}
; STRICT-O2-NEXT:  {{^}}  ret{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0:       {{^}}  or{{$}}
; STRICT-O0-NEXT:  {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmp_uno_f64'{{$}}
define i32 @fcmp_uno_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmp.f64(double %a, double %b, metadata !"uno", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_oeq_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_oeq_f64'{{$}}
define i32 @fcmps_oeq_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f64(double %a, double %b, metadata !"oeq", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_ogt_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  cgt{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_ogt_f64'{{$}}
define i32 @fcmps_ogt_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f64(double %a, double %b, metadata !"ogt", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_oge_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  clt.un{{$}}
; STRICT-NEXT:     {{^}}  ldc.i4.0{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_oge_f64'{{$}}
define i32 @fcmps_oge_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f64(double %a, double %b, metadata !"oge", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_olt_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  clt{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_olt_f64'{{$}}
define i32 @fcmps_olt_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f64(double %a, double %b, metadata !"olt", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_ole_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  cgt.un{{$}}
; STRICT-NEXT:     {{^}}  ldc.i4.0{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_ole_f64'{{$}}
define i32 @fcmps_ole_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f64(double %a, double %b, metadata !"ole", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_one_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-O2-NEXT:  {{^\{$}}
; STRICT-O2-NEXT:  {{^}}  .maxstack 3{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  cgt{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  clt{{$}}
; STRICT-O2-NEXT:  {{^}}  or{{$}}
; STRICT-O2-NEXT:  {{^}}  ret{{$}}
; STRICT-O0-DAG:   {{^}}  cgt{{$}}
; STRICT-O0-DAG:   {{^}}  clt{{$}}
; STRICT-O0:       {{^}}  or{{$}}
; STRICT-O0-NEXT:  {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_one_f64'{{$}}
define i32 @fcmps_one_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f64(double %a, double %b, metadata !"one", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_ord_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-O2-NEXT:  {{^\{$}}
; STRICT-O2-NEXT:  {{^}}  .maxstack 3{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  and{{$}}
; STRICT-O2-NEXT:  {{^}}  ret{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0:       {{^}}  and{{$}}
; STRICT-O0-NEXT:  {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_ord_f64'{{$}}
define i32 @fcmps_ord_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f64(double %a, double %b, metadata !"ord", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_ueq_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-O2-NEXT:  {{^\{$}}
; STRICT-O2-NEXT:  {{^}}  .maxstack 3{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  cgt{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  clt{{$}}
; STRICT-O2-NEXT:  {{^}}  or{{$}}
; STRICT-O2-NEXT:  {{^}}  {{ldc\.i4\.1|ldc\.i4\.0}}{{$}}
; STRICT-O2-NEXT:  {{^}}  {{xor|ceq}}{{$}}
; STRICT-O2-NEXT:  {{^}}  ret{{$}}
; STRICT-O0-DAG:   {{^}}  cgt{{$}}
; STRICT-O0-DAG:   {{^}}  clt{{$}}
; STRICT-O0:       {{^}}  or{{$}}
; STRICT-O0:       {{^}}  {{xor|ceq}}{{$}}
; STRICT-O0-NEXT:  {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_ueq_f64'{{$}}
define i32 @fcmps_ueq_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f64(double %a, double %b, metadata !"ueq", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_ugt_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  cgt.un{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_ugt_f64'{{$}}
define i32 @fcmps_ugt_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f64(double %a, double %b, metadata !"ugt", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_uge_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  clt{{$}}
; STRICT-NEXT:     {{^}}  ldc.i4.0{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_uge_f64'{{$}}
define i32 @fcmps_uge_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f64(double %a, double %b, metadata !"uge", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_ult_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  clt.un{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_ult_f64'{{$}}
define i32 @fcmps_ult_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f64(double %a, double %b, metadata !"ult", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_ule_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  cgt{{$}}
; STRICT-NEXT:     {{^}}  ldc.i4.0{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_ule_f64'{{$}}
define i32 @fcmps_ule_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f64(double %a, double %b, metadata !"ule", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_une_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-NEXT:     {{^\{$}}
; STRICT-NEXT:     {{^}}  .maxstack 2{{$}}
; STRICT-NEXT:     {{^}}  ldarg.0{{$}}
; STRICT-NEXT:     {{^}}  ldarg.1{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ldc.i4.0{{$}}
; STRICT-NEXT:     {{^}}  ceq{{$}}
; STRICT-NEXT:     {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_une_f64'{{$}}
define i32 @fcmps_une_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f64(double %a, double %b, metadata !"une", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

; STRICT-LABEL:    .method {{.*}}'fcmps_uno_f64'(
; STRICT-SAME:     float64, float64) cil managed{{$}}
; STRICT-O2-NEXT:  {{^\{$}}
; STRICT-O2-NEXT:  {{^}}  .maxstack 3{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  ldc.i4.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  ldarg.1{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  ldc.i4.0{{$}}
; STRICT-O2-NEXT:  {{^}}  ceq{{$}}
; STRICT-O2-NEXT:  {{^}}  or{{$}}
; STRICT-O2-NEXT:  {{^}}  ret{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0-DAG:   {{^}}  ceq{{$}}
; STRICT-O0:       {{^}}  or{{$}}
; STRICT-O0-NEXT:  {{^}}  ret{{$}}
; STRICT-NEXT:     {{^}}} // end of method 'fcmps_uno_f64'{{$}}
define i32 @fcmps_uno_f64(double %a, double %b) #0 {
  %c = call i1 @llvm.experimental.constrained.fcmps.f64(double %a, double %b, metadata !"uno", metadata !"fpexcept.strict") #0
  %r = zext i1 %c to i32
  ret i32 %r
}

declare i1 @llvm.experimental.constrained.fcmp.f32(float, float, metadata, metadata)
declare i1 @llvm.experimental.constrained.fcmps.f32(float, float, metadata, metadata)
declare i1 @llvm.experimental.constrained.fcmp.f64(double, double, metadata, metadata)
declare i1 @llvm.experimental.constrained.fcmps.f64(double, double, metadata, metadata)

attributes #0 = { strictfp }

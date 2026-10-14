; Fused floating-point compare-and-branch (ImplementationPlan.md M6.a/M6.b,
; plan 5.1 and 5.4 'Control'; recon isel.md 3.2, 5.3, 10.7 and 11.7). A
; brcond on an fcmp of two float32 or float64 values selects one fused branch
; B<cc>_{F32,F64} and prints its long form: beq (oeq), bne.un (une), blt (olt),
; blt.un (ult), ble (ole), ble.un (ule), bgt (ogt), bgt.un (ugt), bge (oge) and
; bge.un (uge). The ordered CIL branches are not taken and the .un branches
; are taken when an operand is NaN (isel.md 3.2), so each mnemonic has the
; NaN behaviour of its predicate; no compare-as-value (ceq/cgt/clt) and no
; brtrue/brfalse is left for these predicates.
;
; Three layouts are checked for every predicate:
; - '<p>_<ty>': the false block follows the entry block, so SelectionDAG keeps
;   the predicate: '<mnemonic> true-block' (plus 'br false-block' at -O0, where
;   branch folding does not run and the fall-through branch stays).
; - '<p>_<ty>_inv': the true block follows the entry block, so SelectionDAG
;   inverts the condition. The inverse of an FP predicate flips its unordered
;   bit (olt -> uge, so 'blt' becomes 'bge.un', never 'bge'), and the branch
;   goes to the false block.
; - 'loop_<p>_<ty>': a single-block loop whose exit block is not the next
;   block. ISel emits '<mnemonic> exit; br loop' (kept at -O0); at -O2 branch
;   folding reverses the condition with MSILInstrInfo::reverseBranchCondition
;   (isel.md 11.7, FP column) to make the loop branch to itself:
;   '<inverse> loop'. A wrong (integer) inverse table would print 'bge' for
;   'blt' and change the result for NaN inputs. At -O2 the entry->loop edge
;   initializes the loop value (0.0) between 'brfalse' and the loop label; the
;   store is not pinned to a local or an argument slot.
; The ord, uno, one and ueq predicates are Expand (M5): their compares are
; built from ceq/cgt/clt values and the branch tests the combined int32
; value. With nnan the predicates have no ordered/unordered flavour (SETLT,
; ...) and select the ordered mnemonic; their inverse is the ordered inverse.
; The MIR lines check the selected record and its operand order (the target
; block, then the left and the right operand) right after instruction
; selection, which is the same at -O0 and -O2.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=conv. \
; DEFINE:   --implicit-check-not=switch --implicit-check-not="{{^ +b[a-z.]+\.s }}" \
; DEFINE:   --implicit-check-not=throw
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=MIR
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "brcond-fp.c"

; ---- f32: the predicate's own mnemonic ---------------------------------------

; MIR-LABEL: name: oeq_f32{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:   [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BEQ_F32 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'oeq_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  beq @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'oeq_f32'{{$}}
define i32 @oeq_f32(float %a, float %b) {
entry:
  %c = fcmp oeq float %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: une_f32{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:   [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BNE_UN_F32 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'une_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bne.un @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'une_f32'{{$}}
define i32 @une_f32(float %a, float %b) {
entry:
  %c = fcmp une float %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: olt_f32{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:   [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BLT_F32 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'olt_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  blt @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'olt_f32'{{$}}
define i32 @olt_f32(float %a, float %b) {
entry:
  %c = fcmp olt float %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: ult_f32{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:   [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BLT_UN_F32 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ult_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  blt.un @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ult_f32'{{$}}
define i32 @ult_f32(float %a, float %b) {
entry:
  %c = fcmp ult float %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: ole_f32{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:   [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BLE_F32 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ole_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ble @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ole_f32'{{$}}
define i32 @ole_f32(float %a, float %b) {
entry:
  %c = fcmp ole float %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: ule_f32{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:   [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BLE_UN_F32 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ule_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ble.un @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ule_f32'{{$}}
define i32 @ule_f32(float %a, float %b) {
entry:
  %c = fcmp ule float %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: ogt_f32{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:   [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BGT_F32 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ogt_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bgt @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ogt_f32'{{$}}
define i32 @ogt_f32(float %a, float %b) {
entry:
  %c = fcmp ogt float %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: ugt_f32{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:   [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BGT_UN_F32 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ugt_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bgt.un @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ugt_f32'{{$}}
define i32 @ugt_f32(float %a, float %b) {
entry:
  %c = fcmp ugt float %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: oge_f32{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:   [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BGE_F32 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'oge_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bge @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'oge_f32'{{$}}
define i32 @oge_f32(float %a, float %b) {
entry:
  %c = fcmp oge float %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: uge_f32{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:   [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BGE_UN_F32 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'uge_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bge.un @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'uge_f32'{{$}}
define i32 @uge_f32(float %a, float %b) {
entry:
  %c = fcmp uge float %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; ---- f32: the true block is next, so the inverse is selected -----------------

; CHECK-LABEL: .method {{.*}}'olt_f32_inv'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bge.un @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'olt_f32_inv'{{$}}
define i32 @olt_f32_inv(float %a, float %b) {
entry:
  %c = fcmp olt float %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'ule_f32_inv'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bgt @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ule_f32_inv'{{$}}
define i32 @ule_f32_inv(float %a, float %b) {
entry:
  %c = fcmp ule float %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'ogt_f32_inv'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ble.un @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ogt_f32_inv'{{$}}
define i32 @ogt_f32_inv(float %a, float %b) {
entry:
  %c = fcmp ogt float %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'uge_f32_inv'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  blt @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'uge_f32_inv'{{$}}
define i32 @uge_f32_inv(float %a, float %b) {
entry:
  %c = fcmp uge float %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; ---- f64: the predicate's own mnemonic ---------------------------------------

; MIR-LABEL: name: oeq_f64{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:   [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BEQ_F64 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'oeq_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  beq @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'oeq_f64'{{$}}
define i32 @oeq_f64(double %a, double %b) {
entry:
  %c = fcmp oeq double %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: une_f64{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:   [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BNE_UN_F64 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'une_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bne.un @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'une_f64'{{$}}
define i32 @une_f64(double %a, double %b) {
entry:
  %c = fcmp une double %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: olt_f64{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:   [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BLT_F64 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'olt_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  blt @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'olt_f64'{{$}}
define i32 @olt_f64(double %a, double %b) {
entry:
  %c = fcmp olt double %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: ult_f64{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:   [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BLT_UN_F64 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ult_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  blt.un @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ult_f64'{{$}}
define i32 @ult_f64(double %a, double %b) {
entry:
  %c = fcmp ult double %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: ole_f64{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:   [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BLE_F64 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ole_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ble @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ole_f64'{{$}}
define i32 @ole_f64(double %a, double %b) {
entry:
  %c = fcmp ole double %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: ule_f64{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:   [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BLE_UN_F64 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ule_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ble.un @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ule_f64'{{$}}
define i32 @ule_f64(double %a, double %b) {
entry:
  %c = fcmp ule double %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: ogt_f64{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:   [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BGT_F64 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ogt_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bgt @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ogt_f64'{{$}}
define i32 @ogt_f64(double %a, double %b) {
entry:
  %c = fcmp ogt double %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: ugt_f64{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:   [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BGT_UN_F64 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ugt_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bgt.un @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ugt_f64'{{$}}
define i32 @ugt_f64(double %a, double %b) {
entry:
  %c = fcmp ugt double %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: oge_f64{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:   [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BGE_F64 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'oge_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bge @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'oge_f64'{{$}}
define i32 @oge_f64(double %a, double %b) {
entry:
  %c = fcmp oge double %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: uge_f64{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:   [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BGE_UN_F64 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'uge_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bge.un @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'uge_f64'{{$}}
define i32 @uge_f64(double %a, double %b) {
entry:
  %c = fcmp uge double %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; ---- f64: the true block is next, so the inverse is selected -----------------

; CHECK-LABEL: .method {{.*}}'oeq_f64_inv'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bne.un @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'oeq_f64_inv'{{$}}
define i32 @oeq_f64_inv(double %a, double %b) {
entry:
  %c = fcmp oeq double %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'une_f64_inv'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  beq @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'une_f64_inv'{{$}}
define i32 @une_f64_inv(double %a, double %b) {
entry:
  %c = fcmp une double %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'olt_f64_inv'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bge.un @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'olt_f64_inv'{{$}}
define i32 @olt_f64_inv(double %a, double %b) {
entry:
  %c = fcmp olt double %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'ult_f64_inv'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bge @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ult_f64_inv'{{$}}
define i32 @ult_f64_inv(double %a, double %b) {
entry:
  %c = fcmp ult double %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'ole_f64_inv'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bgt.un @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ole_f64_inv'{{$}}
define i32 @ole_f64_inv(double %a, double %b) {
entry:
  %c = fcmp ole double %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'ule_f64_inv'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bgt @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ule_f64_inv'{{$}}
define i32 @ule_f64_inv(double %a, double %b) {
entry:
  %c = fcmp ule double %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'ogt_f64_inv'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ble.un @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ogt_f64_inv'{{$}}
define i32 @ogt_f64_inv(double %a, double %b) {
entry:
  %c = fcmp ogt double %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'ugt_f64_inv'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ble @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ugt_f64_inv'{{$}}
define i32 @ugt_f64_inv(double %a, double %b) {
entry:
  %c = fcmp ugt double %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'oge_f64_inv'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  blt.un @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'oge_f64_inv'{{$}}
define i32 @oge_f64_inv(double %a, double %b) {
entry:
  %c = fcmp oge double %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'uge_f64_inv'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  blt @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'uge_f64_inv'{{$}}
define i32 @uge_f64_inv(double %a, double %b) {
entry:
  %c = fcmp uge double %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; ---- operand order: the left operand is pushed first -------------------------

; MIR-LABEL: name: olt_f32_swapped{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:   [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BLT_F32 %bb.[[T:[0-9]+]], [[B]], [[A]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'olt_f32_swapped'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  blt @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'olt_f32_swapped'{{$}}
define i32 @olt_f32_swapped(float %a, float %b) {
entry:
  %c = fcmp olt float %b, %a
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: uge_f64_swapped{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:   [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BGE_UN_F64 %bb.[[T:[0-9]+]], [[B]], [[A]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'uge_f64_swapped'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  bge.un @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'uge_f64_swapped'{{$}}
define i32 @uge_f64_swapped(double %a, double %b) {
entry:
  %c = fcmp uge double %b, %a
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; ---- nnan: no ordered/unordered flavour, ordered mnemonics -------------------

; MIR-LABEL: name: olt_nnan_f64{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR-DAG:   [[B:%[0-9]+]]:f64 = ARGUMENT_F64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BLT_F64 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'olt_nnan_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  blt @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'olt_nnan_f64'{{$}}
define i32 @olt_nnan_f64(double %a, double %b) {
entry:
  %c = fcmp nnan olt double %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; CHECK-LABEL: .method {{.*}}'olt_nnan_f64_inv'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bge @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'olt_nnan_f64_inv'{{$}}
define i32 @olt_nnan_f64_inv(double %a, double %b) {
entry:
  %c = fcmp nnan olt double %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; MIR-LABEL: name: ule_fast_f32{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR-DAG:   [[B:%[0-9]+]]:f32 = ARGUMENT_F32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BLE_F32 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ule_fast_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ble @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ule_fast_f32'{{$}}
define i32 @ule_fast_f32(float %a, float %b) {
entry:
  %c = fcmp fast ule float %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; CHECK-LABEL: .method {{.*}}'ule_fast_f32_inv'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bgt @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ule_fast_f32_inv'{{$}}
define i32 @ule_fast_f32_inv(float %a, float %b) {
entry:
  %c = fcmp fast ule float %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; ---- branch reversal in a single-block loop ----------------------------------

; CHECK-LABEL: .method {{.*}}'loop_oeq_f64'(
; CHECK-SAME:  float64, float64, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0x0000000000000000){{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  beq @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  bne.un @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0xBFF0000000000000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_oeq_f64'{{$}}
define double @loop_oeq_f64(double %step, double %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi double [ 0.0, %entry ], [ %x1, %loop ]
  %x1 = fadd double %x, %step
  %c = fcmp oeq double %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret double -1.0
exit:
  ret double %x1
}

; CHECK-LABEL: .method {{.*}}'loop_une_f64'(
; CHECK-SAME:  float64, float64, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0x0000000000000000){{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  bne.un @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  beq @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0xBFF0000000000000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_une_f64'{{$}}
define double @loop_une_f64(double %step, double %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi double [ 0.0, %entry ], [ %x1, %loop ]
  %x1 = fadd double %x, %step
  %c = fcmp une double %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret double -1.0
exit:
  ret double %x1
}

; CHECK-LABEL: .method {{.*}}'loop_olt_f64'(
; CHECK-SAME:  float64, float64, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0x0000000000000000){{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  blt @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  bge.un @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0xBFF0000000000000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_olt_f64'{{$}}
define double @loop_olt_f64(double %step, double %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi double [ 0.0, %entry ], [ %x1, %loop ]
  %x1 = fadd double %x, %step
  %c = fcmp olt double %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret double -1.0
exit:
  ret double %x1
}

; CHECK-LABEL: .method {{.*}}'loop_ult_f64'(
; CHECK-SAME:  float64, float64, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0x0000000000000000){{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  blt.un @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  bge @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0xBFF0000000000000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_ult_f64'{{$}}
define double @loop_ult_f64(double %step, double %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi double [ 0.0, %entry ], [ %x1, %loop ]
  %x1 = fadd double %x, %step
  %c = fcmp ult double %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret double -1.0
exit:
  ret double %x1
}

; CHECK-LABEL: .method {{.*}}'loop_ole_f64'(
; CHECK-SAME:  float64, float64, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0x0000000000000000){{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  ble @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  bgt.un @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0xBFF0000000000000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_ole_f64'{{$}}
define double @loop_ole_f64(double %step, double %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi double [ 0.0, %entry ], [ %x1, %loop ]
  %x1 = fadd double %x, %step
  %c = fcmp ole double %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret double -1.0
exit:
  ret double %x1
}

; CHECK-LABEL: .method {{.*}}'loop_ule_f64'(
; CHECK-SAME:  float64, float64, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0x0000000000000000){{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  ble.un @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  bgt @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0xBFF0000000000000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_ule_f64'{{$}}
define double @loop_ule_f64(double %step, double %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi double [ 0.0, %entry ], [ %x1, %loop ]
  %x1 = fadd double %x, %step
  %c = fcmp ule double %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret double -1.0
exit:
  ret double %x1
}

; CHECK-LABEL: .method {{.*}}'loop_ogt_f64'(
; CHECK-SAME:  float64, float64, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0x0000000000000000){{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  bgt @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  ble.un @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0xBFF0000000000000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_ogt_f64'{{$}}
define double @loop_ogt_f64(double %step, double %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi double [ 0.0, %entry ], [ %x1, %loop ]
  %x1 = fadd double %x, %step
  %c = fcmp ogt double %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret double -1.0
exit:
  ret double %x1
}

; CHECK-LABEL: .method {{.*}}'loop_ugt_f64'(
; CHECK-SAME:  float64, float64, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0x0000000000000000){{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  bgt.un @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  ble @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0xBFF0000000000000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_ugt_f64'{{$}}
define double @loop_ugt_f64(double %step, double %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi double [ 0.0, %entry ], [ %x1, %loop ]
  %x1 = fadd double %x, %step
  %c = fcmp ugt double %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret double -1.0
exit:
  ret double %x1
}

; CHECK-LABEL: .method {{.*}}'loop_oge_f64'(
; CHECK-SAME:  float64, float64, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0x0000000000000000){{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  bge @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  blt.un @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0xBFF0000000000000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_oge_f64'{{$}}
define double @loop_oge_f64(double %step, double %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi double [ 0.0, %entry ], [ %x1, %loop ]
  %x1 = fadd double %x, %step
  %c = fcmp oge double %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret double -1.0
exit:
  ret double %x1
}

; CHECK-LABEL: .method {{.*}}'loop_uge_f64'(
; CHECK-SAME:  float64, float64, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0x0000000000000000){{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  bge.un @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  blt @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0xBFF0000000000000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_uge_f64'{{$}}
define double @loop_uge_f64(double %step, double %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi double [ 0.0, %entry ], [ %x1, %loop ]
  %x1 = fadd double %x, %step
  %c = fcmp uge double %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret double -1.0
exit:
  ret double %x1
}

; CHECK-LABEL: .method {{.*}}'loop_oeq_f32'(
; CHECK-SAME:  float32, float32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0x00000000){{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  beq @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  bne.un @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0xBF800000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_oeq_f32'{{$}}
define float @loop_oeq_f32(float %step, float %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi float [ 0.0, %entry ], [ %x1, %loop ]
  %x1 = fadd float %x, %step
  %c = fcmp oeq float %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret float -1.0
exit:
  ret float %x1
}

; CHECK-LABEL: .method {{.*}}'loop_une_f32'(
; CHECK-SAME:  float32, float32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0x00000000){{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  bne.un @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  beq @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0xBF800000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_une_f32'{{$}}
define float @loop_une_f32(float %step, float %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi float [ 0.0, %entry ], [ %x1, %loop ]
  %x1 = fadd float %x, %step
  %c = fcmp une float %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret float -1.0
exit:
  ret float %x1
}

; CHECK-LABEL: .method {{.*}}'loop_olt_f32'(
; CHECK-SAME:  float32, float32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0x00000000){{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  blt @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  bge.un @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0xBF800000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_olt_f32'{{$}}
define float @loop_olt_f32(float %step, float %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi float [ 0.0, %entry ], [ %x1, %loop ]
  %x1 = fadd float %x, %step
  %c = fcmp olt float %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret float -1.0
exit:
  ret float %x1
}

; CHECK-LABEL: .method {{.*}}'loop_ult_f32'(
; CHECK-SAME:  float32, float32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0x00000000){{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  blt.un @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  bge @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0xBF800000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_ult_f32'{{$}}
define float @loop_ult_f32(float %step, float %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi float [ 0.0, %entry ], [ %x1, %loop ]
  %x1 = fadd float %x, %step
  %c = fcmp ult float %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret float -1.0
exit:
  ret float %x1
}

; CHECK-LABEL: .method {{.*}}'loop_ole_f32'(
; CHECK-SAME:  float32, float32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0x00000000){{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  ble @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  bgt.un @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0xBF800000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_ole_f32'{{$}}
define float @loop_ole_f32(float %step, float %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi float [ 0.0, %entry ], [ %x1, %loop ]
  %x1 = fadd float %x, %step
  %c = fcmp ole float %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret float -1.0
exit:
  ret float %x1
}

; CHECK-LABEL: .method {{.*}}'loop_ule_f32'(
; CHECK-SAME:  float32, float32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0x00000000){{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  ble.un @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  bgt @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0xBF800000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_ule_f32'{{$}}
define float @loop_ule_f32(float %step, float %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi float [ 0.0, %entry ], [ %x1, %loop ]
  %x1 = fadd float %x, %step
  %c = fcmp ule float %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret float -1.0
exit:
  ret float %x1
}

; CHECK-LABEL: .method {{.*}}'loop_ogt_f32'(
; CHECK-SAME:  float32, float32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0x00000000){{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  bgt @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  ble.un @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0xBF800000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_ogt_f32'{{$}}
define float @loop_ogt_f32(float %step, float %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi float [ 0.0, %entry ], [ %x1, %loop ]
  %x1 = fadd float %x, %step
  %c = fcmp ogt float %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret float -1.0
exit:
  ret float %x1
}

; CHECK-LABEL: .method {{.*}}'loop_ugt_f32'(
; CHECK-SAME:  float32, float32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0x00000000){{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  bgt.un @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  ble @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0xBF800000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_ugt_f32'{{$}}
define float @loop_ugt_f32(float %step, float %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi float [ 0.0, %entry ], [ %x1, %loop ]
  %x1 = fadd float %x, %step
  %c = fcmp ugt float %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret float -1.0
exit:
  ret float %x1
}

; CHECK-LABEL: .method {{.*}}'loop_oge_f32'(
; CHECK-SAME:  float32, float32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0x00000000){{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  bge @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  blt.un @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0xBF800000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_oge_f32'{{$}}
define float @loop_oge_f32(float %step, float %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi float [ 0.0, %entry ], [ %x1, %loop ]
  %x1 = fadd float %x, %step
  %c = fcmp oge float %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret float -1.0
exit:
  ret float %x1
}

; CHECK-LABEL: .method {{.*}}'loop_uge_f32'(
; CHECK-SAME:  float32, float32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0x00000000){{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  bge.un @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  blt @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0xBF800000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_uge_f32'{{$}}
define float @loop_uge_f32(float %step, float %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi float [ 0.0, %entry ], [ %x1, %loop ]
  %x1 = fadd float %x, %step
  %c = fcmp uge float %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret float -1.0
exit:
  ret float %x1
}

; ---- Expand predicates: compares as values, then a branch on the result ------
; ord = (a oeq a) and (b oeq b), uno = (a une a) or (b une b),
; one = (a ogt b) or (a olt b), ueq = not one; the branch tests the int32
; result (brtrue, or a fused int32 compare with 1).

; CHECK-LABEL: .method {{.*}}'ord_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK:       {{^}}  and{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  {{brtrue}} @BB[[F:[0-9]+]]_[[D:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[D]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ord_f64'{{$}}
define i32 @ord_f64(double %a, double %b) {
entry:
  %c = fcmp ord double %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; CHECK-LABEL: .method {{.*}}'ord_f64_inv'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK:       {{^}}  or{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  {{brtrue}} @BB[[F:[0-9]+]]_[[D:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[D]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ord_f64_inv'{{$}}
define i32 @ord_f64_inv(double %a, double %b) {
entry:
  %c = fcmp ord double %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'uno_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK:       {{^}}  or{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  {{brtrue}} @BB[[F:[0-9]+]]_[[D:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[D]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'uno_f64'{{$}}
define i32 @uno_f64(double %a, double %b) {
entry:
  %c = fcmp uno double %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; CHECK-LABEL: .method {{.*}}'uno_f64_inv'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK:       {{^}}  and{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  {{brtrue}} @BB[[F:[0-9]+]]_[[D:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[D]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'uno_f64_inv'{{$}}
define i32 @uno_f64_inv(double %a, double %b) {
entry:
  %c = fcmp uno double %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'one_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK:       {{^}}  or{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  {{brtrue}} @BB[[F:[0-9]+]]_[[D:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[D]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'one_f64'{{$}}
define i32 @one_f64(double %a, double %b) {
entry:
  %c = fcmp one double %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; CHECK-LABEL: .method {{.*}}'one_f64_inv'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK:       {{^}}  or{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  {{bne\.un|brfalse}} @BB[[F:[0-9]+]]_[[D:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[D]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'one_f64_inv'{{$}}
define i32 @one_f64_inv(double %a, double %b) {
entry:
  %c = fcmp one double %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'ueq_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK:       {{^}}  or{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  {{bne\.un|brfalse}} @BB[[F:[0-9]+]]_[[D:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[D]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ueq_f64'{{$}}
define i32 @ueq_f64(double %a, double %b) {
entry:
  %c = fcmp ueq double %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; CHECK-LABEL: .method {{.*}}'ueq_f64_inv'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK:       {{^}}  or{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  {{brtrue}} @BB[[F:[0-9]+]]_[[D:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[D]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ueq_f64_inv'{{$}}
define i32 @ueq_f64_inv(double %a, double %b) {
entry:
  %c = fcmp ueq double %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

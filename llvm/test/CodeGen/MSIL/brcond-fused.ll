; Fused integer compare-and-branch (ImplementationPlan.md M6.a/M6.b, plan 5.1
; and 5.4 'Control'; recon isel.md 5.3, 10.7 and 11.7). A brcond on an icmp of
; two int32 values or two native ints (i64 and pointers, decision D2) selects
; one fused branch B<cc>_{I32,I64} and prints its short form: beq (eq), bne.un
; (ne), blt/blt.un (slt/ult), ble/ble.un (sle/ule), bgt/bgt.un (sgt/ugt) and
; bge/bge.un (sge/uge). No compare-as-value (ceq/cgt/clt) and no brtrue or
; brfalse is left, the operands are pushed in IR order, and a 64-bit constant
; operand is a native int ('ldc.i4 N; conv.i' or 'ldc.i8 N; conv.i').
;
; Three layouts are checked:
; - '<p>_<ty>': the false block follows the entry block, so SelectionDAG keeps
;   the predicate; the fused branch goes to the true block ('br false-block'
;   stays at -O0, where branch folding does not run).
; - '<p>_<ty>_inv': the true block follows, SelectionDAG inverts the
;   condition (slt -> sge, ult -> uge, eq -> ne, ...), and the branch goes to
;   the false block.
; - 'loop_<p>_<ty>': a single-block loop whose exit block is not the next
;   block; ISel emits '<mnemonic> exit; br loop' (kept at -O0), and at -O2
;   branch folding reverses the condition with
;   MSILInstrInfo::reverseBranchCondition (isel.md 11.7, integer column) so
;   the loop branches to itself. The loop value is a product, not an
;   induction variable, so LSR leaves the exit test alone. At -O2 the
;   entry->loop edge initializes it (1) between 'brfalse' and the loop
;   label; the store is not pinned to a local or an argument slot (an int32
;   loop value can reuse the dead argument slot of %k).
; Compares with a constant keep the constant LLVM canonicalizes to (sle 10 is
; slt 11, sge 0 is sgt -1). The MIR lines check the selected record and its
; operand order (target block, left operand, right operand) after instruction
; selection, which is the same at -O0 and -O2.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=switch \
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

source_filename = "brcond-fused.c"

; ---- i32: the predicate's own mnemonic ---------------------------------------

; MIR-LABEL: name: eq_i32{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR-DAG:   [[B:%[0-9]+]]:i32 = ARGUMENT_I32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BEQ_I32 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'eq_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  beq.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'eq_i32'{{$}}
define i32 @eq_i32(i32 %a, i32 %b) {
entry:
  %c = icmp eq i32 %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: ne_i32{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR-DAG:   [[B:%[0-9]+]]:i32 = ARGUMENT_I32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BNE_UN_I32 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ne_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bne.un.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ne_i32'{{$}}
define i32 @ne_i32(i32 %a, i32 %b) {
entry:
  %c = icmp ne i32 %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: slt_i32{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR-DAG:   [[B:%[0-9]+]]:i32 = ARGUMENT_I32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BLT_I32 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'slt_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  blt.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'slt_i32'{{$}}
define i32 @slt_i32(i32 %a, i32 %b) {
entry:
  %c = icmp slt i32 %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: ult_i32{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR-DAG:   [[B:%[0-9]+]]:i32 = ARGUMENT_I32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BLT_UN_I32 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ult_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  blt.un.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ult_i32'{{$}}
define i32 @ult_i32(i32 %a, i32 %b) {
entry:
  %c = icmp ult i32 %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: sle_i32{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR-DAG:   [[B:%[0-9]+]]:i32 = ARGUMENT_I32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BLE_I32 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'sle_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ble.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sle_i32'{{$}}
define i32 @sle_i32(i32 %a, i32 %b) {
entry:
  %c = icmp sle i32 %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: ule_i32{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR-DAG:   [[B:%[0-9]+]]:i32 = ARGUMENT_I32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BLE_UN_I32 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ule_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ble.un.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ule_i32'{{$}}
define i32 @ule_i32(i32 %a, i32 %b) {
entry:
  %c = icmp ule i32 %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: sgt_i32{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR-DAG:   [[B:%[0-9]+]]:i32 = ARGUMENT_I32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BGT_I32 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'sgt_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bgt.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sgt_i32'{{$}}
define i32 @sgt_i32(i32 %a, i32 %b) {
entry:
  %c = icmp sgt i32 %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: ugt_i32{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR-DAG:   [[B:%[0-9]+]]:i32 = ARGUMENT_I32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BGT_UN_I32 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ugt_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bgt.un.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ugt_i32'{{$}}
define i32 @ugt_i32(i32 %a, i32 %b) {
entry:
  %c = icmp ugt i32 %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: sge_i32{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR-DAG:   [[B:%[0-9]+]]:i32 = ARGUMENT_I32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BGE_I32 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'sge_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bge.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sge_i32'{{$}}
define i32 @sge_i32(i32 %a, i32 %b) {
entry:
  %c = icmp sge i32 %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: uge_i32{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR-DAG:   [[B:%[0-9]+]]:i32 = ARGUMENT_I32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BGE_UN_I32 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'uge_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bge.un.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'uge_i32'{{$}}
define i32 @uge_i32(i32 %a, i32 %b) {
entry:
  %c = icmp uge i32 %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; ---- i64: the predicate's own mnemonic ---------------------------------------

; MIR-LABEL: name: eq_i64{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR-DAG:   [[B:%[0-9]+]]:i64 = ARGUMENT_I64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BEQ_I64 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'eq_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  beq.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'eq_i64'{{$}}
define i32 @eq_i64(i64 %a, i64 %b) {
entry:
  %c = icmp eq i64 %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: ne_i64{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR-DAG:   [[B:%[0-9]+]]:i64 = ARGUMENT_I64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BNE_UN_I64 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ne_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bne.un.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ne_i64'{{$}}
define i32 @ne_i64(i64 %a, i64 %b) {
entry:
  %c = icmp ne i64 %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: slt_i64{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR-DAG:   [[B:%[0-9]+]]:i64 = ARGUMENT_I64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BLT_I64 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'slt_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  blt.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'slt_i64'{{$}}
define i32 @slt_i64(i64 %a, i64 %b) {
entry:
  %c = icmp slt i64 %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: ult_i64{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR-DAG:   [[B:%[0-9]+]]:i64 = ARGUMENT_I64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BLT_UN_I64 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ult_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  blt.un.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ult_i64'{{$}}
define i32 @ult_i64(i64 %a, i64 %b) {
entry:
  %c = icmp ult i64 %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: sle_i64{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR-DAG:   [[B:%[0-9]+]]:i64 = ARGUMENT_I64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BLE_I64 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'sle_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ble.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sle_i64'{{$}}
define i32 @sle_i64(i64 %a, i64 %b) {
entry:
  %c = icmp sle i64 %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: ule_i64{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR-DAG:   [[B:%[0-9]+]]:i64 = ARGUMENT_I64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BLE_UN_I64 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ule_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ble.un.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ule_i64'{{$}}
define i32 @ule_i64(i64 %a, i64 %b) {
entry:
  %c = icmp ule i64 %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: sgt_i64{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR-DAG:   [[B:%[0-9]+]]:i64 = ARGUMENT_I64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BGT_I64 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'sgt_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bgt.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sgt_i64'{{$}}
define i32 @sgt_i64(i64 %a, i64 %b) {
entry:
  %c = icmp sgt i64 %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: ugt_i64{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR-DAG:   [[B:%[0-9]+]]:i64 = ARGUMENT_I64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BGT_UN_I64 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ugt_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bgt.un.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ugt_i64'{{$}}
define i32 @ugt_i64(i64 %a, i64 %b) {
entry:
  %c = icmp ugt i64 %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: sge_i64{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR-DAG:   [[B:%[0-9]+]]:i64 = ARGUMENT_I64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BGE_I64 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'sge_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bge.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sge_i64'{{$}}
define i32 @sge_i64(i64 %a, i64 %b) {
entry:
  %c = icmp sge i64 %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: uge_i64{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR-DAG:   [[B:%[0-9]+]]:i64 = ARGUMENT_I64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BGE_UN_I64 %bb.[[T:[0-9]+]], [[A]], [[B]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'uge_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bge.un.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'uge_i64'{{$}}
define i32 @uge_i64(i64 %a, i64 %b) {
entry:
  %c = icmp uge i64 %a, %b
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; ---- i32: the true block is next, so the inverse is selected -----------------

; CHECK-LABEL: .method {{.*}}'eq_i32_inv'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bne.un.s @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'eq_i32_inv'{{$}}
define i32 @eq_i32_inv(i32 %a, i32 %b) {
entry:
  %c = icmp eq i32 %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'ne_i32_inv'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  beq.s @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ne_i32_inv'{{$}}
define i32 @ne_i32_inv(i32 %a, i32 %b) {
entry:
  %c = icmp ne i32 %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'slt_i32_inv'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bge.s @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'slt_i32_inv'{{$}}
define i32 @slt_i32_inv(i32 %a, i32 %b) {
entry:
  %c = icmp slt i32 %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'ult_i32_inv'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bge.un.s @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ult_i32_inv'{{$}}
define i32 @ult_i32_inv(i32 %a, i32 %b) {
entry:
  %c = icmp ult i32 %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'sle_i32_inv'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bgt.s @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sle_i32_inv'{{$}}
define i32 @sle_i32_inv(i32 %a, i32 %b) {
entry:
  %c = icmp sle i32 %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'ule_i32_inv'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bgt.un.s @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ule_i32_inv'{{$}}
define i32 @ule_i32_inv(i32 %a, i32 %b) {
entry:
  %c = icmp ule i32 %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'sgt_i32_inv'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ble.s @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sgt_i32_inv'{{$}}
define i32 @sgt_i32_inv(i32 %a, i32 %b) {
entry:
  %c = icmp sgt i32 %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'ugt_i32_inv'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ble.un.s @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ugt_i32_inv'{{$}}
define i32 @ugt_i32_inv(i32 %a, i32 %b) {
entry:
  %c = icmp ugt i32 %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'sge_i32_inv'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  blt.s @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sge_i32_inv'{{$}}
define i32 @sge_i32_inv(i32 %a, i32 %b) {
entry:
  %c = icmp sge i32 %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'uge_i32_inv'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  blt.un.s @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'uge_i32_inv'{{$}}
define i32 @uge_i32_inv(i32 %a, i32 %b) {
entry:
  %c = icmp uge i32 %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; ---- i64: the true block is next, so the inverse is selected -----------------

; CHECK-LABEL: .method {{.*}}'slt_i64_inv'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bge.s @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'slt_i64_inv'{{$}}
define i32 @slt_i64_inv(i64 %a, i64 %b) {
entry:
  %c = icmp slt i64 %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'ult_i64_inv'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bge.un.s @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ult_i64_inv'{{$}}
define i32 @ult_i64_inv(i64 %a, i64 %b) {
entry:
  %c = icmp ult i64 %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'sge_i64_inv'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  blt.s @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sge_i64_inv'{{$}}
define i32 @sge_i64_inv(i64 %a, i64 %b) {
entry:
  %c = icmp sge i64 %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'uge_i64_inv'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  blt.un.s @BB[[F:[0-9]+]]_[[E:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'uge_i64_inv'{{$}}
define i32 @uge_i64_inv(i64 %a, i64 %b) {
entry:
  %c = icmp uge i64 %a, %b
  br i1 %c, label %t, label %f
t:
  ret i32 1
f:
  ret i32 0
}

; ---- operand order: the left operand is pushed first -------------------------

; MIR-LABEL: name: slt_i32_swapped{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR-DAG:   [[B:%[0-9]+]]:i32 = ARGUMENT_I32 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BLT_I32 %bb.[[T:[0-9]+]], [[B]], [[A]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'slt_i32_swapped'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  blt.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'slt_i32_swapped'{{$}}
define i32 @slt_i32_swapped(i32 %a, i32 %b) {
entry:
  %c = icmp slt i32 %b, %a
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: ugt_i64_swapped{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR-DAG:   [[B:%[0-9]+]]:i64 = ARGUMENT_I64 1,
; MIR-NOT:   {{^ *%[0-9]+:i32 = }}
; MIR:       {{^ *}}BGT_UN_I64 %bb.[[T:[0-9]+]], [[B]], [[A]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[N:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[N]].f:
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ugt_i64_swapped'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  bgt.un.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ugt_i64_swapped'{{$}}
define i32 @ugt_i64_swapped(i64 %a, i64 %b) {
entry:
  %c = icmp ugt i64 %b, %a
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; ---- a constant operand ------------------------------------------------------

; MIR-LABEL: name: slt_i32_10{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR-DAG:   [[C:%[0-9]+]]:i32 = CONST_I32 10,
; MIR:       {{^ *}}BLT_I32 %bb.[[T:[0-9]+]], [[A]], [[C]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.{{[0-9]+}}, implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'slt_i32_10'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 10{{$}}
; CHECK-NEXT:  {{^}}  blt.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'slt_i32_10'{{$}}
define i32 @slt_i32_10(i32 %a) {
entry:
  %c = icmp slt i32 %a, 10
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: sle_i32_10{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR-DAG:   [[C:%[0-9]+]]:i32 = CONST_I32 11,
; MIR:       {{^ *}}BLT_I32 %bb.[[T:[0-9]+]], [[A]], [[C]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.{{[0-9]+}}, implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'sle_i32_10'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 11{{$}}
; CHECK-NEXT:  {{^}}  blt.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sle_i32_10'{{$}}
define i32 @sle_i32_10(i32 %a) {
entry:
  %c = icmp sle i32 %a, 10
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: sge_i32_0{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR-DAG:   [[C:%[0-9]+]]:i32 = CONST_I32 -1,
; MIR:       {{^ *}}BGT_I32 %bb.[[T:[0-9]+]], [[A]], [[C]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.{{[0-9]+}}, implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'sge_i32_0'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.m1{{$}}
; CHECK-NEXT:  {{^}}  bgt.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sge_i32_0'{{$}}
define i32 @sge_i32_0(i32 %a) {
entry:
  %c = icmp sge i32 %a, 0
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: eq_i32_5{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR-DAG:   [[C:%[0-9]+]]:i32 = CONST_I32 5,
; MIR:       {{^ *}}BEQ_I32 %bb.[[T:[0-9]+]], [[A]], [[C]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.{{[0-9]+}}, implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'eq_i32_5'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.5{{$}}
; CHECK-NEXT:  {{^}}  beq.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'eq_i32_5'{{$}}
define i32 @eq_i32_5(i32 %a) {
entry:
  %c = icmp eq i32 %a, 5
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: uge_i32_7{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR-DAG:   [[C:%[0-9]+]]:i32 = CONST_I32 6,
; MIR:       {{^ *}}BGT_UN_I32 %bb.[[T:[0-9]+]], [[A]], [[C]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.{{[0-9]+}}, implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'uge_i32_7'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.6{{$}}
; CHECK-NEXT:  {{^}}  bgt.un.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'uge_i32_7'{{$}}
define i32 @uge_i32_7(i32 %a) {
entry:
  %c = icmp uge i32 %a, 7
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: ult_i64_100{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR-DAG:   [[C:%[0-9]+]]:i64 = CONST_I64 100,
; MIR:       {{^ *}}BLT_UN_I64 %bb.[[T:[0-9]+]], [[A]], [[C]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.{{[0-9]+}}, implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ult_i64_100'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4{{(\.s)?}} 100{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  blt.un.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ult_i64_100'{{$}}
define i32 @ult_i64_100(i64 %a) {
entry:
  %c = icmp ult i64 %a, 100
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: ne_i64_m2{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR-DAG:   [[C:%[0-9]+]]:i64 = CONST_I64 -2,
; MIR:       {{^ *}}BNE_UN_I64 %bb.[[T:[0-9]+]], [[A]], [[C]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.{{[0-9]+}}, implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ne_i64_m2'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4{{(\.s)?}} -2{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  bne.un.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ne_i64_m2'{{$}}
define i32 @ne_i64_m2(i64 %a) {
entry:
  %c = icmp ne i64 %a, -2
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; MIR-LABEL: name: ugt_i64_2p32{{$}}
; MIR-DAG:   [[A:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR-DAG:   [[C:%[0-9]+]]:i64 = CONST_I64 4294967296,
; MIR:       {{^ *}}BGT_UN_I64 %bb.[[T:[0-9]+]], [[A]], [[C]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.{{[0-9]+}}, implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'ugt_i64_2p32'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i8 4294967296{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  bgt.un.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ugt_i64_2p32'{{$}}
define i32 @ugt_i64_2p32(i64 %a) {
entry:
  %c = icmp ugt i64 %a, 4294967296
  br i1 %c, label %t, label %f
f:
  ret i32 0
t:
  ret i32 1
}

; ---- branch reversal in a single-block loop ----------------------------------

; CHECK-LABEL: .method {{.*}}'loop_eq_i32'(
; CHECK-SAME:  int32, int32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  beq.s @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  bne.un.s @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.m1{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_eq_i32'{{$}}
define i32 @loop_eq_i32(i32 %step, i32 %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi i32 [ 1, %entry ], [ %x1, %loop ]
  %x1 = mul i32 %x, %step
  %c = icmp eq i32 %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret i32 -1
exit:
  ret i32 %x1
}

; CHECK-LABEL: .method {{.*}}'loop_ne_i32'(
; CHECK-SAME:  int32, int32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  bne.un.s @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  beq.s @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.m1{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_ne_i32'{{$}}
define i32 @loop_ne_i32(i32 %step, i32 %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi i32 [ 1, %entry ], [ %x1, %loop ]
  %x1 = mul i32 %x, %step
  %c = icmp ne i32 %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret i32 -1
exit:
  ret i32 %x1
}

; CHECK-LABEL: .method {{.*}}'loop_slt_i32'(
; CHECK-SAME:  int32, int32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  blt.s @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  bge.s @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.m1{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_slt_i32'{{$}}
define i32 @loop_slt_i32(i32 %step, i32 %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi i32 [ 1, %entry ], [ %x1, %loop ]
  %x1 = mul i32 %x, %step
  %c = icmp slt i32 %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret i32 -1
exit:
  ret i32 %x1
}

; CHECK-LABEL: .method {{.*}}'loop_ult_i32'(
; CHECK-SAME:  int32, int32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  blt.un.s @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  bge.un.s @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.m1{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_ult_i32'{{$}}
define i32 @loop_ult_i32(i32 %step, i32 %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi i32 [ 1, %entry ], [ %x1, %loop ]
  %x1 = mul i32 %x, %step
  %c = icmp ult i32 %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret i32 -1
exit:
  ret i32 %x1
}

; CHECK-LABEL: .method {{.*}}'loop_sle_i32'(
; CHECK-SAME:  int32, int32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  ble.s @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  bgt.s @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.m1{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_sle_i32'{{$}}
define i32 @loop_sle_i32(i32 %step, i32 %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi i32 [ 1, %entry ], [ %x1, %loop ]
  %x1 = mul i32 %x, %step
  %c = icmp sle i32 %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret i32 -1
exit:
  ret i32 %x1
}

; CHECK-LABEL: .method {{.*}}'loop_ule_i32'(
; CHECK-SAME:  int32, int32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  ble.un.s @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  bgt.un.s @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.m1{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_ule_i32'{{$}}
define i32 @loop_ule_i32(i32 %step, i32 %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi i32 [ 1, %entry ], [ %x1, %loop ]
  %x1 = mul i32 %x, %step
  %c = icmp ule i32 %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret i32 -1
exit:
  ret i32 %x1
}

; CHECK-LABEL: .method {{.*}}'loop_sgt_i32'(
; CHECK-SAME:  int32, int32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  bgt.s @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  ble.s @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.m1{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_sgt_i32'{{$}}
define i32 @loop_sgt_i32(i32 %step, i32 %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi i32 [ 1, %entry ], [ %x1, %loop ]
  %x1 = mul i32 %x, %step
  %c = icmp sgt i32 %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret i32 -1
exit:
  ret i32 %x1
}

; CHECK-LABEL: .method {{.*}}'loop_ugt_i32'(
; CHECK-SAME:  int32, int32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  bgt.un.s @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  ble.un.s @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.m1{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_ugt_i32'{{$}}
define i32 @loop_ugt_i32(i32 %step, i32 %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi i32 [ 1, %entry ], [ %x1, %loop ]
  %x1 = mul i32 %x, %step
  %c = icmp ugt i32 %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret i32 -1
exit:
  ret i32 %x1
}

; CHECK-LABEL: .method {{.*}}'loop_sge_i32'(
; CHECK-SAME:  int32, int32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  bge.s @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  blt.s @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.m1{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_sge_i32'{{$}}
define i32 @loop_sge_i32(i32 %step, i32 %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi i32 [ 1, %entry ], [ %x1, %loop ]
  %x1 = mul i32 %x, %step
  %c = icmp sge i32 %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret i32 -1
exit:
  ret i32 %x1
}

; CHECK-LABEL: .method {{.*}}'loop_uge_i32'(
; CHECK-SAME:  int32, int32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  bge.un.s @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  blt.un.s @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.m1{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_uge_i32'{{$}}
define i32 @loop_uge_i32(i32 %step, i32 %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi i32 [ 1, %entry ], [ %x1, %loop ]
  %x1 = mul i32 %x, %step
  %c = icmp uge i32 %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret i32 -1
exit:
  ret i32 %x1
}

; CHECK-LABEL: .method {{.*}}'loop_eq_i64'(
; CHECK-SAME:  native int, native int, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  beq.s @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  bne.un.s @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_eq_i64'{{$}}
define i64 @loop_eq_i64(i64 %step, i64 %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi i64 [ 1, %entry ], [ %x1, %loop ]
  %x1 = mul i64 %x, %step
  %c = icmp eq i64 %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret i64 -1
exit:
  ret i64 %x1
}

; CHECK-LABEL: .method {{.*}}'loop_ne_i64'(
; CHECK-SAME:  native int, native int, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  bne.un.s @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  beq.s @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_ne_i64'{{$}}
define i64 @loop_ne_i64(i64 %step, i64 %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi i64 [ 1, %entry ], [ %x1, %loop ]
  %x1 = mul i64 %x, %step
  %c = icmp ne i64 %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret i64 -1
exit:
  ret i64 %x1
}

; CHECK-LABEL: .method {{.*}}'loop_slt_i64'(
; CHECK-SAME:  native int, native int, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  blt.s @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  bge.s @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_slt_i64'{{$}}
define i64 @loop_slt_i64(i64 %step, i64 %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi i64 [ 1, %entry ], [ %x1, %loop ]
  %x1 = mul i64 %x, %step
  %c = icmp slt i64 %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret i64 -1
exit:
  ret i64 %x1
}

; CHECK-LABEL: .method {{.*}}'loop_ult_i64'(
; CHECK-SAME:  native int, native int, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  blt.un.s @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  bge.un.s @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_ult_i64'{{$}}
define i64 @loop_ult_i64(i64 %step, i64 %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi i64 [ 1, %entry ], [ %x1, %loop ]
  %x1 = mul i64 %x, %step
  %c = icmp ult i64 %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret i64 -1
exit:
  ret i64 %x1
}

; CHECK-LABEL: .method {{.*}}'loop_sle_i64'(
; CHECK-SAME:  native int, native int, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  ble.s @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  bgt.s @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_sle_i64'{{$}}
define i64 @loop_sle_i64(i64 %step, i64 %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi i64 [ 1, %entry ], [ %x1, %loop ]
  %x1 = mul i64 %x, %step
  %c = icmp sle i64 %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret i64 -1
exit:
  ret i64 %x1
}

; CHECK-LABEL: .method {{.*}}'loop_ule_i64'(
; CHECK-SAME:  native int, native int, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  ble.un.s @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  bgt.un.s @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_ule_i64'{{$}}
define i64 @loop_ule_i64(i64 %step, i64 %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi i64 [ 1, %entry ], [ %x1, %loop ]
  %x1 = mul i64 %x, %step
  %c = icmp ule i64 %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret i64 -1
exit:
  ret i64 %x1
}

; CHECK-LABEL: .method {{.*}}'loop_sgt_i64'(
; CHECK-SAME:  native int, native int, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  bgt.s @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  ble.s @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_sgt_i64'{{$}}
define i64 @loop_sgt_i64(i64 %step, i64 %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi i64 [ 1, %entry ], [ %x1, %loop ]
  %x1 = mul i64 %x, %step
  %c = icmp sgt i64 %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret i64 -1
exit:
  ret i64 %x1
}

; CHECK-LABEL: .method {{.*}}'loop_ugt_i64'(
; CHECK-SAME:  native int, native int, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  bgt.un.s @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  ble.un.s @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_ugt_i64'{{$}}
define i64 @loop_ugt_i64(i64 %step, i64 %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi i64 [ 1, %entry ], [ %x1, %loop ]
  %x1 = mul i64 %x, %step
  %c = icmp ugt i64 %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret i64 -1
exit:
  ret i64 %x1
}

; CHECK-LABEL: .method {{.*}}'loop_sge_i64'(
; CHECK-SAME:  native int, native int, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  bge.s @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  blt.s @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_sge_i64'{{$}}
define i64 @loop_sge_i64(i64 %step, i64 %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi i64 [ 1, %entry ], [ %x1, %loop ]
  %x1 = mul i64 %x, %step
  %c = icmp sge i64 %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret i64 -1
exit:
  ret i64 %x1
}

; CHECK-LABEL: .method {{.*}}'loop_uge_i64'(
; CHECK-SAME:  native int, native int, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[MID:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[LOOP]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[LOOP:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{^}}@
; O0:          {{^}}  bge.un.s @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[LOOP]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; O0:          {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; O2:          {{^}}  blt.un.s @BB[[F]]_[[LOOP]]{{$}}
; O2-NOT:      {{^}}@
; O2:          {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_[[MID]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'loop_uge_i64'{{$}}
define i64 @loop_uge_i64(i64 %step, i64 %limit, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %loop
loop:
  %x = phi i64 [ 1, %entry ], [ %x1, %loop ]
  %x1 = mul i64 %x, %step
  %c = icmp uge i64 %limit, %x1
  br i1 %c, label %exit, label %loop
mid:
  ret i64 -1
exit:
  ret i64 %x1
}

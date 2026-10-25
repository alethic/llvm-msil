; select (ImplementationPlan.md decision D34, M4.b/M4.c; plan 5.4 'Control'
; SELECT_<RC>; recon isel.md 11.6): CIL has no select instruction, so ISD::SELECT
; on int32, native int, float32 and float64 values selects a SELECT_<RC> pseudo
; that the custom inserter (emitSelect) expands at once into a triangle: the
; block ends with 'BR_IF Sink, $cond', an empty false block falls through to
; Sink, and Sink starts with '$dst = PHI $t, ThisMBB, $f, FalseMBB'. The
; custom inserter builds the successor lists and the PHI explicitly and does
; not rely on branch analysis; -verify-machineinstrs, which checks every PHI
; against the predecessor lists, runs on every llc line here. In the CIL every
; select is a 'brtrue.s' to an '@BB<f>_<b>' label (never brfalse)
; and no SELECT pseudo survives. The only other branches are the zero tests
; of the ctlz and cttz expansions in sel_then_ctlz and sel_then_cttz, which
; branch folding may invert at -O2, so only their O2 lines accept 'brfalse'
; (and 'beq' or 'bne.un'); everywhere else --implicit-check-not=brfalse
; applies. Conditions here are integer compares or i1 values (selects on
; fcmp conditions are run by msil/test/M1/02-fp/select-fp.ll); an i1
; argument without zeroext is masked with 'ldc.i4.1; and' first. The generic
; expansions of smin, smax, umin and umax build such selects (they are
; Expand), so they become a compare plus a triangle. A select between two
; constants that differ by one, or between a power of two and zero, is
; arithmetic instead (convertSelectOfConstantsToMath, D34).
;
; The PRE lines check the operand order of SELECT_<RC> before the custom
; inserter runs (plan 5.4: the condition, then $t, then $f); the MIR lines
; check the block structure right after it (finalize-isel), including which
; value the PHI takes from which block: $t from the entry block, whose BR_IF
; jumps straight to the join, and $f from the empty false block. The CIL
; lines check the branch, its label and the code after the join at -O2 and
; -O0; branches use their shortest forms (Q26), which in these small bodies
; are always the short ones.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=brfalse \
; DEFINE:   --implicit-check-not=SELECT --implicit-check-not=call
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefixes=MIR,MIRO2 --implicit-check-not=SELECT_
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O2 -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefixes=MIR,MIRO2 --implicit-check-not=SELECT_
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=MIR --implicit-check-not=SELECT_
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -stop-before=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=PRE
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs -stop-before=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=PRE
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "select.c"

; Before the custom inserter: SELECT_I32 takes the condition, then the value
; for true, then the value for false.
; PRE-LABEL: name: select_i32
; PRE-DAG:   [[C:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; PRE-DAG:   [[T:%[0-9]+]]:i32 = ARGUMENT_I32 1,
; PRE-DAG:   [[F:%[0-9]+]]:i32 = ARGUMENT_I32 2,
; PRE:       [[COND:%[0-9]+]]:i32 = {{C[A-Z_]+}}_I32 [[C]], %{{[0-9]+}}, implicit-def dead $arguments
; PRE-NEXT:  [[R:%[0-9]+]]:i32 = SELECT_I32 [[COND]], [[T]], [[F]], implicit-def dead $arguments
; PRE-NEXT:  RETURN [[R]], implicit-def dead $arguments

; The triangle right after instruction selection: the entry block has two
; successors and ends with BR_IF to the join block; the block laid out after
; it is empty and has the join block as its only successor; the join block
; starts with the PHI, which takes $t on the edge from the entry block (the
; taken BR_IF) and $f on the edge from the false block.
; MIR-LABEL: name: select_i32
; MIR:       {{^ *}}bb.0{{[ .:]}}
; MIR-NEXT:  successors: %bb.{{[0-9]+}}{{.*}}, %bb.{{[0-9]+}}
; MIR-DAG:   [[T:%[0-9]+]]:i32 = ARGUMENT_I32 1,
; MIR-DAG:   [[F:%[0-9]+]]:i32 = ARGUMENT_I32 2,
; MIR:       BR_IF %bb.[[SINK:[0-9]+]], %{{[0-9]+}}
; MIR:       {{^ *}}bb.[[FALSE:[0-9]+]]{{[ .:]}}
; MIR-NEXT:  successors: %bb.[[SINK]]{{(\(0x[0-9a-f]+\))?$}}
; MIR-NOT:   {{^ *[%$A-Z]}}
; MIR:       {{^ *}}bb.[[SINK]]{{[ .:]}}
; MIR-NEXT:  [[R:%[0-9]+]]:i32 = PHI [[T]], %bb.0, [[F]], %bb.[[FALSE]]{{$}}
; MIR-NEXT:  RETURN [[R]], implicit-def dead $arguments

; The first function of the module has number 0, so its labels are @BB0_<b>.
; CHECK-LABEL: .method {{.*}}'select_i32'(
; CHECK-SAME:  int32, int32, int32) cil managed{{$}}
; CHECK:       {{^}}  brtrue.s @BB0_[[B:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB0_[[B]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'select_i32'{{$}}
define i32 @select_i32(i32 %c, i32 %t, i32 %f) {
  %b = icmp ne i32 %c, 0
  %r = select i1 %b, i32 %t, i32 %f
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'select_i64'(
; CHECK-SAME:  native int, native int, native int, native int) cil managed{{$}}
; CHECK:       {{^}}  clt.un{{$}}
; O2-NEXT:     {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; O0:          {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'select_i64'{{$}}
define i64 @select_i64(i64 %a, i64 %b, i64 %t, i64 %f) {
  %c = icmp ult i64 %a, %b
  %r = select i1 %c, i64 %t, i64 %f
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'select_f32'(
; CHECK-SAME:  int32, int32, float32, float32) cil managed{{$}}
; CHECK:       {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; O0:          {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'select_f32'{{$}}
define float @select_f32(i32 %a, i32 %b, float %t, float %f) {
  %c = icmp eq i32 %a, %b
  %r = select i1 %c, float %t, float %f
  ret float %r
}

; CHECK-LABEL: .method {{.*}}'select_f64'(
; CHECK-SAME:  native int, native int, float64, float64) cil managed{{$}}
; CHECK:       {{^}}  cgt{{$}}
; O2-NEXT:     {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; O0:          {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'select_f64'{{$}}
define double @select_f64(i64 %a, i64 %b, double %t, double %f) {
  %c = icmp sgt i64 %a, %b
  %r = select i1 %c, double %t, double %f
  ret double %r
}

; A zeroext i1 argument is the condition as it is.
; CHECK-LABEL: .method {{.*}}'select_ptr'(
; CHECK-SAME:  int32, native int, native int) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'select_ptr'{{$}}
define ptr @select_ptr(i1 zeroext %c, ptr %t, ptr %f) {
  %r = select i1 %c, ptr %t, ptr %f
  ret ptr %r
}

; An i1 argument without zeroext may have garbage in its upper bits.
; CHECK-LABEL: .method {{.*}}'select_i1_noext'(
; CHECK-SAME:  int32, int32, int32) cil managed{{$}}
; O2:          {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  and{{$}}
; O2-NEXT:     {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; O0:          {{^}}  ldc.i4.1{{$}}
; O0-NEXT:     {{^}}  and{{$}}
; O0:          {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'select_i1_noext'{{$}}
define i32 @select_i1_noext(i1 %c, i32 %t, i32 %f) {
  %r = select i1 %c, i32 %t, i32 %f
  ret i32 %r
}

; Constants that need a select: both are materialized, then the triangle.
; CHECK-LABEL: .method {{.*}}'select_consts'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-DAG:   {{^}}  ldc.i4.s 100{{$}}
; CHECK-DAG:   {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; CHECK-DAG:   {{^}}  ldc.i4.7{{$}}
; CHECK:       {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'select_consts'{{$}}
define i32 @select_consts(i1 zeroext %c) {
  %r = select i1 %c, i32 100, i32 7
  ret i32 %r
}

; The code after the select continues in the join block.
; CHECK-LABEL: .method {{.*}}'select_then_add'(
; CHECK-SAME:  int32, int32, int32) cil managed{{$}}
; CHECK:       {{^}}  clt{{$}}
; O2-NEXT:     {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; O0:          {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'select_then_add'{{$}}
define i32 @select_then_add(i32 %a, i32 %b, i32 %c) {
  %s = icmp slt i32 %a, %b
  %m = select i1 %s, i32 %a, i32 %b
  %r = add i32 %m, %c
  ret i32 %r
}

; Two selects in a row: two triangles, the second one inside the first join
; block.
; CHECK-LABEL: .method {{.*}}'two_selects'(
; CHECK-SAME:  int32, int32, int32, int32) cil managed{{$}}
; CHECK:       {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B1:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[B1]]:{{$}}
; CHECK:       {{^}}  brtrue.s @BB[[F]]_[[B2:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[B2]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'two_selects'{{$}}
define i32 @two_selects(i32 %a, i32 %b, i32 %c, i32 %d) {
  %c1 = icmp slt i32 %a, %b
  %x = select i1 %c1, i32 %a, i32 %b
  %c2 = icmp ugt i32 %x, %c
  %y = select i1 %c2, i32 %x, i32 %d
  ret i32 %y
}

; ---- a select in a block that already has successors -----------------------

; At -O1 and above CodeGenPrepare wraps a ctlz or cttz whose is_zero_poison
; operand is false in a zero test (section 12, 'D16 clarified'), so the
; select's block already ends with a branch to cond.end and cond.false when
; the custom inserter splits it. The select's block then keeps only the BR_IF
; to the join block and has the false block and the join block as its two
; successors; the join block, which starts with the PHI of $t and $f, takes
; over the zero test (BR_UNLESS of the int32 value, or BEQ_I64 of the native
; int value and 0, to cond.end), the BR to cond.false and the two successors
; of the original block, and the PHI in cond.end names the join block as its
; predecessor, not the entry block (MIRO2). In the CIL the select's brtrue and
; label stay as they are. Branch folding and tail duplication turn the zero
; test into one short conditional branch and copy cond.end, which returns
; the width, into its predecessor, so both paths end in 'ret' and the method
; has an early return. The O2 lines check that branch, then the width with its
; 'ret' and the other path's 'ret' in either order (CHECK-DAG; the first
; pattern spans two or three lines), and that the method ends right after the
; later 'ret'; they fix neither the branch sense nor the block order. At -O0
; the ctlz and cttz expansions are straight-line code.
; MIRO2-LABEL: name: sel_then_ctlz
; MIRO2:       {{^ *}}bb.0{{[ .:]}}
; MIRO2-NEXT:  successors: %bb.{{[0-9]+}}{{(\(0x[0-9a-f]+\))?}}, %bb.{{[0-9]+}}{{(\(0x[0-9a-f]+\))?$}}
; MIRO2-DAG:   [[T:%[0-9]+]]:i32 = ARGUMENT_I32 1,
; MIRO2-DAG:   [[F:%[0-9]+]]:i32 = ARGUMENT_I32 2,
; MIRO2:       {{^ *}}BR_IF %bb.[[SINK:[0-9]+]], %{{[0-9]+}}
; MIRO2-NOT:   {{^ *[%$A-Z]}}
; MIRO2:       {{^ *}}bb.[[FALSE:[0-9]+]]{{[ .:]}}
; MIRO2-NEXT:  successors: %bb.[[SINK]]{{(\(0x[0-9a-f]+\))?$}}
; MIRO2-NOT:   {{^ *[%$A-Z]}}
; MIRO2:       {{^ *}}bb.[[SINK]]{{[ .:]}}
; MIRO2-NEXT:  successors: %bb.[[END:[0-9]+]]{{(\(0x[0-9a-f]+\))?}}, %bb.[[CF:[0-9]+]]{{(\(0x[0-9a-f]+\))?$}}
; MIRO2-NOT:   {{^ *}}bb.
; MIRO2:       {{^ *}}%{{[0-9]+}}:i32 = PHI [[T]], %bb.0, [[F]], %bb.[[FALSE]]{{$}}
; MIRO2-NOT:   {{^ *}}bb.
; MIRO2:       {{^ *}}BR_UNLESS %bb.[[END]], %{{[0-9]+}}
; MIRO2-NEXT:  {{^ *}}BR %bb.[[CF]],
; MIRO2-NOT:   {{^ *[%$A-Z]}}
; MIRO2:       {{^ *}}bb.[[CF]].cond.false:
; MIRO2:       {{^ *}}bb.[[END]].cond.end:
; MIRO2-NEXT:  {{^ *}}%{{[0-9]+}}:i32 = PHI %{{[0-9]+}}, %bb.[[SINK]], %{{[0-9]+}}, %bb.[[CF]]{{$}}
; MIRO2-NEXT:  RETURN

; CHECK-LABEL: .method {{.*}}'sel_then_ctlz'(
; CHECK-SAME:  int32, int32, int32) cil managed{{$}}
; CHECK:       {{^}}  cgt.un{{$}}
; O2-NEXT:     {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; O0:          {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[B]]:{{$}}
; O0-NOT:      {{b(rtrue|rfalse|eq|ne|lt|le|gt|ge)|switch}}
; O0:          {{^}}  ret{{$}}
; O0-NEXT:     {{^}}} // end of method 'sel_then_ctlz'{{$}}
; O2:          {{^}}  {{brtrue|brfalse}}.s @BB[[F]]_{{[0-9]+}}{{$}}
; O2-NOT:      {{b(rtrue|rfalse|eq|ne|lt|le|gt|ge)|switch}}
; O2-DAG:      {{^}}  ldc.i4{{(\.s)?}} 32{{[[:space:]]+}}ret{{$}}
; O2-DAG:      {{^}}  ret{{$}}
; O2-NEXT:     {{^}}} // end of method 'sel_then_ctlz'{{$}}
define i32 @sel_then_ctlz(i32 %c, i32 %x, i32 %y) {
  %b = icmp ne i32 %c, 0
  %s = select i1 %b, i32 %x, i32 %y
  %z = call i32 @llvm.ctlz.i32(i32 %s, i1 false)
  ret i32 %z
}

; MIRO2-LABEL: name: sel_then_cttz
; MIRO2:       {{^ *}}bb.0{{[ .:]}}
; MIRO2-NEXT:  successors: %bb.{{[0-9]+}}{{(\(0x[0-9a-f]+\))?}}, %bb.{{[0-9]+}}{{(\(0x[0-9a-f]+\))?$}}
; MIRO2-DAG:   [[T:%[0-9]+]]:i64 = ARGUMENT_I64 1,
; MIRO2-DAG:   [[F:%[0-9]+]]:i64 = ARGUMENT_I64 2,
; MIRO2:       {{^ *}}BR_IF %bb.[[SINK:[0-9]+]], %{{[0-9]+}}
; MIRO2-NOT:   {{^ *[%$A-Z]}}
; MIRO2:       {{^ *}}bb.[[FALSE:[0-9]+]]{{[ .:]}}
; MIRO2-NEXT:  successors: %bb.[[SINK]]{{(\(0x[0-9a-f]+\))?$}}
; MIRO2-NOT:   {{^ *[%$A-Z]}}
; MIRO2:       {{^ *}}bb.[[SINK]]{{[ .:]}}
; MIRO2-NEXT:  successors: %bb.[[END:[0-9]+]]{{(\(0x[0-9a-f]+\))?}}, %bb.[[CF:[0-9]+]]{{(\(0x[0-9a-f]+\))?$}}
; MIRO2-NOT:   {{^ *}}bb.
; MIRO2:       {{^ *}}%{{[0-9]+}}:i64 = PHI [[T]], %bb.0, [[F]], %bb.[[FALSE]]{{$}}
; MIRO2-NOT:   {{^ *}}bb.
; MIRO2:       {{^ *}}BEQ_I64 %bb.[[END]], %{{[0-9]+}}, %{{[0-9]+}}
; MIRO2-NEXT:  {{^ *}}BR %bb.[[CF]],
; MIRO2-NOT:   {{^ *[%$A-Z]}}
; MIRO2:       {{^ *}}bb.[[CF]].cond.false:
; MIRO2:       {{^ *}}bb.[[END]].cond.end:
; MIRO2-NEXT:  {{^ *}}%{{[0-9]+}}:i64 = PHI %{{[0-9]+}}, %bb.[[SINK]], %{{[0-9]+}}, %bb.[[CF]]{{$}}
; MIRO2-NEXT:  RETURN

; CHECK-LABEL: .method {{.*}}'sel_then_cttz'(
; CHECK-SAME:  native int, native int, native int) cil managed{{$}}
; CHECK:       {{^}}  clt.un{{$}}
; O2-NEXT:     {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; O0:          {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[B]]:{{$}}
; O0-NOT:      {{b(rtrue|rfalse|eq|ne|lt|le|gt|ge)|switch}}
; O0:          {{^}}  ret{{$}}
; O0-NEXT:     {{^}}} // end of method 'sel_then_cttz'{{$}}
; O2:          {{^}}  {{beq|bne\.un|brtrue|brfalse}}.s @BB[[F]]_{{[0-9]+}}{{$}}
; O2-NOT:      {{b(rtrue|rfalse|eq|ne|lt|le|gt|ge)|switch}}
; O2-DAG:      {{^}}  ldc.i4{{(\.s)?}} 64{{[[:space:]]+}}conv.i{{[[:space:]]+}}ret{{$}}
; O2-DAG:      {{^}}  ret{{$}}
; O2-NEXT:     {{^}}} // end of method 'sel_then_cttz'{{$}}
define i64 @sel_then_cttz(i64 %c, i64 %x, i64 %y) {
  %b = icmp ult i64 %c, 5
  %s = select i1 %b, i64 %x, i64 %y
  %z = call i64 @llvm.cttz.i64(i64 %s, i1 false)
  ret i64 %z
}

; ---- min and max expand to a compare and a select --------------------------

; smin(a, b) is select(a < b, a, b): the PHI takes a from the entry block
; and b from the false block.
; PRE-LABEL: name: smin_i32
; PRE-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; PRE-DAG:   [[B:%[0-9]+]]:i32 = ARGUMENT_I32 1,
; PRE:       [[COND:%[0-9]+]]:i32 = CLT_I32 [[A]], [[B]], implicit-def dead $arguments
; PRE-NEXT:  [[R:%[0-9]+]]:i32 = SELECT_I32 [[COND]], [[A]], [[B]], implicit-def dead $arguments

; MIR-LABEL: name: smin_i32
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR-DAG:   [[B:%[0-9]+]]:i32 = ARGUMENT_I32 1,
; MIR:       [[COND:%[0-9]+]]:i32 = CLT_I32 [[A]], [[B]], implicit-def dead $arguments
; MIR-NEXT:  BR_IF %bb.[[SINK:[0-9]+]], [[COND]], implicit-def
; MIR:       {{^ *}}bb.[[FALSE:[0-9]+]]{{[ .:]}}
; MIR-NEXT:  successors: %bb.[[SINK]]{{(\(0x[0-9a-f]+\))?$}}
; MIR:       {{^ *}}bb.[[SINK]]{{[ .:]}}
; MIR-NEXT:  [[R:%[0-9]+]]:i32 = PHI [[A]], %bb.0, [[B]], %bb.[[FALSE]]{{$}}
; MIR-NEXT:  RETURN [[R]], implicit-def dead $arguments

; CHECK-LABEL: .method {{.*}}'smin_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK:       {{^}}  clt{{$}}
; O2-NEXT:     {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; O0:          {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'smin_i32'{{$}}
define i32 @smin_i32(i32 %a, i32 %b) {
  %r = call i32 @llvm.smin.i32(i32 %a, i32 %b)
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'smax_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK:       {{^}}  cgt{{$}}
; O2-NEXT:     {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; O0:          {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'smax_i32'{{$}}
define i32 @smax_i32(i32 %a, i32 %b) {
  %r = call i32 @llvm.smax.i32(i32 %a, i32 %b)
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'umin_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK:       {{^}}  clt.un{{$}}
; O2-NEXT:     {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; O0:          {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'umin_i32'{{$}}
define i32 @umin_i32(i32 %a, i32 %b) {
  %r = call i32 @llvm.umin.i32(i32 %a, i32 %b)
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'umax_i32'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK:       {{^}}  cgt.un{{$}}
; O2-NEXT:     {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; O0:          {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'umax_i32'{{$}}
define i32 @umax_i32(i32 %a, i32 %b) {
  %r = call i32 @llvm.umax.i32(i32 %a, i32 %b)
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'smin_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK:       {{^}}  clt{{$}}
; O2-NEXT:     {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; O0:          {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'smin_i64'{{$}}
define i64 @smin_i64(i64 %a, i64 %b) {
  %r = call i64 @llvm.smin.i64(i64 %a, i64 %b)
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'umax_i64'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK:       {{^}}  cgt.un{{$}}
; O2-NEXT:     {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; O0:          {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'umax_i64'{{$}}
define i64 @umax_i64(i64 %a, i64 %b) {
  %r = call i64 @llvm.umax.i64(i64 %a, i64 %b)
  ret i64 %r
}

; ---- selects of constants that stay arithmetic (no brtrue) -----------------

; c ? 5 : 4 is zext(c) + 4. The add may print as a disjoint 'or': %c is
; zeroext, so bit 0 never meets the 4, and the DAGCombiner turns the add into
; 'or disjoint'. D34 only requires that the select stays arithmetic.
; CHECK-LABEL: .method {{.*}}'select_consts_differ_by_one'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NOT:   brtrue
; CHECK:       {{^}}  {{add|or}}{{$}}
; CHECK-NOT:   brtrue
; CHECK:       {{^}}} // end of method 'select_consts_differ_by_one'{{$}}
define i32 @select_consts_differ_by_one(i1 zeroext %c) {
  %r = select i1 %c, i32 5, i32 4
  ret i32 %r
}

; c ? 8 : 0 is zext(c) << 3.
; CHECK-LABEL: .method {{.*}}'select_pow2_or_zero'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NOT:   brtrue
; CHECK:       {{^}}  shl{{$}}
; CHECK-NOT:   brtrue
; CHECK:       {{^}}} // end of method 'select_pow2_or_zero'{{$}}
define i32 @select_pow2_or_zero(i1 zeroext %c) {
  %r = select i1 %c, i32 8, i32 0
  ret i32 %r
}

; (a < b) ? -1 : 0 is the sign-extended compare.
; CHECK-LABEL: .method {{.*}}'select_minus_one_or_zero'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NOT:   brtrue
; CHECK:       {{^}}  clt{{$}}
; CHECK-NOT:   brtrue
; CHECK:       {{^}}} // end of method 'select_minus_one_or_zero'{{$}}
define i64 @select_minus_one_or_zero(i64 %a, i64 %b) {
  %c = icmp slt i64 %a, %b
  %r = select i1 %c, i64 -1, i64 0
  ret i64 %r
}

declare i32 @llvm.smin.i32(i32, i32)
declare i32 @llvm.smax.i32(i32, i32)
declare i32 @llvm.umin.i32(i32, i32)
declare i32 @llvm.umax.i32(i32, i32)
declare i64 @llvm.smin.i64(i64, i64)
declare i64 @llvm.umax.i64(i64, i64)
declare i32 @llvm.ctlz.i32(i32, i1)
declare i64 @llvm.cttz.i64(i64, i1)

; Branches (ImplementationPlan.md M6.a/M6.b, plan 5.1 and 5.4 'Control'; recon
; isel.md 5.3 and 10.7): 'br' selects BR ('br'), a brcond on an int32
; condition selects BR_IF ('brtrue'), and a brcond on 'x == 0' (and on the
; inverted condition SelectionDAG builds when the true block is the next
; block) selects BR_UNLESS ('brfalse'), so no compare-as-value (ceq, cgt) is
; left in front of a branch on a boolean or on an int32 zero test. A plain i1
; argument is masked first ('ldc.i4.1; and'); a zeroext one is used as it is.
; Branches print their shortest forms with '@BB<function>_<block>' labels
; (here always 'br.s', 'brtrue.s' or 'brfalse.s', Q26), and a block that is
; reached only by falling through has no label.
;
; SelectionDAG always emits the branch to the false block, even when that
; block comes next. At -O0 it stays ('br' to the next label); at -O2
; MSILInstrInfo::analyzeBranch lets branch folding remove it. A conditional
; branch on a compare that CodeGenPrepare sinks next to it (-O2) is fused
; (brcond-fused.ll); at -O0 the compare stays in its own block and the branch
; tests the int32 value it left in a local. An infinite loop ends the method
; with 'br' to its own label, which is a valid end of a method body (no
; fall-off).
;
; The MIR lines check the selected records (plan 5.4: 'BR_IF' and
; 'BR_UNLESS' take the target block, then the condition) right after
; instruction selection, which is the same at -O0 and -O2.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=switch \
; DEFINE:   --implicit-check-not=throw \
; DEFINE:   --implicit-check-not=ceq --implicit-check-not=cgt
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

source_filename = "br.c"

; An if/else on a zeroext i1. The true block comes next, so the condition is
; inverted: 'brfalse' to the else block. The join is small enough for tail
; duplication at -O2, so only the order of the two arms is checked.
; MIR-LABEL: name: diamond_zext{{$}}
; MIR:       [[C:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR:       {{^ *}}BR_UNLESS %bb.[[ELSE:[0-9]+]], [[C]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.[[THEN:[0-9]+]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[THEN]].t:
; MIR:       {{^ *}}bb.[[ELSE]].f:

; CHECK-LABEL: .method {{.*}}'diamond_zext'(
; CHECK-SAME:  int32, int32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[ELSE:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[THEN:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[THEN]]:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK:       {{^}}@BB[[F]]_[[ELSE]]:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'diamond_zext'{{$}}
define i32 @diamond_zext(i1 zeroext %c, i32 %a, i32 %b) {
entry:
  br i1 %c, label %t, label %f
t:
  %x = add i32 %a, %b
  br label %j
f:
  %y = sub i32 %a, %b
  br label %j
j:
  %r = phi i32 [ %x, %t ], [ %y, %f ]
  ret i32 %r
}

; A plain i1 argument has undefined upper bits: 'ldc.i4.1; and', then
; 'brtrue' to the true block (the false block comes next).
; MIR-LABEL: name: branch_i1{{$}}
; MIR:       [[C:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR:       [[M:%[0-9]+]]:i32 = AND_I32 [[C]],
; MIR:       {{^ *}}BR_IF %bb.[[T:[0-9]+]], [[M]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.{{[0-9]+}}, implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'branch_i1'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.3{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'branch_i1'{{$}}
define i32 @branch_i1(i1 %c) {
entry:
  br i1 %c, label %t, label %f
f:
  ret i32 3
t:
  ret i32 4
}

; 'br (not c)' on a zeroext i1 is a zero test: 'brfalse' to the true block.
; MIR-LABEL: name: branch_not_zext{{$}}
; MIR:       [[C:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR:       {{^ *}}BR_UNLESS %bb.[[T:[0-9]+]], [[C]], implicit-def dead $arguments{{$}}
; MIR-NEXT:  {{^ *}}BR %bb.{{[0-9]+}}, implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'branch_not_zext'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.3{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'branch_not_zext'{{$}}
define i32 @branch_not_zext(i1 zeroext %c) {
entry:
  %n = xor i1 %c, true
  br i1 %n, label %t, label %f
f:
  ret i32 3
t:
  ret i32 4
}

; x == 0 is 'brfalse', x != 0 is 'brtrue' (isel.md 5.3), with no constant.
; MIR-LABEL: name: eqz_i32{{$}}
; MIR:       [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR:       {{^ *}}BR_UNLESS %bb.[[T:[0-9]+]], [[A]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'eqz_i32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.3{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'eqz_i32'{{$}}
define i32 @eqz_i32(i32 %a) {
entry:
  %c = icmp eq i32 %a, 0
  br i1 %c, label %t, label %f
f:
  ret i32 3
t:
  ret i32 4
}

; MIR-LABEL: name: nez_i32{{$}}
; MIR:       [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR:       {{^ *}}BR_IF %bb.[[T:[0-9]+]], [[A]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[T]].t:

; CHECK-LABEL: .method {{.*}}'nez_i32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.3{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'nez_i32'{{$}}
define i32 @nez_i32(i32 %a) {
entry:
  %c = icmp ne i32 %a, 0
  br i1 %c, label %t, label %f
f:
  ret i32 3
t:
  ret i32 4
}

; A native int zero test: a fused 'beq'/'bne.un' against a native int 0, or
; brfalse/brtrue, which CIL also defines on a native int. Either way it is a
; single branch to the true block and no compare-as-value.
; CHECK-LABEL: .method {{.*}}'eqz_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK:       {{^}}  {{beq|brfalse}}.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'eqz_i64'{{$}}
define i32 @eqz_i64(i64 %a) {
entry:
  %c = icmp eq i64 %a, 0
  br i1 %c, label %t, label %f
f:
  ret i32 3
t:
  ret i32 4
}

; CHECK-LABEL: .method {{.*}}'nez_ptr'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK:       {{^}}  {{bne\.un|brtrue}}.s @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'nez_ptr'{{$}}
define i32 @nez_ptr(ptr %p) {
entry:
  %c = icmp ne ptr %p, null
  br i1 %c, label %t, label %f
f:
  ret i32 3
t:
  ret i32 4
}

; A condition computed in an earlier block. At -O2 CodeGenPrepare sinks the
; compare next to the branch, which fuses it ('blt'); at -O0 the int32 value
; of the compare crosses blocks in a local and the branch is 'brtrue' on it
; (masked, because the value is an i1). The polarity and target of the entry
; branch are not checked: at -O2 the critical edge entry->mid is split to hold
; the PHI copy (m = 0), and that edge block, not 'other', may come next. The
; join 'mid' is the first label after the 'mul' of 'other' either way.
; CHECK-LABEL: .method {{.*}}'cond_elsewhere'(
; CHECK-SAME:  int32, int32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  {{brfalse|brtrue}}.s @BB[[F:[0-9]+]]_{{[0-9]+}}{{$}}
; CHECK:       {{^}}  mul{{$}}
; CHECK:       {{^}}@BB[[F]]_[[MID:[0-9]+]]:{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  blt.s @BB[[F]]_[[T:[0-9]+]]{{$}}
; O0:          {{^}}  and{{$}}
; O0-NEXT:     {{^}}  brtrue.s @BB[[F]]_[[T:[0-9]+]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK:       {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'cond_elsewhere'{{$}}
define i32 @cond_elsewhere(i32 %a, i32 %b, i32 %k) {
entry:
  %c = icmp slt i32 %a, %b
  %z = icmp eq i32 %k, 0
  br i1 %z, label %mid, label %other
other:
  %v = mul i32 %a, %k
  br label %mid
mid:
  %m = phi i32 [ 0, %entry ], [ %v, %other ]
  br i1 %c, label %t, label %f
f:
  ret i32 %m
t:
  %r = add i32 %m, 1
  ret i32 %r
}

; The join is too large to duplicate, so the then-arm ends with the
; unconditional 'br.s' to it at -O0 and -O2; the else-arm falls through to it
; at -O2 and branches to it at -O0.
; CHECK-LABEL: .method {{.*}}'big_join'(
; CHECK-SAME:  int32, int32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brfalse.s @BB[[F:[0-9]+]]_[[ELSE:[0-9]+]]{{$}}
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  br.s @BB[[F]]_[[J:[0-9]+]]{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[ELSE]]:{{$}}
; CHECK:       {{^}}  sub{{$}}
; O2-NOT:      {{^}}  br{{(\.s)?}} @
; O0:          {{^}}  br.s @BB[[F]]_[[J]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[J]]:{{$}}
; CHECK:       {{^}}  mul{{$}}
; CHECK:       {{^}}  xor{{$}}
; CHECK:       {{^}}  shl{{$}}
; CHECK:       {{^}}  or{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'big_join'{{$}}
define i32 @big_join(i1 zeroext %c, i32 %a, i32 %b) {
entry:
  br i1 %c, label %t, label %f
t:
  %x = add i32 %a, %b
  br label %j
f:
  %y = sub i32 %a, %b
  br label %j
j:
  %r = phi i32 [ %x, %t ], [ %y, %f ]
  %r1 = mul i32 %r, %a
  %r2 = xor i32 %r1, %b
  %r3 = shl i32 %r2, 3
  %r4 = or i32 %r3, %a
  ret i32 %r4
}

; An infinite loop: the method ends with 'br' to the loop's own label.
; MIR-LABEL: name: spin{{$}}
; MIR:       {{^ *}}bb.[[L:[0-9]+]].loop:
; MIR-NOT:   {{^ *}}bb.
; MIR:       {{^ *}}BR %bb.[[L]], implicit-def dead $arguments{{$}}

; CHECK-LABEL: .method {{.*}}'spin'(
; CHECK-SAME:  ) cil managed{{$}}
; O0:          {{^}}  br.s @BB[[F:[0-9]+]]_[[L:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[L]]:{{$}}
; O2:          {{^}}@BB[[F:[0-9]+]]_[[L:[0-9]+]]:{{$}}
; CHECK-NEXT:  {{^}}  br.s @BB[[F]]_[[L]]{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'spin'{{$}}
define void @spin() {
entry:
  br label %loop
loop:
  br label %loop
}

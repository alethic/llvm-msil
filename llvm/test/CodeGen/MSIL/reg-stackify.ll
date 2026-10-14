; MSILRegStackify on integer code and calls (ImplementationPlan.md M3.a, M4;
; recon passes-frame.md 5 and isel.md 9), ported from the integer cases of
; llvm/test/CodeGen/WebAssembly/reg-stackify.ll (the memory, SIMD, loop and
; __stack_pointer cases wait for M6/M7). At -O2 a value with one use is moved
; next to its use and stays on the evaluation stack, so expressions become
; trees; a cheap constant used twice is rematerialized; a value whose first
; use dominates the others is kept with 'dup' plus a store (a tee), into a CIL
; local or into the slot of an argument that is dead by then (MSILRegColoring);
; operands of commutable instructions are swapped when that lets a value stay
; on the stack. Nothing with side effects moves across other side effects:
; calls keep their order and a call result that is needed after a later call
; waits in a local. Division and remainder have side effects (they throw) but
; are marked TrapsOnlyOnUB, because they throw only on undefined behaviour: a
; division may sink past a call or another division, but a call never moves
; below a division, and instruction selection keeps a division after an
; earlier call or trap (plan 5.4, 'TrapsOnlyOnUB' clarified in section 12). A
; call that does not return means the division never runs, so running the
; division first could add an exception the program does not have. The
; WebAssembly callees are declarations; here they are defined, because calls
; of declarations need P/Invoke (M8).
;
; At -O0 MSILRegStackify only stackifies a value defined right before its use,
; so -O0 is checked for the order of calls and divisions only (O0). The order
; of a call and a later division is checked at both levels (CHECK), and once
; more with the source-order scheduler (SRC), so that MSILRegStackify alone
; must keep the call first.

; The debug-value cases (DBG) are a module of their own, dbg.ll: a module with
; debug info gives every method '@func_begin<N>:' and '@func_end<N>:' labels.
; The x86_64 MSIL triples would otherwise use instruction-referencing variable
; locations, whose LiveDebugValues needs physical registers, which MSIL never
; has (section 12, 'M4.a extended': MSIL keeps DBG_VALUE). A debug value adds
; no instruction and only a '//' comment, which -asm-verbose=false suppresses,
; so the IL is the same as without it.

; RUN: split-file %s %t
; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs \
; RUN:   < %t/main.ll | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs \
; RUN:   < %t/main.ll | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs \
; RUN:   < %t/main.ll | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs \
; RUN:   < %t/main.ll | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O2 -pre-RA-sched=source -asm-verbose=false \
; RUN:   -verify-machineinstrs < %t/main.ll | FileCheck %s --check-prefix=SRC
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O2 -pre-RA-sched=source -asm-verbose=false \
; RUN:   -verify-machineinstrs < %t/main.ll | FileCheck %s --check-prefix=SRC
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil < %t/main.ll -o %t/main.o2.il \
; RUN:   && %ilasm-verify -output=%t/main.o2.dll %t/main.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/main.ll -o %t/main.o0.il \
; RUN:   && %ilasm-verify -output=%t/main.o0.dll %t/main.o0.il %}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs \
; RUN:   < %t/dbg.ll | %{check} --check-prefixes=DBG,DBGO2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs \
; RUN:   < %t/dbg.ll | %{check} --check-prefixes=DBG,DBGO2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs \
; RUN:   < %t/dbg.ll | %{check} --check-prefixes=DBG,DBGO0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs \
; RUN:   < %t/dbg.ll | %{check} --check-prefixes=DBG,DBGO0
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %t/dbg.ll -o %t/dbg.o2.il \
; RUN:   && %ilasm-verify -output=%t/dbg.o2.dll %t/dbg.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/dbg.ll -o %t/dbg.o0.il \
; RUN:   && %ilasm-verify -output=%t/dbg.o0.dll %t/dbg.o0.il %}

;--- main.ll
source_filename = "reg-stackify.c"

define void @side_effect() {
  ret void
}
define void @use_a(i32 %x) {
  ret void
}
define void @use_b(i32 %x) {
  ret void
}
define void @use_2(i32 %x, i32 %y) {
  ret void
}
define i32 @red() {
  ret i32 1
}
define i32 @green() {
  ret i32 2
}
define i32 @blue() {
  ret i32 3
}
define i32 @callee(i32 %x) {
  ret i32 %x
}
; A callee that does not return (a failed validation, say).
define i32 @f(i32 %x) noinline {
  call void @llvm.trap()
  unreachable
}
declare void @llvm.trap()

; Division can be sunk past a call: it traps only on undefined behaviour.
; CHECK-LABEL: .method {{.*}}'sink_trap'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  call void 'side_effect'(){{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  div{{$}}
; CHECK:       {{^}}} // end of method 'sink_trap'{{$}}
define i32 @sink_trap(i32 %x, i32 %y) {
  %t = sdiv i32 %x, %y
  call void @side_effect()
  ret i32 %t
}

; Divisions may also be reordered among themselves.
; CHECK-LABEL: .method {{.*}}'div_reorder'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  sub{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  sub{{$}}
; CHECK:       {{^}}} // end of method 'div_reorder'{{$}}
define i32 @div_reorder(i32 %x, i32 %y) {
  %a = sdiv i32 %x, %y
  %b = sdiv i32 %y, %x
  %s = sub i32 %b, %a
  ret i32 %s
}

; But a call never moves below a division, even when the division is the first
; operand of their common use and stackifying both into the add would need
; that: f does not return, so for y == 0 the program must stop in f, not with
; a DivideByZeroException from a division it never reaches.
; CHECK-LABEL: .method {{.*}}'call_then_div'(
; CHECK-NOT:   {{^}}  {{div|rem}}
; CHECK:       {{^}}  call int32 'f'(int32){{$}}
; CHECK:       {{^}}  div{{$}}
; CHECK:       {{^}}  add{{$}}
; CHECK:       {{^}}} // end of method 'call_then_div'{{$}}
; SRC-LABEL:   .method {{.*}}'call_then_div'(
; SRC-NOT:     {{^}}  {{div|rem}}
; SRC:         {{^}}  call int32 'f'(int32){{$}}
; SRC:         {{^}}  div{{$}}
; SRC:         {{^}}  add{{$}}
; SRC:         {{^}}} // end of method 'call_then_div'{{$}}
define i32 @call_then_div(i32 %x, i32 %y) {
  %c = call i32 @f(i32 %x)
  %d = sdiv i32 %x, %y
  %r = add i32 %d, %c
  ret i32 %r
}

; The same after a void call, for an unsigned native int remainder and for a
; signed int32 remainder: the result of the call is not even needed.
; CHECK-LABEL: .method {{.*}}'call_then_urem_i64'(
; CHECK-NOT:   {{^}}  {{div|rem}}
; CHECK:       {{^}}  call void 'use_a'(int32){{$}}
; CHECK:       {{^}}  rem.un{{$}}
; CHECK:       {{^}}} // end of method 'call_then_urem_i64'{{$}}
define i64 @call_then_urem_i64(i64 %x, i64 %y, i32 %z) {
  call void @use_a(i32 %z)
  %r = urem i64 %x, %y
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'call_then_srem'(
; CHECK-NOT:   {{^}}  {{div|rem}}
; CHECK:       {{^}}  call void 'use_a'(int32){{$}}
; CHECK:       {{^}}  rem{{$}}
; CHECK:       {{^}}} // end of method 'call_then_srem'{{$}}
define i32 @call_then_srem(i32 %x, i32 %y) {
  call void @use_a(i32 %y)
  %r = srem i32 %x, %y
  ret i32 %r
}

; And llvm.trap before a division throws before any division.
; CHECK-LABEL: .method {{.*}}'trap_then_div'(
; CHECK-NOT:   {{^}}  {{div|rem}}
; CHECK:       {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  throw{{$}}
; CHECK:       {{^}}} // end of method 'trap_then_div'{{$}}
define i32 @trap_then_div(i32 %x, i32 %y) {
  call void @llvm.trap()
  %d = sdiv i32 %x, %y
  ret i32 %d
}

; A call result is not moved across another call: it waits in a local.
; CHECK-LABEL: .method {{.*}}'no_sink_call'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  .locals (int32){{$}}
; CHECK-NEXT:  {{^}}  call int32 'red'(){{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'green'(){{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @no_sink_call(i32 %x) {
  %a = call i32 @red()
  %b = call i32 @green()
  %s = sub i32 %b, %a
  ret i32 %s
}

; The same with a call that has no result in between.
; CHECK-LABEL: .method {{.*}}'no_sink_call_past_void_call'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  .locals (int32){{$}}
; CHECK-NEXT:  {{^}}  call int32 'red'(){{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  call void 'side_effect'(){{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @no_sink_call_past_void_call() {
  %a = call i32 @red()
  call void @side_effect()
  ret i32 %a
}

; Division is already in tree order: the whole function is one tree.
; CHECK-LABEL: .method {{.*}}'div_tree'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 5{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  ldarg.3{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  ldarg.s 4{{$}}
; O2-NEXT:     {{^}}  ldarg.s 5{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  ldarg.s 6{{$}}
; O2-NEXT:     {{^}}  ldarg.s 7{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  ldarg.s 8{{$}}
; O2-NEXT:     {{^}}  ldarg.s 9{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  ldarg.s 10{{$}}
; O2-NEXT:     {{^}}  ldarg.s 11{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  ldarg.s 12{{$}}
; O2-NEXT:     {{^}}  ldarg.s 13{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  ldarg.s 14{{$}}
; O2-NEXT:     {{^}}  ldarg.s 15{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O2-NEXT:     {{^}}} // end of method 'div_tree'{{$}}
; O0:          {{^}}  div{{$}}
define i32 @div_tree(i32 %a, i32 %b, i32 %c, i32 %d, i32 %e, i32 %f, i32 %g, i32 %h, i32 %i, i32 %j, i32 %k, i32 %l, i32 %m, i32 %n, i32 %o, i32 %p) {
entry:
  %div = sdiv i32 %a, %b
  %div1 = sdiv i32 %c, %d
  %div2 = sdiv i32 %div, %div1
  %div3 = sdiv i32 %e, %f
  %div4 = sdiv i32 %g, %h
  %div5 = sdiv i32 %div3, %div4
  %div6 = sdiv i32 %div2, %div5
  %div7 = sdiv i32 %i, %j
  %div8 = sdiv i32 %k, %l
  %div9 = sdiv i32 %div7, %div8
  %div10 = sdiv i32 %m, %n
  %div11 = sdiv i32 %o, %p
  %div12 = sdiv i32 %div10, %div11
  %div13 = sdiv i32 %div9, %div12
  %div14 = sdiv i32 %div6, %div13
  ret i32 %div14
}

; An expression tree of non-trapping operations.
; CHECK-LABEL: .method {{.*}}'tree'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  ldarg.3{{$}}
; O2-NEXT:     {{^}}  sub{{$}}
; O2-NEXT:     {{^}}  mul{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O2-NEXT:     {{^}}} // end of method 'tree'{{$}}
; O0:          {{^}}  mul{{$}}
define i32 @tree(i32 %a, i32 %b, i32 %c, i32 %d) {
  %x = add i32 %a, %b
  %y = sub i32 %c, %d
  %z = mul i32 %x, %y
  ret i32 %z
}

; A constant used twice is rematerialized instead of kept in a local.
; CHECK-LABEL: .method {{.*}}'remat_const'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4 1000{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldc.i4 1000{{$}}
; O2-NEXT:     {{^}}  rem{{$}}
; O2-NEXT:     {{^}}  xor{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O2-NEXT:     {{^}}} // end of method 'remat_const'{{$}}
; O0:          {{^}}  xor{{$}}
define i32 @remat_const(i32 %x, i32 %y) {
  %a = sdiv i32 %x, 1000
  %b = srem i32 %y, 1000
  %c = xor i32 %a, %b
  ret i32 %c
}

; A simple multiple-use case: the product is kept with a tee (dup and a store
; into a local, or into the slot of the dead argument %x).
; CHECK-LABEL: .method {{.*}}'simple_multiple_use'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2:          {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  mul{{$}}
; O2-NEXT:     {{^}}  dup{{$}}
; O2-NEXT:     {{^}}  {{starg\.s 0|stloc\.0}}{{$}}
; O2-NEXT:     {{^}}  call void 'use_a'(int32){{$}}
; O2-NEXT:     {{^}}  {{ldarg\.0|ldloc\.0}}{{$}}
; O2-NEXT:     {{^}}  call void 'use_b'(int32){{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  call void 'use_a'(int32){{$}}
; O0:          {{^}}  call void 'use_b'(int32){{$}}
; CHECK:       {{^}}} // end of method 'simple_multiple_use'{{$}}
define void @simple_multiple_use(i32 %x, i32 %y) {
  %mul = mul i32 %y, %x
  call void @use_a(i32 %mul)
  call void @use_b(i32 %mul)
  ret void
}

; Multiple uses of the same value in one instruction.
; CHECK-LABEL: .method {{.*}}'multiple_uses_in_same_insn'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2:          {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  mul{{$}}
; O2-NEXT:     {{^}}  dup{{$}}
; O2-NEXT:     {{^}}  {{starg\.s 0|stloc\.0}}{{$}}
; O2-NEXT:     {{^}}  {{ldarg\.0|ldloc\.0}}{{$}}
; O2-NEXT:     {{^}}  call void 'use_2'(int32, int32){{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  call void 'use_2'(int32, int32){{$}}
; CHECK:       {{^}}} // end of method 'multiple_uses_in_same_insn'{{$}}
define void @multiple_uses_in_same_insn(i32 %x, i32 %y) {
  %mul = mul i32 %y, %x
  call void @use_2(i32 %mul, i32 %mul)
  ret void
}

; Commute operands to achieve better stackifying.
; CHECK-LABEL: .method {{.*}}'commute'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  call int32 'red'(){{$}}
; O2-NEXT:     {{^}}  call int32 'green'(){{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  call int32 'blue'(){{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  call int32 'red'(){{$}}
; O0:          {{^}}  call int32 'green'(){{$}}
; O0:          {{^}}  call int32 'blue'(){{$}}
; CHECK:       {{^}}} // end of method 'commute'{{$}}
define i32 @commute() {
  %call = call i32 @red()
  %call1 = call i32 @green()
  %add = add i32 %call1, %call
  %call2 = call i32 @blue()
  %add3 = add i32 %add, %call2
  ret i32 %add3
}

; Don't stackify a register when it would move the def of the register past
; a use of the register.
; CHECK-LABEL: .method {{.*}}'no_stackify_past_use'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  .locals (int32){{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  call int32 'callee'(int32){{$}}
; O2-NEXT:     {{^}}  stloc.0{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  call int32 'callee'(int32){{$}}
; O2-NEXT:     {{^}}  ldloc.0{{$}}
; O2-NEXT:     {{^}}  sub{{$}}
; O2-NEXT:     {{^}}  ldloc.0{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  call int32 'callee'(int32){{$}}
; O0:          {{^}}  call int32 'callee'(int32){{$}}
; O0:          {{^}}  div{{$}}
; CHECK:       {{^}}} // end of method 'no_stackify_past_use'{{$}}
define i32 @no_stackify_past_use(i32 %arg) {
  %tmp1 = call i32 @callee(i32 %arg)
  %tmp2 = add i32 %arg, 1
  %tmp3 = call i32 @callee(i32 %tmp2)
  %tmp5 = sub i32 %tmp3, %tmp1
  %tmp6 = sdiv i32 %tmp5, %tmp1
  ret i32 %tmp6
}

; The same with a commutative operator: the operands are swapped and the
; first result is kept with a tee.
; CHECK-LABEL: .method {{.*}}'commute_to_fix_ordering'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 4{{$}}
; O2-NEXT:     {{^}}  .locals (int32){{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  call int32 'callee'(int32){{$}}
; O2-NEXT:     {{^}}  dup{{$}}
; O2-NEXT:     {{^}}  stloc.0{{$}}
; O2-NEXT:     {{^}}  ldloc.0{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  call int32 'callee'(int32){{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  mul{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  call int32 'callee'(int32){{$}}
; O0:          {{^}}  call int32 'callee'(int32){{$}}
; O0:          {{^}}  mul{{$}}
; CHECK:       {{^}}} // end of method 'commute_to_fix_ordering'{{$}}
define i32 @commute_to_fix_ordering(i32 %arg) {
  %tmp1 = call i32 @callee(i32 %arg)
  %tmp2 = add i32 %arg, 1
  %tmp3 = call i32 @callee(i32 %tmp2)
  %tmp5 = add i32 %tmp3, %tmp1
  %tmp6 = mul i32 %tmp5, %tmp1
  ret i32 %tmp6
}

;--- dbg.ll
source_filename = "reg-stackify-dbg.c"

define void @use_a(i32 %x) {
  ret void
}
define void @use_b(i32 %x) {
  ret void
}

; Multiple uses in the same block with a debug value between them: the same
; code as simple_multiple_use. Unlike in the WebAssembly original, the
; instructions have locations, as clang's do: the instruction-referencing
; LiveDebugValues crash this case guards against needs a located mul.
; DBG-LABEL:   .method {{.*}}'same_block_use_with_dbg_value'(
; DBG:         {{^}}  .maxstack 2{{$}}
; DBGO2:       {{^}}  ldarg.1{{$}}
; DBGO2-NEXT:  {{^}}  ldarg.0{{$}}
; DBGO2-NEXT:  {{^}}  mul{{$}}
; DBGO2-NEXT:  {{^}}  dup{{$}}
; DBGO2-NEXT:  {{^}}  {{starg\.s 0|stloc\.0}}{{$}}
; DBGO2-NEXT:  {{^}}  call void 'use_a'(int32){{$}}
; DBGO2-NEXT:  {{^}}  {{ldarg\.0|ldloc\.0}}{{$}}
; DBGO2-NEXT:  {{^}}  call void 'use_b'(int32){{$}}
; DBGO2-NEXT:  {{^}}  ret{{$}}
; DBGO0:       {{^}}  mul{{$}}
; DBGO0:       {{^}}  call void 'use_a'(int32){{$}}
; DBGO0:       {{^}}  call void 'use_b'(int32){{$}}
; DBG:         {{^}}} // end of method 'same_block_use_with_dbg_value'{{$}}
define void @same_block_use_with_dbg_value(i32 %x, i32 %y) !dbg !4 {
  %mul = mul i32 %y, %x, !dbg !8
  call void @use_a(i32 %mul), !dbg !8
    #dbg_value(i32 %mul, !7, !DIExpression(), !8)
  call void @use_b(i32 %mul), !dbg !8
  ret void, !dbg !8
}

; A debug value of a constant in a function that only traps.
; DBG-LABEL:   .method {{.*}}'ignore_dbg_value'(
; DBG:         {{^}}  .maxstack 1{{$}}
; DBG-NEXT:    {{^}}  ldnull{{$}}
; DBG-NEXT:    {{^}}  throw{{$}}
; DBG-NOT:     {{^}}  {{[a-z]}}
; DBG:         {{^}}} // end of method 'ignore_dbg_value'{{$}}
define void @ignore_dbg_value() !dbg !9 {
    #dbg_value(i32 0, !10, !DIExpression(), !11)
  unreachable
}

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, producer: "clang", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !2)
!1 = !DIFile(filename: "reg-stackify-dbg.c", directory: "/")
!2 = !{}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = distinct !DISubprogram(name: "same_block_use_with_dbg_value", scope: !1, file: !1, line: 10, type: !5, scopeLine: 10, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !2)
!5 = !DISubroutineType(types: !2)
!6 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!7 = !DILocalVariable(name: "nzcnt", scope: !4, file: !1, line: 15, type: !6)
!8 = !DILocation(line: 15, column: 6, scope: !4)
!9 = distinct !DISubprogram(name: "ignore_dbg_value", scope: !1, file: !1, line: 20, type: !5, scopeLine: 20, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !2)
!10 = !DILocalVariable(name: "nzcnt", scope: !9, file: !1, line: 21, type: !6)
!11 = !DILocation(line: 21, column: 6, scope: !9)

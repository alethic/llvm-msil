; MSILRegStackify on integers, calls, memory and loops (ImplementationPlan.md
; M3.a, M4, M6, M7; recon passes-frame.md 5 and isel.md 9), ported from
; llvm/test/CodeGen/WebAssembly/reg-stackify.ll. At -O2 a value with one use is
; moved next to its use and stays on the evaluation stack, so expressions become
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
; Memory follows the WebAssembly rules. A load does not move across a store or
; across a call that may write memory, and such a call does not move across a
; load (no0, no_stackify_call_past_load, no_stackify_store_past_load); a
; volatile load also counts as a store with side effects (no1). A
; dereferenceable load marked !invariant.load reads memory that nothing writes
; and moves freely (yes0, store_past_invar_load). A call reads and writes
; memory unless the called function says otherwise: a call of a memory(none)
; function moves across stores, even volatile ones (sink_readnone_call), and a
; call of a memory(read) function across loads but not across stores
; (sink_readonly_call_past_load, no_sink_readonly_call); no call moves across
; another call. MSILRegStackify takes these attributes from the called
; function, not from the call site, and llc infers none, so the callees carry
; them on their definitions. Pointer arguments stand in for the WebAssembly
; globals, which arrive in M8.
;
; Not ported: commute_simd_min and commute_simd_max, because Phase 1 has no
; vectors (D8); stackpointer_dependency, because MSIL has no __stack_pointer
; global: the frame is one localloc in the prologue, and llvm.frameaddress is
; a diagnostic (dynamic-alloca-error.ll). call_indirect_stackify is ported to
; call-indirect.ll, with the other indirect calls (M9).
; no_stackify_past_epilogue becomes stackify_past_epilogue, because the MSIL
; epilogue is empty.
;
; At -O0 MSILRegStackify only stackifies a value defined right before its use.
; The integer cases check -O0 for the order of calls and divisions only (O0);
; the memory and control-flow cases check the whole -O0 body as well. The order
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

; The memory and control-flow cases. Their callees are defined like the ones
; above; the memory attributes are on the definitions, where MSILRegStackify
; looks for them.
define i32 @readnone_callee() memory(none) nounwind {
  ret i32 4
}
define i32 @readonly_callee() memory(read) nounwind {
  ret i32 5
}
define void @evoke_side_effects() {
  ret void
}
define i32 @use_memory(ptr %p) {
  %v = load i32, ptr %p
  ret i32 %v
}

; No because of pointer aliasing: the store may write what the load reads, so
; the load stays above it and its value waits in a local.
; CHECK-LABEL: .method {{.*}}'no0'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  .locals (int32){{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'no0'{{$}}
define i32 @no0(ptr %p, ptr %q) {
  %t = load i32, ptr %q
  store i32 0, ptr %p
  ret i32 %t
}

; No because of side effects: a volatile load is ordered even when it is
; invariant.
; CHECK-LABEL: .method {{.*}}'no1'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  .locals (int32){{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'no1'{{$}}
define i32 @no1(ptr %p, ptr dereferenceable(4) align(4) %q) {
  %t = load volatile i32, ptr %q, !invariant.load !0
  store volatile i32 0, ptr %p
  ret i32 %t
}

; Yes because of invariant load and no side effects: the load sinks below the
; store.
; CHECK-LABEL: .method {{.*}}'yes0'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  stind.i4{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldind.i4{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O2-NEXT:     {{^}}} // end of method 'yes0'{{$}}
; O0-NEXT:     {{^\{$}}
; O0-NEXT:     {{^}}  .maxstack 2{{$}}
; O0-NEXT:     {{^}}  .locals (int32){{$}}
; O0-NEXT:     {{^}}  ldarg.1{{$}}
; O0-NEXT:     {{^}}  ldind.i4{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; O0-NEXT:     {{^}}  ldarg.0{{$}}
; O0-NEXT:     {{^}}  ldc.i4.0{{$}}
; O0-NEXT:     {{^}}  stind.i4{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; O0-NEXT:     {{^}}} // end of method 'yes0'{{$}}
define i32 @yes0(ptr %p, ptr dereferenceable(4) align(4) %q) {
  %t = load i32, ptr %q, !invariant.load !0
  store i32 0, ptr %p
  ret i32 %t
}

; Yes because of no intervening side effects.
; CHECK-LABEL: .method {{.*}}'yes1'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'yes1'{{$}}
define i32 @yes1(ptr %q) {
  %t = load volatile i32, ptr %q
  ret i32 %t
}

; Yes because undefined behavior can be sunk past a store: the WebAssembly
; sink_trap, whose division sinks below a volatile store (sink_trap above
; sinks it below a call).
; CHECK-LABEL: .method {{.*}}'sink_trap_past_store'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  volatile.{{$}}
; O2-NEXT:     {{^}}  stind.i4{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O2-NEXT:     {{^}}} // end of method 'sink_trap_past_store'{{$}}
; O0-NEXT:     {{^\{$}}
; O0-NEXT:     {{^}}  .maxstack 2{{$}}
; O0-NEXT:     {{^}}  .locals (int32){{$}}
; O0-NEXT:     {{^}}  ldarg.0{{$}}
; O0-NEXT:     {{^}}  ldarg.1{{$}}
; O0-NEXT:     {{^}}  div{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; O0-NEXT:     {{^}}  ldarg.2{{$}}
; O0-NEXT:     {{^}}  ldc.i4.0{{$}}
; O0-NEXT:     {{^}}  volatile.{{$}}
; O0-NEXT:     {{^}}  stind.i4{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; O0-NEXT:     {{^}}} // end of method 'sink_trap_past_store'{{$}}
define i32 @sink_trap_past_store(i32 %x, i32 %y, ptr %p) {
  %t = sdiv i32 %x, %y
  store volatile i32 0, ptr %p
  ret i32 %t
}

; Yes because the call is readnone (memory(none)): it sinks below a volatile
; store.
; CHECK-LABEL: .method {{.*}}'sink_readnone_call'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  volatile.{{$}}
; O2-NEXT:     {{^}}  stind.i4{{$}}
; O2-NEXT:     {{^}}  call int32 'readnone_callee'(){{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O2-NEXT:     {{^}}} // end of method 'sink_readnone_call'{{$}}
; O0-NEXT:     {{^\{$}}
; O0-NEXT:     {{^}}  .maxstack 2{{$}}
; O0-NEXT:     {{^}}  .locals (int32){{$}}
; O0-NEXT:     {{^}}  call int32 'readnone_callee'(){{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; O0-NEXT:     {{^}}  ldarg.2{{$}}
; O0-NEXT:     {{^}}  ldc.i4.0{{$}}
; O0-NEXT:     {{^}}  volatile.{{$}}
; O0-NEXT:     {{^}}  stind.i4{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; O0-NEXT:     {{^}}} // end of method 'sink_readnone_call'{{$}}
define i32 @sink_readnone_call(i32 %x, i32 %y, ptr %p) {
  %t = call i32 @readnone_callee()
  store volatile i32 0, ptr %p
  ret i32 %t
}

; No because the call is readonly (memory(read)) and there is an intervening
; store.
; CHECK-LABEL: .method {{.*}}'no_sink_readonly_call'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  .locals (int32){{$}}
; CHECK-NEXT:  {{^}}  call int32 'readonly_callee'(){{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'no_sink_readonly_call'{{$}}
define i32 @no_sink_readonly_call(i32 %x, i32 %y, ptr %p) {
  %t = call i32 @readonly_callee()
  store i32 0, ptr %p
  ret i32 %t
}

; Not in the WebAssembly test: yes because the call is readonly and only a
; load intervenes. A call without memory attributes may not do this
; (no_stackify_call_past_load).
; CHECK-LABEL: .method {{.*}}'sink_readonly_call_past_load'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldind.i4{{$}}
; O2-NEXT:     {{^}}  call int32 'readonly_callee'(){{$}}
; O2-NEXT:     {{^}}  sub{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O2-NEXT:     {{^}}} // end of method 'sink_readonly_call_past_load'{{$}}
; O0-NEXT:     {{^\{$}}
; O0-NEXT:     {{^}}  .maxstack 2{{$}}
; O0-NEXT:     {{^}}  .locals (int32){{$}}
; O0-NEXT:     {{^}}  call int32 'readonly_callee'(){{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; O0-NEXT:     {{^}}  ldarg.0{{$}}
; O0-NEXT:     {{^}}  ldind.i4{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; O0-NEXT:     {{^}}  sub{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; O0-NEXT:     {{^}}} // end of method 'sink_readonly_call_past_load'{{$}}
define i32 @sink_readonly_call_past_load(ptr %p) {
  %t = call i32 @readonly_callee()
  %v = load i32, ptr %p
  %s = sub i32 %v, %t
  ret i32 %s
}

; Don't schedule stack uses into the stack: the four comparisons, which
; instruction selection emits before both xors, become one tree that feeds
; the branch, and the constants 1 and 2 are rematerialized at each use.
; CHECK-LABEL: .method {{.*}}'stack_uses'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 4{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  clt{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldc.i4.2{{$}}
; O2-NEXT:     {{^}}  clt{{$}}
; O2-NEXT:     {{^}}  xor{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  clt{{$}}
; O2-NEXT:     {{^}}  ldarg.3{{$}}
; O2-NEXT:     {{^}}  ldc.i4.2{{$}}
; O2-NEXT:     {{^}}  clt{{$}}
; O2-NEXT:     {{^}}  xor{{$}}
; O2-NEXT:     {{^}}  beq.s @BB[[F:[0-9]+]]_2{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_2:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O2-NEXT:     {{^}}} // end of method 'stack_uses'{{$}}
; O0-NEXT:     {{^\{$}}
; O0-NEXT:     {{^}}  .maxstack 3{{$}}
; O0-NEXT:     {{^}}  .locals (int32, int32, int32, int32, int32, int32){{$}}
; O0-NEXT:     {{^}}  ldc.i4.1{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; O0-NEXT:     {{^}}  ldarg.0{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; O0-NEXT:     {{^}}  clt{{$}}
; O0-NEXT:     {{^}}  stloc.1{{$}}
; O0-NEXT:     {{^}}  ldc.i4.2{{$}}
; O0-NEXT:     {{^}}  stloc.2{{$}}
; O0-NEXT:     {{^}}  ldarg.1{{$}}
; O0-NEXT:     {{^}}  ldloc.2{{$}}
; O0-NEXT:     {{^}}  clt{{$}}
; O0-NEXT:     {{^}}  stloc.3{{$}}
; O0-NEXT:     {{^}}  ldarg.2{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; O0-NEXT:     {{^}}  clt{{$}}
; O0-NEXT:     {{^}}  stloc.s 4{{$}}
; O0-NEXT:     {{^}}  ldarg.3{{$}}
; O0-NEXT:     {{^}}  ldloc.2{{$}}
; O0-NEXT:     {{^}}  clt{{$}}
; O0-NEXT:     {{^}}  stloc.s 5{{$}}
; O0-NEXT:     {{^}}  ldloc.1{{$}}
; O0-NEXT:     {{^}}  ldloc.3{{$}}
; O0-NEXT:     {{^}}  xor{{$}}
; O0-NEXT:     {{^}}  ldloc.s 4{{$}}
; O0-NEXT:     {{^}}  ldloc.s 5{{$}}
; O0-NEXT:     {{^}}  xor{{$}}
; O0-NEXT:     {{^}}  beq.s @BB[[F:[0-9]+]]_2{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_1{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_1:{{$}}
; O0-NEXT:     {{^}}  ldc.i4.0{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_2:{{$}}
; O0-NEXT:     {{^}}  ldc.i4.1{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; O0-NEXT:     {{^}}} // end of method 'stack_uses'{{$}}
define i32 @stack_uses(i32 %x, i32 %y, i32 %z, i32 %w) {
entry:
  %c = icmp sle i32 %x, 0
  %d = icmp sle i32 %y, 1
  %e = icmp sle i32 %z, 0
  %f = icmp sle i32 %w, 1
  %g = xor i1 %c, %d
  %h = xor i1 %e, %f
  %i = xor i1 %g, %h
  br i1 %i, label %true, label %false
true:
  ret i32 0
false:
  ret i32 1
}

; Test an interesting case where the load has multiple uses and cannot be
; trivially stackified. However, it can be stackified with a tee (dup and
; stloc). Pointers are native int, so the inttoptr of the i32 is a widening
; (conv.u), whose two uses get a tee as well. The back edge branches on poison
; (undef in the WebAssembly test) and folds away at -O2.
; CHECK-LABEL: .method {{.*}}'multiple_uses'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  .locals (int32, native int){{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  ldind.i4{{$}}
; O2-NEXT:     {{^}}  dup{{$}}
; O2-NEXT:     {{^}}  stloc.0{{$}}
; O2-NEXT:     {{^}}  conv.u{{$}}
; O2-NEXT:     {{^}}  dup{{$}}
; O2-NEXT:     {{^}}  stloc.1{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  bge.un.s @BB[[F:[0-9]+]]_3{{$}}
; O2-NEXT:     {{^}}  ldloc.1{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  blt.un.s @BB[[F]]_3{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  ldloc.0{{$}}
; O2-NEXT:     {{^}}  stind.i4{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_3:{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O2-NEXT:     {{^}}} // end of method 'multiple_uses'{{$}}
; O0-NEXT:     {{^\{$}}
; O0-NEXT:     {{^}}  .maxstack 2{{$}}
; O0-NEXT:     {{^}}  .locals (int32, native int){{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F:[0-9]+]]_1{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_1:{{$}}
; O0-NEXT:     {{^}}  ldarg.2{{$}}
; O0-NEXT:     {{^}}  ldind.i4{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; O0-NEXT:     {{^}}  conv.u{{$}}
; O0-NEXT:     {{^}}  stloc.1{{$}}
; O0-NEXT:     {{^}}  ldloc.1{{$}}
; O0-NEXT:     {{^}}  ldarg.1{{$}}
; O0-NEXT:     {{^}}  bge.un.s @BB[[F]]_4{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_2{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_2:{{$}}
; O0-NEXT:     {{^}}  ldloc.1{{$}}
; O0-NEXT:     {{^}}  ldarg.0{{$}}
; O0-NEXT:     {{^}}  blt.un.s @BB[[F]]_4{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_3{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_3:{{$}}
; O0-NEXT:     {{^}}  ldarg.2{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; O0-NEXT:     {{^}}  stind.i4{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_4{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_4:{{$}}
; O0-NEXT:     {{^}}  ldc.i4.0{{$}}
; O0-NEXT:     {{^}}  brtrue.s @BB[[F]]_1{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_5{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_5:{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; O0-NEXT:     {{^}}} // end of method 'multiple_uses'{{$}}
define void @multiple_uses(ptr %arg0, ptr %arg1, ptr %arg2) nounwind {
bb:
  br label %loop

loop:
  %tmp7 = load i32, ptr %arg2
  %tmp8 = inttoptr i32 %tmp7 to ptr
  %tmp9 = icmp uge ptr %tmp8, %arg1
  %tmp10 = icmp ult ptr %tmp8, %arg0
  %tmp11 = or i1 %tmp9, %tmp10
  br i1 %tmp11, label %back, label %then

then:
  store i32 %tmp7, ptr %arg2
  br label %back

back:
  br i1 poison, label %return, label %loop

return:
  ret void
}

; Don't stackify stores across other instructions with side effects: the
; stores and the calls keep their order. The DAG combiner stores the double
; 2.0 as its bit pattern, an i64, so as a native int.
; CHECK-LABEL: .method {{.*}}'stackify_store_across_side_effects'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i8 4611686018427387904{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  stind.i{{$}}
; O2-NEXT:     {{^}}  call void 'evoke_side_effects'(){{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i8 4611686018427387904{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  stind.i{{$}}
; O2-NEXT:     {{^}}  call void 'evoke_side_effects'(){{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O2-NEXT:     {{^}}} // end of method 'stackify_store_across_side_effects'{{$}}
; O0-NEXT:     {{^\{$}}
; O0-NEXT:     {{^}}  .maxstack 2{{$}}
; O0-NEXT:     {{^}}  .locals (native int){{$}}
; O0-NEXT:     {{^}}  ldc.i8 4611686018427387904{{$}}
; O0-NEXT:     {{^}}  conv.i{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; O0-NEXT:     {{^}}  ldarg.0{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; O0-NEXT:     {{^}}  stind.i{{$}}
; O0-NEXT:     {{^}}  call void 'evoke_side_effects'(){{$}}
; O0-NEXT:     {{^}}  ldarg.0{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; O0-NEXT:     {{^}}  stind.i{{$}}
; O0-NEXT:     {{^}}  call void 'evoke_side_effects'(){{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; O0-NEXT:     {{^}}} // end of method 'stackify_store_across_side_effects'{{$}}
define void @stackify_store_across_side_effects(ptr nocapture %d) {
entry:
  store double 2.0, ptr %d
  call void @evoke_side_effects()
  store double 2.0, ptr %d
  call void @evoke_side_effects()
  ret void
}

; Stackify individual defs of virtual registers with multiple defs. CIL has no
; select, so the selects become branches, and no def of a register with
; several defs (from PHI elimination) is followed by a use in its own block:
; they all stay in locals, where WebAssembly stackifies one into f64.select.
; Only single-def values, such as the constants of the compares and adds, are
; stackified; stackify_indvar keeps one def of such a register with a tee.
; CHECK-LABEL: .method {{.*}}'multiple_defs'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  .locals (float64, float64, float64, float64, float64){{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0x0000000000000000){{$}}
; O2-NEXT:     {{^}}  stloc.0{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  and{{$}}
; O2-NEXT:     {{^}}  starg.s 2{{$}}
; O2-NEXT:     {{^}}  ldarg.3{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  and{{$}}
; O2-NEXT:     {{^}}  starg.s 3{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldc.i4.2{{$}}
; O2-NEXT:     {{^}}  or{{$}}
; O2-NEXT:     {{^}}  ldc.i4.s 14{{$}}
; O2-NEXT:     {{^}}  ceq{{$}}
; O2-NEXT:     {{^}}  starg.s 1{{$}}
; O2-NEXT:     {{^}}  ldarg.s 4{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  and{{$}}
; O2-NEXT:     {{^}}  starg.s 4{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0x0000000000000000){{$}}
; O2-NEXT:     {{^}}  stloc.1{{$}}
; O2-NEXT:     {{^}}@BB[[F:[0-9]+]]_1:{{$}}
; O2-NEXT:     {{^}}  ldloc.1{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0x40373C083126E979){{$}}
; O2-NEXT:     {{^}}  bge.un.s @BB[[F]]_9{{$}}
; O2-NEXT:     {{^}}  ldloc.1{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0xBFF0000000000000){{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  stloc.2{{$}}
; O2-NEXT:     {{^}}  ldloc.0{{$}}
; O2-NEXT:     {{^}}  stloc.3{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_3:{{$}}
; O2-NEXT:     {{^}}  ldloc.3{{$}}
; O2-NEXT:     {{^}}  stloc.0{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0xC0C62CC8F5C28F5C){{$}}
; O2-NEXT:     {{^}}  stloc.3{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  brtrue.s @BB[[F]]_5{{$}}
; O2-NEXT:     {{^}}  ldloc.2{{$}}
; O2-NEXT:     {{^}}  stloc.3{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_5:{{$}}
; O2-NEXT:     {{^}}  ldloc.3{{$}}
; O2-NEXT:     {{^}}  ldloc.0{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  stloc.s 4{{$}}
; O2-NEXT:     {{^}}  ldloc.2{{$}}
; O2-NEXT:     {{^}}  stloc.3{{$}}
; O2-NEXT:     {{^}}  ldarg.3{{$}}
; O2-NEXT:     {{^}}  brtrue.s @BB[[F]]_8{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0xC0C62CC8F5C28F5C){{$}}
; O2-NEXT:     {{^}}  stloc.3{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  brtrue.s @BB[[F]]_8{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0xBFCE147AE147B000){{$}}
; O2-NEXT:     {{^}}  stloc.3{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_8:{{$}}
; O2-NEXT:     {{^}}  ldloc.3{{$}}
; O2-NEXT:     {{^}}  ldloc.s 4{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  stloc.3{{$}}
; O2-NEXT:     {{^}}  ldloc.1{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0x40373C083126E979){{$}}
; O2-NEXT:     {{^}}  blt.s @BB[[F]]_3{{$}}
; O2-NEXT:     {{^}}@BB[[F]]_9:{{$}}
; O2-NEXT:     {{^}}  ldloc.1{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0x3FF0000000000000){{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  stloc.1{{$}}
; O2-NEXT:     {{^}}  ldarg.s 4{{$}}
; O2-NEXT:     {{^}}  brfalse.s @BB[[F]]_1{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O2-NEXT:     {{^}}} // end of method 'multiple_defs'{{$}}
; O0-NEXT:     {{^\{$}}
; O0-NEXT:     {{^}}  .maxstack 2{{$}}
; O0-NEXT:     {{^}}  .locals (float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, int32, int32, float64, float64, int32, float64, int32, float64, float64, float64, float64, float64, float64, int32){{$}}
; O0-NEXT:     {{^}}  ldc.r8 float64(0x0000000000000000){{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; O0-NEXT:     {{^}}  stloc.1{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; O0-NEXT:     {{^}}  stloc.2{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F:[0-9]+]]_1{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_1:{{$}}
; O0-NEXT:     {{^}}  ldloc.2{{$}}
; O0-NEXT:     {{^}}  stloc.3{{$}}
; O0-NEXT:     {{^}}  ldloc.1{{$}}
; O0-NEXT:     {{^}}  stloc.s 4{{$}}
; O0-NEXT:     {{^}}  ldc.r8 float64(0x40373C083126E979){{$}}
; O0-NEXT:     {{^}}  stloc.s 5{{$}}
; O0-NEXT:     {{^}}  ldloc.s 4{{$}}
; O0-NEXT:     {{^}}  stloc.s 6{{$}}
; O0-NEXT:     {{^}}  ldloc.s 4{{$}}
; O0-NEXT:     {{^}}  stloc.s 7{{$}}
; O0-NEXT:     {{^}}  ldloc.3{{$}}
; O0-NEXT:     {{^}}  ldloc.s 5{{$}}
; O0-NEXT:     {{^}}  bge.un @BB[[F]]_9{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_2{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_2:{{$}}
; O0-NEXT:     {{^}}  ldloc.s 6{{$}}
; O0-NEXT:     {{^}}  stloc.s 8{{$}}
; O0-NEXT:     {{^}}  ldloc.3{{$}}
; O0-NEXT:     {{^}}  ldc.r8 float64(0xBFF0000000000000){{$}}
; O0-NEXT:     {{^}}  add{{$}}
; O0-NEXT:     {{^}}  stloc.s 9{{$}}
; O0-NEXT:     {{^}}  ldc.i4.1{{$}}
; O0-NEXT:     {{^}}  stloc.s 10{{$}}
; O0-NEXT:     {{^}}  ldarg.2{{$}}
; O0-NEXT:     {{^}}  ldloc.s 10{{$}}
; O0-NEXT:     {{^}}  and{{$}}
; O0-NEXT:     {{^}}  stloc.s 11{{$}}
; O0-NEXT:     {{^}}  ldc.r8 float64(0xC0C62CC8F5C28F5C){{$}}
; O0-NEXT:     {{^}}  stloc.s 12{{$}}
; O0-NEXT:     {{^}}  ldloc.s 11{{$}}
; O0-NEXT:     {{^}}  brtrue.s @BB[[F]]_4{{$}}
; O0-NEXT:     {{^}}  ldloc.s 9{{$}}
; O0-NEXT:     {{^}}  stloc.s 12{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_4:{{$}}
; O0-NEXT:     {{^}}  ldloc.s 12{{$}}
; O0-NEXT:     {{^}}  ldloc.s 8{{$}}
; O0-NEXT:     {{^}}  add{{$}}
; O0-NEXT:     {{^}}  stloc.s 13{{$}}
; O0-NEXT:     {{^}}  ldarg.3{{$}}
; O0-NEXT:     {{^}}  ldloc.s 10{{$}}
; O0-NEXT:     {{^}}  and{{$}}
; O0-NEXT:     {{^}}  stloc.s 14{{$}}
; O0-NEXT:     {{^}}  ldloc.s 9{{$}}
; O0-NEXT:     {{^}}  stloc.s 15{{$}}
; O0-NEXT:     {{^}}  ldloc.s 14{{$}}
; O0-NEXT:     {{^}}  brtrue.s @BB[[F]]_8{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_5{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_5:{{$}}
; O0-NEXT:     {{^}}  ldarg.1{{$}}
; O0-NEXT:     {{^}}  ldc.i4.2{{$}}
; O0-NEXT:     {{^}}  or{{$}}
; O0-NEXT:     {{^}}  ldc.i4.s 14{{$}}
; O0-NEXT:     {{^}}  ceq{{$}}
; O0-NEXT:     {{^}}  stloc.s 16{{$}}
; O0-NEXT:     {{^}}  ldc.r8 float64(0xBFCE147AE147B000){{$}}
; O0-NEXT:     {{^}}  stloc.s 17{{$}}
; O0-NEXT:     {{^}}  ldc.r8 float64(0xC0C62CC8F5C28F5C){{$}}
; O0-NEXT:     {{^}}  stloc.s 18{{$}}
; O0-NEXT:     {{^}}  ldloc.s 16{{$}}
; O0-NEXT:     {{^}}  brtrue.s @BB[[F]]_7{{$}}
; O0-NEXT:     {{^}}  ldloc.s 17{{$}}
; O0-NEXT:     {{^}}  stloc.s 18{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_7:{{$}}
; O0-NEXT:     {{^}}  ldloc.s 18{{$}}
; O0-NEXT:     {{^}}  stloc.s 15{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_8{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_8:{{$}}
; O0-NEXT:     {{^}}  ldloc.s 15{{$}}
; O0-NEXT:     {{^}}  ldloc.s 13{{$}}
; O0-NEXT:     {{^}}  add{{$}}
; O0-NEXT:     {{^}}  stloc.s 19{{$}}
; O0-NEXT:     {{^}}  ldc.r8 float64(0x40373C083126E979){{$}}
; O0-NEXT:     {{^}}  stloc.s 20{{$}}
; O0-NEXT:     {{^}}  ldloc.s 19{{$}}
; O0-NEXT:     {{^}}  stloc.s 6{{$}}
; O0-NEXT:     {{^}}  ldloc.s 8{{$}}
; O0-NEXT:     {{^}}  stloc.s 7{{$}}
; O0-NEXT:     {{^}}  ldloc.3{{$}}
; O0-NEXT:     {{^}}  ldloc.s 20{{$}}
; O0-NEXT:     {{^}}  blt @BB[[F]]_2{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_9{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_9:{{$}}
; O0-NEXT:     {{^}}  ldloc.s 7{{$}}
; O0-NEXT:     {{^}}  stloc.s 21{{$}}
; O0-NEXT:     {{^}}  ldloc.3{{$}}
; O0-NEXT:     {{^}}  ldc.r8 float64(0x3FF0000000000000){{$}}
; O0-NEXT:     {{^}}  add{{$}}
; O0-NEXT:     {{^}}  stloc.s 22{{$}}
; O0-NEXT:     {{^}}  ldarg.s 4{{$}}
; O0-NEXT:     {{^}}  ldc.i4.1{{$}}
; O0-NEXT:     {{^}}  and{{$}}
; O0-NEXT:     {{^}}  stloc.s 23{{$}}
; O0-NEXT:     {{^}}  ldloc.s 21{{$}}
; O0-NEXT:     {{^}}  stloc.1{{$}}
; O0-NEXT:     {{^}}  ldloc.s 22{{$}}
; O0-NEXT:     {{^}}  stloc.2{{$}}
; O0-NEXT:     {{^}}  ldloc.s 23{{$}}
; O0-NEXT:     {{^}}  brfalse @BB[[F]]_1{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_10{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_10:{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; O0-NEXT:     {{^}}} // end of method 'multiple_defs'{{$}}
define void @multiple_defs(i32 %arg, i32 %arg1, i1 %arg2, i1 %arg3, i1 %arg4) {
bb:
  br label %bb5

bb5:                                              ; preds = %bb21, %bb
  %tmp = phi double [ 0.000000e+00, %bb ], [ %tmp22, %bb21 ]
  %tmp6 = phi double [ 0.000000e+00, %bb ], [ %tmp23, %bb21 ]
  %tmp7 = fcmp olt double %tmp6, 2.323450e+01
  br i1 %tmp7, label %bb8, label %bb21

bb8:                                              ; preds = %bb17, %bb5
  %tmp9 = phi double [ %tmp19, %bb17 ], [ %tmp, %bb5 ]
  %tmp10 = fadd double %tmp6, -1.000000e+00
  %tmp11 = select i1 %arg2, double -1.135357e+04, double %tmp10
  %tmp12 = fadd double %tmp11, %tmp9
  br i1 %arg3, label %bb17, label %bb13

bb13:                                             ; preds = %bb8
  %tmp14 = or i32 %arg1, 2
  %tmp15 = icmp eq i32 %tmp14, 14
  %tmp16 = select i1 %tmp15, double -1.135357e+04, double 0xBFCE147AE147B000
  br label %bb17

bb17:                                             ; preds = %bb13, %bb8
  %tmp18 = phi double [ %tmp16, %bb13 ], [ %tmp10, %bb8 ]
  %tmp19 = fadd double %tmp18, %tmp12
  %tmp20 = fcmp olt double %tmp6, 2.323450e+01
  br i1 %tmp20, label %bb8, label %bb21

bb21:                                             ; preds = %bb17, %bb5
  %tmp22 = phi double [ %tmp, %bb5 ], [ %tmp9, %bb17 ]
  %tmp23 = fadd double %tmp6, 1.000000e+00
  br i1 %arg4, label %exit, label %bb5
exit:
  ret void
}

; Don't move calls past loads: @red has no memory attributes, so its call may
; write what the load reads. A pointer argument stands in for the global
; @count of the WebAssembly test.
; CHECK-LABEL: .method {{.*}}'no_stackify_call_past_load'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  .locals (int32, int32){{$}}
; CHECK-NEXT:  {{^}}  call int32 'red'(){{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  stloc.1{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'callee'(int32){{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ldloc.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'no_stackify_call_past_load'{{$}}
define i32 @no_stackify_call_past_load(ptr %count) {
  %a = call i32 @red()
  %b = load i32, ptr %count, align 4
  call i32 @callee(i32 %a)
  ret i32 %b
}

; Don't move stores past loads if there may be aliasing: the load stays below
; the store, and it does not sink below the call either.
; CHECK-LABEL: .method {{.*}}'no_stackify_store_past_load'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  .locals (int32){{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'callee'(int32){{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'no_stackify_store_past_load'{{$}}
define i32 @no_stackify_store_past_load(i32 %a, ptr %p1, ptr %p2) {
  store i32 %a, ptr %p1
  %b = load i32, ptr %p2, align 4
  call i32 @callee(i32 %a)
  ret i32 %b
}

; Can still stackify past invariant loads: the invariant load sinks below the
; call into the ret.
; CHECK-LABEL: .method {{.*}}'store_past_invar_load'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  stind.i4{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  call int32 'callee'(int32){{$}}
; O2-NEXT:     {{^}}  pop{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  ldind.i4{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O2-NEXT:     {{^}}} // end of method 'store_past_invar_load'{{$}}
; O0-NEXT:     {{^\{$}}
; O0-NEXT:     {{^}}  .maxstack 2{{$}}
; O0-NEXT:     {{^}}  .locals (int32){{$}}
; O0-NEXT:     {{^}}  ldarg.1{{$}}
; O0-NEXT:     {{^}}  ldarg.0{{$}}
; O0-NEXT:     {{^}}  stind.i4{{$}}
; O0-NEXT:     {{^}}  ldarg.2{{$}}
; O0-NEXT:     {{^}}  ldind.i4{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; O0-NEXT:     {{^}}  ldarg.0{{$}}
; O0-NEXT:     {{^}}  call int32 'callee'(int32){{$}}
; O0-NEXT:     {{^}}  pop{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; O0-NEXT:     {{^}}} // end of method 'store_past_invar_load'{{$}}
define i32 @store_past_invar_load(i32 %a, ptr %p1, ptr dereferenceable(4) align(4) %p2) {
  store i32 %a, ptr %p1
  %b = load i32, ptr %p2, !invariant.load !0
  call i32 @callee(i32 %a)
  ret i32 %b
}

; WebAssembly's no_stackify_past_epilogue keeps a call result off the stack
; because the epilogue, which restores __stack_pointer, comes before the
; return. The MSIL frame is one localloc in the prologue and the epilogue is
; empty (CIL releases the frame when the method returns), so the result goes
; straight to ret.
; CHECK-LABEL: .method {{.*}}'stackify_past_epilogue'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4(\.s)? 12}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  call int32 'use_memory'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'stackify_past_epilogue'{{$}}
define i32 @stackify_past_epilogue() {
  %x = alloca i32
  %call = call i32 @use_memory(ptr %x)
  ret i32 %call
}

; Stackify a loop induction variable into a loop comparison: the increment is
; kept with a tee into the local of the induction variable, a register with
; two defs.
; CHECK-LABEL: .method {{.*}}'stackify_indvar'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  .locals (int32){{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  stloc.0{{$}}
; O2-NEXT:     {{^}}@BB[[F:[0-9]+]]_1:{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldloc.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  volatile.{{$}}
; O2-NEXT:     {{^}}  ldind.i4{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  volatile.{{$}}
; O2-NEXT:     {{^}}  stind.i4{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldloc.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  dup{{$}}
; O2-NEXT:     {{^}}  stloc.0{{$}}
; O2-NEXT:     {{^}}  bne.un.s @BB[[F]]_1{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O2-NEXT:     {{^}}} // end of method 'stackify_indvar'{{$}}
; O0-NEXT:     {{^\{$}}
; O0-NEXT:     {{^}}  .maxstack 3{{$}}
; O0-NEXT:     {{^}}  .locals (int32, int32, int32){{$}}
; O0-NEXT:     {{^}}  ldc.i4.0{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F:[0-9]+]]_1{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_1:{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; O0-NEXT:     {{^}}  stloc.1{{$}}
; O0-NEXT:     {{^}}  ldarg.1{{$}}
; O0-NEXT:     {{^}}  ldloc.1{{$}}
; O0-NEXT:     {{^}}  ldarg.1{{$}}
; O0-NEXT:     {{^}}  volatile.{{$}}
; O0-NEXT:     {{^}}  ldind.i4{{$}}
; O0-NEXT:     {{^}}  add{{$}}
; O0-NEXT:     {{^}}  volatile.{{$}}
; O0-NEXT:     {{^}}  stind.i4{{$}}
; O0-NEXT:     {{^}}  ldloc.1{{$}}
; O0-NEXT:     {{^}}  ldc.i4.1{{$}}
; O0-NEXT:     {{^}}  add{{$}}
; O0-NEXT:     {{^}}  stloc.2{{$}}
; O0-NEXT:     {{^}}  ldloc.2{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; O0-NEXT:     {{^}}  ldloc.2{{$}}
; O0-NEXT:     {{^}}  ldarg.0{{$}}
; O0-NEXT:     {{^}}  bne.un.s @BB[[F]]_1{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_2{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_2:{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; O0-NEXT:     {{^}}} // end of method 'stackify_indvar'{{$}}
define void @stackify_indvar(i32 %tmp, ptr %v) {
bb:
  br label %bb3

bb3:                                              ; preds = %bb3, %bb2
  %tmp4 = phi i32 [ %tmp7, %bb3 ], [ 0, %bb ]
  %tmp5 = load volatile i32, ptr %v, align 4
  %tmp6 = add nsw i32 %tmp5, %tmp4
  store volatile i32 %tmp6, ptr %v, align 4
  %tmp7 = add nuw nsw i32 %tmp4, 1
  %tmp8 = icmp eq i32 %tmp7, %tmp
  br i1 %tmp8, label %bb10, label %bb3

bb10:                                             ; preds = %bb9, %bb
  ret void
}

!0 = !{}

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

; Loops and PHIs (ImplementationPlan.md M6; plan 5.4 'Stack effect contract';
; recon passes-frame.md 5.3-5.4). A loop header is a branch target, so its
; label is printed and the latch branches back to it ('b... @BB<f>_<header>'
; after the header's label). PHI elimination turns every loop-carried value
; into a local of its CIL type ('.locals (...)': int32, native int, float64),
; written before the branch and read after the label, so the evaluation
; stack is empty at every block boundary (MSILStackCheck rejects anything
; else). Swapped PHIs (Fibonacci), a float64 accumulator, nested loops, a loop
; with two exits and LCSSA PHIs, a native int (pointer) induction and a
; switch inside a loop all compile at -O0 and -O2 with -verify-machineinstrs
; and assemble. LSR may rewrite the induction variables at -O2, so the exit
; tests are only checked for their target, not for their mnemonic.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 \
; DEFINE:   --implicit-check-not="{{^ +b[a-z.]+\.s }}" --implicit-check-not=throw
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "loop-phi.c"

; for (i = 0; i != n; ++i) s += i; guarded by n > 0.
; CHECK-LABEL: .method {{.*}}'sum_to'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK:       {{^}}  .locals (int32, int32{{.*}}){{$}}
; CHECK:       {{^}}@BB[[F:[0-9]+]]_[[L:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  b{{[a-z.]+}} @BB[[F]]_[[L]]{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sum_to'{{$}}
define i32 @sum_to(i32 %n) {
entry:
  %g = icmp sgt i32 %n, 0
  br i1 %g, label %loop, label %exit
loop:
  %i = phi i32 [ 0, %entry ], [ %i1, %loop ]
  %s = phi i32 [ 0, %entry ], [ %s1, %loop ]
  %s1 = add i32 %s, %i
  %i1 = add i32 %i, 1
  %d = icmp eq i32 %i1, %n
  br i1 %d, label %exit, label %loop
exit:
  %r = phi i32 [ 0, %entry ], [ %s1, %loop ]
  ret i32 %r
}

; a, b = b, a + b: the two PHIs read each other's old values.
; CHECK-LABEL: .method {{.*}}'fib'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK:       {{^}}  .locals (int32, int32{{.*}}){{$}}
; CHECK:       {{^}}@BB[[F:[0-9]+]]_[[L:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  b{{[a-z.]+}} @BB[[F]]_[[L]]{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fib'{{$}}
define i32 @fib(i32 %n) {
entry:
  br label %loop
loop:
  %a = phi i32 [ 0, %entry ], [ %b, %loop ]
  %b = phi i32 [ 1, %entry ], [ %ab, %loop ]
  %k = phi i32 [ %n, %entry ], [ %k1, %loop ]
  %ab = add i32 %a, %b
  %k1 = add i32 %k, -1
  %more = icmp sgt i32 %k1, 0
  br i1 %more, label %loop, label %exit
exit:
  ret i32 %a
}

; A float64 accumulator lives in a float64 local.
; CHECK-LABEL: .method {{.*}}'fp_accum'(
; CHECK-SAME:  float64, int32) cil managed{{$}}
; CHECK:       {{^}}  .locals ({{.*}}float64{{.*}}){{$}}
; CHECK:       {{^}}@BB[[F:[0-9]+]]_[[L:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ldc.r8 float64(0x3FF0000000000000){{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  b{{[a-z.]+}} @BB[[F]]_[[L]]{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fp_accum'{{$}}
define double @fp_accum(double %x, i32 %n) {
entry:
  br label %loop
loop:
  %acc = phi double [ 0.0, %entry ], [ %acc1, %loop ]
  %i = phi i32 [ 0, %entry ], [ %i1, %loop ]
  %t = fmul double %acc, %x
  %acc1 = fadd double %t, 1.0
  %i1 = add i32 %i, 1
  %d = icmp slt i32 %i1, %n
  br i1 %d, label %loop, label %exit
exit:
  ret double %acc1
}

; Nested loops: the inner latch branches to the inner header, the outer latch
; to the outer header, which comes first.
; CHECK-LABEL: .method {{.*}}'nested'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK:       {{^}}@BB[[F:[0-9]+]]_[[OUTER:[0-9]+]]:{{$}}
; CHECK:       {{^}}@BB[[F]]_[[INNER:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  xor{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  b{{[a-z.]+}} @BB[[F]]_[[INNER]]{{$}}
; CHECK:       {{^}}  b{{[a-z.]+}} @BB[[F]]_[[OUTER]]{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'nested'{{$}}
define i32 @nested(i32 %n, i32 %m) {
entry:
  br label %outer
outer:
  %i = phi i32 [ 0, %entry ], [ %i1, %outer.latch ]
  %s = phi i32 [ 0, %entry ], [ %s2, %outer.latch ]
  br label %inner
inner:
  %j = phi i32 [ 0, %outer ], [ %j1, %inner ]
  %s1 = phi i32 [ %s, %outer ], [ %sx, %inner ]
  %p = mul i32 %i, %j
  %sx = xor i32 %s1, %p
  %j1 = add i32 %j, 1
  %dj = icmp slt i32 %j1, %m
  br i1 %dj, label %inner, label %outer.latch
outer.latch:
  %s2 = phi i32 [ %sx, %inner ]
  %i1 = add i32 %i, 1
  %di = icmp slt i32 %i1, %n
  br i1 %di, label %outer, label %exit
exit:
  ret i32 %s2
}

; Two exits with LCSSA PHIs: the early exit is a fused 'bgt' out of the loop
; header, the latch branches back with 'blt'.
; CHECK-LABEL: .method {{.*}}'multi_exit'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK:       {{^}}@BB[[F:[0-9]+]]_[[L:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  bgt @BB[[F]]_[[EARLY:[0-9]+]]{{$}}
; CHECK:       {{^}}  blt @BB[[F]]_[[L]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[EARLY]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  neg{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK:       {{^}}} // end of method 'multi_exit'{{$}}
define i32 @multi_exit(i32 %n, i32 %lim) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i1, %latch ]
  %v = phi i32 [ 1, %entry ], [ %v1, %latch ]
  %v1 = mul i32 %v, 3
  %big = icmp sgt i32 %v1, %lim
  br i1 %big, label %early, label %latch
latch:
  %i1 = add i32 %i, 1
  %d = icmp slt i32 %i1, %n
  br i1 %d, label %loop, label %exit
early:
  %e = phi i32 [ %v1, %loop ]
  %er = sub i32 0, %e
  ret i32 %er
exit:
  %x = phi i32 [ %v1, %latch ]
  ret i32 %x
}

; A pointer induction: native int PHIs and native int arithmetic.
; CHECK-LABEL: .method {{.*}}'ptr_walk'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK:       {{^}}@BB[[F:[0-9]+]]_[[L:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  ldc.i4{{(\.s)?}} 16{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  b{{[a-z.]+}} @BB[[F]]_[[L]]{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ptr_walk'{{$}}
define i64 @ptr_walk(ptr %p, i64 %n) {
entry:
  br label %loop
loop:
  %q = phi ptr [ %p, %entry ], [ %q1, %loop ]
  %k = phi i64 [ %n, %entry ], [ %k1, %loop ]
  %q1 = getelementptr i8, ptr %q, i64 16
  %k1 = add i64 %k, -1
  %z = icmp eq i64 %k1, 0
  br i1 %z, label %exit, label %loop
exit:
  %r = ptrtoint ptr %q1 to i64
  ret i64 %r
}

; A jump table inside a loop: every case block and the default join the
; latch, which branches back to the header.
define void @foo0() noinline {
  ret void
}

; CHECK-LABEL: .method {{.*}}'switch_loop'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK:       {{^}}@BB[[F:[0-9]+]]_[[L:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  xor{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  bgt.un @BB[[F]]_{{[0-9]+}}{{$}}
; CHECK:       {{^}}  switch (@BB[[F]]_[[S0:[0-9]+]], @BB[[F]]_{{[0-9]+}}, @BB[[F]]_{{[0-9]+}}, @BB[[F]]_{{[0-9]+}}, @BB[[F]]_{{[0-9]+}}){{$}}
; CHECK-NEXT:  {{^}}  br @BB[[F]]_[[S0]]{{$}}
; CHECK:       {{^}}  b{{[a-z.]+}} @BB[[F]]_[[L]]{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'switch_loop'{{$}}
define i32 @switch_loop(i32 %n) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i1, %latch ]
  %s = phi i32 [ 0, %entry ], [ %s1, %latch ]
  %k = xor i32 %i, %n
  switch i32 %k, label %dflt [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
    i32 4, label %c4
  ]
c0:
  %v0 = add i32 %s, 1
  br label %latch
c1:
  %v1 = mul i32 %s, 3
  br label %latch
c2:
  %v2 = xor i32 %s, 5
  br label %latch
c3:
  %v3 = sub i32 %s, 7
  br label %latch
c4:
  call void @foo0()
  br label %latch
dflt:
  br label %latch
latch:
  %s1 = phi i32 [ %v0, %c0 ], [ %v1, %c1 ], [ %v2, %c2 ], [ %v3, %c3 ], [ %s, %c4 ], [ %s, %dflt ]
  %i1 = add i32 %i, 1
  %d = icmp slt i32 %i1, %n
  br i1 %d, label %loop, label %exit
exit:
  ret i32 %s1
}

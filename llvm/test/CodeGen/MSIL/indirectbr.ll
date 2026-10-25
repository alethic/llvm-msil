; indirectbr (ImplementationPlan.md M6.a: "indirectbr is handled by
; IndirectBrExpand (already in the pipeline)", section 5.3 MSILPassConfig;
; MSILSubtarget::enableIndirectBrExpand; cf.
; llvm/test/CodeGen/WebAssembly/indirectbr.ll, which needs loads and
; globals). IndirectBrExpand replaces every blockaddress of the function with
; a small native int (1, 2, ...) and every indirectbr with a switch on that
; integer, so CIL never sees a code address: the result is ordinary control
; flow, a compare chain or a jump table on a native int index ('conv.i4'
; before 'switch', decision D35), and no BRIND reaches instruction selection.
; Every method still ends in 'ret' (labels that name removed address-taken
; blocks may follow it, but no instruction does), passes
; -verify-machineinstrs and assembles.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=calli \
; DEFINE:   --implicit-check-not=throw \
; DEFINE:   --implicit-check-not=ldftn --implicit-check-not=JTI
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=MIR --implicit-check-not=BRIND
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=MIR --implicit-check-not=BRIND
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "indirectbr.c"

; Two destinations: block a is 1 and block b is 2, the switch on the chosen
; integer has a single case (2 -> b) and a becomes its default, and the
; select of the two integers folds into the branch: x != 0 goes to b.
; MIR-LABEL: name: ibr2{{$}}
; MIR-NOT:   BR_TABLE

; CHECK-LABEL: .method {{.*}}'ibr2'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NOT:   switch
; CHECK:       {{^}}  brtrue.s @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br.s @BB[[F]]_[[A:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[A]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 10{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 20{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NOT:   {{^}}  {{[a-z]}}
; CHECK:       {{^}}} // end of method 'ibr2'{{$}}
define i32 @ibr2(i32 %x) {
entry:
  %c = icmp eq i32 %x, 0
  %t = select i1 %c, ptr blockaddress(@ibr2, %a), ptr blockaddress(@ibr2, %b)
  indirectbr ptr %t, [label %a, label %b]
a:
  ret i32 10
b:
  ret i32 20
}

; Five destinations chosen by a chain of selects: the index is a native int
; (b0 = 1 ... b4 = 5), the switch has the cases 2..5 and the default b0, so
; it is a jump table: index - 2 is range-checked as a native int against 3,
; converted with conv.i4 and dispatched by 'switch'.
; MIR-LABEL: name: ibr5{{$}}
; MIR:       [[IDX:%[0-9]+]]:i32 = CONV_I4_I64
; MIR:       {{^ *}}BGT_UN_I64 %bb.{{[0-9]+}}, %{{[0-9]+}}, %{{[0-9]+}}, implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}BR_TABLE_I32 [[IDX]], %bb.[[B1:[0-9]+]], %bb.[[B2:[0-9]+]], %bb.[[B3:[0-9]+]], %bb.[[B4:[0-9]+]], %bb.[[B1]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[B1]].b1:
; MIR:       {{^ *}}bb.[[B2]].b2:
; MIR:       {{^ *}}bb.[[B3]].b3:
; MIR:       {{^ *}}bb.[[B4]].b4:

; CHECK-LABEL: .method {{.*}}'ibr5'(
; CHECK-SAME:  int32) cil managed{{$}}
; O0:          {{^}}  conv.i4{{$}}
; CHECK:       {{^}}  bgt.un.s @BB[[F:[0-9]+]]_[[DEF:[0-9]+]]{{$}}
; O2:          {{^}}  conv.i4{{$}}
; O2-NEXT:     {{^}}  switch ([[L1:@BB[0-9]+_[0-9]+]], [[L2:@BB[0-9]+_[0-9]+]], [[L3:@BB[0-9]+_[0-9]+]], [[L4:@BB[0-9]+_[0-9]+]]){{$}}
; O0-NOT:      conv.
; O0:          {{^}}  switch ([[L1:@BB[0-9]+_[0-9]+]], [[L2:@BB[0-9]+_[0-9]+]], [[L3:@BB[0-9]+_[0-9]+]], [[L4:@BB[0-9]+_[0-9]+]]){{$}}
; CHECK-NEXT:  {{^}}  br.s [[L1]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[DEF]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 100{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}[[L1]]:{{$}}
; CHECK:       {{^}}  add{{$}}
; CHECK:       {{^}}[[L2]]:{{$}}
; CHECK:       {{^}}  mul{{$}}
; CHECK:       {{^}}[[L3]]:{{$}}
; CHECK:       {{^}}  xor{{$}}
; CHECK:       {{^}}[[L4]]:{{$}}
; CHECK:       {{^}}  shl{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NOT:   {{^}}  {{[a-z]}}
; CHECK:       {{^}}} // end of method 'ibr5'{{$}}
define i32 @ibr5(i32 %x) {
entry:
  %c1 = icmp eq i32 %x, 1
  %c2 = icmp eq i32 %x, 2
  %c3 = icmp eq i32 %x, 3
  %c4 = icmp eq i32 %x, 4
  %s1 = select i1 %c1, ptr blockaddress(@ibr5, %b1), ptr blockaddress(@ibr5, %b0)
  %s2 = select i1 %c2, ptr blockaddress(@ibr5, %b2), ptr %s1
  %s3 = select i1 %c3, ptr blockaddress(@ibr5, %b3), ptr %s2
  %s4 = select i1 %c4, ptr blockaddress(@ibr5, %b4), ptr %s3
  indirectbr ptr %s4, [label %b0, label %b1, label %b2, label %b3, label %b4]
b0:
  ret i32 100
b1:
  %r1 = add i32 %x, 11
  ret i32 %r1
b2:
  %r2 = mul i32 %x, 13
  ret i32 %r2
b3:
  %r3 = xor i32 %x, 17
  ret i32 %r3
b4:
  %r4 = shl i32 %x, 3
  ret i32 %r4
}

; A computed-goto loop (a threaded interpreter): the next state is a PHI of
; block addresses, which becomes a native int PHI; the dispatch is a compare
; chain on it, and every state branches back to the loop test.
; CHECK-LABEL: .method {{.*}}'ibr_loop'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK:       {{^}}  .locals ({{.*}}native int{{.*}}){{$}}
; CHECK:       {{^}}@BB[[F:[0-9]+]]_[[D:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  bge{{(\.s)?}} @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; CHECK-DAG:   {{^}}  br.s @BB[[F]]_[[D]]{{$}}
; CHECK-DAG:   {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; CHECK-NOT:   switch
; CHECK:       {{^}}} // end of method 'ibr_loop'{{$}}
define i32 @ibr_loop(i32 %n) {
entry:
  br label %dispatch
dispatch:
  %t = phi ptr [ blockaddress(@ibr_loop, %s0), %entry ], [ %t0, %s0 ], [ %t1, %s1 ], [ blockaddress(@ibr_loop, %s0), %s2 ]
  %acc = phi i32 [ 0, %entry ], [ %a0, %s0 ], [ %a1, %s1 ], [ %a2, %s2 ]
  %i = phi i32 [ 0, %entry ], [ %i0, %s0 ], [ %i1, %s1 ], [ %i2, %s2 ]
  %done = icmp sge i32 %i, %n
  br i1 %done, label %exit, label %go
go:
  indirectbr ptr %t, [label %s0, label %s1, label %s2]
s0:
  %a0 = add i32 %acc, 1
  %i0 = add i32 %i, 1
  %b0 = trunc i32 %a0 to i1
  %t0 = select i1 %b0, ptr blockaddress(@ibr_loop, %s1), ptr blockaddress(@ibr_loop, %s2)
  br label %dispatch
s1:
  %a1 = mul i32 %acc, 3
  %i1 = add i32 %i, 1
  %b1 = trunc i32 %a1 to i1
  %t1 = select i1 %b1, ptr blockaddress(@ibr_loop, %s2), ptr blockaddress(@ibr_loop, %s0)
  br label %dispatch
s2:
  %a2 = xor i32 %acc, 5
  %i2 = add i32 %i, 1
  br label %dispatch
exit:
  ret i32 %acc
}

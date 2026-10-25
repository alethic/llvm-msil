; switch on native int values (ImplementationPlan.md M6.a/M6.b, decision D35,
; plan 5.1 and 5.4 'BR_TABLE_I32'; recon isel.md 2.8 and 11.2, superseded for
; the index type by plan section 12 (there is no BR_TABLE_I64); port of the
; i64 parts of llvm/test/CodeGen/WebAssembly/switch.ll). The jump-table index
; is int32 (getJumpTableRegTy returns i32), because CIL 'switch' on a native
; int fails ILVerify (StackUnexpected). SelectionDAGBuilder subtracts the
; lowest case and range-checks the result at full width, as a native int
; compare ('bgt.un' against 'ldc ...; conv.i'), and only then truncates it
; for the table: the index is 'conv.i4' right before 'switch'. A value that
; differs from a case only above bit 31 (2^32 + 1 for case 1, say) therefore
; fails the range check and takes the default instead of aliasing a case.
; LLVM's range check is never removed.
;
; At -O2 machine sinking moves the conv.i4 into the table block, so the
; header block holds only the range check; at -O0 both stay in the header.
; An IR 'trunc' to i32 before the switch is a plain int32 switch: the range
; check compares the truncated value as an int32. A sparse switch is fused
; native int compares (a balanced tree at -O2, a linear chain at -O0, where
; the generic SelectionDAGBuilder does not split case clusters) whose
; constants outside the int32 range are 'ldc.i8 N; conv.i' at both levels.
; The MIR lines check, right after instruction selection, that the range
; check (BGT_UN_I64) reads the full-width value, that the BR_TABLE_I32
; index is the CONV_I4_I64 of it, and that the last BR_TABLE_I32 operand is
; the first table entry again: the 'br' printed after 'switch' names that
; entry, a dummy target that never runs (plan section 12, 'Switch contract
; amended').

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=JTI \
; DEFINE:   --implicit-check-not=throw \
; DEFINE:   --implicit-check-not=conv.u --implicit-check-not=.data
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefixes=MIR,MIRO2 --implicit-check-not=BR_TABLE_I64
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=MIR --implicit-check-not=BR_TABLE_I64
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "switch-i64.c"

; Every case calls a different function, so no two case blocks are alike.
define void @foo0() noinline {
  ret void
}
define void @foo1() noinline {
  ret void
}
define void @foo2() noinline {
  ret void
}
define void @foo3() noinline {
  ret void
}
define void @foo4() noinline {
  ret void
}
define void @foo5() noinline {
  ret void
}

; Cases 0..23: no subtraction; the native int argument is compared with the
; native int 23, then converted for the table.
; MIR-LABEL: name: bar64{{$}}
; MIR:       [[N:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR-DAG:   [[IDX:%[0-9]+]]:i32 = CONV_I4_I64 [[N]], implicit-def dead $arguments
; MIR-DAG:   [[C:%[0-9]+]]:i64 = CONST_I64 23,
; MIR:       {{^ *}}BGT_UN_I64 %bb.[[DEF:[0-9]+]], [[N]], [[C]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}BR_TABLE_I32 [[IDX]], %bb.[[A:[0-9]+]], %bb.[[A]], %bb.[[A]], %bb.[[A]], %bb.[[A]], %bb.[[A]], %bb.[[A]], %bb.[[B:[0-9]+]], %bb.[[B]], %bb.[[B]], %bb.[[B]], %bb.[[B]], %bb.[[B]], %bb.[[B]], %bb.[[B]], %bb.[[C2:[0-9]+]], %bb.[[C2]], %bb.[[C2]], %bb.[[C2]], %bb.[[C2]], %bb.[[C2]], %bb.[[D:[0-9]+]], %bb.[[E:[0-9]+]], %bb.[[G:[0-9]+]], %bb.[[A]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[A]].sw.bb:
; MIR:       {{^ *}}bb.[[B]].sw.bb.1:
; MIR:       {{^ *}}bb.[[C2]].sw.bb.2:
; MIR:       {{^ *}}bb.[[D]].sw.bb.3:
; MIR:       {{^ *}}bb.[[E]].sw.bb.4:
; MIR:       {{^ *}}bb.[[G]].sw.bb.5:
; MIR:       {{^ *}}bb.[[DEF]].sw.epilog:

; CHECK-LABEL: .method {{.*}}'bar64'(
; CHECK-SAME:  native int) cil managed{{$}}
; O2:          {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4{{(\.s)?}} 23{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  bgt.un @BB[[F:[0-9]+]]_[[DEF:[0-9]+]]{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  conv.i4{{$}}
; O0-DAG:      {{^}}  conv.i4{{$}}
; O0-DAG:      {{^}}  ldc.i4{{(\.s)?}} 23{{$}}
; O0:          {{^}}  bgt.un @BB[[F:[0-9]+]]_[[DEF:[0-9]+]]{{$}}
; O0-NOT:      conv.
; CHECK:       {{^}}  switch ([[L0:@BB[0-9]+_[0-9]+]], [[L0]], [[L0]], [[L0]], [[L0]], [[L0]], [[L0]], [[L1:@BB[0-9]+_[0-9]+]], [[L1]], [[L1]], [[L1]], [[L1]], [[L1]], [[L1]], [[L1]], [[L2:@BB[0-9]+_[0-9]+]], [[L2]], [[L2]], [[L2]], [[L2]], [[L2]], [[L3:@BB[0-9]+_[0-9]+]], [[L4:@BB[0-9]+_[0-9]+]], [[L5:@BB[0-9]+_[0-9]+]]){{$}}
; CHECK-NEXT:  {{^}}  br.s [[L0]]{{$}}
; CHECK:       {{^}}[[L0]]:{{$}}
; CHECK-NEXT:  {{^}}  call void 'foo0'(){{$}}
; CHECK:       {{^}}[[L1]]:{{$}}
; CHECK-NEXT:  {{^}}  call void 'foo1'(){{$}}
; CHECK:       {{^}}[[L2]]:{{$}}
; CHECK-NEXT:  {{^}}  call void 'foo2'(){{$}}
; CHECK:       {{^}}[[L3]]:{{$}}
; CHECK-NEXT:  {{^}}  call void 'foo3'(){{$}}
; CHECK:       {{^}}[[L4]]:{{$}}
; CHECK-NEXT:  {{^}}  call void 'foo4'(){{$}}
; CHECK:       {{^}}[[L5]]:{{$}}
; CHECK-NEXT:  {{^}}  call void 'foo5'(){{$}}
; CHECK:       {{^}}@BB[[F]]_[[DEF]]:{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'bar64'{{$}}
define void @bar64(i64 %n) {
entry:
  switch i64 %n, label %sw.epilog [
    i64 0, label %sw.bb
    i64 1, label %sw.bb
    i64 2, label %sw.bb
    i64 3, label %sw.bb
    i64 4, label %sw.bb
    i64 5, label %sw.bb
    i64 6, label %sw.bb
    i64 7, label %sw.bb.1
    i64 8, label %sw.bb.1
    i64 9, label %sw.bb.1
    i64 10, label %sw.bb.1
    i64 11, label %sw.bb.1
    i64 12, label %sw.bb.1
    i64 13, label %sw.bb.1
    i64 14, label %sw.bb.1
    i64 15, label %sw.bb.2
    i64 16, label %sw.bb.2
    i64 17, label %sw.bb.2
    i64 18, label %sw.bb.2
    i64 19, label %sw.bb.2
    i64 20, label %sw.bb.2
    i64 21, label %sw.bb.3
    i64 22, label %sw.bb.4
    i64 23, label %sw.bb.5
  ]

sw.bb:
  call void @foo0()
  br label %sw.epilog

sw.bb.1:
  call void @foo1()
  br label %sw.epilog

sw.bb.2:
  call void @foo2()
  br label %sw.epilog

sw.bb.3:
  call void @foo3()
  br label %sw.epilog

sw.bb.4:
  call void @foo4()
  br label %sw.epilog

sw.bb.5:
  call void @foo5()
  br label %sw.epilog

sw.epilog:
  ret void
}

; Cases 2^32 .. 2^32+5: the index is value - 2^32, computed as a native int
; ('ldc.i8 -4294967296; conv.i; add'), compared unsigned with the native
; int 5, and only then converted. 1, 2, ... (the same low 32 bits) are not
; cases: their index is above 5 at full width. The 'sub' of 9 in case c4
; prints as an 'add' of the native int -9 (the generic DAGCombiner fold).
; MIR-LABEL: name: high64{{$}}
; MIR:       [[N:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR-DAG:   [[K:%[0-9]+]]:i64 = CONST_I64 -4294967296,
; MIR-DAG:   [[S:%[0-9]+]]:i64 = ADD_I64 [[N]], [[K]], implicit-def dead $arguments
; MIR-DAG:   [[IDX:%[0-9]+]]:i32 = CONV_I4_I64 [[S]], implicit-def dead $arguments
; MIR-DAG:   [[C:%[0-9]+]]:i64 = CONST_I64 5,
; MIR:       {{^ *}}BGT_UN_I64 %bb.{{[0-9]+}}, [[S]], [[C]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}BR_TABLE_I32 [[IDX]], %bb.[[C0:[0-9]+]], %bb.[[C1:[0-9]+]], %bb.[[C2:[0-9]+]], %bb.[[C3:[0-9]+]], %bb.[[C4:[0-9]+]], %bb.[[C5:[0-9]+]], %bb.[[C0]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[C0]].c0:
; MIR:       {{^ *}}bb.[[C5]].c5:

; CHECK-LABEL: .method {{.*}}'high64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK:       {{^}}  ldc.i8 -4294967296{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O0:          {{^}}  add{{$}}
; O2:          {{^}}  ldc.i4{{(\.5| 5)}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  bgt.un.s @BB[[F:[0-9]+]]_[[DEF:[0-9]+]]{{$}}
; O2:          {{^}}  conv.i4{{$}}
; O2-NEXT:     {{^}}  switch ([[L0:@BB[0-9]+_[0-9]+]], [[L1:@BB[0-9]+_[0-9]+]], [[L2:@BB[0-9]+_[0-9]+]], [[L3:@BB[0-9]+_[0-9]+]], [[L4:@BB[0-9]+_[0-9]+]], [[L5:@BB[0-9]+_[0-9]+]]){{$}}
; O0:          {{^}}  conv.i4{{$}}
; O0:          {{^}}  bgt.un.s @BB[[F:[0-9]+]]_[[DEF:[0-9]+]]{{$}}
; O0-NOT:      conv.
; O0:          {{^}}  switch ([[L0:@BB[0-9]+_[0-9]+]], [[L1:@BB[0-9]+_[0-9]+]], [[L2:@BB[0-9]+_[0-9]+]], [[L3:@BB[0-9]+_[0-9]+]], [[L4:@BB[0-9]+_[0-9]+]], [[L5:@BB[0-9]+_[0-9]+]]){{$}}
; CHECK-NEXT:  {{^}}  br.s [[L0]]{{$}}
; CHECK:       {{^}}[[L0]]:{{$}}
; CHECK:       {{^}}  ldc.i4{{(\.7| 7)}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK:       {{^}}  add{{$}}
; CHECK:       {{^}}[[L1]]:{{$}}
; CHECK:       {{^}}  mul{{$}}
; CHECK:       {{^}}[[L2]]:{{$}}
; CHECK:       {{^}}  xor{{$}}
; CHECK:       {{^}}[[L3]]:{{$}}
; CHECK:       {{^}}  shl{{$}}
; CHECK:       {{^}}[[L4]]:{{$}}
; CHECK:       {{^}}  ldc.i4.s -9{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK:       {{^}}[[L5]]:{{$}}
; CHECK:       {{^}}  or{{$}}
; CHECK:       {{^}}@BB[[F]]_[[DEF]]:{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'high64'{{$}}
define i64 @high64(i64 %n) {
entry:
  switch i64 %n, label %def [
    i64 4294967296, label %c0
    i64 4294967297, label %c1
    i64 4294967298, label %c2
    i64 4294967299, label %c3
    i64 4294967300, label %c4
    i64 4294967301, label %c5
  ]
c0:
  %r0 = add i64 %n, 7
  ret i64 %r0
c1:
  %r1 = mul i64 %n, 3
  ret i64 %r1
c2:
  %r2 = xor i64 %n, 5
  ret i64 %r2
c3:
  %r3 = shl i64 %n, 2
  ret i64 %r3
c4:
  %r4 = sub i64 %n, 9
  ret i64 %r4
c5:
  %r5 = or i64 %n, 64
  ret i64 %r5
def:
  ret i64 -1
}

; Negative cases -3..2: the index is value + 3 (a native int add); -4 and
; 2^32 - 3 both fail the full-width range check.
; CHECK-LABEL: .method {{.*}}'neg64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4{{(\.3| 3)}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O0:          {{^}}  add{{$}}
; O2:          {{^}}  ldc.i4{{(\.5| 5)}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  bgt.un.s @BB[[F:[0-9]+]]_[[DEF:[0-9]+]]{{$}}
; O2:          {{^}}  conv.i4{{$}}
; O2-NEXT:     {{^}}  switch ([[A:@BB[0-9]+_[0-9]+]], [[B:@BB[0-9]+_[0-9]+]], [[A]], [[C:@BB[0-9]+_[0-9]+]], [[B]], [[D:@BB[0-9]+_[0-9]+]]){{$}}
; O0:          {{^}}  conv.i4{{$}}
; O0:          {{^}}  bgt.un.s @BB[[F:[0-9]+]]_[[DEF:[0-9]+]]{{$}}
; O0-NOT:      conv.
; O0:          {{^}}  switch ([[A:@BB[0-9]+_[0-9]+]], [[B:@BB[0-9]+_[0-9]+]], [[A]], [[C:@BB[0-9]+_[0-9]+]], [[B]], [[D:@BB[0-9]+_[0-9]+]]){{$}}
; CHECK-NEXT:  {{^}}  br.s [[A]]{{$}}
; CHECK:       {{^}}[[A]]:{{$}}
; CHECK-NEXT:  {{^}}  call void 'foo0'(){{$}}
; CHECK:       {{^}}[[B]]:{{$}}
; CHECK-NEXT:  {{^}}  call void 'foo1'(){{$}}
; CHECK:       {{^}}[[C]]:{{$}}
; CHECK-NEXT:  {{^}}  call void 'foo2'(){{$}}
; CHECK:       {{^}}[[D]]:{{$}}
; CHECK-NEXT:  {{^}}  call void 'foo3'(){{$}}
; CHECK:       {{^}}@BB[[F]]_[[DEF]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'neg64'{{$}}
define i32 @neg64(i64 %n) {
entry:
  switch i64 %n, label %def [
    i64 -3, label %a
    i64 -2, label %b
    i64 -1, label %a
    i64 0, label %c
    i64 1, label %b
    i64 2, label %d
  ]
a:
  call void @foo0()
  ret i32 10
b:
  call void @foo1()
  ret i32 20
c:
  call void @foo2()
  ret i32 30
d:
  call void @foo3()
  ret i32 40
def:
  ret i32 0
}

; A switch on an IR 'trunc' of a native int is an int32 switch: conv.i4
; first, then an int32 range check (no conv.i on the constant) and a switch
; on the same int32 value.
; MIR-LABEL: name: truncated{{$}}
; MIR:       [[N:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR:       [[T:%[0-9]+]]:i32 = CONV_I4_I64 [[N]], implicit-def dead $arguments
; MIR:       {{^ *}}BGT_UN_I32 %bb.{{[0-9]+}}, [[T]], %{{[0-9]+}}, implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}BR_TABLE_I32 [[T]], %bb.[[T0:[0-9]+]], %bb.{{[0-9]+}}, %bb.{{[0-9]+}}, %bb.{{[0-9]+}}, %bb.{{[0-9]+}}, %bb.[[T0]], implicit-def dead $arguments{{$}}

; CHECK-LABEL: .method {{.*}}'truncated'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NOT:   conv.
; O2:          {{^}}  ldc.i4.4{{$}}
; O2-NEXT:     {{^}}  bgt.un.s @BB[[F:[0-9]+]]_[[DEF:[0-9]+]]{{$}}
; O0:          {{^}}  bgt.un.s @BB[[F:[0-9]+]]_[[DEF:[0-9]+]]{{$}}
; CHECK-NOT:   conv.
; CHECK:       {{^}}  switch ([[A:@BB[0-9]+_[0-9]+]], [[B:@BB[0-9]+_[0-9]+]], [[C:@BB[0-9]+_[0-9]+]], [[D:@BB[0-9]+_[0-9]+]], [[E:@BB[0-9]+_[0-9]+]]){{$}}
; CHECK-NEXT:  {{^}}  br.s [[A]]{{$}}
; CHECK:       {{^}}[[A]]:{{$}}
; CHECK-NEXT:  {{^}}  call void 'foo0'(){{$}}
; CHECK:       {{^}}[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  call void 'foo4'(){{$}}
; CHECK:       {{^}}@BB[[F]]_[[DEF]]:{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'truncated'{{$}}
define void @truncated(i64 %n) {
entry:
  %m = trunc i64 %n to i32
  switch i32 %m, label %default [
    i32 0, label %bb0
    i32 1, label %bb1
    i32 2, label %bb2
    i32 3, label %bb3
    i32 4, label %bb4
  ]
bb0:
  call void @foo0()
  ret void
bb1:
  call void @foo1()
  ret void
bb2:
  call void @foo2()
  ret void
bb3:
  call void @foo3()
  ret void
bb4:
  call void @foo4()
  ret void
default:
  ret void
}

; A sparse native int switch: fused native int compares, one per case. At
; -O2 they form a balanced tree with the pivot 'bgt' against 2^32 - 1; at
; -O0 they are a linear chain of 'beq' in case order (-5, 1, 2^32, 2^40),
; each followed by 'br' to the next test. Constants outside the int32 range
; are 'ldc.i8 N; conv.i' at both levels.
; MIR-LABEL: name: sparse64{{$}}
; MIR-NOT:   BR_TABLE
; MIRO2-DAG: [[P:%[0-9]+]]:i64 = CONST_I64 4294967295,
; MIRO2-DAG: {{^ *}}BGT_I64 %bb.{{[0-9]+}}, %{{[0-9]+}}, [[P]], implicit-def dead $arguments{{$}}
; MIR-DAG:   [[C1:%[0-9]+]]:i64 = CONST_I64 1,
; MIR-DAG:   {{^ *}}BEQ_I64 %bb.{{[0-9]+}}, %{{[0-9]+}}, [[C1]], implicit-def dead $arguments{{$}}
; MIR-DAG:   [[C2:%[0-9]+]]:i64 = CONST_I64 4294967296,
; MIR-DAG:   {{^ *}}BEQ_I64 %bb.{{[0-9]+}}, %{{[0-9]+}}, [[C2]], implicit-def dead $arguments{{$}}
; MIR-DAG:   [[C3:%[0-9]+]]:i64 = CONST_I64 1099511627776,
; MIR-DAG:   {{^ *}}BEQ_I64 %bb.{{[0-9]+}}, %{{[0-9]+}}, [[C3]], implicit-def dead $arguments{{$}}
; MIR-DAG:   [[C4:%[0-9]+]]:i64 = CONST_I64 -5,
; MIR-DAG:   {{^ *}}BEQ_I64 %bb.{{[0-9]+}}, %{{[0-9]+}}, [[C4]], implicit-def dead $arguments{{$}}

; CHECK-LABEL: .method {{.*}}'sparse64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NOT:   switch
; O2:          {{^}}  ldc.i8 4294967295{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  bgt.s @BB{{[0-9]+_[0-9]+}}{{$}}
; O0:          {{^}}  ldc.i4.s -5{{$}}
; O0-NEXT:     {{^}}  conv.i{{$}}
; O0-NEXT:     {{^}}  beq.s @BB{{[0-9]+_[0-9]+}}{{$}}
; O0:          {{^}}  ldc.i4.1{{$}}
; O0-NEXT:     {{^}}  conv.i{{$}}
; O0-NEXT:     {{^}}  beq.s @BB{{[0-9]+_[0-9]+}}{{$}}
; CHECK:       {{^}}  ldc.i8 4294967296{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; O0-NEXT:     {{^}}  beq.s @BB{{[0-9]+_[0-9]+}}{{$}}
; CHECK:       {{^}}  ldc.i8 1099511627776{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; O0-NEXT:     {{^}}  beq.s @BB{{[0-9]+_[0-9]+}}{{$}}
; CHECK-NOT:   switch
; CHECK:       {{^}}} // end of method 'sparse64'{{$}}
define i32 @sparse64(i64 %n) {
entry:
  switch i64 %n, label %def [
    i64 1, label %a
    i64 4294967296, label %b
    i64 1099511627776, label %c
    i64 -5, label %d
  ]
a:
  call void @foo0()
  ret i32 11
b:
  call void @foo1()
  ret i32 12
c:
  call void @foo2()
  ret i32 13
d:
  call void @foo3()
  ret i32 14
def:
  ret i32 0
}

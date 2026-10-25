; switch on int32 values (ImplementationPlan.md M6.a/M6.b, decision D35, plan
; 5.1 and 5.4 'BR_TABLE_I32'; recon isel.md 2.8, 5.3 and 11.2; port of
; llvm/test/CodeGen/WebAssembly/switch.ll). A dense switch becomes a jump
; table: BR_JT is lowered to MSILISD::BR_TABLE and selects BR_TABLE_I32,
; which prints 'switch (L0, L1, ...)' with one '@BB<function>_<block>' label
; per table entry (cases that share a destination repeat its label) and then
; 'br L0' on the next line. That 'br' names the first table entry (plan
; section 12, 'Switch contract amended'), a dummy target that never runs: the
; range check in front of 'switch' already sent every value outside the
; table to the default block. The jump-table index is int32
; (getJumpTableRegTy, D35), so an int32 switch value goes to 'switch' with
; no conversion, and no jump-table data is ever emitted (no JTI symbols, no
; data directives).
;
; LLVM's range check is never removed (there is no
; WebAssemblyFixBrTableDefaults): the value minus the lowest case is compared
; unsigned with the table size - 1 and 'bgt.un' goes to the default block
; before 'switch' runs. A sparse switch is fused compares ('beq', 'bgt', ...)
; and no 'switch': a balanced tree at -O2, a linear chain at -O0 (the generic
; SelectionDAGBuilder splits case clusters only when optimizing). A 'sub' of
; a constant prints as an 'add' of its negation (the generic DAGCombiner
; fold, at every level). Two switches in one function each get their own
; 'switch' and 'br'. The MIR lines check the BR_TABLE_I32 operands right after
; instruction selection: the int32 index, the table entries in case order,
; and the first entry once more as the last operand (the target of the
; 'br'), at -O0 and -O2. A native int switch value is range-checked at full
; width before its index is converted with conv.i4 ('wide64'; switch-i64.ll
; has more), and switch-unreachable-default.ll has switches without a range
; check.
;
; No jump table has more than 4096 entries (plan section 12, 'M6.a extended
; (jump-table size)'), at every optimization level and also when optimizing
; for size: MSILISD::BR_TABLE carries every entry as an operand of one
; SDNode, so a larger range must become several tables, or tables and
; compares, each behind LLVM's range check. The JT4096 and JTSPLIT lines
; check this at -O2 on modules that %{jtsize} (the Python script at the end
; of this file) generates at test time; every case block calls its own
; noinline function, so nothing folds the switch into arithmetic. 4096
; consecutive cases still make one 'switch' with 4096 targets. 4097
; consecutive cases (more than 10% dense, the default minimum), and the 2049
; cases 0, 2, ..., 4096 in an 'optsize' function (50% dense, above the 40%
; that optimizing for size needs; the generic isSuitableForJumpTable ignores
; the maximum table size when optimizing for size), make no 'switch' with
; more than 4096 targets. The lowest and the highest case of each module go
; to blocks of their own ('lo' and 'hi'), so no single table can hold both;
; the script counts the targets of every 'switch' and checks that the calls
; in both blocks are reachable from the method entry, so the case left out of
; a table is still reached through a compare and branch or a second
; 'switch'. Only -O2 is tested: at -O0 SelectionDAG builds a table only when
; the whole range fits in one (findJumpTables partitions the cases only when
; optimizing), so there the 4097 cases become a linear chain of 4097
; compares, which takes over ten seconds with -verify-machineinstrs.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=JTI \
; DEFINE:   --implicit-check-not=throw \
; DEFINE:   --implicit-check-not=.data --implicit-check-not=bytearray
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefixes=MIR,MIRO2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=MIR
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

; Jump-table size boundary. '%{jtsize} gen N STEP [ATTR]' writes a module
; whose @big switches on the N cases 0, STEP, 2*STEP, ...; '%{jtsize} count'
; copies llc's output and appends what it found in 'big'.
; DEFINE: %{jtsize} = %python -c "exec(''.join(l[4:] for l in open(r'%s') if l.startswith(';PY ')))"
; RUN: %{jtsize} gen 4096 1 > %t.jt4096.ll
; RUN: %{jtsize} gen 4097 1 > %t.jt4097.ll
; RUN: %{jtsize} gen 2049 2 optsize > %t.jtoptsize.ll
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t.jt4096.ll \
; RUN:   | %{jtsize} count | FileCheck %s --check-prefix=JT4096 --implicit-check-not=switch
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t.jt4096.ll \
; RUN:   | %{jtsize} count | FileCheck %s --check-prefix=JT4096 --implicit-check-not=switch
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t.jt4097.ll \
; RUN:   | %{jtsize} count | FileCheck %s --check-prefix=JTSPLIT --implicit-check-not="more than 4096"
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t.jt4097.ll \
; RUN:   | %{jtsize} count | FileCheck %s --check-prefix=JTSPLIT --implicit-check-not="more than 4096"
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t.jtoptsize.ll \
; RUN:   | %{jtsize} count | FileCheck %s --check-prefix=JTSPLIT --implicit-check-not="more than 4096"
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t.jtoptsize.ll \
; RUN:   | %{jtsize} count | FileCheck %s --check-prefix=JTSPLIT --implicit-check-not="more than 4096"
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %t.jt4096.ll -o %t.jt4096.il \
; RUN:   && %ilasm-verify -output=%t.jt4096.dll %t.jt4096.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil < %t.jt4097.ll -o %t.jt4097.il \
; RUN:   && %ilasm-verify -output=%t.jt4097.dll %t.jt4097.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %t.jtoptsize.ll -o %t.jtoptsize.il \
; RUN:   && %ilasm-verify -output=%t.jtoptsize.dll %t.jtoptsize.il %}

source_filename = "switch.c"

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

; Cases 0..23 go to six blocks (7, 8, 6, 1, 1 and 1 cases). The range check
; is 'value >u 23' on the int32 argument itself.
; MIR-LABEL: name: bar32{{$}}
; MIR:       [[C:%[0-9]+]]:i32 = CONST_I32 23,
; MIR:       {{^ *}}BGT_UN_I32 %bb.[[DEF:[0-9]+]], %{{[0-9]+}}, [[C]], implicit-def dead $arguments{{$}}
; MIR-NOT:   CONV
; MIR:       {{^ *}}BR_TABLE_I32 %{{[0-9]+}}, %bb.[[A:[0-9]+]], %bb.[[A]], %bb.[[A]], %bb.[[A]], %bb.[[A]], %bb.[[A]], %bb.[[A]], %bb.[[B:[0-9]+]], %bb.[[B]], %bb.[[B]], %bb.[[B]], %bb.[[B]], %bb.[[B]], %bb.[[B]], %bb.[[B]], %bb.[[C2:[0-9]+]], %bb.[[C2]], %bb.[[C2]], %bb.[[C2]], %bb.[[C2]], %bb.[[C2]], %bb.[[D:[0-9]+]], %bb.[[E:[0-9]+]], %bb.[[G:[0-9]+]], %bb.[[A]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[A]].sw.bb:
; MIR:       {{^ *}}bb.[[B]].sw.bb.1:
; MIR:       {{^ *}}bb.[[C2]].sw.bb.2:
; MIR:       {{^ *}}bb.[[D]].sw.bb.3:
; MIR:       {{^ *}}bb.[[E]].sw.bb.4:
; MIR:       {{^ *}}bb.[[G]].sw.bb.5:
; MIR:       {{^ *}}bb.[[DEF]].sw.epilog:

; CHECK-LABEL: .method {{.*}}'bar32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NOT:   conv.
; O2:          {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4.s 23{{$}}
; O2-NEXT:     {{^}}  bgt.un @BB[[F:[0-9]+]]_[[DEF:[0-9]+]]{{$}}
; O0:          {{^}}  ldc.i4.s 23{{$}}
; O0:          {{^}}  bgt.un @BB[[F:[0-9]+]]_[[DEF:[0-9]+]]{{$}}
; CHECK-NOT:   conv.
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
; CHECK-NEXT:  {{^}}} // end of method 'bar32'{{$}}
define void @bar32(i32 %n) {
entry:
  switch i32 %n, label %sw.epilog [
    i32 0, label %sw.bb
    i32 1, label %sw.bb
    i32 2, label %sw.bb
    i32 3, label %sw.bb
    i32 4, label %sw.bb
    i32 5, label %sw.bb
    i32 6, label %sw.bb
    i32 7, label %sw.bb.1
    i32 8, label %sw.bb.1
    i32 9, label %sw.bb.1
    i32 10, label %sw.bb.1
    i32 11, label %sw.bb.1
    i32 12, label %sw.bb.1
    i32 13, label %sw.bb.1
    i32 14, label %sw.bb.1
    i32 15, label %sw.bb.2
    i32 16, label %sw.bb.2
    i32 17, label %sw.bb.2
    i32 18, label %sw.bb.2
    i32 19, label %sw.bb.2
    i32 20, label %sw.bb.2
    i32 21, label %sw.bb.3
    i32 22, label %sw.bb.4
    i32 23, label %sw.bb.5
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

; Cases 10..15: the table index is value - 10 (an 'add' of -10), which is
; also the value the range check compares with 5.
; MIR-LABEL: name: offset32{{$}}
; MIR:       [[C:%[0-9]+]]:i32 = CONST_I32 5,
; MIR:       {{^ *}}BGT_UN_I32 %bb.{{[0-9]+}}, %{{[0-9]+}}, [[C]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}BR_TABLE_I32 %{{[0-9]+}}, %bb.[[C10:[0-9]+]], %bb.[[C11:[0-9]+]], %bb.[[C12:[0-9]+]], %bb.[[C13:[0-9]+]], %bb.[[C14:[0-9]+]], %bb.[[C15:[0-9]+]], %bb.[[C10]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[C10]].c10:
; MIR:       {{^ *}}bb.[[C11]].c11:
; MIR:       {{^ *}}bb.[[C12]].c12:
; MIR:       {{^ *}}bb.[[C13]].c13:
; MIR:       {{^ *}}bb.[[C14]].c14:
; MIR:       {{^ *}}bb.[[C15]].c15:

; CHECK-LABEL: .method {{.*}}'offset32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NOT:   conv.
; O2:          {{^}}  ldc.i4.s -10{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2:          {{^}}  ldc.i4.5{{$}}
; O2-NEXT:     {{^}}  bgt.un.s @BB[[F:[0-9]+]]_[[DEF:[0-9]+]]{{$}}
; O0:          {{^}}  add{{$}}
; O0:          {{^}}  bgt.un.s @BB[[F:[0-9]+]]_[[DEF:[0-9]+]]{{$}}
; CHECK-NOT:   conv.
; CHECK:       {{^}}  switch ([[L10:@BB[0-9]+_[0-9]+]], [[L11:@BB[0-9]+_[0-9]+]], [[L12:@BB[0-9]+_[0-9]+]], [[L13:@BB[0-9]+_[0-9]+]], [[L14:@BB[0-9]+_[0-9]+]], [[L15:@BB[0-9]+_[0-9]+]]){{$}}
; CHECK-NEXT:  {{^}}  br.s [[L10]]{{$}}
; CHECK:       {{^}}[[L10]]:{{$}}
; CHECK:       {{^}}  ldc.i4.7{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK:       {{^}}[[L11]]:{{$}}
; CHECK:       {{^}}  mul{{$}}
; CHECK:       {{^}}[[L12]]:{{$}}
; CHECK:       {{^}}  xor{{$}}
; CHECK:       {{^}}[[L13]]:{{$}}
; CHECK:       {{^}}  shl{{$}}
; CHECK:       {{^}}[[L14]]:{{$}}
; CHECK:       {{^}}  ldc.i4.s -9{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK:       {{^}}[[L15]]:{{$}}
; CHECK:       {{^}}  or{{$}}
; CHECK:       {{^}}@BB[[F]]_[[DEF]]:{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.m1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'offset32'{{$}}
define i32 @offset32(i32 %n) {
entry:
  switch i32 %n, label %def [
    i32 10, label %c10
    i32 11, label %c11
    i32 12, label %c12
    i32 13, label %c13
    i32 14, label %c14
    i32 15, label %c15
  ]
c10:
  %r10 = add i32 %n, 7
  ret i32 %r10
c11:
  %r11 = mul i32 %n, 3
  ret i32 %r11
c12:
  %r12 = xor i32 %n, 5
  ret i32 %r12
c13:
  %r13 = shl i32 %n, 2
  ret i32 %r13
c14:
  %r14 = sub i32 %n, 9
  ret i32 %r14
c15:
  %r15 = or i32 %n, 64
  ret i32 %r15
def:
  ret i32 -1
}

; Negative cases -3..2: the index is value + 3; a negative value below -3
; wraps to a large unsigned index and takes the default.
; CHECK-LABEL: .method {{.*}}'neg32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NOT:   conv.
; O2:          {{^}}  ldc.i4.3{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2:          {{^}}  ldc.i4.5{{$}}
; O2-NEXT:     {{^}}  bgt.un.s @BB[[F:[0-9]+]]_[[DEF:[0-9]+]]{{$}}
; O0:          {{^}}  add{{$}}
; O0:          {{^}}  bgt.un.s @BB[[F:[0-9]+]]_[[DEF:[0-9]+]]{{$}}
; CHECK-NOT:   conv.
; CHECK:       {{^}}  switch ([[A:@BB[0-9]+_[0-9]+]], [[B:@BB[0-9]+_[0-9]+]], [[A]], [[C:@BB[0-9]+_[0-9]+]], [[B]], [[D:@BB[0-9]+_[0-9]+]]){{$}}
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
; CHECK-NEXT:  {{^}}} // end of method 'neg32'{{$}}
define i32 @neg32(i32 %n) {
entry:
  switch i32 %n, label %def [
    i32 -3, label %a
    i32 -2, label %b
    i32 -1, label %a
    i32 0, label %c
    i32 1, label %b
    i32 2, label %d
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

; A zeroext i16 switch value is already an int32: no sub, no conversion.
; CHECK-LABEL: .method {{.*}}'narrow16'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NOT:   conv.
; O2:          {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4.4{{$}}
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
; CHECK-NEXT:  {{^}}} // end of method 'narrow16'{{$}}
define void @narrow16(i16 zeroext %n) {
entry:
  switch i16 %n, label %def [
    i16 0, label %a
    i16 1, label %b
    i16 2, label %c
    i16 3, label %d
    i16 4, label %e
  ]
a:
  call void @foo0()
  br label %def
b:
  call void @foo1()
  br label %def
c:
  call void @foo2()
  br label %def
d:
  call void @foo3()
  br label %def
e:
  call void @foo4()
  br label %def
def:
  ret void
}

; A native int switch value (the WebAssembly test's bar64; switch-i64.ll has
; the rest): the full-width value is range-checked as a native int, and only
; the index that passed is converted with conv.i4 for 'switch'.
; MIR-LABEL: name: wide64{{$}}
; MIR:       [[N:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR-DAG:   [[IDX:%[0-9]+]]:i32 = CONV_I4_I64 [[N]], implicit-def dead $arguments
; MIR-DAG:   [[C:%[0-9]+]]:i64 = CONST_I64 4,
; MIR:       {{^ *}}BGT_UN_I64 %bb.{{[0-9]+}}, [[N]], [[C]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}BR_TABLE_I32 [[IDX]], %bb.[[W0:[0-9]+]], %bb.{{[0-9]+}}, %bb.{{[0-9]+}}, %bb.{{[0-9]+}}, %bb.{{[0-9]+}}, %bb.[[W0]], implicit-def dead $arguments{{$}}

; CHECK-LABEL: .method {{.*}}'wide64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4{{(\.4| 4)}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  bgt.un.s @BB[[F:[0-9]+]]_[[DEF:[0-9]+]]{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  conv.i4{{$}}
; O0:          {{^}}  bgt.un.s @BB[[F:[0-9]+]]_[[DEF:[0-9]+]]{{$}}
; O0-NOT:      conv.
; CHECK:       {{^}}  switch (@BB[[F]]_[[W0:[0-9]+]], @BB[[F]]_{{[0-9]+}}, @BB[[F]]_{{[0-9]+}}, @BB[[F]]_{{[0-9]+}}, @BB[[F]]_{{[0-9]+}}){{$}}
; CHECK-NEXT:  {{^}}  br.s @BB[[F]]_[[W0]]{{$}}
; CHECK:       {{^}}@BB[[F]]_[[DEF]]:{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'wide64'{{$}}
define void @wide64(i64 %n) {
entry:
  switch i64 %n, label %def [
    i64 0, label %a
    i64 1, label %b
    i64 2, label %c
    i64 3, label %d
    i64 4, label %e
  ]
a:
  call void @foo0()
  br label %def
b:
  call void @foo1()
  br label %def
c:
  call void @foo2()
  br label %def
d:
  call void @foo3()
  br label %def
e:
  call void @foo4()
  br label %def
def:
  ret void
}

; Five cases spread over 1..100000 are too sparse for a table: fused
; compares on the int32 value, one per case. At -O2 they form a balanced
; tree with the pivot 'bgt 999'; at -O0 they are a linear chain of 'beq' in
; case order, each followed by 'br' to the next test.
; MIR-LABEL: name: sparse32{{$}}
; MIR-NOT:   BR_TABLE
; MIRO2-DAG: [[P:%[0-9]+]]:i32 = CONST_I32 999,
; MIRO2-DAG: {{^ *}}BGT_I32 %bb.{{[0-9]+}}, %{{[0-9]+}}, [[P]], implicit-def dead $arguments{{$}}
; MIR-DAG:   [[C1:%[0-9]+]]:i32 = CONST_I32 1,
; MIR-DAG:   {{^ *}}BEQ_I32 %bb.{{[0-9]+}}, %{{[0-9]+}}, [[C1]], implicit-def dead $arguments{{$}}
; MIR-DAG:   [[C2:%[0-9]+]]:i32 = CONST_I32 100,
; MIR-DAG:   {{^ *}}BEQ_I32 %bb.{{[0-9]+}}, %{{[0-9]+}}, [[C2]], implicit-def dead $arguments{{$}}
; MIR-DAG:   [[C3:%[0-9]+]]:i32 = CONST_I32 1000,
; MIR-DAG:   {{^ *}}BEQ_I32 %bb.{{[0-9]+}}, %{{[0-9]+}}, [[C3]], implicit-def dead $arguments{{$}}
; MIR-DAG:   [[C4:%[0-9]+]]:i32 = CONST_I32 10000,
; MIR-DAG:   {{^ *}}BEQ_I32 %bb.{{[0-9]+}}, %{{[0-9]+}}, [[C4]], implicit-def dead $arguments{{$}}
; MIR-DAG:   [[C5:%[0-9]+]]:i32 = CONST_I32 100000,
; MIR-DAG:   {{^ *}}BEQ_I32 %bb.{{[0-9]+}}, %{{[0-9]+}}, [[C5]], implicit-def dead $arguments{{$}}

; CHECK-LABEL: .method {{.*}}'sparse32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NOT:   switch
; O2:          {{^}}  ldc.i4 999{{$}}
; O2-NEXT:     {{^}}  bgt.s @BB{{[0-9]+_[0-9]+}}{{$}}
; O0:          {{^}}  ldc.i4.1{{$}}
; O0-NEXT:     {{^}}  beq.s @BB{{[0-9]+_[0-9]+}}{{$}}
; O0:          {{^}}  ldc.i4.s 100{{$}}
; O0-NEXT:     {{^}}  beq.s @BB{{[0-9]+_[0-9]+}}{{$}}
; O0:          {{^}}  ldc.i4 1000{{$}}
; O0-NEXT:     {{^}}  beq.s @BB{{[0-9]+_[0-9]+}}{{$}}
; O0:          {{^}}  ldc.i4 10000{{$}}
; O0-NEXT:     {{^}}  beq.s @BB{{[0-9]+_[0-9]+}}{{$}}
; O0:          {{^}}  ldc.i4 100000{{$}}
; O0-NEXT:     {{^}}  beq.s @BB{{[0-9]+_[0-9]+}}{{$}}
; CHECK-NOT:   switch
; CHECK:       {{^}}} // end of method 'sparse32'{{$}}
define i32 @sparse32(i32 %n) {
entry:
  switch i32 %n, label %def [
    i32 1, label %a
    i32 100, label %b
    i32 1000, label %c
    i32 10000, label %d
    i32 100000, label %e
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
e:
  call void @foo4()
  ret i32 15
def:
  ret i32 0
}

; Two jump tables in one function, each with its own range check, 'switch'
; and 'br'.
; MIR-LABEL: name: two_switches{{$}}
; MIR:       {{^ *}}BR_TABLE_I32 %{{[0-9]+}}, %bb.[[X0:[0-9]+]], %bb.{{[0-9]+}}, %bb.{{[0-9]+}}, %bb.{{[0-9]+}}, %bb.{{[0-9]+}}, %bb.[[X0]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}BR_TABLE_I32 %{{[0-9]+}}, %bb.[[Y0:[0-9]+]], %bb.{{[0-9]+}}, %bb.{{[0-9]+}}, %bb.{{[0-9]+}}, %bb.{{[0-9]+}}, %bb.[[Y0]], implicit-def dead $arguments{{$}}

; CHECK-LABEL: .method {{.*}}'two_switches'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK:       {{^}}  bgt.un.s @BB[[F:[0-9]+]]_{{[0-9]+}}{{$}}
; CHECK:       {{^}}  switch (@BB[[F]]_[[X0:[0-9]+]], @BB[[F]]_{{[0-9]+}}, @BB[[F]]_{{[0-9]+}}, @BB[[F]]_{{[0-9]+}}, @BB[[F]]_{{[0-9]+}}){{$}}
; CHECK-NEXT:  {{^}}  br.s @BB[[F]]_[[X0]]{{$}}
; CHECK:       {{^}}  bgt.un.s @BB[[F]]_{{[0-9]+}}{{$}}
; CHECK:       {{^}}  switch (@BB[[F]]_[[Y0:[0-9]+]], @BB[[F]]_{{[0-9]+}}, @BB[[F]]_{{[0-9]+}}, @BB[[F]]_{{[0-9]+}}, @BB[[F]]_{{[0-9]+}}){{$}}
; CHECK-NEXT:  {{^}}  br.s @BB[[F]]_[[Y0]]{{$}}
; CHECK-NOT:   switch
; CHECK:       {{^}}} // end of method 'two_switches'{{$}}
define i32 @two_switches(i32 %x, i32 %y) {
entry:
  switch i32 %x, label %mid [
    i32 0, label %a0
    i32 1, label %a1
    i32 2, label %a2
    i32 3, label %a3
    i32 4, label %a4
  ]
a0:
  call void @foo0()
  br label %mid
a1:
  call void @foo1()
  br label %mid
a2:
  call void @foo2()
  br label %mid
a3:
  call void @foo3()
  br label %mid
a4:
  call void @foo4()
  br label %mid
mid:
  switch i32 %y, label %out [
    i32 0, label %b0
    i32 1, label %b1
    i32 2, label %b2
    i32 3, label %b3
    i32 4, label %b4
  ]
b0:
  call void @foo5()
  ret i32 10
b1:
  call void @foo4()
  ret i32 11
b2:
  call void @foo3()
  ret i32 12
b3:
  call void @foo2()
  ret i32 13
b4:
  call void @foo1()
  ret i32 14
out:
  ret i32 -1
}

; ---- Jump-table size boundary: checks and the %{jtsize} script ----

; 4096 consecutive cases (0 .. 4095): one range check against 4095 and one
; 'switch' with 4096 targets, the first of which is case 0's block 'lo'.
; JT4096-LABEL: .method {{.*}}'big'(int32) cil managed{{$}}
; JT4096:       {{^}}  ldc.i4 4095{{$}}
; JT4096-NEXT:  {{^}}  bgt.un @BB[[F:[0-9]+]]_[[DEF:[0-9]+]]{{$}}
; JT4096-NEXT:  {{^}}  ldarg.0{{$}}
; JT4096-NEXT:  {{^}}  switch ([[LO:@BB[0-9]+_[0-9]+]], {{.*}}){{$}}
; JT4096-NEXT:  {{^}}  br.s [[LO]]{{$}}
; JT4096:       {{^}}[[LO]]:{{$}}
; JT4096-NEXT:  {{^}}  call void 'lo'(){{$}}
; JT4096:       {{^}}@BB[[F]]_[[DEF]]:{{$}}
; JT4096-NEXT:  {{^}}  ldc.i4.s 99{{$}}
; JT4096:       {{^}}} // end of method 'big'{{$}}
; JT4096:       {{^}}switch with 4096 targets{{$}}
; JT4096-NEXT:  {{^}}lo is reached{{$}}
; JT4096-NEXT:  {{^}}hi is reached{{$}}

; 4097 cases (0 .. 4096), and 2049 cases (0, 2, .., 4096) in an optsize
; function: at least one 'switch', none with more than 4096 targets
; (--implicit-check-not), the 'br' after the first one names its first
; target, and both 'lo' (case 0) and 'hi' (case 4096) are reachable.
; JTSPLIT-LABEL: .method {{.*}}'big'(int32) cil managed{{$}}
; JTSPLIT:       {{^}}  switch ([[T0:@BB[0-9]+_[0-9]+]], {{.*}}){{$}}
; JTSPLIT-NEXT:  {{^}}  br [[T0]]{{$}}
; JTSPLIT:       {{^}}} // end of method 'big'{{$}}
; JTSPLIT:       {{^}}switch with {{[0-9]+}} targets{{$}}
; JTSPLIT:       {{^}}lo is reached{{$}}
; JTSPLIT-NEXT:  {{^}}hi is reached{{$}}

; The DEFINE of %{jtsize} runs the lines below that start with ';PY ',
; without that prefix, as one Python program.
;PY import re, sys
;PY sys.stdout.reconfigure(newline='\n')
;PY if sys.argv[1] == 'gen':
;PY     # gen N STEP [ATTR...]: @big switches on the N values 0, STEP, ...;
;PY     # the lowest goes to block lo, the highest to block hi and the others
;PY     # to f0 .. f4 in turn. Every block calls its own noinline function.
;PY     n, step = int(sys.argv[2]), int(sys.argv[3])
;PY     blocks = ['lo', 'hi'] + ['f' + str(k) for k in range(5)]
;PY     dests = ['lo'] + ['f' + str(i % 5) for i in range(1, n - 1)] + ['hi']
;PY     print('source_filename = "jt-size.c"')
;PY     for b in blocks:
;PY         print('define void @' + b + '() noinline {\n  ret void\n}')
;PY     print('define i32 @big(i32 %x) ' + ' '.join(sys.argv[4:]) + ' {')
;PY     print('entry:\n  switch i32 %x, label %def [')
;PY     for i, d in enumerate(dests):
;PY         print('    i32 ' + str(i * step) + ', label %' + d)
;PY     print('  ]')
;PY     for k, b in enumerate(blocks):
;PY         print(b + ':\n  call void @' + b + '()\n  ret i32 ' + str(k))
;PY     print('def:\n  ret i32 99\n}')
;PY else:
;PY     # count: copies the CIL from stdin, then prints the number of targets
;PY     # of every 'switch' in 'big' and whether the calls of lo and hi are
;PY     # reachable from its entry: 'switch' and branches go to their labels,
;PY     # and every instruction but 'br', 'br.s', 'ret' and 'throw' also falls
;PY     # through to the next one.
;PY     il = sys.stdin.read()
;PY     sys.stdout.write(il)
;PY     lines = il.split('\n')
;PY     start = [i for i, l in enumerate(lines)
;PY              if l.startswith('.method ') and "'big'(" in l][0]
;PY     code, at = [], {}
;PY     for l in lines[start + 1:]:
;PY         l = l.split('//')[0].strip()
;PY         if l.startswith('}'):
;PY             break
;PY         if l.startswith('@BB') and l.endswith(':'):
;PY             at[l[:-1]] = len(code)
;PY         elif l and l[0] not in '{.':
;PY             code.append(l)
;PY     seen, todo = set(), [0]
;PY     while todo:
;PY         i = todo.pop()
;PY         if i in seen or i >= len(code):
;PY             continue
;PY         seen.add(i)
;PY         todo += [at[t] for t in re.findall(r'@BB[0-9]+_[0-9]+', code[i])]
;PY         if code[i].split()[0] not in ('br', 'br.s', 'ret', 'throw'):
;PY             todo.append(i + 1)
;PY     for l in code:
;PY         if l.startswith('switch '):
;PY             n = l.count('@BB')
;PY             over = ', more than 4096' if n > 4096 else ''
;PY             print('switch with', n, 'targets' + over)
;PY     for f in ('lo', 'hi'):
;PY         live = [i for i in seen if code[i] == "call void '" + f + "'()"]
;PY         print(f, 'is reached' if live else 'is not reached')

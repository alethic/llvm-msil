; switch with an unreachable default (ImplementationPlan.md M6.a/M6.b,
; decision D35, plan 5.1 and 5.4 'BR_TABLE_I32'; port of
; llvm/test/CodeGen/WebAssembly/switch-unreachable-default.ll). When the
; default destination is 'unreachable', SelectionDAGBuilder omits the range
; check: a jump table goes straight to 'switch' (no compare and no branch in
; front of it) and its index is still int32 ('conv.i4' for a native int). An
; index outside the table is then undefined behaviour. As after every
; 'switch', the 'br' on the next line names the first table entry (plan
; section 12, 'Switch contract amended'; the MIR lines check that the last
; BR_TABLE_I32 operand is the first one), never the removed default block,
; and the output must assemble. The unreachable default block itself is gone
; (no 'ldnull; throw' for it).
;
; Switches too small for a table (fewer than four cases) and the WebAssembly
; 'split' function (an i8 switch whose case blocks are 'unreachable') become
; compare chains whose last compare is omitted; their 'unreachable' case
; blocks end in 'ldnull; throw' (TrapUnreachable). The MIR lines check that
; nothing but the BR_TABLE_I32 terminates the switch block.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=JTI
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=MIR
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "switch-unreachable-default.c"

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

; Two cases: a single compare decides between them.
; CHECK-LABEL: .method {{.*}}'foo'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NOT:   {{switch|throw}}
; CHECK:       {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NOT:   {{switch|throw}}
; CHECK:       {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'foo'{{$}}
define i32 @foo(i32 %x) {
entry:
  switch i32 %x, label %unreachable [
    i32 0, label %bb0
    i32 1, label %bb1
  ]

bb0:
  ret i32 0

bb1:
  ret i32 1

unreachable:
  unreachable
}

; CHECK-LABEL: .method {{.*}}'split'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NOT:   switch
; CHECK-DAG:   {{^}}  ret{{$}}
; CHECK-DAG:   {{^}}  ldnull{{$}}
; CHECK-DAG:   {{^}}  throw{{$}}
; CHECK-NOT:   switch
; CHECK:       {{^}}} // end of method 'split'{{$}}
define void @split(i8 %c) {
entry:
  switch i8 %c, label %sw.default [
    i8 114, label %return
    i8 103, label %sw.bb1
    i8 98, label %sw.bb2
    i8 97, label %sw.bb3
    i8 48, label %sw.bb4
    i8 49, label %sw.bb5
  ]

sw.bb1:
  unreachable

sw.bb2:
  unreachable

sw.bb3:
  unreachable

sw.bb4:
  unreachable

sw.bb5:
  unreachable

sw.default:
  unreachable

return:
  ret void
}

; Five dense int32 cases: 'switch' on the argument with no range check.
; MIR-LABEL: name: dense5{{$}}
; MIR-NOT:   {{^ *(BR_IF|BR_UNLESS|BGT_UN_I32|BGE_UN_I32|BLT_UN_I32|BLE_UN_I32) }}
; MIR:       {{^ *}}BR_TABLE_I32 %{{[0-9]+}}, %bb.[[A:[0-9]+]], %bb.[[B:[0-9]+]], %bb.[[C:[0-9]+]], %bb.[[D:[0-9]+]], %bb.[[E:[0-9]+]], %bb.[[A]], implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[A]].a:
; MIR:       {{^ *}}bb.[[E]].e:

; CHECK-LABEL: .method {{.*}}'dense5'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NOT:   {{^}}  {{b[a-z.]+|conv.*}}
; CHECK:       {{^}}  switch ([[A:@BB[0-9]+_[0-9]+]], [[B:@BB[0-9]+_[0-9]+]], [[C:@BB[0-9]+_[0-9]+]], [[D:@BB[0-9]+_[0-9]+]], [[E:@BB[0-9]+_[0-9]+]]){{$}}
; CHECK-NEXT:  {{^}}  br.s [[A]]{{$}}
; CHECK-NOT:   throw
; CHECK:       {{^}}[[A]]:{{$}}
; CHECK-NEXT:  {{^}}  call void 'foo0'(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK:       {{^}}[[B]]:{{$}}
; CHECK-NEXT:  {{^}}  call void 'foo1'(){{$}}
; CHECK:       {{^}}[[C]]:{{$}}
; CHECK-NEXT:  {{^}}  call void 'foo2'(){{$}}
; CHECK:       {{^}}[[D]]:{{$}}
; CHECK-NEXT:  {{^}}  call void 'foo3'(){{$}}
; CHECK:       {{^}}[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  call void 'foo4'(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'dense5'{{$}}
define void @dense5(i32 %x) {
entry:
  switch i32 %x, label %unreachable [
    i32 0, label %a
    i32 1, label %b
    i32 2, label %c
    i32 3, label %d
    i32 4, label %e
  ]
a:
  call void @foo0()
  ret void
b:
  call void @foo1()
  ret void
c:
  call void @foo2()
  ret void
d:
  call void @foo3()
  ret void
e:
  call void @foo4()
  ret void
unreachable:
  unreachable
}

; Five dense native int cases 10..14, and there is still no range check (at
; either width). With no range check the generic DAG narrows the index
; (trunc (sub x, 10)) to (add (trunc x), -10): the value is converted with
; conv.i4 first, and the int32 'add' of -10 is the index. That is the same
; index modulo 2^32, and a value outside the table is undefined behaviour.
; MIR-LABEL: name: dense5_i64{{$}}
; MIR:       [[N:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR-NOT:   {{^ *(BR_IF|BR_UNLESS|B(GT|GE|LT|LE)_UN_I(32|64)) }}
; MIR-DAG:   [[CV:%[0-9]+]]:i32 = CONV_I4_I64 [[N]], implicit-def dead $arguments
; MIR-DAG:   [[K:%[0-9]+]]:i32 = CONST_I32 -10,
; MIR-NOT:   {{^ *(BR_IF|BR_UNLESS|B(GT|GE|LT|LE)_UN_I(32|64)) }}
; MIR:       [[IDX:%[0-9]+]]:i32 = ADD_I32 [[CV]], [[K]], implicit-def dead $arguments
; MIR-NOT:   {{^ *(BR_IF|BR_UNLESS|B(GT|GE|LT|LE)_UN_I(32|64)) }}
; MIR:       {{^ *}}BR_TABLE_I32 [[IDX]], %bb.[[A:[0-9]+]], %bb.{{[0-9]+}}, %bb.{{[0-9]+}}, %bb.{{[0-9]+}}, %bb.{{[0-9]+}}, %bb.[[A]], implicit-def dead $arguments{{$}}

; CHECK-LABEL: .method {{.*}}'dense5_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NOT:   {{^}}  b{{[a-z.]+}}
; CHECK:       {{^}}  conv.i4{{$}}
; CHECK-NOT:   {{^}}  b{{[a-z.]+}}
; CHECK:       {{^}}  switch ([[A:@BB[0-9]+_[0-9]+]], [[B:@BB[0-9]+_[0-9]+]], [[C:@BB[0-9]+_[0-9]+]], [[D:@BB[0-9]+_[0-9]+]], [[E:@BB[0-9]+_[0-9]+]]){{$}}
; CHECK-NEXT:  {{^}}  br.s [[A]]{{$}}
; CHECK-NOT:   throw
; CHECK:       {{^}}[[A]]:{{$}}
; CHECK-NEXT:  {{^}}  call void 'foo0'(){{$}}
; CHECK:       {{^}}[[E]]:{{$}}
; CHECK-NEXT:  {{^}}  call void 'foo4'(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'dense5_i64'{{$}}
define void @dense5_i64(i64 %x) {
entry:
  switch i64 %x, label %unreachable [
    i64 10, label %a
    i64 11, label %b
    i64 12, label %c
    i64 13, label %d
    i64 14, label %e
  ]
a:
  call void @foo0()
  ret void
b:
  call void @foo1()
  ret void
c:
  call void @foo2()
  ret void
d:
  call void @foo3()
  ret void
e:
  call void @foo4()
  ret void
unreachable:
  unreachable
}

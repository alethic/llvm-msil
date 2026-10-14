; Irreducible control flow (ImplementationPlan.md M6; Design.md section 3,
; 'WebAssemblyCFGStackify and WebAssemblyFixIrreducibleControlFlow' are not
; carried over: CIL allows arbitrary branches inside a method as long as the
; evaluation stack is consistent at every branch target). A cycle with more
; than one entry block compiles as it is: each entry is a plain branch target,
; there is no dispatch block, no 'switch' and no helper local, and every
; value that crosses a block boundary goes through a local (MSILRegStackify
; keeps the stack empty there; MSILStackCheck verifies it). The output
; assembles and passes -verify-machineinstrs at -O0 and -O2.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=switch \
; DEFINE:   --implicit-check-not="{{^ +b[a-z.]+\.s }}" --implicit-check-not=throw
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
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "irreducible.c"

; Two blocks that branch to each other, each entered from the entry block.
; The entry block's condition (x < 0, true block next) is inverted to
; 'x > -1': 'bgt' to b, falling through to a. At -O2 the fall-through edge
; first stores a's PHI input (0); the store is not pinned to a local or an
; argument slot.
; MIR-LABEL: name: irr{{$}}
; MIR:       {{^ *}}BGT_I32 %bb.[[B:[0-9]+]], %{{[0-9]+}}, %{{[0-9]+}}, implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[A:[0-9]+]].a:
; MIR:       {{^ *}}BGE_I32 %bb.[[EXIT:[0-9]+]], %{{[0-9]+}}, %{{[0-9]+}}, implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[B]].b:
; MIR:       {{^ *}}BLT_I32 %bb.[[A]], %{{[0-9]+}}, %{{[0-9]+}}, implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[EXIT]].exit:

; CHECK-LABEL: .method {{.*}}'irr'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK:       {{^}}  bgt @BB[[F:[0-9]+]]_[[B:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[A:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[A]]:{{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  {{stloc|starg}}
; O2-NEXT:     {{^}}@BB[[F]]_[[A:[0-9]+]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  add{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  bge @BB[[F]]_[[EXIT:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[B]]{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[B]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  mul{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  blt @BB[[F]]_[[A]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[EXIT]]{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'irr'{{$}}
define i32 @irr(i32 %x, i32 %n) {
entry:
  %c = icmp slt i32 %x, 0
  br i1 %c, label %a, label %b
a:
  %ia = phi i32 [ 0, %entry ], [ %ib1, %b ]
  %ia1 = add i32 %ia, 1
  %ca = icmp slt i32 %ia1, %n
  br i1 %ca, label %b, label %exit
b:
  %ib = phi i32 [ 5, %entry ], [ %ia1, %a ]
  %ib1 = mul i32 %ib, 3
  %cb = icmp slt i32 %ib1, %n
  br i1 %cb, label %a, label %exit
exit:
  %r = phi i32 [ %ia1, %a ], [ %ib1, %b ]
  ret i32 %r
}

; A three-block cycle a -> b -> c -> a that a small switch can enter at any
; of the three blocks; every block can also leave it. The three blocks keep
; their order and their code, and c branches back to a.
; MIR-LABEL: name: irr3{{$}}
; MIR:       {{^ *}}bb.[[A:[0-9]+]].a:
; MIR:       {{^ *}}BGE_UN_I32 %bb.[[EXIT:[0-9]+]], %{{[0-9]+}}, %{{[0-9]+}}, implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.{{[0-9]+}}.b:
; MIR:       {{^ *}}BGT_UN_I32 %bb.[[EXIT]], %{{[0-9]+}}, %{{[0-9]+}}, implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.{{[0-9]+}}.c:
; MIR:       {{^ *}}BR_IF %bb.[[A]], %{{[0-9]+}}, implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[EXIT]].exit:

; CHECK-LABEL: .method {{.*}}'irr3'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4.7{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK:       {{^}}  bge.un @BB[[F:[0-9]+]]_[[EXIT:[0-9]+]]{{$}}
; CHECK:       {{^}}  ldc.i4.s 9{{$}}
; CHECK-NEXT:  {{^}}  xor{{$}}
; CHECK:       {{^}}  bgt.un @BB[[F]]_[[EXIT]]{{$}}
; CHECK:       {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  shl{{$}}
; CHECK:       {{^}}  brtrue @BB[[F]]_{{[0-9]+}}{{$}}
; CHECK:       {{^}}@BB[[F]]_[[EXIT]]:{{$}}
; CHECK-NOT:   {{^}}@
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'irr3'{{$}}
define i32 @irr3(i32 %x, i32 %n) {
entry:
  %k = and i32 %x, 3
  switch i32 %k, label %c [
    i32 0, label %a
    i32 1, label %b
  ]
a:
  %va = phi i32 [ %x, %entry ], [ %vc1, %c ]
  %va1 = add i32 %va, 7
  %ta = icmp ult i32 %va1, %n
  br i1 %ta, label %b, label %exit
b:
  %vb = phi i32 [ %x, %entry ], [ %va1, %a ]
  %vb1 = xor i32 %vb, 9
  %tb = icmp ugt i32 %vb1, %n
  br i1 %tb, label %exit, label %c
c:
  %vc = phi i32 [ %x, %entry ], [ %vb1, %b ]
  %vc1 = shl i32 %vc, 1
  %tc = icmp eq i32 %vc1, 0
  br i1 %tc, label %exit, label %a
exit:
  %r = phi i32 [ %va1, %a ], [ %vb1, %b ], [ %vc1, %c ]
  ret i32 %r
}

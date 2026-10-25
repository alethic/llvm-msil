; The encoding of 'switch' (ImplementationPlan.md M18.a (2)-(3), M18.b; plan
; 5.1 and decision D35). BR_TABLE_I32 is two MCInsts: 'switch' and the 'br'
; to its default operand. The switch is 0x45, the target count as a u32, then
; one 32-bit entry per target relative to the end of the whole instruction,
; ECMA-335 III.3.66: entry i (from 0) of N is a PC-relative FK_PCRel_4 fixup
; on 'label - 4 * (N - i)' at offset 5 + 4 * i, which MCAssembler resolves to
; label - (fixup position + 4 * (N - i)) = label - end of the switch (recon
; p2/mc-streamer.md 5.4 and E3). Switch entries are always 32-bit, even for a
; target right after the switch. The 'br' after the switch is an ordinary
; branch and takes the form its displacement allows: br.s in 'sw', where its
; target follows directly, and br in 'swfar', where the 122-byte call of 'd13'
; and three more cases lie between. An i64 index is range-checked at full
; width (its bound is the CONST_I64 'ldc.i4.3; conv.i', shortest since Q26)
; and converted with conv.i4 before the switch. The range checks are
; ordinary branches too: bgt.un.s, or bgt.un over the filler. Every byte was
; checked against Framework ilasm's image of the same text.

; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -show-mc-encoding \
; RUN:   | FileCheck %s
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -show-mc-encoding \
; RUN:   | FileCheck %s
; RUN: %if ilasm %{ llc < %s -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -show-mc-encoding -o %t.il && %ilasm-verify -output=%t.dll %t.il %}

define void @i(i32 %x) {
  ret void
}

define void @d13(double, double, double, double, double, double, double, double, double, double, double, double, double) {
  ret void
}

; CHECK-LABEL: .method {{.*}}'sw'(
; CHECK:      .maxstack 2
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: ldarg.0 // encoding: [0x02]
; CHECK-NEXT: ldc.i4.3 // encoding: [0x19]
; CHECK-NEXT: bgt.un.s @[[DEF:BB[0-9]+_[0-9]+]] // encoding: [0x35,A]
; CHECK-NEXT: // fixup A - offset: 1, value: @[[DEF]]-1, kind: FK_PCRel_1
; CHECK-NEXT: // %bb.1:
; CHECK-NEXT: ldarg.0 // encoding: [0x02]
; CHECK-NEXT: switch (@[[C0:BB[0-9]+_[0-9]+]], @[[C1:BB[0-9]+_[0-9]+]], @[[C2:BB[0-9]+_[0-9]+]], @[[C3:BB[0-9]+_[0-9]+]]) // encoding: [0x45,0x04,0x00,0x00,0x00,A,A,A,A,B,B,B,B,C,C,C,C,D,D,D,D]
; CHECK-NEXT: // fixup A - offset: 5, value: @[[C0]]-16, kind: FK_PCRel_4
; CHECK-NEXT: // fixup B - offset: 9, value: @[[C1]]-12, kind: FK_PCRel_4
; CHECK-NEXT: // fixup C - offset: 13, value: @[[C2]]-8, kind: FK_PCRel_4
; CHECK-NEXT: // fixup D - offset: 17, value: @[[C3]]-4, kind: FK_PCRel_4
; CHECK-NEXT: br.s @[[C0]] // encoding: [0x2b,A]
; CHECK-NEXT: // fixup A - offset: 1, value: @[[C0]]-1, kind: FK_PCRel_1
; CHECK-NEXT: @[[C0]]:
; CHECK-NEXT: ldc.i4.s 10 // encoding: [0x1f,0x0a]
; CHECK-NEXT: call void 'i'(int32) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'i'(int32), kind: fixup_cil_method_token
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: @[[C1]]:
; CHECK-NEXT: ldc.i4.s 11 // encoding: [0x1f,0x0b]
; CHECK-NEXT: call void 'i'(int32) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'i'(int32), kind: fixup_cil_method_token
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: @[[C2]]:
; CHECK-NEXT: ldc.i4.s 12 // encoding: [0x1f,0x0c]
; CHECK-NEXT: call void 'i'(int32) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'i'(int32), kind: fixup_cil_method_token
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: @[[C3]]:
; CHECK-NEXT: ldc.i4.s 13 // encoding: [0x1f,0x0d]
; CHECK-NEXT: call void 'i'(int32) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'i'(int32), kind: fixup_cil_method_token
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: @[[DEF]]:
; CHECK-NEXT: ldc.i4.s 14 // encoding: [0x1f,0x0e]
; CHECK-NEXT: call void 'i'(int32) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'i'(int32), kind: fixup_cil_method_token
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method 'sw'
define void @sw(i32 %x) {
entry:
  switch i32 %x, label %def [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
  ]
c0:
  call void @i(i32 10)
  ret void
c1:
  call void @i(i32 11)
  ret void
c2:
  call void @i(i32 12)
  ret void
c3:
  call void @i(i32 13)
  ret void
def:
  call void @i(i32 14)
  ret void
}

; The case of 0 is laid out last, so the br to it (the switch's default
; operand) is long. The entries are 32-bit whatever the distance: the second
; one names the block right after the br.
; CHECK-LABEL: .method {{.*}}'swfar'(
; CHECK:      .maxstack 13
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: ldarg.0 // encoding: [0x02]
; CHECK-NEXT: ldc.i4.3 // encoding: [0x19]
; CHECK-NEXT: bgt.un @[[DEF:BB[0-9]+_[0-9]+]] // encoding: [0x42,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: @[[DEF]]-4, kind: FK_PCRel_4
; CHECK-NEXT: // %bb.1:
; CHECK-NEXT: ldarg.0 // encoding: [0x02]
; CHECK-NEXT: switch (@[[C0:BB[0-9]+_[0-9]+]], @[[C1:BB[0-9]+_[0-9]+]], @[[C2:BB[0-9]+_[0-9]+]], @[[C3:BB[0-9]+_[0-9]+]]) // encoding: [0x45,0x04,0x00,0x00,0x00,A,A,A,A,B,B,B,B,C,C,C,C,D,D,D,D]
; CHECK-NEXT: // fixup A - offset: 5, value: @[[C0]]-16, kind: FK_PCRel_4
; CHECK-NEXT: // fixup B - offset: 9, value: @[[C1]]-12, kind: FK_PCRel_4
; CHECK-NEXT: // fixup C - offset: 13, value: @[[C2]]-8, kind: FK_PCRel_4
; CHECK-NEXT: // fixup D - offset: 17, value: @[[C3]]-4, kind: FK_PCRel_4
; CHECK-NEXT: br @[[C0]] // encoding: [0x38,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: @[[C0]]-4, kind: FK_PCRel_4
; CHECK-NEXT: @[[C1]]:
; CHECK-NEXT: ldc.r8 float64(0x3FF0000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0xf0,0x3f]
; CHECK:      call void 'd13'({{.*}}) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'd13'({{.*}}), kind: fixup_cil_method_token
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: @[[C2]]:
; CHECK-NEXT: ldc.i4.s 12 // encoding: [0x1f,0x0c]
; CHECK-NEXT: call void 'i'(int32) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'i'(int32), kind: fixup_cil_method_token
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: @[[C3]]:
; CHECK-NEXT: ldc.i4.s 13 // encoding: [0x1f,0x0d]
; CHECK-NEXT: call void 'i'(int32) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'i'(int32), kind: fixup_cil_method_token
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: @[[DEF]]:
; CHECK-NEXT: ldc.i4.s 14 // encoding: [0x1f,0x0e]
; CHECK-NEXT: call void 'i'(int32) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'i'(int32), kind: fixup_cil_method_token
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: @[[C0]]:
; CHECK-NEXT: ldc.i4.s 10 // encoding: [0x1f,0x0a]
; CHECK-NEXT: call void 'i'(int32) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'i'(int32), kind: fixup_cil_method_token
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method 'swfar'
define void @swfar(i32 %x) {
entry:
  switch i32 %x, label %def [
    i32 0, label %c0
    i32 1, label %c1
    i32 2, label %c2
    i32 3, label %c3
  ]
c1:
  call void @d13(double 1.0, double 2.0, double 3.0, double 4.0, double 5.0, double 6.0, double 7.0, double 8.0, double 9.0, double 10.0, double 11.0, double 12.0, double 13.0)
  ret void
c2:
  call void @i(i32 12)
  ret void
c3:
  call void @i(i32 13)
  ret void
def:
  call void @i(i32 14)
  ret void
c0:
  call void @i(i32 10)
  ret void
}

; CHECK-LABEL: .method {{.*}}'sw64'(
; CHECK:      .maxstack 2
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: ldarg.0 // encoding: [0x02]
; CHECK-NEXT: ldc.i4.3 // encoding: [0x19]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: bgt.un.s @[[DEF:BB[0-9]+_[0-9]+]] // encoding: [0x35,A]
; CHECK-NEXT: // fixup A - offset: 1, value: @[[DEF]]-1, kind: FK_PCRel_1
; CHECK-NEXT: // %bb.1:
; CHECK-NEXT: ldarg.0 // encoding: [0x02]
; CHECK-NEXT: conv.i4 // encoding: [0x69]
; CHECK-NEXT: switch (@[[C0:BB[0-9]+_[0-9]+]], @[[C1:BB[0-9]+_[0-9]+]], @[[C2:BB[0-9]+_[0-9]+]], @[[C3:BB[0-9]+_[0-9]+]]) // encoding: [0x45,0x04,0x00,0x00,0x00,A,A,A,A,B,B,B,B,C,C,C,C,D,D,D,D]
; CHECK-NEXT: // fixup A - offset: 5, value: @[[C0]]-16, kind: FK_PCRel_4
; CHECK-NEXT: // fixup B - offset: 9, value: @[[C1]]-12, kind: FK_PCRel_4
; CHECK-NEXT: // fixup C - offset: 13, value: @[[C2]]-8, kind: FK_PCRel_4
; CHECK-NEXT: // fixup D - offset: 17, value: @[[C3]]-4, kind: FK_PCRel_4
; CHECK-NEXT: br.s @[[C0]] // encoding: [0x2b,A]
; CHECK-NEXT: // fixup A - offset: 1, value: @[[C0]]-1, kind: FK_PCRel_1
; CHECK-NEXT: @[[C0]]:
; CHECK:      @[[DEF]]:
; CHECK-NEXT: ldc.i4.s 14 // encoding: [0x1f,0x0e]
define void @sw64(i64 %x) {
entry:
  switch i64 %x, label %def [
    i64 0, label %c0
    i64 1, label %c1
    i64 2, label %c2
    i64 3, label %c3
  ]
c0:
  call void @i(i32 10)
  ret void
c1:
  call void @i(i32 11)
  ret void
c2:
  call void @i(i32 12)
  ret void
c3:
  call void @i(i32 13)
  ret void
def:
  call void @i(i32 14)
  ret void
}

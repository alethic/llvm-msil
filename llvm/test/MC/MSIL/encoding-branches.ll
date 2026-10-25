; Branch forms and branch fixups (ImplementationPlan.md M18.a (3), M18.b;
; plan section 12, 2026-10-21: Q26). msil-ld never relaxes branches (plan
; 5.8), so the MSIL MC lowering chooses each method's branch forms once,
; before emission: every branch starts short and is widened while its
; displacement from the next instruction leaves [-128, 127], until nothing
; changes. A short branch is its opcode and one byte with a PC-relative
; one-byte fixup on 'label - 1' (printed FK_PCRel_1 by the encoding comments
; of llvm/lib/MC/MCAsmStreamer.cpp); a long branch has four bytes and an
; FK_PCRel_4 fixup on 'label - 4'. MCAssembler resolves a PC-relative value as
; target - fixup position, so both give 'target - next instruction', as
; ECMA-335 III.1.7.5 asks.
;
; The fillers have known sizes: 'd13' with 13 ldc.r8 arguments is 13 * 9 + 5
; = 122 bytes, 'd12' is 12 * 9 + 5 = 113, call 'v' is 5, ldc.i4.1 + call 'i'
; is 6, ldc.i4 1000 + call 'i' is 10, and the loop test ldarg.0, volatile.,
; ldind.i4 is 4. Hence:
; - fwd127: the forward branch spans 122 + 5 = 127 bytes: brfalse.s, the
;   largest displacement that fits;
; - fwd128: 122 + 6 = 128 bytes: brfalse;
; - back128: 122 + 4 = 126 bytes from the loop label to the two-byte
;   brtrue.s, displacement -128, the smallest that fits;
; - back129: 113 + 10 + 4 = 127 bytes, so brtrue.s would need -129: brtrue
;   (displacement -132);
; - chain127: the loop's back edge spans the test, the exit branch and the
;   122-byte body: br.s would need -130, so it is br; the exit branch then
;   spans the body and the five-byte br, 127 bytes, and stays brfalse.s;
; - chain128: the same with a 123-byte body: the back edge widens, and only
;   because it did, the exit branch spans 123 + 5 = 128 bytes and widens too
;   (with a short back edge it would have fit): the forms are a fixed point,
;   not one pass over the short sizes;
; - small: every branch short (bge.s forward, br.s back).
; Every form and displacement was checked against Framework ilasm's image of
; the same text (ilasm encodes the forms it is given; it never relaxes).

; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -show-mc-encoding \
; RUN:   | FileCheck %s
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -show-mc-encoding \
; RUN:   | FileCheck %s
; RUN: %if ilasm %{ llc < %s -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -show-mc-encoding -o %t.il && %ilasm-verify -output=%t.dll %t.il %}

define void @v() {
  ret void
}

define void @i(i32 %x) {
  ret void
}

define void @d12(double, double, double, double, double, double, double, double, double, double, double, double) {
  ret void
}

define void @d13(double, double, double, double, double, double, double, double, double, double, double, double, double) {
  ret void
}

; CHECK-LABEL: .method {{.*}}'fwd127'(
; CHECK:      .maxstack 13
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: ldarg.0 // encoding: [0x02]
; CHECK-NEXT: brfalse.s @[[DONE:BB[0-9]+_[0-9]+]] // encoding: [0x2c,A]
; CHECK-NEXT: // fixup A - offset: 1, value: @[[DONE]]-1, kind: FK_PCRel_1
; CHECK-NEXT: // %bb.1:
; CHECK-NEXT: ldc.r8 float64(0x3FF0000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0xf0,0x3f]
; CHECK-NEXT: ldc.r8 float64(0x4000000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4008000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4010000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4014000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x14,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4018000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x40]
; CHECK-NEXT: ldc.r8 float64(0x401C000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x1c,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4020000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4022000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x22,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4024000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x24,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4026000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x26,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4028000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x28,0x40]
; CHECK-NEXT: ldc.r8 float64(0x402A000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x2a,0x40]
; CHECK-NEXT: call void 'd13'(float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'd13'(float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64), kind: fixup_cil_method_token
; CHECK-NEXT: call void 'v'() // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'v'(), kind: fixup_cil_method_token
; CHECK-NEXT: @[[DONE]]:
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method 'fwd127'
define void @fwd127(i32 %c) {
entry:
  %z = icmp eq i32 %c, 0
  br i1 %z, label %done, label %body
body:
  call void @d13(double 1.0, double 2.0, double 3.0, double 4.0, double 5.0, double 6.0, double 7.0, double 8.0, double 9.0, double 10.0, double 11.0, double 12.0, double 13.0)
  call void @v()
  br label %done
done:
  ret void
}

; CHECK-LABEL: .method {{.*}}'fwd128'(
; CHECK:      .maxstack 13
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: ldarg.0 // encoding: [0x02]
; CHECK-NEXT: brfalse @[[DONE:BB[0-9]+_[0-9]+]] // encoding: [0x39,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: @[[DONE]]-4, kind: FK_PCRel_4
; CHECK-NEXT: // %bb.1:
; CHECK-NEXT: ldc.r8 float64(0x3FF0000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0xf0,0x3f]
; CHECK-NEXT: ldc.r8 float64(0x4000000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4008000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4010000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4014000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x14,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4018000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x40]
; CHECK-NEXT: ldc.r8 float64(0x401C000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x1c,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4020000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4022000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x22,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4024000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x24,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4026000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x26,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4028000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x28,0x40]
; CHECK-NEXT: ldc.r8 float64(0x402A000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x2a,0x40]
; CHECK-NEXT: call void 'd13'(float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'd13'(float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64), kind: fixup_cil_method_token
; CHECK-NEXT: ldc.i4.1 // encoding: [0x17]
; CHECK-NEXT: call void 'i'(int32) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'i'(int32), kind: fixup_cil_method_token
; CHECK-NEXT: @[[DONE]]:
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method 'fwd128'
define void @fwd128(i32 %c) {
entry:
  %z = icmp eq i32 %c, 0
  br i1 %z, label %done, label %body
body:
  call void @d13(double 1.0, double 2.0, double 3.0, double 4.0, double 5.0, double 6.0, double 7.0, double 8.0, double 9.0, double 10.0, double 11.0, double 12.0, double 13.0)
  call void @i(i32 1)
  br label %done
done:
  ret void
}

; CHECK-LABEL: .method {{.*}}'back128'(
; CHECK:      .maxstack 13
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: @[[LOOP:BB[0-9]+_[0-9]+]]:
; CHECK-NEXT: // =>This Inner Loop Header: Depth=1
; CHECK-NEXT: ldc.r8 float64(0x3FF0000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0xf0,0x3f]
; CHECK-NEXT: ldc.r8 float64(0x4000000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4008000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4010000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4014000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x14,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4018000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x40]
; CHECK-NEXT: ldc.r8 float64(0x401C000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x1c,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4020000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4022000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x22,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4024000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x24,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4026000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x26,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4028000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x28,0x40]
; CHECK-NEXT: ldc.r8 float64(0x402A000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x2a,0x40]
; CHECK-NEXT: call void 'd13'(float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'd13'(float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64), kind: fixup_cil_method_token
; CHECK-NEXT: ldarg.0 // encoding: [0x02]
; CHECK-NEXT: volatile. // encoding: [0xfe,0x13]
; CHECK-NEXT: ldind.i4 // encoding: [0x4a]
; CHECK-NEXT: brtrue.s @[[LOOP]] // encoding: [0x2d,A]
; CHECK-NEXT: // fixup A - offset: 1, value: @[[LOOP]]-1, kind: FK_PCRel_1
; CHECK-NEXT: // %bb.2:
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method 'back128'
define void @back128(ptr %p) {
entry:
  br label %loop
loop:
  call void @d13(double 1.0, double 2.0, double 3.0, double 4.0, double 5.0, double 6.0, double 7.0, double 8.0, double 9.0, double 10.0, double 11.0, double 12.0, double 13.0)
  %v = load volatile i32, ptr %p
  %c = icmp ne i32 %v, 0
  br i1 %c, label %loop, label %exit
exit:
  ret void
}

; CHECK-LABEL: .method {{.*}}'back129'(
; CHECK:      .maxstack 12
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: @[[LOOP:BB[0-9]+_[0-9]+]]:
; CHECK-NEXT: // =>This Inner Loop Header: Depth=1
; CHECK-NEXT: ldc.r8 float64(0x3FF0000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0xf0,0x3f]
; CHECK-NEXT: ldc.r8 float64(0x4000000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4008000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4010000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4014000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x14,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4018000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x40]
; CHECK-NEXT: ldc.r8 float64(0x401C000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x1c,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4020000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4022000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x22,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4024000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x24,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4026000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x26,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4028000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x28,0x40]
; CHECK-NEXT: call void 'd12'(float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'd12'(float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64), kind: fixup_cil_method_token
; CHECK-NEXT: ldc.i4 1000 // encoding: [0x20,0xe8,0x03,0x00,0x00]
; CHECK-NEXT: call void 'i'(int32) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'i'(int32), kind: fixup_cil_method_token
; CHECK-NEXT: ldarg.0 // encoding: [0x02]
; CHECK-NEXT: volatile. // encoding: [0xfe,0x13]
; CHECK-NEXT: ldind.i4 // encoding: [0x4a]
; CHECK-NEXT: brtrue @[[LOOP]] // encoding: [0x3a,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: @[[LOOP]]-4, kind: FK_PCRel_4
; CHECK-NEXT: // %bb.2:
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method 'back129'
define void @back129(ptr %p) {
entry:
  br label %loop
loop:
  call void @d12(double 1.0, double 2.0, double 3.0, double 4.0, double 5.0, double 6.0, double 7.0, double 8.0, double 9.0, double 10.0, double 11.0, double 12.0)
  call void @i(i32 1000)
  %v = load volatile i32, ptr %p
  %c = icmp ne i32 %v, 0
  br i1 %c, label %loop, label %exit
exit:
  ret void
}

; CHECK-LABEL: .method {{.*}}'chain127'(
; CHECK:      .maxstack 13
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: @[[HEAD:BB[0-9]+_[0-9]+]]:
; CHECK-NEXT: // =>This Inner Loop Header: Depth=1
; CHECK-NEXT: ldarg.0 // encoding: [0x02]
; CHECK-NEXT: volatile. // encoding: [0xfe,0x13]
; CHECK-NEXT: ldind.i4 // encoding: [0x4a]
; CHECK-NEXT: brfalse.s @[[EXIT:BB[0-9]+_[0-9]+]] // encoding: [0x2c,A]
; CHECK-NEXT: // fixup A - offset: 1, value: @[[EXIT]]-1, kind: FK_PCRel_1
; CHECK-NEXT: // %bb.2:
; CHECK-NEXT: // in Loop: Header=[[HEAD]] Depth=1
; CHECK-NEXT: ldc.r8 float64(0x3FF0000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0xf0,0x3f]
; CHECK-NEXT: ldc.r8 float64(0x4000000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4008000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4010000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4014000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x14,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4018000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x40]
; CHECK-NEXT: ldc.r8 float64(0x401C000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x1c,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4020000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4022000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x22,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4024000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x24,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4026000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x26,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4028000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x28,0x40]
; CHECK-NEXT: ldc.r8 float64(0x402A000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x2a,0x40]
; CHECK-NEXT: call void 'd13'(float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'd13'(float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64), kind: fixup_cil_method_token
; CHECK-NEXT: br @[[HEAD]] // encoding: [0x38,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: @[[HEAD]]-4, kind: FK_PCRel_4
; CHECK-NEXT: @[[EXIT]]:
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method 'chain127'
define void @chain127(ptr %p) {
entry:
  br label %head
head:
  %v = load volatile i32, ptr %p
  %c = icmp eq i32 %v, 0
  br i1 %c, label %exit, label %body
body:
  call void @d13(double 1.0, double 2.0, double 3.0, double 4.0, double 5.0, double 6.0, double 7.0, double 8.0, double 9.0, double 10.0, double 11.0, double 12.0, double 13.0)
  br label %head
exit:
  ret void
}

; CHECK-LABEL: .method {{.*}}'chain128'(
; CHECK:      .maxstack 12
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: @[[HEAD:BB[0-9]+_[0-9]+]]:
; CHECK-NEXT: // =>This Inner Loop Header: Depth=1
; CHECK-NEXT: ldarg.0 // encoding: [0x02]
; CHECK-NEXT: volatile. // encoding: [0xfe,0x13]
; CHECK-NEXT: ldind.i4 // encoding: [0x4a]
; CHECK-NEXT: brfalse @[[EXIT:BB[0-9]+_[0-9]+]] // encoding: [0x39,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: @[[EXIT]]-4, kind: FK_PCRel_4
; CHECK-NEXT: // %bb.2:
; CHECK-NEXT: // in Loop: Header=[[HEAD]] Depth=1
; CHECK-NEXT: ldc.r8 float64(0x3FF0000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0xf0,0x3f]
; CHECK-NEXT: ldc.r8 float64(0x4000000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4008000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4010000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4014000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x14,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4018000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x40]
; CHECK-NEXT: ldc.r8 float64(0x401C000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x1c,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4020000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x20,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4022000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x22,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4024000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x24,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4026000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x26,0x40]
; CHECK-NEXT: ldc.r8 float64(0x4028000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0x28,0x40]
; CHECK-NEXT: call void 'd12'(float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'd12'(float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64, float64), kind: fixup_cil_method_token
; CHECK-NEXT: ldc.i4 1000 // encoding: [0x20,0xe8,0x03,0x00,0x00]
; CHECK-NEXT: call void 'i'(int32) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'i'(int32), kind: fixup_cil_method_token
; CHECK-NEXT: br @[[HEAD]] // encoding: [0x38,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: @[[HEAD]]-4, kind: FK_PCRel_4
; CHECK-NEXT: @[[EXIT]]:
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method 'chain128'
define void @chain128(ptr %p) {
entry:
  br label %head
head:
  %v = load volatile i32, ptr %p
  %c = icmp eq i32 %v, 0
  br i1 %c, label %exit, label %body
body:
  call void @d12(double 1.0, double 2.0, double 3.0, double 4.0, double 5.0, double 6.0, double 7.0, double 8.0, double 9.0, double 10.0, double 11.0, double 12.0)
  call void @i(i32 1000)
  br label %head
exit:
  ret void
}

; CHECK-LABEL: .method {{.*}}'small'(
; CHECK:      .maxstack 2
; CHECK-NEXT: .locals (int32)
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: ldc.i4.0 // encoding: [0x16]
; CHECK-NEXT: stloc.0 // encoding: [0x0a]
; CHECK-NEXT: @[[HEAD:BB[0-9]+_[0-9]+]]:
; CHECK-NEXT: // =>This Inner Loop Header: Depth=1
; CHECK-NEXT: ldloc.0 // encoding: [0x06]
; CHECK-NEXT: ldarg.0 // encoding: [0x02]
; CHECK-NEXT: bge.s @[[EXIT:BB[0-9]+_[0-9]+]] // encoding: [0x2f,A]
; CHECK-NEXT: // fixup A - offset: 1, value: @[[EXIT]]-1, kind: FK_PCRel_1
; CHECK-NEXT: // %bb.2:
; CHECK-NEXT: // in Loop: Header=[[HEAD]] Depth=1
; CHECK-NEXT: call void 'v'() // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'v'(), kind: fixup_cil_method_token
; CHECK-NEXT: ldloc.0 // encoding: [0x06]
; CHECK-NEXT: ldc.i4.1 // encoding: [0x17]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: stloc.0 // encoding: [0x0a]
; CHECK-NEXT: br.s @[[HEAD]] // encoding: [0x2b,A]
; CHECK-NEXT: // fixup A - offset: 1, value: @[[HEAD]]-1, kind: FK_PCRel_1
; CHECK-NEXT: @[[EXIT]]:
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method 'small'
define void @small(i32 %n) {
entry:
  br label %head
head:
  %i = phi i32 [0, %entry], [%i1, %body]
  %c = icmp slt i32 %i, %n
  br i1 %c, label %body, label %exit
body:
  call void @v()
  %i1 = add i32 %i, 1
  br label %head
exit:
  ret void
}

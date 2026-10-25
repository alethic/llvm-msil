; The binary encoding of one MCInst per CIL instruction (ImplementationPlan.md
; M18.b), as 'llc -show-mc-encoding' prints it in the encoding comments of
; llvm/lib/MC/MCAsmStreamer.cpp: every inline operand kind of
; CILOpcodes.def that the backend emits.
; - The opcode is one byte, or 0xFE and one byte (ceq, unaligned., volatile.,
;   cpblk, ldarg).
; - Operands are little-endian: ShortInlineVar u8 (ldarg.s), ShortInlineI i8
;   (ldc.i4.s) and u8 (unaligned.), InlineI i32, InlineI8 i64, ShortInlineR
;   and InlineR the exact IEEE bits. A float32 signaling NaN keeps its bits
;   (0x7FA00000): unlike ilasm, which sets the quiet bit (0x7FE00000;
;   recon p2/mc-streamer.md E4), the encoder copies the constant.
; - A token operand (InlineMethod, InlineField, InlineSig) is four zero bytes
;   and a fixup of the token class at the operand's offset: call and ldftn
;   fixup_cil_method_token, ldsfld fixup_cil_field_token, calli
;   fixup_cil_sig_token. The fixup's value prints in whole-program form, the
;   same text as the operand: a defined method ('f'), a field ('g'), the
;   address field of a function (FUNC_ADDR, '<msil>fn_f'), a calli signature
;   and a BCL member with its [mscorlib] scope (plan 5.11.7).
; - Multi-opcode records are several MCInsts, each with its own encoding: the
;   memory flags (unaligned. N, volatile.) before the access, ldc.i8 and conv.i
;   for an i64 constant outside int32, ldnull and throw for a trap.
; Branches, switches, synthesized bodies, the shortest forms and InlineVar
; (indices from 256) are in encoding-branches.ll, encoding-switch.ll,
; encoding-synthesized.ll and shortest-forms.ll. Every expected byte was
; checked against Framework ilasm's image of the same text (llvm-cil-dump
; --headers --tables and the method bodies), the snan constant excepted.
; The encoding comments are ILAsm comments, so the output still assembles.

; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -show-mc-encoding \
; RUN:   | FileCheck %s
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -show-mc-encoding \
; RUN:   | FileCheck %s
; RUN: %if ilasm %{ llc < %s -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -show-mc-encoding -o %t.il && %ilasm-verify -output=%t.dll %t.il %}

@g = global i32 7

; CHECK-LABEL: .method {{.*}}'f'(
; CHECK:      .maxstack 1
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: ldarg.0 // encoding: [0x02]
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method 'f'
define i32 @f(i32 %x) {
  ret i32 %x
}

; CHECK-LABEL: .method {{.*}}'ops'(
; CHECK:      .maxstack 4
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: ldarg.1 // encoding: [0x03]
; CHECK-NEXT: ldc.i4.s -5 // encoding: [0x1f,0xfb]
; CHECK-NEXT: call int32 'f'(int32) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: int32 'f'(int32), kind: fixup_cil_method_token
; CHECK-NEXT: ldsfld native int 'g' // encoding: [0x7e,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int 'g', kind: fixup_cil_field_token
; CHECK-NEXT: ldind.i4 // encoding: [0x4a]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: ldarg.1 // encoding: [0x03]
; CHECK-NEXT: ldarg.2 // encoding: [0x04]
; CHECK-NEXT: calli int32(native int) // encoding: [0x29,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: int32(native int), kind: fixup_cil_sig_token
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: ldc.i4 1000 // encoding: [0x20,0xe8,0x03,0x00,0x00]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: ldarg.0 // encoding: [0x02]
; CHECK-NEXT: ceq // encoding: [0xfe,0x01]
; CHECK-NEXT: unaligned. 1 // encoding: [0xfe,0x12,0x01]
; CHECK-NEXT: stind.i4 // encoding: [0x54]
; CHECK-NEXT: ldarg.1 // encoding: [0x03]
; CHECK-NEXT: unaligned. 2 // encoding: [0xfe,0x12,0x02]
; CHECK-NEXT: volatile. // encoding: [0xfe,0x13]
; CHECK-NEXT: ldind.i4 // encoding: [0x4a]
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method 'ops'
define i32 @ops(i32 %a, ptr %p, ptr %fp) {
  %c = call i32 @f(i32 -5)
  %v = load i32, ptr @g
  %s = add i32 %c, %v
  %t = add i32 %s, 1000
  %r = call i32 %fp(ptr %p)
  %u = add i32 %t, %r
  %e = icmp eq i32 %u, %a
  %z = zext i1 %e to i32
  store i32 %z, ptr %p, align 1
  %w = load volatile i32, ptr %p, align 2
  ret i32 %w
}

; CHECK-LABEL: .method {{.*}}'vars'(
; CHECK:      .maxstack 2
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: ldarg.s 4 // encoding: [0x0e,0x04]
; CHECK-NEXT: ldarg.0 // encoding: [0x02]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method 'vars'
define i32 @vars(i32 %a0, i32 %a1, i32 %a2, i32 %a3, i32 %a4) {
  %x = add i32 %a4, %a0
  ret i32 %x
}

; CHECK-LABEL: .method {{.*}}'big'(
; CHECK:      .maxstack 1
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: ldc.i8 4886718345 // encoding: [0x21,0x89,0x67,0x45,0x23,0x01,0x00,0x00,0x00]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method 'big'
define i64 @big() {
  ret i64 4886718345
}

; CHECK-LABEL: .method {{.*}}'snan'(
; CHECK:      .maxstack 1
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: ldc.r4 float32(0x7FA00000) // encoding: [0x22,0x00,0x00,0xa0,0x7f]
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method 'snan'
define float @snan() {
  ret float 0x7FF4000000000000
}

; CHECK-LABEL: .method {{.*}}'dbl'(
; CHECK:      .maxstack 1
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: ldc.r8 float64(0x3FF8000000000000) // encoding: [0x23,0x00,0x00,0x00,0x00,0x00,0x00,0xf8,0x3f]
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method 'dbl'
define double @dbl() {
  ret double 1.5
}

; CHECK-LABEL: .method {{.*}}'faddr'(
; CHECK:      .maxstack 1
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: ldsfld native int '<msil>fn_f' // encoding: [0x7e,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int '<msil>fn_f', kind: fixup_cil_field_token
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method 'faddr'
define ptr @faddr() {
  ret ptr @f
}

; CHECK-LABEL: .method {{.*}}'blk'(
; CHECK:      .maxstack 3
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: ldarg.0 // encoding: [0x02]
; CHECK-NEXT: ldarg.1 // encoding: [0x03]
; CHECK-NEXT: ldc.i4.s 16 // encoding: [0x1f,0x10]
; CHECK-NEXT: unaligned. 1 // encoding: [0xfe,0x12,0x01]
; CHECK-NEXT: cpblk // encoding: [0xfe,0x17]
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method 'blk'
define void @blk(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr %d, ptr %s, i64 16, i1 false)
  ret void
}

; CHECK-LABEL: .method {{.*}}'fence'(
; CHECK:      .maxstack 0
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: call void [mscorlib]System.Threading.Interlocked::MemoryBarrier() // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void [mscorlib]System.Threading.Interlocked::MemoryBarrier(), kind: fixup_cil_method_token
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method 'fence'
define void @fence() {
  fence seq_cst
  ret void
}

; CHECK-LABEL: .method {{.*}}'trap'(
; CHECK:      .maxstack 1
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: ldnull // encoding: [0x14]
; CHECK-NEXT: throw // encoding: [0x7a]
; CHECK-NEXT: } // end of method 'trap'
define void @trap() {
  call void @llvm.trap()
  unreachable
}

; The module initializer ends by filling the address field of 'f': ldftn, a
; two-byte opcode whose token is at offset 2, then stsfld. The rest of its
; encoding is pinned in encoding-synthesized.ll.
; CHECK-LABEL: .method {{.*}} .cctor()
; CHECK:      ldftn int32 'f'(int32) // encoding: [0xfe,0x06,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 2, value: int32 'f'(int32), kind: fixup_cil_method_token
; CHECK-NEXT: stsfld native int '<msil>fn_f' // encoding: [0x80,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int '<msil>fn_f', kind: fixup_cil_field_token
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method '.cctor'

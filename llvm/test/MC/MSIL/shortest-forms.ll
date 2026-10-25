; The shortest forms (ImplementationPlan.md M18.a (2); plan section 12,
; 2026-10-21: Q26, which amends plan 5.1). Each MCInst is one CIL instruction,
; and the lowering picks the shortest form by cli::ILBuilder's rule (plan
; 5.7.5):
; - ldc.i4: ldc.i4.m1, ldc.i4.0-8, ldc.i4.s for -128..127, else ldc.i4. Phase
;   1 already printed CONST_I32 this way; since Q26 the same holds for
;   CONST_I64 ('ldc.i4.m1; conv.i', 'ldc.i4.s 9; conv.i', ...; ldc.i8 only
;   outside int32) and for the offsets of GLOBAL_ADDR (ldsfld, then the
;   shortest ldc and add; ldc.i8, conv.i and add beyond int32; no offset, no
;   add), which Phase 1 printed with ldc.i4 (plan 5.1);
; - argument and local indices: ldarg.0-3 / ldloc.0-3 / stloc.0-3, the .s
;   forms below 256 (u8 operand) and the long forms from 256 (FE prefix, u16
;   operand); starg has no .N form;
; - a tee is two MCInsts, dup and the store: 'dup; starg.s 1' (ARG_TEE) and
;   'dup; stloc.0' (LOCAL_TEE).
; The index checks need 257 parameters and 257 locals; Inputs/gen-indices.py
; writes that module. Every byte was checked against Framework ilasm's image
; of the same text.

; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -show-mc-encoding \
; RUN:   | FileCheck %s
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -show-mc-encoding \
; RUN:   | FileCheck %s
; RUN: %if ilasm %{ llc < %s -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -show-mc-encoding -o %t.il && %ilasm-verify -output=%t.dll %t.il %}

; RUN: %python %S/Inputs/gen-indices.py > %t.indices.ll
; RUN: llc < %t.indices.ll -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -show-mc-encoding \
; RUN:   | FileCheck %s --check-prefix=IDX
; RUN: llc < %t.indices.ll -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -show-mc-encoding \
; RUN:   | FileCheck %s --check-prefix=IDX
; RUN: %if ilasm %{ llc < %t.indices.ll -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -show-mc-encoding -o %t.indices.il && %ilasm-verify -output=%t.indices.dll %t.indices.il %}

@arr = global [64 x i32] zeroinitializer

define void @s64(i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64, i64) {
  ret void
}

define void @sp(ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr, ptr) {
  ret void
}

; CONST_I64 at the boundaries of each form.
; CHECK-LABEL: .method {{.*}}'consts'(
; CHECK:      .maxstack 13
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: ldc.i4.m1 // encoding: [0x15]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: ldc.i4.0 // encoding: [0x16]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: ldc.i4.8 // encoding: [0x1e]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: ldc.i4.s 9 // encoding: [0x1f,0x09]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: ldc.i4.s 127 // encoding: [0x1f,0x7f]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: ldc.i4 128 // encoding: [0x20,0x80,0x00,0x00,0x00]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: ldc.i4.s -128 // encoding: [0x1f,0x80]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: ldc.i4 -129 // encoding: [0x20,0x7f,0xff,0xff,0xff]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: ldc.i4 2147483647 // encoding: [0x20,0xff,0xff,0xff,0x7f]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: ldc.i4 -2147483648 // encoding: [0x20,0x00,0x00,0x00,0x80]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: ldc.i8 2147483648 // encoding: [0x21,0x00,0x00,0x00,0x80,0x00,0x00,0x00,0x00]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: ldc.i8 -2147483649 // encoding: [0x21,0xff,0xff,0xff,0x7f,0xff,0xff,0xff,0xff]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: ldc.i8 4294967295 // encoding: [0x21,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: call void 's64'(native int, native int, native int, native int, native int, native int, native int, native int, native int, native int, native int, native int, native int) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 's64'(native int, native int, native int, native int, native int, native int, native int, native int, native int, native int, native int, native int, native int), kind: fixup_cil_method_token
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method 'consts'
define void @consts() {
  call void @s64(i64 -1, i64 0, i64 8, i64 9, i64 127, i64 128, i64 -128, i64 -129, i64 2147483647, i64 -2147483648, i64 2147483648, i64 -2147483649, i64 4294967295)
  ret void
}

; GLOBAL_ADDR with the offsets 0, 4, 8, 12, 124, 128, 252, -4, -128 and 2^32.
; CHECK-LABEL: .method {{.*}}'offsets'(
; CHECK:      .maxstack 11
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: ldsfld native int 'arr' // encoding: [0x7e,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int 'arr', kind: fixup_cil_field_token
; CHECK-NEXT: ldsfld native int 'arr' // encoding: [0x7e,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int 'arr', kind: fixup_cil_field_token
; CHECK-NEXT: ldc.i4.4 // encoding: [0x1a]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: ldsfld native int 'arr' // encoding: [0x7e,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int 'arr', kind: fixup_cil_field_token
; CHECK-NEXT: ldc.i4.8 // encoding: [0x1e]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: ldsfld native int 'arr' // encoding: [0x7e,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int 'arr', kind: fixup_cil_field_token
; CHECK-NEXT: ldc.i4.s 12 // encoding: [0x1f,0x0c]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: ldsfld native int 'arr' // encoding: [0x7e,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int 'arr', kind: fixup_cil_field_token
; CHECK-NEXT: ldc.i4.s 124 // encoding: [0x1f,0x7c]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: ldsfld native int 'arr' // encoding: [0x7e,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int 'arr', kind: fixup_cil_field_token
; CHECK-NEXT: ldc.i4 128 // encoding: [0x20,0x80,0x00,0x00,0x00]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: ldsfld native int 'arr' // encoding: [0x7e,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int 'arr', kind: fixup_cil_field_token
; CHECK-NEXT: ldc.i4 252 // encoding: [0x20,0xfc,0x00,0x00,0x00]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: ldsfld native int 'arr' // encoding: [0x7e,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int 'arr', kind: fixup_cil_field_token
; CHECK-NEXT: ldc.i4.s -4 // encoding: [0x1f,0xfc]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: ldsfld native int 'arr' // encoding: [0x7e,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int 'arr', kind: fixup_cil_field_token
; CHECK-NEXT: ldc.i4.s -128 // encoding: [0x1f,0x80]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: ldsfld native int 'arr' // encoding: [0x7e,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: native int 'arr', kind: fixup_cil_field_token
; CHECK-NEXT: ldc.i8 4294967296 // encoding: [0x21,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00]
; CHECK-NEXT: conv.i // encoding: [0xd3]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: call void 'sp'(native int, native int, native int, native int, native int, native int, native int, native int, native int, native int) // encoding: [0x28,A,A,A,A]
; CHECK-NEXT: // fixup A - offset: 1, value: void 'sp'(native int, native int, native int, native int, native int, native int, native int, native int, native int, native int), kind: fixup_cil_method_token
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method 'offsets'
define void @offsets() {
  call void @sp(ptr @arr, ptr getelementptr (i8, ptr @arr, i64 4), ptr getelementptr (i8, ptr @arr, i64 8), ptr getelementptr (i8, ptr @arr, i64 12), ptr getelementptr (i8, ptr @arr, i64 124), ptr getelementptr (i8, ptr @arr, i64 128), ptr getelementptr (i8, ptr @arr, i64 252), ptr getelementptr (i8, ptr @arr, i64 -4), ptr getelementptr (i8, ptr @arr, i64 -128), ptr getelementptr (i8, ptr @arr, i64 4294967296))
  ret void
}

; ARG_TEE: the sum lives in the slot of %b, which is dead after the add.
; CHECK-LABEL: .method {{.*}}'tee'(
; CHECK:      .maxstack 2
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: ldarg.0 // encoding: [0x02]
; CHECK-NEXT: ldarg.1 // encoding: [0x03]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: dup // encoding: [0x25]
; CHECK-NEXT: starg.s 1 // encoding: [0x10,0x01]
; CHECK-NEXT: ldc.i4.m1 // encoding: [0x15]
; CHECK-NEXT: bgt.s @[[L0:BB[0-9]+_[0-9]+]] // encoding: [0x30,A]
; CHECK-NEXT: // fixup A - offset: 1, value: @[[L0]]-1, kind: FK_PCRel_1
; CHECK-NEXT: // %bb.1:
; CHECK-NEXT: ldc.i4.1 // encoding: [0x17]
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: @[[L0]]:
; CHECK-NEXT: ldarg.1 // encoding: [0x03]
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method 'tee'
define i32 @tee(i32 %a, i32 %b) {
  %s = add i32 %a, %b
  %c = icmp sgt i32 %s, -1
  br i1 %c, label %pos, label %neg
neg:
  ret i32 1
pos:
  ret i32 %s
}

; LOCAL_TEE: no parameter slot has the sum's type, so it takes a local.
; CHECK-LABEL: .method {{.*}}'ltee'(
; CHECK:      .maxstack 2
; CHECK-NEXT: .locals (int32)
; CHECK-NEXT: // %bb.0:
; CHECK-NEXT: ldarg.0 // encoding: [0x02]
; CHECK-NEXT: ldind.i4 // encoding: [0x4a]
; CHECK-NEXT: ldc.i4.7 // encoding: [0x1d]
; CHECK-NEXT: add // encoding: [0x58]
; CHECK-NEXT: dup // encoding: [0x25]
; CHECK-NEXT: stloc.0 // encoding: [0x0a]
; CHECK-NEXT: ldc.i4.m1 // encoding: [0x15]
; CHECK-NEXT: bgt.s @[[L0:BB[0-9]+_[0-9]+]] // encoding: [0x30,A]
; CHECK-NEXT: // fixup A - offset: 1, value: @[[L0]]-1, kind: FK_PCRel_1
; CHECK-NEXT: // %bb.1:
; CHECK-NEXT: ldc.i4.1 // encoding: [0x17]
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: @[[L0]]:
; CHECK-NEXT: ldloc.0 // encoding: [0x06]
; CHECK-NEXT: ret // encoding: [0x2a]
; CHECK-NEXT: } // end of method 'ltee'
define i32 @ltee(ptr %p) {
  %v = load i32, ptr %p
  %s = add i32 %v, 7
  %c = icmp sgt i32 %s, -1
  br i1 %c, label %pos, label %neg
neg:
  ret i32 1
pos:
  ret i32 %s
}

; The indices 3, 4, 255 and 256 of arguments and locals (Inputs/gen-indices.py).
; IDX-LABEL: .method {{.*}}'args'(
; IDX:      .maxstack 2
; IDX-NEXT: // %bb.0:
; IDX-NEXT: ldarg.3 // encoding: [0x05]
; IDX-NEXT: ldarg.s 4 // encoding: [0x0e,0x04]
; IDX-NEXT: add // encoding: [0x58]
; IDX-NEXT: ldarg.s 255 // encoding: [0x0e,0xff]
; IDX-NEXT: add // encoding: [0x58]
; IDX-NEXT: ldarg 256 // encoding: [0xfe,0x09,0x00,0x01]
; IDX-NEXT: add // encoding: [0x58]
; IDX-NEXT: ret // encoding: [0x2a]
; IDX-NEXT: } // end of method 'args'

; IDX-LABEL: .method {{.*}}'locals'(
; IDX:      stloc.2 // encoding: [0x0c]
; IDX-NEXT: ldarg.0 // encoding: [0x02]
; IDX-NEXT: volatile. // encoding: [0xfe,0x13]
; IDX-NEXT: ldind.i4 // encoding: [0x4a]
; IDX-NEXT: stloc.3 // encoding: [0x0d]
; IDX-NEXT: ldarg.0 // encoding: [0x02]
; IDX-NEXT: volatile. // encoding: [0xfe,0x13]
; IDX-NEXT: ldind.i4 // encoding: [0x4a]
; IDX-NEXT: stloc.s 4 // encoding: [0x13,0x04]
; IDX:      stloc.s 254 // encoding: [0x13,0xfe]
; IDX-NEXT: ldarg.0 // encoding: [0x02]
; IDX-NEXT: volatile. // encoding: [0xfe,0x13]
; IDX-NEXT: ldind.i4 // encoding: [0x4a]
; IDX-NEXT: stloc.s 255 // encoding: [0x13,0xff]
; IDX-NEXT: ldarg.0 // encoding: [0x02]
; IDX-NEXT: volatile. // encoding: [0xfe,0x13]
; IDX-NEXT: ldind.i4 // encoding: [0x4a]
; IDX-NEXT: stloc 256 // encoding: [0xfe,0x0e,0x00,0x01]
; IDX:      ldloc.2 // encoding: [0x08]
; IDX-NEXT: add // encoding: [0x58]
; IDX-NEXT: ldloc.3 // encoding: [0x09]
; IDX-NEXT: add // encoding: [0x58]
; IDX-NEXT: ldloc.s 4 // encoding: [0x11,0x04]
; IDX:      ldloc.s 254 // encoding: [0x11,0xfe]
; IDX-NEXT: add // encoding: [0x58]
; IDX-NEXT: ldloc.s 255 // encoding: [0x11,0xff]
; IDX-NEXT: add // encoding: [0x58]
; IDX-NEXT: ldloc 256 // encoding: [0xfe,0x0c,0x00,0x01]
; IDX-NEXT: add // encoding: [0x58]
; IDX-NEXT: ret // encoding: [0x2a]
; IDX-NEXT: } // end of method 'locals'

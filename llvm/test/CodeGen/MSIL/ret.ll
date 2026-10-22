; Returns of every Phase-1 value type (ImplementationPlan.md 5.1, M3.c/M3.d):
; the method header names the CIL result type (int32, native int for i64 and
; pointers (decision D2), float32, float64, void), the constant is loaded with
; the shortest ldc form and returned with 'ret', and .maxstack is exact.
; The printed text of every floating-point constant is bit-exact, including
; -0.0, infinities, denormals, NaN payloads and signaling NaNs (ldc.r4
; float32(0x...), ldc.r8 float64(0x...)). The assembled bytes are not always:
; in Phase 1 ilasm (Framework 4.8 and NuGet 10.0.12) sets the quiet bit of a
; float32 signaling NaN in ldc.r4 (plan section 12, row of 2026-10-13 'Plan 5.1
; ... qualified'), until the Phase-2 PE writer replaces ilasm; float64
; signaling NaNs survive. So ret_f32_snan checks only the printed text.
; i1, i8 and i16 results are int32 in CIL: they are returned only as constants
; or as arguments that already carry the matching zeroext/signext attribute, so
; no extension instruction is needed (narrow arithmetic arrives in M4). An
; undef or poison result is a constant zero at -O0 and a read of a never-written
; CIL local at -O2 (see ret_i32_undef). 'unreachable' ends the method with
; ldnull; throw (TrapUnreachable, decision D8), never by falling off its end.
; Access is 'public' for every non-local linkage (linkonce and weak
; definitions are ordinary definitions, decision D26) and 'assembly' for
; internal and private functions. Since M12 MSILPrepareIR drops a linkonce or
; linkonce_odr definition that nothing uses (discardable-defs.ll), so
; ret_linkonce_odr has a caller; the internal and private functions, which
; nothing uses either, stay. The body checks run at llc's default -O2;
; the -O0 runs check the headers, the undef and poison bodies (O0) and that
; the output assembles.

; No GNU assembler directive may reach the output (asmprinter.md 12).
; DEFINE: %{sig} = FileCheck %s --check-prefix=SIG \
; DEFINE:   --implicit-check-not="{{^[[:space:]]*\.(text|data|bss|section|globl|weak|p2align|align|type|size|file|ident|addrsig)([[:space:]]|$)}}"

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s -o %t.lnx.il
; RUN: FileCheck %s --input-file=%t.lnx.il
; RUN: %{sig} --input-file=%t.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s -o %t.win.il
; RUN: FileCheck %s --input-file=%t.win.il
; RUN: %{sig} --input-file=%t.win.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %s -o %t.lnx.o0.il
; RUN: %{sig} --input-file=%t.lnx.o0.il
; RUN: FileCheck %s --check-prefix=O0 --input-file=%t.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %s -o %t.win.o0.il
; RUN: %{sig} --input-file=%t.win.o0.il
; RUN: FileCheck %s --check-prefix=O0 --input-file=%t.win.o0.il
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

; Every method header, in order, as whole lines.
; SIG:      {{^}}.method public static void 'ret_void'() cil managed{{$}}
; SIG:      {{^}}.method public static int32 'ret_i32'() cil managed{{$}}
; SIG:      {{^}}.method public static native int 'ret_i64_small'() cil managed{{$}}
; SIG:      {{^}}.method public static native int 'ret_i64_large'() cil managed{{$}}
; SIG:      {{^}}.method public static native int 'ret_ptr_null'() cil managed{{$}}
; SIG:      {{^}}.method public static float32 'ret_f32_one'() cil managed{{$}}
; SIG:      {{^}}.method public static float32 'ret_f32_pos_zero'() cil managed{{$}}
; SIG:      {{^}}.method public static float32 'ret_f32_neg_zero'() cil managed{{$}}
; SIG:      {{^}}.method public static float32 'ret_f32_nan_payload'() cil managed{{$}}
; SIG:      {{^}}.method public static float32 'ret_f32_snan'() cil managed{{$}}
; SIG:      {{^}}.method public static float32 'ret_f32_neg_inf'() cil managed{{$}}
; SIG:      {{^}}.method public static float32 'ret_f32_denormal'() cil managed{{$}}
; SIG:      {{^}}.method public static float64 'ret_f64_one_half'() cil managed{{$}}
; SIG:      {{^}}.method public static float64 'ret_f64_pos_zero'() cil managed{{$}}
; SIG:      {{^}}.method public static float64 'ret_f64_neg_zero'() cil managed{{$}}
; SIG:      {{^}}.method public static float64 'ret_f64_qnan_payload'() cil managed{{$}}
; SIG:      {{^}}.method public static float64 'ret_f64_neg_qnan'() cil managed{{$}}
; SIG:      {{^}}.method public static float64 'ret_f64_snan'() cil managed{{$}}
; SIG:      {{^}}.method public static float64 'ret_f64_pos_inf'() cil managed{{$}}
; SIG:      {{^}}.method public static float64 'ret_f64_denormal'() cil managed{{$}}
; SIG:      {{^}}.method public static float64 'ret_f64_max'() cil managed{{$}}
; SIG:      {{^}}.method public static int32 'ret_i1_zext_true'() cil managed{{$}}
; SIG:      {{^}}.method public static int32 'ret_i1_sext_true'() cil managed{{$}}
; SIG:      {{^}}.method public static int32 'ret_u8_max'() cil managed{{$}}
; SIG:      {{^}}.method public static int32 'ret_s8_min'() cil managed{{$}}
; SIG:      {{^}}.method public static int32 'ret_u16_max'() cil managed{{$}}
; SIG:      {{^}}.method public static int32 'ret_s16_min'() cil managed{{$}}
; SIG:      {{^}}.method public static int32 'ret_i1_zext_arg'(int32) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'ret_u8_arg'(int32) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'ret_s8_arg'(int32) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'ret_s16_arg'(int32) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'ret_u16_arg'(int32) cil managed{{$}}
; SIG:      {{^}}.method public static int32 'ret_i32_undef'() cil managed{{$}}
; SIG:      {{^}}.method public static float64 'ret_f64_poison'() cil managed{{$}}
; SIG:      {{^}}.method public static void 'ret_void_unreachable'() cil managed{{$}}
; SIG:      {{^}}.method public static int32 'ret_i32_unreachable'() cil managed{{$}}
; SIG:      {{^}}.method assembly static int32 'ret_internal'() cil managed{{$}}
; SIG:      {{^}}.method assembly static int32 'ret_private'() cil managed{{$}}
; SIG:      {{^}}.method public static int32 'ret_linkonce_odr'() cil managed{{$}}
; SIG:      {{^}}.method public static int32 'ret_weak'() cil managed{{$}}
; SIG:      {{^}}.method public static int32 'ret_hidden'() cil managed{{$}}
; SIG:      {{^}}.method public static int32 'call_ret_linkonce_odr'() cil managed{{$}}
; SIG-NOT:  .method

source_filename = "ret.c"

; CHECK-LABEL: .method {{.*}}'ret_void'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ret_void'{{$}}
define void @ret_void() {
  ret void
}

; CHECK-LABEL: .method {{.*}}'ret_i32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.7{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ret_i32'{{$}}
define i32 @ret_i32() {
  ret i32 7
}

; A 64-bit value is a native int: an int32-range constant is ldc.i4 and conv.i,
; a larger one ldc.i8 and conv.i (decision D2). ldc + conv.i needs no extra
; stack slot.
; CHECK-LABEL: .method {{.*}}'ret_i64_small'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  {{ldc.i4.s -2|ldc.i4 -2}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ret_i64_small'{{$}}
define i64 @ret_i64_small() {
  ret i64 -2
}

; CHECK-LABEL: .method {{.*}}'ret_i64_large'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i8 81985529216486895{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ret_i64_large'{{$}}
define i64 @ret_i64_large() {
  ret i64 81985529216486895 ; 0x0123456789ABCDEF
}

; CHECK-LABEL: .method {{.*}}'ret_ptr_null'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  {{ldc.i4.0|ldc.i4 0}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ret_ptr_null'{{$}}
define ptr @ret_ptr_null() {
  ret ptr null
}

; float32 constants (IR writes a float constant as the double of the same
; value).
; CHECK-LABEL: .method {{.*}}'ret_f32_one'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.r4 float32(0x3F800000){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ret_f32_one'{{$}}
define float @ret_f32_one() {
  ret float 1.0
}

; CHECK-LABEL: .method {{.*}}'ret_f32_pos_zero'(
; CHECK:       {{^}}  ldc.r4 float32(0x00000000){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define float @ret_f32_pos_zero() {
  ret float 0.0
}

; CHECK-LABEL: .method {{.*}}'ret_f32_neg_zero'(
; CHECK:       {{^}}  ldc.r4 float32(0x80000000){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define float @ret_f32_neg_zero() {
  ret float -0.0
}

; Quiet NaN with payload 1: float32 0x7FC00001.
; CHECK-LABEL: .method {{.*}}'ret_f32_nan_payload'(
; CHECK:       {{^}}  ldc.r4 float32(0x7FC00001){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define float @ret_f32_nan_payload() {
  ret float 0x7FF8000020000000
}

; Signaling NaN with payload 1: float32 0x7F800001. The printed text keeps its
; bits, which is all that is checked: Phase-1 ilasm assembles this ldc.r4 as
; 0x7FC00001 (quiet bit set; plan section 12, 2026-10-13), so the round trips
; below only show that it assembles, and no execution test may depend on the
; bits of a float32 signaling NaN until the Phase-2 PE writer.
; CHECK-LABEL: .method {{.*}}'ret_f32_snan'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.r4 float32(0x7F800001){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ret_f32_snan'{{$}}
define float @ret_f32_snan() {
  ret float 0x7FF0000020000000
}

; CHECK-LABEL: .method {{.*}}'ret_f32_neg_inf'(
; CHECK:       {{^}}  ldc.r4 float32(0xFF800000){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define float @ret_f32_neg_inf() {
  ret float 0xFFF0000000000000
}

; The smallest float32 denormal, 2^-149.
; CHECK-LABEL: .method {{.*}}'ret_f32_denormal'(
; CHECK:       {{^}}  ldc.r4 float32(0x00000001){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define float @ret_f32_denormal() {
  ret float 0x36A0000000000000
}

; CHECK-LABEL: .method {{.*}}'ret_f64_one_half'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.r8 float64(0x3FF8000000000000){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ret_f64_one_half'{{$}}
define double @ret_f64_one_half() {
  ret double 1.5
}

; CHECK-LABEL: .method {{.*}}'ret_f64_pos_zero'(
; CHECK:       {{^}}  ldc.r8 float64(0x0000000000000000){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @ret_f64_pos_zero() {
  ret double 0.0
}

; CHECK-LABEL: .method {{.*}}'ret_f64_neg_zero'(
; CHECK:       {{^}}  ldc.r8 float64(0x8000000000000000){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @ret_f64_neg_zero() {
  ret double -0.0
}

; CHECK-LABEL: .method {{.*}}'ret_f64_qnan_payload'(
; CHECK:       {{^}}  ldc.r8 float64(0x7FF8000000000001){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @ret_f64_qnan_payload() {
  ret double 0x7FF8000000000001
}

; CHECK-LABEL: .method {{.*}}'ret_f64_neg_qnan'(
; CHECK:       {{^}}  ldc.r8 float64(0xFFF8000000000000){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @ret_f64_neg_qnan() {
  ret double 0xFFF8000000000000
}

; A float64 signaling NaN keeps its bits in the printed text, and ilasm keeps
; them in ldc.r8 too (unlike float32, see ret_f32_snan).
; CHECK-LABEL: .method {{.*}}'ret_f64_snan'(
; CHECK:       {{^}}  ldc.r8 float64(0x7FF0000000000001){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @ret_f64_snan() {
  ret double 0x7FF0000000000001
}

; CHECK-LABEL: .method {{.*}}'ret_f64_pos_inf'(
; CHECK:       {{^}}  ldc.r8 float64(0x7FF0000000000000){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @ret_f64_pos_inf() {
  ret double 0x7FF0000000000000
}

; CHECK-LABEL: .method {{.*}}'ret_f64_denormal'(
; CHECK:       {{^}}  ldc.r8 float64(0x0000000000000001){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @ret_f64_denormal() {
  ret double 0x0000000000000001
}

; CHECK-LABEL: .method {{.*}}'ret_f64_max'(
; CHECK:       {{^}}  ldc.r8 float64(0x7FEFFFFFFFFFFFFF){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @ret_f64_max() {
  ret double 0x7FEFFFFFFFFFFFFF
}

; Narrow results are int32: the constant is extended as the return attribute
; says (true is 1 zero-extended and -1 sign-extended).
; CHECK-LABEL: .method {{.*}}'ret_i1_zext_true'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define zeroext i1 @ret_i1_zext_true() {
  ret i1 true
}

; CHECK-LABEL: .method {{.*}}'ret_i1_sext_true'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.m1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define signext i1 @ret_i1_sext_true() {
  ret i1 true
}

; CHECK-LABEL: .method {{.*}}'ret_u8_max'(
; CHECK:       {{^}}  ldc.i4 255{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define zeroext i8 @ret_u8_max() {
  ret i8 -1
}

; CHECK-LABEL: .method {{.*}}'ret_s8_min'(
; CHECK:       {{^}}  ldc.i4.s -128{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define signext i8 @ret_s8_min() {
  ret i8 -128
}

; CHECK-LABEL: .method {{.*}}'ret_u16_max'(
; CHECK:       {{^}}  ldc.i4 65535{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define zeroext i16 @ret_u16_max() {
  ret i16 -1
}

; CHECK-LABEL: .method {{.*}}'ret_s16_min'(
; CHECK:       {{^}}  ldc.i4 -32768{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define signext i16 @ret_s16_min() {
  ret i16 -32768
}

; A narrow argument with the same extension attribute as the result is
; returned as it is: no conv, no and.
; CHECK-LABEL: .method {{.*}}'ret_i1_zext_arg'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ret_i1_zext_arg'{{$}}
define zeroext i1 @ret_i1_zext_arg(i1 zeroext %x) {
  ret i1 %x
}

; CHECK-LABEL: .method {{.*}}'ret_u8_arg'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define zeroext i8 @ret_u8_arg(i8 zeroext %x) {
  ret i8 %x
}

; CHECK-LABEL: .method {{.*}}'ret_s8_arg'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define signext i8 @ret_s8_arg(i8 signext %x) {
  ret i8 %x
}

; CHECK-LABEL: .method {{.*}}'ret_s16_arg'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define signext i16 @ret_s16_arg(i16 signext %x) {
  ret i16 %x
}

; CHECK-LABEL: .method {{.*}}'ret_u16_arg'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define zeroext i16 @ret_u16_arg(i16 zeroext %x) {
  ret i16 %x
}

; An undef or poison result. At -O0 the IMPLICIT_DEF reaches RegStackify,
; which stackifies it and turns it into CONST_<RC> 0 of the result type, as
; WebAssemblyRegStackify does: a constant zero, no local. At -O1 and above
; ProcessImplicitDefs has already removed the IMPLICIT_DEF, so ExplicitLocals
; gives the undef use a CIL local that is never written and the method returns
; its (unspecified) value, as WebAssembly returns a never-written local; that
; is fine because the value is undef (recon passes-frame.md 6.6). The
; RegStackify rewrite itself is checked in MIR by the undef_* cases of
; reg-stackify-basic.mir.
; CHECK-LABEL: .method {{.*}}'ret_i32_undef'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  .locals (int32){{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ret_i32_undef'{{$}}
; O0-LABEL:    .method {{.*}}'ret_i32_undef'(
; O0-NEXT:     {{^\{}}
; O0-NOT:      {{^ *[^/ ]}}
; O0:          {{^}}  .maxstack 1{{( +//.*)?$}}
; O0-NOT:      {{^ *[^/ ]}}
; O0:          {{^}}  ldc.i4.0{{( +//.*)?$}}
; O0-NEXT:     {{^}}  ret{{( +//.*)?$}}
; O0-NOT:      {{^ *[^/ ]}}
; O0:          {{^}}} // end of method 'ret_i32_undef'{{$}}
define i32 @ret_i32_undef() {
  ret i32 undef
}

; CHECK-LABEL: .method {{.*}}'ret_f64_poison'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  .locals (float64){{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ret_f64_poison'{{$}}
; O0-LABEL:    .method {{.*}}'ret_f64_poison'(
; O0-NEXT:     {{^\{}}
; O0-NOT:      {{^ *[^/ ]}}
; O0:          {{^}}  .maxstack 1{{( +//.*)?$}}
; O0-NOT:      {{^ *[^/ ]}}
; O0:          {{^}}  ldc.r8 float64(0x0000000000000000){{( +//.*)?$}}
; O0-NEXT:     {{^}}  ret{{( +//.*)?$}}
; O0-NOT:      {{^ *[^/ ]}}
; O0:          {{^}}} // end of method 'ret_f64_poison'{{$}}
define double @ret_f64_poison() {
  ret double poison
}

; 'unreachable' is a trap (ldnull; throw, one extra stack slot); the method
; never falls off its end and needs no 'ret'.
; CHECK-LABEL: .method {{.*}}'ret_void_unreachable'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  throw{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ret_void_unreachable'{{$}}
define void @ret_void_unreachable() {
  unreachable
}

; CHECK-LABEL: .method {{.*}}'ret_i32_unreachable'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  throw{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ret_i32_unreachable'{{$}}
define i32 @ret_i32_unreachable() {
  unreachable
}

; Linkage: the header names the access, and the name is the IR name for every
; linkage (decision D11; never a mangled or '.L'-prefixed symbol).
; CHECK-LABEL: .method {{.*}}'ret_internal'(
; CHECK:       {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ret_internal'{{$}}
define internal i32 @ret_internal() {
  ret i32 1
}

; CHECK-LABEL: .method {{.*}}'ret_private'(
; CHECK:       {{^}}  ldc.i4.2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ret_private'{{$}}
define private i32 @ret_private() {
  ret i32 2
}

; CHECK-LABEL: .method {{.*}}'ret_linkonce_odr'(
; CHECK:       {{^}}  ldc.i4.3{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define linkonce_odr i32 @ret_linkonce_odr() {
  ret i32 3
}

; CHECK-LABEL: .method {{.*}}'ret_weak'(
; CHECK:       {{^}}  ldc.i4.4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define weak i32 @ret_weak() {
  ret i32 4
}

; CHECK-LABEL: .method {{.*}}'ret_hidden'(
; CHECK:       {{^}}  ldc.i4.5{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ret_hidden'{{$}}
define hidden i32 @ret_hidden() {
  ret i32 5
}

; CHECK-LABEL: .method {{.*}}'call_ret_linkonce_odr'(
; CHECK:       {{^}}  call int32 'ret_linkonce_odr'(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_ret_linkonce_odr'{{$}}
define i32 @call_ret_linkonce_odr() {
  %r = call i32 @ret_linkonce_odr()
  ret i32 %r
}

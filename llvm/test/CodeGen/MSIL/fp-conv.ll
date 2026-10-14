; Floating-point conversions (ImplementationPlan.md M5.a, plan 5.4
; 'Conversions' CONV_R4_{I32,I64,F64}, CONV_R8_{I32,I64,F32}, CONV_R_UN_I32,
; CONV_I4/U4/I/U_{F32,F64}; recon isel.md 3.2 and 5.4): each conversion is one
; CIL opcode, except uitofp from int32 to float32, which is conv.r.un (exact
; for every 32-bit value) and then conv.r4 (the only rounding), and sitofp
; from int64 to float32.
;   fpext f32->f64 conv.r8; fptrunc f64->f32 conv.r4 (kept between an fptrunc
;   and an fpext: the rounding to float32 is not folded away);
;   sitofp i32 -> f32/f64 and i64 -> f64 conv.r4/conv.r8 directly; sitofp
;   i64 -> f32 first rounds the native int to odd at bit 11 when it needs
;   more than 53 bits, with integer operations only, and then converts it
;   with one conv.r4, so an int64 source is rounded once even where the JIT
;   folds conv.r4 through float64 (never conv.r8 then conv.r4; plan section
;   12, row 'M5.a extended (int64 to float32 under JIT constant folding)');
;   uitofp i32 -> f64 conv.r.un, -> f32 conv.r.un; conv.r4 (uitofp from a
;   64-bit value is the decision D17 sequence, u64-to-fp.ll);
;   fptosi -> i32/i64 conv.i4/conv.i, fptoui -> i32/i64 conv.u4/conv.u (a
;   64-bit result is a native int, decision D2: never conv.i8 or conv.u8, and
;   never the throwing conv.ovf forms; out-of-range inputs are poison).
; Narrow integers are int32 in CIL: an i8 argument without signext/zeroext is
; extended (conv.i1, conv.u1) before it is converted, an argument that carries
; the attribute is not, and an fptosi/fptoui result that is returned as a
; signext/zeroext i8 or i16 needs no extension (an out-of-range result is
; poison). fptosi.sat and fptoui.sat are Expand: compares and selects around
; the plain conversion, with no call and no memory (their results are checked
; by M5.d). The bitcasts between integers and floating point go through
; memory and are tested in M7.c (plan 5.5). The constrained conversions
; (llvm.experimental.constrained.*, strictfp) compile to the CIL of the plain
; ones, the rounding to odd and the D17 sequence included: CIL has no
; floating-point exceptions and rounds only to nearest (plan section 12, row
; 'M5.a extended (constrained floating point and rounding mode)').
;
; Conversions of an argument compile to the same CIL at -O0 and -O2 (CHECK);
; conversions of computed values are checked exactly at -O2 (O2) and for their
; opcodes at -O0 (O0). The MIR lines check the plan 5.4 record names and
; register classes after instruction selection (there is no CONV_R_UN_I64,
; D17).

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=conv.i8 \
; DEFINE:   --implicit-check-not=conv.u8 --implicit-check-not=conv.ovf --implicit-check-not=call \
; DEFINE:   --implicit-check-not=localloc --implicit-check-not=ldind --implicit-check-not=stind
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=MIR --implicit-check-not=CONV_R_UN_I64
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "fp-conv.c"

; ---- between float32 and float64 ------------------------------------------

; CHECK-LABEL: .method {{.*}}'fpext_f32_f64'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpext_f32_f64'{{$}}
; MIR-LABEL:   name: fpext_f32_f64{{$}}
; MIR:         [[X:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR:         {{%[0-9]+}}:f64 = {{([a-z]+ )*}}CONV_R8_F32 [[X]],
define double @fpext_f32_f64(float %x) {
  %r = fpext float %x to double
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'fptrunc_f64_f32'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fptrunc_f64_f32'{{$}}
; MIR-LABEL:   name: fptrunc_f64_f32{{$}}
; MIR:         [[X:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR:         {{%[0-9]+}}:f32 = {{([a-z]+ )*}}CONV_R4_F64 [[X]],
define float @fptrunc_f64_f32(double %x) {
  %r = fptrunc double %x to float
  ret float %r
}

; Rounding to float32 and widening again is not an identity.
; CHECK-LABEL: .method {{.*}}'fptrunc_fpext_f64'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.r4{{$}}
; CHECK-NEXT:  {{^}}  conv.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fptrunc_fpext_f64'{{$}}
define double @fptrunc_fpext_f64(double %x) {
  %f = fptrunc double %x to float
  %r = fpext float %f to double
  ret double %r
}

; ---- integer to floating point ---------------------------------------------

; CHECK-LABEL: .method {{.*}}'sitofp_i32_f32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sitofp_i32_f32'{{$}}
; MIR-LABEL:   name: sitofp_i32_f32{{$}}
; MIR:         [[X:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR:         {{%[0-9]+}}:f32 = {{([a-z]+ )*}}CONV_R4_I32 [[X]],
define float @sitofp_i32_f32(i32 %x) {
  %r = sitofp i32 %x to float
  ret float %r
}

; CHECK-LABEL: .method {{.*}}'sitofp_i32_f64'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sitofp_i32_f64'{{$}}
; MIR-LABEL:   name: sitofp_i32_f64{{$}}
; MIR:         [[X:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR:         {{%[0-9]+}}:f64 = {{([a-z]+ )*}}CONV_R8_I32 [[X]],
define double @sitofp_i32_f64(i32 %x) {
  %r = sitofp i32 %x to double
  ret double %r
}

; A native int converts to float32 with one rounding. A bare conv.r4 is not
; enough: the .NET Framework 4.8 JIT constant-folds conv.r4 of an int64 as
; (float32)(float64)x, which rounds twice. So x is first rounded to odd at
; bit 11 when its magnitude needs more than 53 bits (t = x xor (x >>s 63);
; f = ((t >>u 53) + 1023) >>u 10; m = f * 0x7FF; x' = (x and not m) or
; (((x and m) + m) and (m + 1))), with no select, branch or memory; x' is
; exact in float64 and rounds to the same float32 as x. Exactly one conv.r4
; follows, of the result of the final or: no conv.r8 (before or after it), no
; conv.r.un, no branch or label (a select would be a D34 triangle). The
; operation order between the 53-bit threshold and the or is not fixed.
; CHECK-LABEL: .method {{.*}}'sitofp_i64_f32'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NOT:   {{^(  conv\.r|  b|  switch|@)}}
; CHECK:       {{^}}  ldc.i4.s 53{{$}}
; CHECK-NEXT:  {{^}}  shr.un{{$}}
; CHECK-NOT:   {{^(  conv\.r|  b|  switch|@)}}
; CHECK:       {{^}}  or{{$}}
; CHECK-NEXT:  {{^}}  conv.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sitofp_i64_f32'{{$}}
; MIR-LABEL:   name: sitofp_i64_f32{{$}}
; MIR:         stack: []
; MIR:         [[X:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR-NOT:     {{CONV_R|SELECT_|PHI|BR}}
; MIR:         {{%[0-9]+}}:i64 = {{([a-z]+ )*}}AND_I64 {{.*}}[[X]],
; MIR-NOT:     {{CONV_R|SELECT_|PHI|BR}}
; MIR:         [[Y:%[0-9]+]]:i64 = {{([a-z]+ )*}}OR_I64 {{%[0-9]+}}, {{%[0-9]+}},
; MIR-NOT:     {{CONV_R|SELECT_|PHI|BR}}
; MIR:         [[R:%[0-9]+]]:f32 = {{([a-z]+ )*}}CONV_R4_I64 [[Y]],
; MIR-NEXT:    RETURN [[R]],
define float @sitofp_i64_f32(i64 %x) {
  %r = sitofp i64 %x to float
  ret float %r
}

; Whether the rounding to odd is needed depends on how many bits the value
; can need. With 10 sign bits (ashr by 9) the magnitude can exceed 2^53, so
; the step stays (the sign of y is the sign of x: x >>s 63). With 11 sign
; bits (ashr by 10) the magnitude is at most 2^53, exact in float64, so a bare
; conv.r4 rounds once on every runtime and is all there is.
; CHECK-LABEL: .method {{.*}}'sitofp_ashr9_i64_f32'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NOT:   {{^(  conv\.r|  b|  switch|@)}}
; CHECK:       {{^}}  ldc.i4.s 9{{$}}
; CHECK-NEXT:  {{^}}  shr{{$}}
; CHECK-NOT:   {{^(  conv\.r|  b|  switch|@)}}
; CHECK:       {{^}}  ldc.i4.s 53{{$}}
; CHECK-NEXT:  {{^}}  shr.un{{$}}
; CHECK-NOT:   {{^(  conv\.r|  b|  switch|@)}}
; CHECK:       {{^}}  ldc.i4 1023{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NOT:   {{^(  conv\.r|  b|  switch|@)}}
; CHECK:       {{^}}  ldc.i4.s 10{{$}}
; CHECK-NEXT:  {{^}}  shr.un{{$}}
; CHECK-NOT:   {{^(  conv\.r|  b|  switch|@)}}
; CHECK:       {{^}}  ldc.i4 2047{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NOT:   {{^(  conv\.r|  b|  switch|@)}}
; CHECK:       {{^}}  or{{$}}
; CHECK-NEXT:  {{^}}  conv.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sitofp_ashr9_i64_f32'{{$}}
define float @sitofp_ashr9_i64_f32(i64 %x) {
  %y = ashr i64 %x, 9
  %r = sitofp i64 %y to float
  ret float %r
}

; CHECK-LABEL: .method {{.*}}'sitofp_ashr10_i64_f32'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 10{{$}}
; CHECK-NEXT:  {{^}}  shr{{$}}
; CHECK-NEXT:  {{^}}  conv.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sitofp_ashr10_i64_f32'{{$}}
define float @sitofp_ashr10_i64_f32(i64 %x) {
  %y = ashr i64 %x, 10
  %r = sitofp i64 %y to float
  ret float %r
}

; CHECK-LABEL: .method {{.*}}'sitofp_i64_f64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sitofp_i64_f64'{{$}}
; MIR-LABEL:   name: sitofp_i64_f64{{$}}
; MIR:         [[X:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR:         {{%[0-9]+}}:f64 = {{([a-z]+ )*}}CONV_R8_I64 [[X]],
define double @sitofp_i64_f64(i64 %x) {
  %r = sitofp i64 %x to double
  ret double %r
}

; conv.r.un of an int32 is exact; conv.r4 is the only rounding.
; CHECK-LABEL: .method {{.*}}'uitofp_i32_f32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.r.un{{$}}
; CHECK-NEXT:  {{^}}  conv.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'uitofp_i32_f32'{{$}}
; MIR-LABEL:   name: uitofp_i32_f32{{$}}
; MIR:         [[X:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR:         [[T0:%[0-9]+]]:f64 = {{([a-z]+ )*}}CONV_R_UN_I32 [[X]],
; MIR:         {{%[0-9]+}}:f32 = {{([a-z]+ )*}}CONV_R4_F64 [[T0]],
define float @uitofp_i32_f32(i32 %x) {
  %r = uitofp i32 %x to float
  ret float %r
}

; conv.r.un alone is the float64 result (a conv.r8 after it is allowed).
; CHECK-LABEL: .method {{.*}}'uitofp_i32_f64'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.r.un{{$}}
; CHECK-NEXT:  {{^}}  {{(conv\.r8|ret)}}{{$}}
; CHECK-NOT:   {{^}}  {{[^r]}}
; CHECK:       {{^}}} // end of method 'uitofp_i32_f64'{{$}}
; MIR-LABEL:   name: uitofp_i32_f64{{$}}
; MIR:         [[X:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR:         {{%[0-9]+}}:f64 = {{([a-z]+ )*}}CONV_R_UN_I32 [[X]],
define double @uitofp_i32_f64(i32 %x) {
  %r = uitofp i32 %x to double
  ret double %r
}

; An i8 argument without signext has unknown upper bits: conv.i1 first.
; CHECK-LABEL: .method {{.*}}'sitofp_i8_f64'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i1{{$}}
; CHECK-NEXT:  {{^}}  conv.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sitofp_i8_f64'{{$}}
define double @sitofp_i8_f64(i8 %x) {
  %r = sitofp i8 %x to double
  ret double %r
}

; A signext i16 argument is already sign-extended.
; CHECK-LABEL: .method {{.*}}'sitofp_i16_signext_f32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sitofp_i16_signext_f32'{{$}}
define float @sitofp_i16_signext_f32(i16 signext %x) {
  %r = sitofp i16 %x to float
  ret float %r
}

; An i8 argument without zeroext is masked to 8 bits first (conv.u1); the
; masked value is converted as an unsigned int32 and rounded once.
; CHECK-LABEL: .method {{.*}}'uitofp_i8_f32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u1{{$}}
; CHECK-NOT:   {{^}}  conv.r8{{$}}
; CHECK:       {{^}}  conv.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'uitofp_i8_f32'{{$}}
define float @uitofp_i8_f32(i8 %x) {
  %r = uitofp i8 %x to float
  ret float %r
}

; ---- floating point to integer ---------------------------------------------

; CHECK-LABEL: .method {{.*}}'fptosi_f32_i32'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fptosi_f32_i32'{{$}}
; MIR-LABEL:   name: fptosi_f32_i32{{$}}
; MIR:         [[X:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR:         {{%[0-9]+}}:i32 = {{([a-z]+ )*}}CONV_I4_F32 [[X]],
define i32 @fptosi_f32_i32(float %x) {
  %r = fptosi float %x to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'fptosi_f64_i32'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fptosi_f64_i32'{{$}}
; MIR-LABEL:   name: fptosi_f64_i32{{$}}
; MIR:         [[X:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR:         {{%[0-9]+}}:i32 = {{([a-z]+ )*}}CONV_I4_F64 [[X]],
define i32 @fptosi_f64_i32(double %x) {
  %r = fptosi double %x to i32
  ret i32 %r
}

; A 64-bit result is a native int: conv.i (D2).
; CHECK-LABEL: .method {{.*}}'fptosi_f32_i64'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fptosi_f32_i64'{{$}}
; MIR-LABEL:   name: fptosi_f32_i64{{$}}
; MIR:         [[X:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR:         {{%[0-9]+}}:i64 = {{([a-z]+ )*}}CONV_I_F32 [[X]],
define i64 @fptosi_f32_i64(float %x) {
  %r = fptosi float %x to i64
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'fptosi_f64_i64'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fptosi_f64_i64'{{$}}
; MIR-LABEL:   name: fptosi_f64_i64{{$}}
; MIR:         [[X:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR:         {{%[0-9]+}}:i64 = {{([a-z]+ )*}}CONV_I_F64 [[X]],
define i64 @fptosi_f64_i64(double %x) {
  %r = fptosi double %x to i64
  ret i64 %r
}

; conv.u4 and conv.u convert the whole unsigned range (isel.md 3.2: 4e9
; to uint32 and 1.8e19 to uint64 on every runtime).
; CHECK-LABEL: .method {{.*}}'fptoui_f32_i32'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fptoui_f32_i32'{{$}}
; MIR-LABEL:   name: fptoui_f32_i32{{$}}
; MIR:         [[X:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR:         {{%[0-9]+}}:i32 = {{([a-z]+ )*}}CONV_U4_F32 [[X]],
define i32 @fptoui_f32_i32(float %x) {
  %r = fptoui float %x to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'fptoui_f64_i32'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fptoui_f64_i32'{{$}}
; MIR-LABEL:   name: fptoui_f64_i32{{$}}
; MIR:         [[X:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR:         {{%[0-9]+}}:i32 = {{([a-z]+ )*}}CONV_U4_F64 [[X]],
define i32 @fptoui_f64_i32(double %x) {
  %r = fptoui double %x to i32
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'fptoui_f32_i64'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fptoui_f32_i64'{{$}}
; MIR-LABEL:   name: fptoui_f32_i64{{$}}
; MIR:         [[X:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR:         {{%[0-9]+}}:i64 = {{([a-z]+ )*}}CONV_U_F32 [[X]],
define i64 @fptoui_f32_i64(float %x) {
  %r = fptoui float %x to i64
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'fptoui_f64_i64'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fptoui_f64_i64'{{$}}
; MIR-LABEL:   name: fptoui_f64_i64{{$}}
; MIR:         [[X:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR:         {{%[0-9]+}}:i64 = {{([a-z]+ )*}}CONV_U_F64 [[X]],
define i64 @fptoui_f64_i64(double %x) {
  %r = fptoui double %x to i64
  ret i64 %r
}

; A narrow result is poison when out of range, so a signext i8 or
; zeroext i16 result needs no extension after the int32 conversion.
; CHECK-LABEL: .method {{.*}}'fptosi_f64_i8_signext'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fptosi_f64_i8_signext'{{$}}
define signext i8 @fptosi_f64_i8_signext(double %x) {
  %r = fptosi double %x to i8
  ret i8 %r
}

; CHECK-LABEL: .method {{.*}}'fptoui_f32_i16_zeroext'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fptoui_f32_i16_zeroext'{{$}}
define zeroext i16 @fptoui_f32_i16_zeroext(float %x) {
  %r = fptoui float %x to i16
  ret i16 %r
}

; ---- conversions of computed values ----------------------------------------

; A float32 sum is widened after the float32 addition.
; CHECK-LABEL: .method {{.*}}'fpext_of_sum_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  conv.r8{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  add{{$}}
; O0-NEXT:     {{^}}  conv.r8{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpext_of_sum_f32'{{$}}
define double @fpext_of_sum_f32(float %a, float %b) {
  %s = fadd float %a, %b
  %r = fpext float %s to double
  ret double %r
}

; A float64 product is rounded to float32 once.
; CHECK-LABEL: .method {{.*}}'fptrunc_of_product_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  mul{{$}}
; O2-NEXT:     {{^}}  conv.r4{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  mul{{$}}
; O0-NEXT:     {{^}}  conv.r4{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fptrunc_of_product_f64'{{$}}
define float @fptrunc_of_product_f64(double %a, double %b) {
  %p = fmul double %a, %b
  %r = fptrunc double %p to float
  ret float %r
}

; int32 arithmetic, converted, then float64 arithmetic, truncated back.
; CHECK-LABEL: .method {{.*}}'mixed_i32_f64'(
; CHECK-SAME:  int32, int32, float64) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  mul{{$}}
; O2-NEXT:     {{^}}  conv.r8{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  div{{$}}
; O2-NEXT:     {{^}}  conv.i4{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  mul{{$}}
; O0-NEXT:     {{^}}  conv.r8{{$}}
; O0:          {{^}}  div{{$}}
; O0-NEXT:     {{^}}  conv.i4{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'mixed_i32_f64'{{$}}
define i32 @mixed_i32_f64(i32 %a, i32 %b, double %c) {
  %m = mul i32 %a, %b
  %f = sitofp i32 %m to double
  %q = fdiv double %f, %c
  %r = fptosi double %q to i32
  ret i32 %r
}

; ---- booleans to floating point -------------------------------------------

; (double)(a < b): LLVM may turn the conversion of a compare into a select
; between 1.0 and 0.0 (a D34 triangle) or keep a conversion of the int32 0/1;
; either way the compare is there and nothing calls or touches memory.
; CHECK-LABEL: .method {{.*}}'bool_to_f64'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK:       {{^}}  clt{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'bool_to_f64'{{$}}
define double @bool_to_f64(i32 %a, i32 %b) {
  %c = icmp slt i32 %a, %b
  %z = zext i1 %c to i32
  %r = sitofp i32 %z to double
  ret double %r
}

; A zeroext i1 converted unsigned (1.0 or 0.0) and signed (-1.0 or 0.0).
; CHECK-LABEL: .method {{.*}}'i1_to_f32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i1_to_f32'{{$}}
define float @i1_to_f32(i1 zeroext %b) {
  %r = uitofp i1 %b to float
  ret float %r
}

; CHECK-LABEL: .method {{.*}}'i1_to_f64_signed'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i1_to_f64_signed'{{$}}
define double @i1_to_f64_signed(i1 zeroext %b) {
  %r = sitofp i1 %b to double
  ret double %r
}

; ---- saturating conversions (Expand) ---------------------------------------

; The plain conversion, clamped by compares and selects (and NaN mapped
; to 0 for the signed forms); no library call. When both bounds of the
; integer type are exact in the source type (float64 to int32), the legalizer
; clamps the value before the conversion with fmaximumnum and fminimumnum,
; which M5.a lowers to compares and selects (plan section 12, row 'M5.a
; amended (minimum and maximum)'), so the conversion may take the clamped
; value instead of the argument; otherwise selects replace the result of the
; conversion of the argument.
; CHECK-LABEL: .method {{.*}}'fptosi_sat_f64_i32'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK:       {{^}}  conv.i4{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fptosi_sat_f64_i32'{{$}}
; MIR-LABEL:   name: fptosi_sat_f64_i32{{$}}
; MIR:         {{%[0-9]+}}:f64 = ARGUMENT_F64 0,
; MIR:         {{%[0-9]+}}:i32 = {{([a-z]+ )*}}CONV_I4_F64 {{%[0-9]+}},
define i32 @fptosi_sat_f64_i32(double %x) {
  %r = call i32 @llvm.fptosi.sat.i32.f64(double %x)
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'fptoui_sat_f32_i32'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK:       {{^}}  conv.u4{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fptoui_sat_f32_i32'{{$}}
; MIR-LABEL:   name: fptoui_sat_f32_i32{{$}}
; MIR:         [[X:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR:         {{%[0-9]+}}:i32 = {{([a-z]+ )*}}CONV_U4_F32 [[X]],
define i32 @fptoui_sat_f32_i32(float %x) {
  %r = call i32 @llvm.fptoui.sat.i32.f32(float %x)
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'fptosi_sat_f32_i64'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK:       {{^}}  conv.i{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fptosi_sat_f32_i64'{{$}}
; MIR-LABEL:   name: fptosi_sat_f32_i64{{$}}
; MIR:         [[X:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR:         {{%[0-9]+}}:i64 = {{([a-z]+ )*}}CONV_I_F32 [[X]],
define i64 @fptosi_sat_f32_i64(float %x) {
  %r = call i64 @llvm.fptosi.sat.i64.f32(float %x)
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'fptoui_sat_f64_i64'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK:       {{^}}  conv.u{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fptoui_sat_f64_i64'{{$}}
; MIR-LABEL:   name: fptoui_sat_f64_i64{{$}}
; MIR:         [[X:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR:         {{%[0-9]+}}:i64 = {{([a-z]+ )*}}CONV_U_F64 [[X]],
define i64 @fptoui_sat_f64_i64(double %x) {
  %r = call i64 @llvm.fptoui.sat.i64.f64(double %x)
  ret i64 %r
}

; ---- constrained conversions (strictfp) ------------------------------------

; The constrained intrinsics of a strictfp function (clang -ffp-model=strict)
; compile to the same CIL as the plain conversions: LowerStrictFP replaces
; STRICT_SINT_TO_FP, STRICT_UINT_TO_FP, STRICT_FP_TO_SINT and STRICT_FP_TO_UINT
; with the plain nodes, and the legalizer does so for fptrunc and fpext.
; CHECK-LABEL: .method {{.*}}'strict_fpext_f32_f64'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'strict_fpext_f32_f64'{{$}}
; MIR-LABEL:   name: strict_fpext_f32_f64{{$}}
; MIR:         [[X:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR:         {{%[0-9]+}}:f64 = {{([a-z]+ )*}}CONV_R8_F32 [[X]],
define double @strict_fpext_f32_f64(float %x) #0 {
  %r = call double @llvm.experimental.constrained.fpext.f64.f32(float %x, metadata !"fpexcept.strict") #0
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'strict_fptrunc_f64_f32'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'strict_fptrunc_f64_f32'{{$}}
; MIR-LABEL:   name: strict_fptrunc_f64_f32{{$}}
; MIR:         [[X:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR:         {{%[0-9]+}}:f32 = {{([a-z]+ )*}}CONV_R4_F64 [[X]],
define float @strict_fptrunc_f64_f32(double %x) #0 {
  %r = call float @llvm.experimental.constrained.fptrunc.f32.f64(double %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  ret float %r
}

; CHECK-LABEL: .method {{.*}}'strict_sitofp_i32_f32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'strict_sitofp_i32_f32'{{$}}
; MIR-LABEL:   name: strict_sitofp_i32_f32{{$}}
; MIR:         [[X:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR:         {{%[0-9]+}}:f32 = {{([a-z]+ )*}}CONV_R4_I32 [[X]],
define float @strict_sitofp_i32_f32(i32 %x) #0 {
  %r = call float @llvm.experimental.constrained.sitofp.f32.i32(i32 %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  ret float %r
}

; CHECK-LABEL: .method {{.*}}'strict_sitofp_i32_f64'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'strict_sitofp_i32_f64'{{$}}
; MIR-LABEL:   name: strict_sitofp_i32_f64{{$}}
; MIR:         [[X:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR:         {{%[0-9]+}}:f64 = {{([a-z]+ )*}}CONV_R8_I32 [[X]],
define double @strict_sitofp_i32_f64(i32 %x) #0 {
  %r = call double @llvm.experimental.constrained.sitofp.f64.i32(i32 %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  ret double %r
}

; The int64 to float32 conversion keeps the rounding to odd of
; sitofp_i64_f32: the constants 63 (the sign), 53 (the threshold), 1023, 10
; and 2047 (0x7FF), each with its operation, in data-flow order, then the
; final or and one conv.r4.
; CHECK-LABEL: .method {{.*}}'strict_sitofp_i64_f32'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NOT:   {{^(  conv\.r|  b|  switch|@)}}
; CHECK:       {{^}}  ldc.i4.s 63{{$}}
; CHECK-NEXT:  {{^}}  shr{{$}}
; CHECK-NOT:   {{^(  conv\.r|  b|  switch|@)}}
; CHECK:       {{^}}  ldc.i4.s 53{{$}}
; CHECK-NEXT:  {{^}}  shr.un{{$}}
; CHECK-NOT:   {{^(  conv\.r|  b|  switch|@)}}
; CHECK:       {{^}}  ldc.i4 1023{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NOT:   {{^(  conv\.r|  b|  switch|@)}}
; CHECK:       {{^}}  ldc.i4.s 10{{$}}
; CHECK-NEXT:  {{^}}  shr.un{{$}}
; CHECK-NOT:   {{^(  conv\.r|  b|  switch|@)}}
; CHECK:       {{^}}  ldc.i4 2047{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NOT:   {{^(  conv\.r|  b|  switch|@)}}
; CHECK:       {{^}}  or{{$}}
; CHECK-NEXT:  {{^}}  conv.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'strict_sitofp_i64_f32'{{$}}
; MIR-LABEL:   name: strict_sitofp_i64_f32{{$}}
; MIR:         stack: []
; MIR:         [[X:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR-DAG:     {{%[0-9]+}}:i32 = CONST_I32 53,
; MIR-DAG:     {{%[0-9]+}}:i64 = CONST_I64 1023,
; MIR-DAG:     {{%[0-9]+}}:i32 = CONST_I32 10,
; MIR-DAG:     {{%[0-9]+}}:i64 = CONST_I64 2047,
; MIR-NOT:     {{CONV_R|SELECT_|PHI|BR}}
; MIR:         [[Y:%[0-9]+]]:i64 = {{([a-z]+ )*}}OR_I64 {{%[0-9]+}}, {{%[0-9]+}},
; MIR-NOT:     {{CONV_R|SELECT_|PHI|BR}}
; MIR:         [[R:%[0-9]+]]:f32 = {{([a-z]+ )*}}CONV_R4_I64 [[Y]],
; MIR-NEXT:    RETURN [[R]],
define float @strict_sitofp_i64_f32(i64 %x) #0 {
  %r = call float @llvm.experimental.constrained.sitofp.f32.i64(i64 %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  ret float %r
}

; CHECK-LABEL: .method {{.*}}'strict_sitofp_i64_f64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'strict_sitofp_i64_f64'{{$}}
; MIR-LABEL:   name: strict_sitofp_i64_f64{{$}}
; MIR:         [[X:%[0-9]+]]:i64 = ARGUMENT_I64 0,
; MIR:         {{%[0-9]+}}:f64 = {{([a-z]+ )*}}CONV_R8_I64 [[X]],
define double @strict_sitofp_i64_f64(i64 %x) #0 {
  %r = call double @llvm.experimental.constrained.sitofp.f64.i64(i64 %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'strict_uitofp_i32_f32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.r.un{{$}}
; CHECK-NEXT:  {{^}}  conv.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'strict_uitofp_i32_f32'{{$}}
; MIR-LABEL:   name: strict_uitofp_i32_f32{{$}}
; MIR:         [[X:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR:         [[T0:%[0-9]+]]:f64 = {{([a-z]+ )*}}CONV_R_UN_I32 [[X]],
; MIR:         {{%[0-9]+}}:f32 = {{([a-z]+ )*}}CONV_R4_F64 [[T0]],
define float @strict_uitofp_i32_f32(i32 %x) #0 {
  %r = call float @llvm.experimental.constrained.uitofp.f32.i32(i32 %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  ret float %r
}

; CHECK-LABEL: .method {{.*}}'strict_uitofp_i32_f64'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.r.un{{$}}
; CHECK-NEXT:  {{^}}  {{(conv\.r8|ret)}}{{$}}
; CHECK-NOT:   {{^}}  {{[^r]}}
; CHECK:       {{^}}} // end of method 'strict_uitofp_i32_f64'{{$}}
; MIR-LABEL:   name: strict_uitofp_i32_f64{{$}}
; MIR:         [[X:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR:         {{%[0-9]+}}:f64 = {{([a-z]+ )*}}CONV_R_UN_I32 [[X]],
define double @strict_uitofp_i32_f64(i32 %x) #0 {
  %r = call double @llvm.experimental.constrained.uitofp.f64.i32(i32 %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  ret double %r
}

; uitofp from a 64-bit value is the decision D17 sequence (its shape is
; checked in u64-to-fp.ll): never conv.r.un, and for float32 the rounding to
; odd before the conv.r4.
; CHECK-LABEL: .method {{.*}}'strict_uitofp_i64_f64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NOT:   {{^(  conv\.r\.un|  conv\.r4|  b|  switch|@)}}
; CHECK:       {{^}}  ldc.i4.s 63{{$}}
; CHECK-NEXT:  {{^}}  shr.un{{$}}
; CHECK-NOT:   {{^(  conv\.r\.un|  conv\.r4|  b|  switch|@)}}
; CHECK:       {{^}}  conv.r8{{$}}
; CHECK-NOT:   {{^(  conv\.r\.un|  conv\.r4|  b|  switch|@)}}
; CHECK:       {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'strict_uitofp_i64_f64'{{$}}
define double @strict_uitofp_i64_f64(i64 %x) #0 {
  %r = call double @llvm.experimental.constrained.uitofp.f64.i64(i64 %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'strict_uitofp_i64_f32'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NOT:   {{^(  conv\.r\.un|  conv\.r8|  b|  switch|@)}}
; CHECK:       {{^}}  ldc.i4.s 63{{$}}
; CHECK-NEXT:  {{^}}  shr.un{{$}}
; CHECK-NOT:   {{^(  conv\.r\.un|  conv\.r8|  b|  switch|@)}}
; CHECK:       {{^}}  ldc.i4.s 53{{$}}
; CHECK-NEXT:  {{^}}  shr.un{{$}}
; CHECK-NOT:   {{^(  conv\.r\.un|  conv\.r8|  b|  switch|@)}}
; CHECK:       {{^}}  ldc.i4 2047{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NOT:   {{^(  conv\.r\.un|  conv\.r8|  b|  switch|@)}}
; CHECK:       {{^}}  conv.r4{{$}}
; CHECK-NOT:   {{^(  conv\.r\.un|  conv\.r8|  b|  switch|@)}}
; CHECK:       {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'strict_uitofp_i64_f32'{{$}}
define float @strict_uitofp_i64_f32(i64 %x) #0 {
  %r = call float @llvm.experimental.constrained.uitofp.f32.i64(i64 %x, metadata !"round.dynamic", metadata !"fpexcept.strict") #0
  ret float %r
}

; CHECK-LABEL: .method {{.*}}'strict_fptosi_f32_i32'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'strict_fptosi_f32_i32'{{$}}
; MIR-LABEL:   name: strict_fptosi_f32_i32{{$}}
; MIR:         [[X:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR:         {{%[0-9]+}}:i32 = {{([a-z]+ )*}}CONV_I4_F32 [[X]],
define i32 @strict_fptosi_f32_i32(float %x) #0 {
  %r = call i32 @llvm.experimental.constrained.fptosi.i32.f32(float %x, metadata !"fpexcept.strict") #0
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'strict_fptosi_f32_i64'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'strict_fptosi_f32_i64'{{$}}
; MIR-LABEL:   name: strict_fptosi_f32_i64{{$}}
; MIR:         [[X:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR:         {{%[0-9]+}}:i64 = {{([a-z]+ )*}}CONV_I_F32 [[X]],
define i64 @strict_fptosi_f32_i64(float %x) #0 {
  %r = call i64 @llvm.experimental.constrained.fptosi.i64.f32(float %x, metadata !"fpexcept.strict") #0
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'strict_fptosi_f64_i32'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'strict_fptosi_f64_i32'{{$}}
; MIR-LABEL:   name: strict_fptosi_f64_i32{{$}}
; MIR:         [[X:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR:         {{%[0-9]+}}:i32 = {{([a-z]+ )*}}CONV_I4_F64 [[X]],
define i32 @strict_fptosi_f64_i32(double %x) #0 {
  %r = call i32 @llvm.experimental.constrained.fptosi.i32.f64(double %x, metadata !"fpexcept.strict") #0
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'strict_fptosi_f64_i64'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'strict_fptosi_f64_i64'{{$}}
; MIR-LABEL:   name: strict_fptosi_f64_i64{{$}}
; MIR:         [[X:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR:         {{%[0-9]+}}:i64 = {{([a-z]+ )*}}CONV_I_F64 [[X]],
define i64 @strict_fptosi_f64_i64(double %x) #0 {
  %r = call i64 @llvm.experimental.constrained.fptosi.i64.f64(double %x, metadata !"fpexcept.strict") #0
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'strict_fptoui_f32_i32'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'strict_fptoui_f32_i32'{{$}}
; MIR-LABEL:   name: strict_fptoui_f32_i32{{$}}
; MIR:         [[X:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR:         {{%[0-9]+}}:i32 = {{([a-z]+ )*}}CONV_U4_F32 [[X]],
define i32 @strict_fptoui_f32_i32(float %x) #0 {
  %r = call i32 @llvm.experimental.constrained.fptoui.i32.f32(float %x, metadata !"fpexcept.strict") #0
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'strict_fptoui_f32_i64'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'strict_fptoui_f32_i64'{{$}}
; MIR-LABEL:   name: strict_fptoui_f32_i64{{$}}
; MIR:         [[X:%[0-9]+]]:f32 = ARGUMENT_F32 0,
; MIR:         {{%[0-9]+}}:i64 = {{([a-z]+ )*}}CONV_U_F32 [[X]],
define i64 @strict_fptoui_f32_i64(float %x) #0 {
  %r = call i64 @llvm.experimental.constrained.fptoui.i64.f32(float %x, metadata !"fpexcept.strict") #0
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'strict_fptoui_f64_i32'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'strict_fptoui_f64_i32'{{$}}
; MIR-LABEL:   name: strict_fptoui_f64_i32{{$}}
; MIR:         [[X:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR:         {{%[0-9]+}}:i32 = {{([a-z]+ )*}}CONV_U4_F64 [[X]],
define i32 @strict_fptoui_f64_i32(double %x) #0 {
  %r = call i32 @llvm.experimental.constrained.fptoui.i32.f64(double %x, metadata !"fpexcept.strict") #0
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'strict_fptoui_f64_i64'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'strict_fptoui_f64_i64'{{$}}
; MIR-LABEL:   name: strict_fptoui_f64_i64{{$}}
; MIR:         [[X:%[0-9]+]]:f64 = ARGUMENT_F64 0,
; MIR:         {{%[0-9]+}}:i64 = {{([a-z]+ )*}}CONV_U_F64 [[X]],
define i64 @strict_fptoui_f64_i64(double %x) #0 {
  %r = call i64 @llvm.experimental.constrained.fptoui.i64.f64(double %x, metadata !"fpexcept.strict") #0
  ret i64 %r
}

declare i32 @llvm.fptosi.sat.i32.f64(double)
declare i32 @llvm.fptoui.sat.i32.f32(float)
declare i64 @llvm.fptosi.sat.i64.f32(float)
declare i64 @llvm.fptoui.sat.i64.f64(double)
declare double @llvm.experimental.constrained.fpext.f64.f32(float, metadata)
declare float @llvm.experimental.constrained.fptrunc.f32.f64(double, metadata, metadata)
declare float @llvm.experimental.constrained.sitofp.f32.i32(i32, metadata, metadata)
declare double @llvm.experimental.constrained.sitofp.f64.i32(i32, metadata, metadata)
declare float @llvm.experimental.constrained.sitofp.f32.i64(i64, metadata, metadata)
declare double @llvm.experimental.constrained.sitofp.f64.i64(i64, metadata, metadata)
declare float @llvm.experimental.constrained.uitofp.f32.i32(i32, metadata, metadata)
declare double @llvm.experimental.constrained.uitofp.f64.i32(i32, metadata, metadata)
declare float @llvm.experimental.constrained.uitofp.f32.i64(i64, metadata, metadata)
declare double @llvm.experimental.constrained.uitofp.f64.i64(i64, metadata, metadata)
declare i32 @llvm.experimental.constrained.fptosi.i32.f32(float, metadata)
declare i64 @llvm.experimental.constrained.fptosi.i64.f32(float, metadata)
declare i32 @llvm.experimental.constrained.fptosi.i32.f64(double, metadata)
declare i64 @llvm.experimental.constrained.fptosi.i64.f64(double, metadata)
declare i32 @llvm.experimental.constrained.fptoui.i32.f32(float, metadata)
declare i64 @llvm.experimental.constrained.fptoui.i64.f32(float, metadata)
declare i32 @llvm.experimental.constrained.fptoui.i32.f64(double, metadata)
declare i64 @llvm.experimental.constrained.fptoui.i64.f64(double, metadata)

attributes #0 = { strictfp }

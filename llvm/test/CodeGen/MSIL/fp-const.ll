; Floating-point constants as operands (ImplementationPlan.md 5.1 'FP
; constants are bit-exact', plan 5.4 CONST_F32/CONST_F64; recon isel.md 5.7):
; a float32 constant is 'ldc.r4 float32(0xXXXXXXXX)' and a float64 constant
; 'ldc.r8 float64(0xXXXXXXXXXXXXXXXX)', printed from the bits, so infinities,
; -0.0, +0.0, the smallest and largest denormals, the smallest normal, the
; largest finite value, inexact decimals such as 0.1 and NaNs with payloads
; and either sign keep every bit. ConstantFP is Legal: no constant pool, no
; data segment, no integer bit pattern converted to floating point, and a
; float64 constant that happens to fit in float32 is still ldc.r8 (no
; conversion follows any ldc.r). ret.ll covers returned constants (M3); here
; the constants are operands of float32 and float64 arithmetic, operands of
; calls and selected values. Operations that LLVM folds for the constant are
; avoided (x + -0.0, x - 0.0, x * 1.0, x * 2.0, x / 2^n, and x - C, which the
; DAG combiner turns into x + -C), and NaNs appear only as call arguments, so
; the instructions below are the ones written. The
; float32 signaling-NaN text is in ret.ll (Phase-1 ilasm quiets it in ldc.r4);
; the float64 one is assembled here.
;
; A single operation on an argument and a constant compiles to the same CIL at
; -O0 and -O2 (CHECK); calls with several constant arguments are checked
; exactly at -O2 (O2) and for their constants at -O0 (O0).

; The opcodes that must never appear are checked over the whole output by a
; second FileCheck run (BAN) whose only directive is the '.assembly' line,
; because implicit negative checks do not look between CHECK-DAG matches.
; DEFINE: %{ban} = FileCheck %s --check-prefix=BAN --implicit-check-not=int64 \
; DEFINE:   --implicit-check-not=conv --implicit-check-not=ldc.i --implicit-check-not=ldsfld \
; DEFINE:   --implicit-check-not=ldind --implicit-check-not=.data --implicit-check-not=localloc
; BAN: {{^}}.assembly '
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.lnx.o2.il
; RUN: FileCheck %s --check-prefixes=CHECK,O2 --input-file=%t.lnx.o2.il
; RUN: %{ban} --input-file=%t.lnx.o2.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.win.o2.il
; RUN: FileCheck %s --check-prefixes=CHECK,O2 --input-file=%t.win.o2.il
; RUN: %{ban} --input-file=%t.win.o2.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.lnx.o0.il
; RUN: FileCheck %s --check-prefixes=CHECK,O0 --input-file=%t.lnx.o0.il
; RUN: %{ban} --input-file=%t.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.win.o0.il
; RUN: FileCheck %s --check-prefixes=CHECK,O0 --input-file=%t.win.o0.il
; RUN: %{ban} --input-file=%t.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.o2.dll %t.lnx.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.o0.dll %t.win.o0.il %}

source_filename = "fp-const.c"

; ---- float32 operands ------------------------------------------------------

; +inf.
; CHECK-LABEL: .method {{.*}}'f32_add_pos_inf'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r4 float32(0x7F800000){{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f32_add_pos_inf'{{$}}
define float @f32_add_pos_inf(float %x) {
  %r = fadd float %x, 0x7FF0000000000000
  ret float %r
}

; -inf.
; CHECK-LABEL: .method {{.*}}'f32_mul_neg_inf'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r4 float32(0xFF800000){{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f32_mul_neg_inf'{{$}}
define float @f32_mul_neg_inf(float %x) {
  %r = fmul float %x, 0xFFF0000000000000
  ret float %r
}

; -0.0 (x * -0.0 is not folded: it is -0.0 or +0.0 or NaN).
; CHECK-LABEL: .method {{.*}}'f32_mul_neg_zero'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r4 float32(0x80000000){{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f32_mul_neg_zero'{{$}}
define float @f32_mul_neg_zero(float %x) {
  %r = fmul float %x, 0x8000000000000000
  ret float %r
}

; +0.0 (x + 0.0 is not x when x is -0.0).
; CHECK-LABEL: .method {{.*}}'f32_add_pos_zero'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r4 float32(0x00000000){{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f32_add_pos_zero'{{$}}
define float @f32_add_pos_zero(float %x) {
  %r = fadd float %x, 0x0000000000000000
  ret float %r
}

; The smallest denormal, 2^-149.
; CHECK-LABEL: .method {{.*}}'f32_mul_denorm_min'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r4 float32(0x00000001){{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f32_mul_denorm_min'{{$}}
define float @f32_mul_denorm_min(float %x) {
  %r = fmul float %x, 0x36A0000000000000
  ret float %r
}

; CHECK-LABEL: .method {{.*}}'f32_mul_neg_denorm_min'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r4 float32(0x80000001){{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f32_mul_neg_denorm_min'{{$}}
define float @f32_mul_neg_denorm_min(float %x) {
  %r = fmul float %x, 0xB6A0000000000000
  ret float %r
}

; The largest denormal.
; CHECK-LABEL: .method {{.*}}'f32_mul_denorm_max'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r4 float32(0x007FFFFF){{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f32_mul_denorm_max'{{$}}
define float @f32_mul_denorm_max(float %x) {
  %r = fmul float %x, 0x380FFFFFC0000000
  ret float %r
}

; The smallest normal, 2^-126.
; CHECK-LABEL: .method {{.*}}'f32_mul_normal_min'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r4 float32(0x00800000){{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f32_mul_normal_min'{{$}}
define float @f32_mul_normal_min(float %x) {
  %r = fmul float %x, 0x3810000000000000
  ret float %r
}

; The largest finite float32.
; CHECK-LABEL: .method {{.*}}'f32_mul_max'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r4 float32(0x7F7FFFFF){{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f32_mul_max'{{$}}
define float @f32_mul_max(float %x) {
  %r = fmul float %x, 0x47EFFFFFE0000000
  ret float %r
}

; 0.1f, inexact (its reciprocal is not exact, so the division stays).
; CHECK-LABEL: .method {{.*}}'f32_div_tenth'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r4 float32(0x3DCCCCCD){{$}}
; CHECK-NEXT:  {{^}}  div{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f32_div_tenth'{{$}}
define float @f32_div_tenth(float %x) {
  %r = fdiv float %x, 0x3FB99999A0000000
  ret float %r
}

; 1.5 - x: the constant is the first operand of sub (x - 1.5 would become
; x + -1.5).
; CHECK-LABEL: .method {{.*}}'f32_sub_one_and_half'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldc.r4 float32(0x3FC00000){{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f32_sub_one_and_half'{{$}}
define float @f32_sub_one_and_half(float %x) {
  %r = fsub float 0x3FF8000000000000, %x
  ret float %r
}

; ---- float64 operands ------------------------------------------------------

; CHECK-LABEL: .method {{.*}}'f64_add_pos_inf'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r8 float64(0x7FF0000000000000){{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f64_add_pos_inf'{{$}}
define double @f64_add_pos_inf(double %x) {
  %r = fadd double %x, 0x7FF0000000000000
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'f64_mul_neg_inf'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r8 float64(0xFFF0000000000000){{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f64_mul_neg_inf'{{$}}
define double @f64_mul_neg_inf(double %x) {
  %r = fmul double %x, 0xFFF0000000000000
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'f64_mul_neg_zero'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r8 float64(0x8000000000000000){{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f64_mul_neg_zero'{{$}}
define double @f64_mul_neg_zero(double %x) {
  %r = fmul double %x, 0x8000000000000000
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'f64_add_pos_zero'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r8 float64(0x0000000000000000){{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f64_add_pos_zero'{{$}}
define double @f64_add_pos_zero(double %x) {
  %r = fadd double %x, 0x0000000000000000
  ret double %r
}

; The smallest denormal, 2^-1074.
; CHECK-LABEL: .method {{.*}}'f64_mul_denorm_min'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r8 float64(0x0000000000000001){{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f64_mul_denorm_min'{{$}}
define double @f64_mul_denorm_min(double %x) {
  %r = fmul double %x, 0x0000000000000001
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'f64_mul_neg_denorm_min'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r8 float64(0x8000000000000001){{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f64_mul_neg_denorm_min'{{$}}
define double @f64_mul_neg_denorm_min(double %x) {
  %r = fmul double %x, 0x8000000000000001
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'f64_mul_denorm_max'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r8 float64(0x000FFFFFFFFFFFFF){{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f64_mul_denorm_max'{{$}}
define double @f64_mul_denorm_max(double %x) {
  %r = fmul double %x, 0x000FFFFFFFFFFFFF
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'f64_mul_normal_min'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r8 float64(0x0010000000000000){{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f64_mul_normal_min'{{$}}
define double @f64_mul_normal_min(double %x) {
  %r = fmul double %x, 0x0010000000000000
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'f64_mul_max'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r8 float64(0x7FEFFFFFFFFFFFFF){{$}}
; CHECK-NEXT:  {{^}}  mul{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f64_mul_max'{{$}}
define double @f64_mul_max(double %x) {
  %r = fmul double %x, 0x7FEFFFFFFFFFFFFF
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'f64_div_tenth'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r8 float64(0x3FB999999999999A){{$}}
; CHECK-NEXT:  {{^}}  div{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f64_div_tenth'{{$}}
define double @f64_div_tenth(double %x) {
  %r = fdiv double %x, 0x3FB999999999999A
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'f64_div_three'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r8 float64(0x4008000000000000){{$}}
; CHECK-NEXT:  {{^}}  div{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f64_div_three'{{$}}
define double @f64_div_three(double %x) {
  %r = fdiv double %x, 0x4008000000000000
  ret double %r
}

; 1.5 fits in float32 but is a float64 constant: ldc.r8.
; CHECK-LABEL: .method {{.*}}'f64_add_one_and_half'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.r8 float64(0x3FF8000000000000){{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f64_add_one_and_half'{{$}}
define double @f64_add_one_and_half(double %x) {
  %r = fadd double %x, 0x3FF8000000000000
  ret double %r
}

; ---- constants as call arguments ------------------------------------------

; CHECK-LABEL: .method {{.*}}'sink_f32x3'(
; CHECK-SAME:  float32, float32, float32) cil managed{{$}}
define void @sink_f32x3(float %a, float %b, float %c) {
  ret void
}

; CHECK-LABEL: .method {{.*}}'sink_f64x3'(
; CHECK-SAME:  float64, float64, float64) cil managed{{$}}
define void @sink_f64x3(double %a, double %b, double %c) {
  ret void
}

; CHECK-LABEL: .method {{.*}}'sink_mixed'(
; CHECK-SAME:  float32, float64, float32, float64) cil managed{{$}}
define void @sink_mixed(float %a, double %b, float %c, double %d) {
  ret void
}

; float32 quiet NaNs: payload 1, the negative default NaN, payload 0x123.
; CHECK-LABEL: .method {{.*}}'nan_args_f32'(
; CHECK-SAME:  ) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0x7FC00001){{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0xFFC00000){{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0x7FC00123){{$}}
; O2-NEXT:     {{^}}  call void 'sink_f32x3'(float32, float32, float32){{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0-DAG:      {{^}}  ldc.r4 float32(0x7FC00001){{$}}
; O0-DAG:      {{^}}  ldc.r4 float32(0xFFC00000){{$}}
; O0-DAG:      {{^}}  ldc.r4 float32(0x7FC00123){{$}}
; O0:          {{^}}  call void 'sink_f32x3'(float32, float32, float32){{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'nan_args_f32'{{$}}
define void @nan_args_f32() {
  call void @sink_f32x3(float 0x7FF8000020000000, float 0xFFF8000000000000, float 0x7FF8002460000000)
  ret void
}

; float64 NaNs: quiet with payload 0x123, negative quiet with payload 1, and
; signaling with payload 1 (ilasm keeps float64 signaling NaNs).
; CHECK-LABEL: .method {{.*}}'nan_args_f64'(
; CHECK-SAME:  ) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0x7FF8000000000123){{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0xFFF8000000000001){{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0x7FF0000000000001){{$}}
; O2-NEXT:     {{^}}  call void 'sink_f64x3'(float64, float64, float64){{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0-DAG:      {{^}}  ldc.r8 float64(0x7FF8000000000123){{$}}
; O0-DAG:      {{^}}  ldc.r8 float64(0xFFF8000000000001){{$}}
; O0-DAG:      {{^}}  ldc.r8 float64(0x7FF0000000000001){{$}}
; O0:          {{^}}  call void 'sink_f64x3'(float64, float64, float64){{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'nan_args_f64'{{$}}
define void @nan_args_f64() {
  call void @sink_f64x3(double 0x7FF8000000000123, double 0xFFF8000000000001, double 0x7FF0000000000001)
  ret void
}

; Signed zeros and infinities of both widths in one call: .maxstack 4.
; CHECK-LABEL: .method {{.*}}'mixed_args'(
; CHECK-SAME:  ) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 4{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0x80000000){{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0x8000000000000000){{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0x7F800000){{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0xFFF0000000000000){{$}}
; O2-NEXT:     {{^}}  call void 'sink_mixed'(float32, float64, float32, float64){{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0-DAG:      {{^}}  ldc.r4 float32(0x80000000){{$}}
; O0-DAG:      {{^}}  ldc.r8 float64(0x8000000000000000){{$}}
; O0-DAG:      {{^}}  ldc.r4 float32(0x7F800000){{$}}
; O0-DAG:      {{^}}  ldc.r8 float64(0xFFF0000000000000){{$}}
; O0:          {{^}}  call void 'sink_mixed'(float32, float64, float32, float64){{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'mixed_args'{{$}}
define void @mixed_args() {
  call void @sink_mixed(float -0.0, double -0.0, float 0x7FF0000000000000, double 0xFFF0000000000000)
  ret void
}

; ---- constants in expressions -----------------------------------------------

; The same constant used twice is loaded twice at -O2 (rematerialized), never
; kept in a local.
; CHECK-LABEL: .method {{.*}}'const_twice_f64'(
; CHECK-SAME:  float64) cil managed{{$}}
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0x3FB999999999999A){{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0x3FB999999999999A){{$}}
; O2-NEXT:     {{^}}  mul{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldc.r8 float64(0x3FB999999999999A){{$}}
; O0:          {{^}}  add{{$}}
; O0:          {{^}}  mul{{$}}
; O0-NEXT:     {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'const_twice_f64'{{$}}
define double @const_twice_f64(double %x) {
  %a = fadd double %x, 0x3FB999999999999A
  %m = fmul double %a, 0x3FB999999999999A
  ret double %m
}

; A constant arm of a select (D34 triangle): -0.0 as float32, NaN as
; float64. The condition is a zeroext i1, so no integer constant is needed.
; CHECK-LABEL: .method {{.*}}'select_neg_zero_f32'(
; CHECK-SAME:  int32, float32) cil managed{{$}}
; CHECK:       {{^}}  ldc.r4 float32(0x80000000){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'select_neg_zero_f32'{{$}}
define float @select_neg_zero_f32(i1 zeroext %c, float %x) {
  %r = select i1 %c, float -0.0, float %x
  ret float %r
}

; CHECK-LABEL: .method {{.*}}'select_nan_f64'(
; CHECK-SAME:  int32, float64) cil managed{{$}}
; CHECK:       {{^}}  ldc.r8 float64(0x7FF8000000000001){{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'select_nan_f64'{{$}}
define double @select_nan_f64(i1 zeroext %c, double %x) {
  %r = select i1 %c, double %x, double 0x7FF8000000000001
  ret double %r
}

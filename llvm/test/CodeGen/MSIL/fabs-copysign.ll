; fabs, copysign and llvm.is.fpclass (ImplementationPlan.md decision D16, M7;
; plan 5.5 moved fabs and copysign here from M5; plan section 12, row 'M7.c
; extended: fabs-copysign.ll also covers llvm.is.fpclass'): Phase 1 calls no
; BCL helper for them (Math.Abs and Math.CopySign wait for the phase-3
; profiles), so FABS and FCOPYSIGN take LLVM's generic expansions, which work on
; the integer bits of the value: the float is bitcast to an integer through a
; stack temporary of the static frame (bitcast.ll), the sign bit is cleared
; with 'and' (fabs) or replaced with 'and' and 'or' (copysign), and the result
; is bitcast back. fabs of a float64 clears bit 63 of a native int, so the mask
; is the native int 0x7FFFFFFFFFFFFFFF (ldc.i8 + conv.i, never int64). The
; generic expansion of llvm.is.fpclass also tests the integer bits, so it needs
; the frame as well (the M5 state was 'frame indices are implemented in M7'):
; every class test compiles, in an ordinary function and in a strictfp
; function (clang emits it for isnan, isinf, isfinite and isnormal under
; -ffp-model=strict), on f32 and f64, at -O0 and -O2, with no library call,
; and the module assembles. M7.e checks the values at run time
; (msil/test/M1/05-memory/fp-bits.c).

; DEFINE: %{check} = FileCheck %s --implicit-check-not="{{ call}}" --implicit-check-not=".locals init" \
; DEFINE:   --implicit-check-not=int64 --implicit-check-not=pinvokeimpl
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil < %s -o %t.lnx.o2.il \
; RUN:   && %ilasm-verify -output=%t.lnx.o2.dll %t.lnx.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "fabs-copysign.c"

declare float @llvm.fabs.f32(float)
declare double @llvm.fabs.f64(double)
declare float @llvm.copysign.f32(float, float)
declare double @llvm.copysign.f64(double, double)
declare i1 @llvm.is.fpclass.f32(float, i32)
declare i1 @llvm.is.fpclass.f64(double, i32)

; fabs(float): float32 -> int32 through memory, and with 0x7FFFFFFF, back.
; CHECK-LABEL: .method {{.*}}'fabs_f32'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK:       {{^}}  localloc{{$}}
; CHECK-NOT:   localloc
; CHECK:       {{^}}  stind.r4{{$}}
; CHECK:       {{^}}  ldind.i4{{$}}
; CHECK:       {{^}}  ldc.i4 2147483647{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK:       {{^}}  stind.i4{{$}}
; CHECK:       {{^}}  ldind.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fabs_f32'{{$}}
define float @fabs_f32(float %x) {
  %r = call float @llvm.fabs.f32(float %x)
  ret float %r
}

; fabs(double): float64 -> native int, and with a native int mask, back.
; CHECK-LABEL: .method {{.*}}'fabs_f64'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK:       {{^}}  localloc{{$}}
; CHECK-NOT:   localloc
; CHECK:       {{^}}  stind.r8{{$}}
; CHECK:       {{^}}  ldind.i{{$}}
; CHECK:       {{^}}  ldc.i8 9223372036854775807{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK:       {{^}}  stind.i{{$}}
; CHECK:       {{^}}  ldind.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fabs_f64'{{$}}
define double @fabs_f64(double %x) {
  %r = call double @llvm.fabs.f64(double %x)
  ret double %r
}

; -fabs(x) (the sign bit set), still through memory, no call.
; CHECK-LABEL: .method {{.*}}'nabs_f64'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK:       {{^}}  localloc{{$}}
; CHECK-NOT:   localloc
; CHECK:       {{^}}  stind.r8{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'nabs_f64'{{$}}
define double @nabs_f64(double %x) {
  %a = call double @llvm.fabs.f64(double %x)
  %r = fneg double %a
  ret double %r
}

; copysign: the magnitude bits of x and the sign bit of y, combined with 'and'
; and 'or' on the integer bits.
; CHECK-LABEL: .method {{.*}}'copysign_f32'(
; CHECK-SAME:  float32, float32) cil managed{{$}}
; CHECK:       {{^}}  localloc{{$}}
; CHECK-NOT:   localloc
; CHECK-DAG:   {{^}}  stind.r4{{$}}
; CHECK-DAG:   {{^}}  and{{$}}
; CHECK-DAG:   {{^}}  or{{$}}
; CHECK:       {{^}}  ldind.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'copysign_f32'{{$}}
define float @copysign_f32(float %x, float %y) {
  %r = call float @llvm.copysign.f32(float %x, float %y)
  ret float %r
}

; CHECK-LABEL: .method {{.*}}'copysign_f64'(
; CHECK-SAME:  float64, float64) cil managed{{$}}
; CHECK:       {{^}}  localloc{{$}}
; CHECK-NOT:   localloc
; CHECK-DAG:   {{^}}  stind.r8{{$}}
; CHECK-DAG:   {{^}}  and{{$}}
; CHECK-DAG:   {{^}}  or{{$}}
; CHECK:       {{^}}  ldind.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'copysign_f64'{{$}}
define double @copysign_f64(double %x, double %y) {
  %r = call double @llvm.copysign.f64(double %x, double %y)
  ret double %r
}

; copysign with a constant negative sign is -fabs(x).
; CHECK-LABEL: .method {{.*}}'copysign_neg_f32'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK:       {{^}}  localloc{{$}}
; CHECK-NOT:   localloc
; CHECK:       {{^}}  stind.r4{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'copysign_neg_f32'{{$}}
define float @copysign_neg_f32(float %x) {
  %r = call float @llvm.copysign.f32(float %x, float -1.0)
  ret float %r
}

; ---- llvm.is.fpclass ----
; Classes: fcSNan 1, fcNan 3, fcInf 516, fcZero 96, fcNegZero 32,
; fcSubnormal 144, fcNormal 264, fcFinite 504. The i1 result is an int32.

; CHECK-LABEL: .method {{.*}}'fpclass_f32_snan'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f32_snan'{{$}}
define i1 @fpclass_f32_snan(float %x) {
  %r = call i1 @llvm.is.fpclass.f32(float %x, i32 1)
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f32_nan'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f32_nan'{{$}}
define i1 @fpclass_f32_nan(float %x) {
  %r = call i1 @llvm.is.fpclass.f32(float %x, i32 3)
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f32_inf'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f32_inf'{{$}}
define i1 @fpclass_f32_inf(float %x) {
  %r = call i1 @llvm.is.fpclass.f32(float %x, i32 516)
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f32_zero'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f32_zero'{{$}}
define i1 @fpclass_f32_zero(float %x) {
  %r = call i1 @llvm.is.fpclass.f32(float %x, i32 96)
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f32_negzero'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f32_negzero'{{$}}
define i1 @fpclass_f32_negzero(float %x) {
  %r = call i1 @llvm.is.fpclass.f32(float %x, i32 32)
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f32_subnormal'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f32_subnormal'{{$}}
define i1 @fpclass_f32_subnormal(float %x) {
  %r = call i1 @llvm.is.fpclass.f32(float %x, i32 144)
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f32_normal'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f32_normal'{{$}}
define i1 @fpclass_f32_normal(float %x) {
  %r = call i1 @llvm.is.fpclass.f32(float %x, i32 264)
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f32_finite'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f32_finite'{{$}}
define i1 @fpclass_f32_finite(float %x) {
  %r = call i1 @llvm.is.fpclass.f32(float %x, i32 504)
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f64_snan'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f64_snan'{{$}}
define i1 @fpclass_f64_snan(double %x) {
  %r = call i1 @llvm.is.fpclass.f64(double %x, i32 1)
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f64_nan'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f64_nan'{{$}}
define i1 @fpclass_f64_nan(double %x) {
  %r = call i1 @llvm.is.fpclass.f64(double %x, i32 3)
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f64_inf'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f64_inf'{{$}}
define i1 @fpclass_f64_inf(double %x) {
  %r = call i1 @llvm.is.fpclass.f64(double %x, i32 516)
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f64_zero'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f64_zero'{{$}}
define i1 @fpclass_f64_zero(double %x) {
  %r = call i1 @llvm.is.fpclass.f64(double %x, i32 96)
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f64_negzero'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f64_negzero'{{$}}
define i1 @fpclass_f64_negzero(double %x) {
  %r = call i1 @llvm.is.fpclass.f64(double %x, i32 32)
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f64_subnormal'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f64_subnormal'{{$}}
define i1 @fpclass_f64_subnormal(double %x) {
  %r = call i1 @llvm.is.fpclass.f64(double %x, i32 144)
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f64_normal'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f64_normal'{{$}}
define i1 @fpclass_f64_normal(double %x) {
  %r = call i1 @llvm.is.fpclass.f64(double %x, i32 264)
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f64_finite'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f64_finite'{{$}}
define i1 @fpclass_f64_finite(double %x) {
  %r = call i1 @llvm.is.fpclass.f64(double %x, i32 504)
  ret i1 %r
}

; In a strictfp function the class tests may not use FP compares, which could
; raise exceptions, so they work on the integer bits through the frame.
; CHECK-LABEL: .method {{.*}}'fpclass_f32_snan_strict'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f32_snan_strict'{{$}}
define i1 @fpclass_f32_snan_strict(float %x) #0 {
  %r = call i1 @llvm.is.fpclass.f32(float %x, i32 1) #0
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f32_nan_strict'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f32_nan_strict'{{$}}
define i1 @fpclass_f32_nan_strict(float %x) #0 {
  %r = call i1 @llvm.is.fpclass.f32(float %x, i32 3) #0
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f32_inf_strict'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f32_inf_strict'{{$}}
define i1 @fpclass_f32_inf_strict(float %x) #0 {
  %r = call i1 @llvm.is.fpclass.f32(float %x, i32 516) #0
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f32_zero_strict'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f32_zero_strict'{{$}}
define i1 @fpclass_f32_zero_strict(float %x) #0 {
  %r = call i1 @llvm.is.fpclass.f32(float %x, i32 96) #0
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f32_negzero_strict'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f32_negzero_strict'{{$}}
define i1 @fpclass_f32_negzero_strict(float %x) #0 {
  %r = call i1 @llvm.is.fpclass.f32(float %x, i32 32) #0
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f32_subnormal_strict'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f32_subnormal_strict'{{$}}
define i1 @fpclass_f32_subnormal_strict(float %x) #0 {
  %r = call i1 @llvm.is.fpclass.f32(float %x, i32 144) #0
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f32_normal_strict'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f32_normal_strict'{{$}}
define i1 @fpclass_f32_normal_strict(float %x) #0 {
  %r = call i1 @llvm.is.fpclass.f32(float %x, i32 264) #0
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f32_finite_strict'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f32_finite_strict'{{$}}
define i1 @fpclass_f32_finite_strict(float %x) #0 {
  %r = call i1 @llvm.is.fpclass.f32(float %x, i32 504) #0
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f64_snan_strict'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f64_snan_strict'{{$}}
define i1 @fpclass_f64_snan_strict(double %x) #0 {
  %r = call i1 @llvm.is.fpclass.f64(double %x, i32 1) #0
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f64_nan_strict'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f64_nan_strict'{{$}}
define i1 @fpclass_f64_nan_strict(double %x) #0 {
  %r = call i1 @llvm.is.fpclass.f64(double %x, i32 3) #0
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f64_inf_strict'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f64_inf_strict'{{$}}
define i1 @fpclass_f64_inf_strict(double %x) #0 {
  %r = call i1 @llvm.is.fpclass.f64(double %x, i32 516) #0
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f64_zero_strict'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f64_zero_strict'{{$}}
define i1 @fpclass_f64_zero_strict(double %x) #0 {
  %r = call i1 @llvm.is.fpclass.f64(double %x, i32 96) #0
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f64_negzero_strict'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f64_negzero_strict'{{$}}
define i1 @fpclass_f64_negzero_strict(double %x) #0 {
  %r = call i1 @llvm.is.fpclass.f64(double %x, i32 32) #0
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f64_subnormal_strict'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f64_subnormal_strict'{{$}}
define i1 @fpclass_f64_subnormal_strict(double %x) #0 {
  %r = call i1 @llvm.is.fpclass.f64(double %x, i32 144) #0
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f64_normal_strict'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f64_normal_strict'{{$}}
define i1 @fpclass_f64_normal_strict(double %x) #0 {
  %r = call i1 @llvm.is.fpclass.f64(double %x, i32 264) #0
  ret i1 %r
}

; CHECK-LABEL: .method {{.*}}'fpclass_f64_finite_strict'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpclass_f64_finite_strict'{{$}}
define i1 @fpclass_f64_finite_strict(double %x) #0 {
  %r = call i1 @llvm.is.fpclass.f64(double %x, i32 504) #0
  ret i1 %r
}
attributes #0 = { strictfp }

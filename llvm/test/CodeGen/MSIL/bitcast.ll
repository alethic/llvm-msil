; Bitcasts between integers and floating point (ImplementationPlan.md decision
; D16, M7; plan 5.5 moved these tests here from M5): CIL has no instruction that
; reinterprets the bits of a float32 as an int32 or of a float64 as a native
; int, and Phase 1 uses no BCL helper (BitConverter is a phase-3 profile
; decision), so ISD::BITCAST takes LLVM's generic expansion through a stack
; temporary: the value is stored with the store of its own type and loaded back
; with the load of the other type, all inside the static frame of M7.b (one
; localloc in the prologue, FP in a native int local). A bitcast of a bitcast
; folds away and needs no frame. The size and the FP offset of the temporary
; depend on the target's stack alignment, so only the prologue's shape and the
; store/load pair are checked.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=localloc --implicit-check-not=".locals init" \
; DEFINE:   --implicit-check-not="{{ call}}" --implicit-check-not=int64 --implicit-check-not=ldind.i8 \
; DEFINE:   --implicit-check-not=stind.i8
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "bitcast.c"

; CHECK-LABEL: .method {{.*}}'f32_to_i32'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.4|ldc\.i4\.8|ldc\.i4\.s 16}}{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  stind.r4{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK:       {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f32_to_i32'{{$}}
define i32 @f32_to_i32(float %f) {
  %i = bitcast float %f to i32
  ret i32 %i
}

; CHECK-LABEL: .method {{.*}}'i32_to_f32'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.4|ldc\.i4\.8|ldc\.i4\.s 16}}{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK:       {{^}}  ldind.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i32_to_f32'{{$}}
define float @i32_to_f32(i32 %i) {
  %f = bitcast i32 %i to float
  ret float %f
}

; A float64 is reinterpreted as a native int (never int64, decision D2).
; CHECK-LABEL: .method {{.*}}'f64_to_i64'(
; CHECK-SAME:  float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.8|ldc\.i4\.s 16}}{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  stind.r8{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK:       {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f64_to_i64'{{$}}
define i64 @f64_to_i64(double %d) {
  %i = bitcast double %d to i64
  ret i64 %i
}

; CHECK-LABEL: .method {{.*}}'i64_to_f64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.8|ldc\.i4\.s 16}}{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK:       {{^}}  ldind.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i64_to_f64'{{$}}
define double @i64_to_f64(i64 %i) {
  %d = bitcast i64 %i to double
  ret double %d
}

; The bits feed integer arithmetic (the sign bit of a float).
; CHECK-LABEL: .method {{.*}}'f32_sign_bit'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK:       {{^}}  localloc{{$}}
; CHECK:       {{^}}  stind.r4{{$}}
; CHECK:       {{^}}  {{ldind\.i4|ldind\.u1|ldind\.i1}}{{$}}
; CHECK:       {{^}}  {{shr\.un|shr}}{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'f32_sign_bit'{{$}}
define i32 @f32_sign_bit(float %f) {
  %i = bitcast float %f to i32
  %s = lshr i32 %i, 31
  ret i32 %s
}

; Integer bits turned into a float that feeds FP arithmetic.
; CHECK-LABEL: .method {{.*}}'i64_bits_plus_one'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK:       {{^}}  localloc{{$}}
; CHECK:       {{^}}  stind.i{{$}}
; CHECK:       {{^}}  ldind.r8{{$}}
; CHECK-NEXT:  {{^}}  ldc.r8 float64(0x3FF0000000000000){{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @i64_bits_plus_one(i64 %i) {
  %d = bitcast i64 %i to double
  %r = fadd double %d, 1.0
  ret double %r
}

; A bitcast and its inverse cancel: no frame.
; CHECK-LABEL: .method {{.*}}'roundtrip'(
; CHECK-SAME:  float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'roundtrip'{{$}}
define float @roundtrip(float %f) {
  %i = bitcast float %f to i32
  %g = bitcast i32 %i to float
  ret float %g
}

; Integer constants (ImplementationPlan.md 5.1 and M3.d): CONST_I32 prints the
; shortest CIL form, ldc.i4.m1 for -1, ldc.i4.0 to ldc.i4.8, ldc.i4.s for the
; rest of -128..127 and ldc.i4 otherwise; each boundary is checked on both
; sides. A 64-bit constant is a native int (decision D2): a value in the int32
; range is loaded with ldc.i4 (in its long or short form) and any other with
; ldc.i8, and both are followed by conv.i, so int64 never appears on the
; evaluation stack and conv.u is never used. ldc plus conv.i needs no extra
; stack slot (.maxstack 1). Constants are rematerialized next to their use,
; so the bodies are the same at -O0 and -O2, and the output assembles (ilasm
; parses ldc.i8 -9223372036854775808 and ldc.i4 -2147483648).

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=conv.u \
; DEFINE:   --implicit-check-not=.locals --implicit-check-not=stloc
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.il \
; RUN:   && %ilasm-verify -output=%t.dll %t.il %}

source_filename = "constants.c"

; CHECK-LABEL: .method {{.*}}'i32_min'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 -2147483648{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i32_min'{{$}}
define i32 @i32_min() {
  ret i32 -2147483648
}

; CHECK-LABEL: .method {{.*}}'i32_m129'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 -129{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i32_m129'{{$}}
define i32 @i32_m129() {
  ret i32 -129
}

; CHECK-LABEL: .method {{.*}}'i32_m128'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s -128{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i32_m128'{{$}}
define i32 @i32_m128() {
  ret i32 -128
}

; CHECK-LABEL: .method {{.*}}'i32_m2'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s -2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i32_m2'{{$}}
define i32 @i32_m2() {
  ret i32 -2
}

; CHECK-LABEL: .method {{.*}}'i32_m1'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.m1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i32_m1'{{$}}
define i32 @i32_m1() {
  ret i32 -1
}

; CHECK-LABEL: .method {{.*}}'i32_0'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i32_0'{{$}}
define i32 @i32_0() {
  ret i32 0
}

; CHECK-LABEL: .method {{.*}}'i32_1'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i32_1'{{$}}
define i32 @i32_1() {
  ret i32 1
}

; CHECK-LABEL: .method {{.*}}'i32_2'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i32_2'{{$}}
define i32 @i32_2() {
  ret i32 2
}

; CHECK-LABEL: .method {{.*}}'i32_3'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.3{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i32_3'{{$}}
define i32 @i32_3() {
  ret i32 3
}

; CHECK-LABEL: .method {{.*}}'i32_4'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i32_4'{{$}}
define i32 @i32_4() {
  ret i32 4
}

; CHECK-LABEL: .method {{.*}}'i32_5'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.5{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i32_5'{{$}}
define i32 @i32_5() {
  ret i32 5
}

; CHECK-LABEL: .method {{.*}}'i32_6'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.6{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i32_6'{{$}}
define i32 @i32_6() {
  ret i32 6
}

; CHECK-LABEL: .method {{.*}}'i32_7'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.7{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i32_7'{{$}}
define i32 @i32_7() {
  ret i32 7
}

; CHECK-LABEL: .method {{.*}}'i32_8'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i32_8'{{$}}
define i32 @i32_8() {
  ret i32 8
}

; CHECK-LABEL: .method {{.*}}'i32_9'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 9{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i32_9'{{$}}
define i32 @i32_9() {
  ret i32 9
}

; CHECK-LABEL: .method {{.*}}'i32_127'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 127{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i32_127'{{$}}
define i32 @i32_127() {
  ret i32 127
}

; CHECK-LABEL: .method {{.*}}'i32_128'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 128{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i32_128'{{$}}
define i32 @i32_128() {
  ret i32 128
}

; CHECK-LABEL: .method {{.*}}'i32_255'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 255{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i32_255'{{$}}
define i32 @i32_255() {
  ret i32 255
}

; CHECK-LABEL: .method {{.*}}'i32_256'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 256{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i32_256'{{$}}
define i32 @i32_256() {
  ret i32 256
}

; CHECK-LABEL: .method {{.*}}'i32_65536'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 65536{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i32_65536'{{$}}
define i32 @i32_65536() {
  ret i32 65536
}

; CHECK-LABEL: .method {{.*}}'i32_max'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 2147483647{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i32_max'{{$}}
define i32 @i32_max() {
  ret i32 2147483647
}

; CHECK-LABEL: .method {{.*}}'i64_0'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.0|ldc\.i4 0}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i64_0'{{$}}
define i64 @i64_0() {
  ret i64 0
}

; CHECK-LABEL: .method {{.*}}'i64_m1'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.m1|ldc\.i4 -1}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i64_m1'{{$}}
define i64 @i64_m1() {
  ret i64 -1
}

; CHECK-LABEL: .method {{.*}}'i64_8'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.8|ldc\.i4 8}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i64_8'{{$}}
define i64 @i64_8() {
  ret i64 8
}

; CHECK-LABEL: .method {{.*}}'i64_127'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.s 127|ldc\.i4 127}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i64_127'{{$}}
define i64 @i64_127() {
  ret i64 127
}

; CHECK-LABEL: .method {{.*}}'i64_m128'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.s -128|ldc\.i4 -128}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i64_m128'{{$}}
define i64 @i64_m128() {
  ret i64 -128
}

; CHECK-LABEL: .method {{.*}}'i64_128'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 128{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i64_128'{{$}}
define i64 @i64_128() {
  ret i64 128
}

; CHECK-LABEL: .method {{.*}}'i64_i32_max'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 2147483647{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i64_i32_max'{{$}}
define i64 @i64_i32_max() {
  ret i64 2147483647
}

; CHECK-LABEL: .method {{.*}}'i64_i32_max_plus_1'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i8 2147483648{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i64_i32_max_plus_1'{{$}}
define i64 @i64_i32_max_plus_1() {
  ret i64 2147483648
}

; CHECK-LABEL: .method {{.*}}'i64_i32_min'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 -2147483648{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i64_i32_min'{{$}}
define i64 @i64_i32_min() {
  ret i64 -2147483648
}

; CHECK-LABEL: .method {{.*}}'i64_i32_min_minus_1'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i8 -2147483649{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i64_i32_min_minus_1'{{$}}
define i64 @i64_i32_min_minus_1() {
  ret i64 -2147483649
}

; CHECK-LABEL: .method {{.*}}'i64_u32_max'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i8 4294967295{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i64_u32_max'{{$}}
define i64 @i64_u32_max() {
  ret i64 4294967295
}

; CHECK-LABEL: .method {{.*}}'i64_2_pow_32'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i8 4294967296{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i64_2_pow_32'{{$}}
define i64 @i64_2_pow_32() {
  ret i64 4294967296
}

; CHECK-LABEL: .method {{.*}}'i64_max'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i8 9223372036854775807{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i64_max'{{$}}
define i64 @i64_max() {
  ret i64 9223372036854775807
}

; CHECK-LABEL: .method {{.*}}'i64_min'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i8 -9223372036854775808{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'i64_min'{{$}}
define i64 @i64_min() {
  ret i64 -9223372036854775808
}

; A pointer constant is a native int like an i64 one.
; CHECK-LABEL: .method {{.*}}'ptr_const'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i8 81985529216486895{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @ptr_const() {
  ret ptr inttoptr (i64 81985529216486895 to ptr)
}

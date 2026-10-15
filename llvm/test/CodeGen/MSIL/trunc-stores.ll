; Truncating stores (ImplementationPlan.md M7.a and M7.b; plan 5.4 'Memory';
; recon isel.md 5.5): an int32 stored as i8 or i16 is a single stind.i1 or
; stind.i2, with no mask in front of it. Truncating stores from i64 are Expand
; (M7.b): the native int value is narrowed with conv.i4 and stored with the
; int32 store (stind.i1, stind.i2 or stind.i4), so a native int is never the
; operand of stind.i1/i2/i4. A double rounded to float and stored is conv.r4
; followed by stind.r4. The low half of an i128 is a native int, so its
; truncation to i64 is a plain stind.i of that half. Each case is checked as
; the whole method body.
;
; Every function is one store and its truncation, so the CIL is the same at
; -O0 and -O2.
;
; half and bfloat (half-trunc.ll): truncating stores from f32 and f64 to f16
; and bf16 are Expand (M7.b), so a float or double rounded to half or bfloat
; and stored is the generic conversion followed by a 16-bit integer store
; (load-store.ll copies half and bfloat with stind.i2). The conversion is a
; compiler-rt call (__truncsfhf2, __truncdfhf2, __truncsfbf2, __truncdfbf2),
; and a library call whose name starts with '__' is an error until phase 3
; (decision D14), so each function gets exactly that one diagnostic, never a
; 'Cannot select' of an unselectable truncating store or a crash; llc exits
; with 1 and the IL on stdout still assembles.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=conv.i8 \
; DEFINE:   --implicit-check-not=conv.u8 --implicit-check-not=stind.i8 --implicit-check-not=localloc
; RUN: split-file %s %t
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/stores.ll \
; RUN:   | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/stores.ll \
; RUN:   | %{check}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/stores.ll \
; RUN:   | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/stores.ll \
; RUN:   | %{check}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %t/stores.ll -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/stores.ll -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

; DEFINE: %{diag} = FileCheck %s --check-prefix=ERR --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/half-trunc.ll -o /dev/null \
; RUN:   2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/half-trunc.ll -o /dev/null \
; RUN:   2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/half-trunc.ll -o /dev/null \
; RUN:   2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/half-trunc.ll -o /dev/null \
; RUN:   2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/half-trunc.ll > %t.half.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.half.dll %t.half.il %}

;--- stores.ll
source_filename = "trunc-stores.c"

; ---- From int32 ----

; CHECK-LABEL: .method {{.*}}'trunc_i32_to_i8'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'trunc_i32_to_i8'{{$}}
define void @trunc_i32_to_i8(ptr %p, i32 %v) {
  %t = trunc i32 %v to i8
  store i8 %t, ptr %p
  ret void
}

; CHECK-LABEL: .method {{.*}}'trunc_i32_to_i16'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @trunc_i32_to_i16(ptr %p, i32 %v) {
  %t = trunc i32 %v to i16
  store i16 %t, ptr %p
  ret void
}

; An int32 constant stored as a byte.
; CHECK-LABEL: .method {{.*}}'store_byte_const'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 42{{$}}
; CHECK-NEXT:  {{^}}  stind.i1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_byte_const(ptr %p) {
  store i8 42, ptr %p
  ret void
}

; The result of int32 arithmetic stored as i16.
; CHECK-LABEL: .method {{.*}}'store_sum_i16'(
; CHECK-SAME:  native int, int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  stind.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_sum_i16(ptr %p, i16 %a, i16 %b) {
  %s = add i16 %a, %b
  store i16 %s, ptr %p
  ret void
}

; ---- From native int: conv.i4, then the int32 store ----

; CHECK-LABEL: .method {{.*}}'trunc_i64_to_i8'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  stind.i1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'trunc_i64_to_i8'{{$}}
define void @trunc_i64_to_i8(ptr %p, i64 %v) {
  %t = trunc i64 %v to i8
  store i8 %t, ptr %p
  ret void
}

; CHECK-LABEL: .method {{.*}}'trunc_i64_to_i16'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  stind.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @trunc_i64_to_i16(ptr %p, i64 %v) {
  %t = trunc i64 %v to i16
  store i16 %t, ptr %p
  ret void
}

; CHECK-LABEL: .method {{.*}}'trunc_i64_to_i32'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @trunc_i64_to_i32(ptr %p, i64 %v) {
  %t = trunc i64 %v to i32
  store i32 %t, ptr %p
  ret void
}

; A pointer converted to a 32-bit integer and stored ((int)(intptr_t)p).
; CHECK-LABEL: .method {{.*}}'store_ptr_as_i32'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_ptr_as_i32(ptr %p, ptr %q) {
  %i = ptrtoint ptr %q to i32
  store i32 %i, ptr %p
  ret void
}

; ---- double to float ----

; CHECK-LABEL: .method {{.*}}'fptrunc_f64_to_f32'(
; CHECK-SAME:  native int, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  conv.r4{{$}}
; CHECK-NEXT:  {{^}}  stind.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fptrunc_f64_to_f32'{{$}}
define void @fptrunc_f64_to_f32(ptr %p, double %v) {
  %t = fptrunc double %v to float
  store float %t, ptr %p
  ret void
}

; ---- i128 ----

; The low half of an i128 (its first native int parameter) stored as i64.
; CHECK-LABEL: .method {{.*}}'trunc_i128_to_i64'(
; CHECK-SAME:  native int, native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @trunc_i128_to_i64(ptr %p, i128 %v) {
  %t = trunc i128 %v to i64
  store i64 %t, ptr %p
  ret void
}

; ... and stored as i32.
; CHECK-LABEL: .method {{.*}}'trunc_i128_to_i32'(
; CHECK-SAME:  native int, native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @trunc_i128_to_i32(ptr %p, i128 %v) {
  %t = trunc i128 %v to i32
  store i32 %t, ptr %p
  ret void
}

;--- half-trunc.ll
source_filename = "trunc-stores-half.c"

; ERR: error: {{.*}}in function fptrunc_f32_to_half void (ptr, float): runtime helper '__truncsfhf2' is not available for MSIL yet (phase 3){{$}}
define void @fptrunc_f32_to_half(ptr %p, float %v) {
  %t = fptrunc float %v to half
  store half %t, ptr %p
  ret void
}

; ERR: error: {{.*}}in function fptrunc_f64_to_half void (ptr, double): runtime helper '__truncdfhf2' is not available for MSIL yet (phase 3){{$}}
define void @fptrunc_f64_to_half(ptr %p, double %v) {
  %t = fptrunc double %v to half
  store half %t, ptr %p
  ret void
}

; ERR: error: {{.*}}in function fptrunc_f32_to_bfloat void (ptr, float): runtime helper '__truncsfbf2' is not available for MSIL yet (phase 3){{$}}
define void @fptrunc_f32_to_bfloat(ptr %p, float %v) {
  %t = fptrunc float %v to bfloat
  store bfloat %t, ptr %p
  ret void
}

; ERR: error: {{.*}}in function fptrunc_f64_to_bfloat void (ptr, double): runtime helper '__truncdfbf2' is not available for MSIL yet (phase 3){{$}}
define void @fptrunc_f64_to_bfloat(ptr %p, double %v) {
  %t = fptrunc double %v to bfloat
  store bfloat %t, ptr %p
  ret void
}

; Extending loads (ImplementationPlan.md M7.a and M7.b; plan 5.4 'Memory';
; recon isel.md 5.5): an i8 or i16 load that is sign- or zero-extended to i32
; is a single extending load, ldind.i1/ldind.i2 or ldind.u1/ldind.u2, with no
; conversion after it. Extending loads to i64 are Expand (M7.b): the value is
; loaded as an int32 (ldind.i1/i2/u1/u2, or ldind.i4 for an i32 source) and
; widened to native int with conv.i (sign) or conv.u (zero), so int64 never
; appears (decision D2). A float loaded and extended to double is Expand too:
; ldind.r4, then conv.r8. Each case is checked as the whole method body, so a
; missing, doubled or wrong conversion fails.
;
; Every function is one load and its extension, so the CIL is the same at -O0
; and -O2.
;
; half and bfloat (half-ext.ll): extending loads from f16 and bf16 to f32 and
; f64 are Expand (M7.b), so a loaded half or bfloat that is extended is the
; 16-bit integer load (load-store.ll copies them with ldind.u2) followed by the
; generic conversion, which is a compiler-rt call: __extendhfsf2 for half,
; __extendbfsf2 for bfloat, also on the way to double. A library call whose
; name starts with '__' is an error until phase 3 (decision D14), so each
; function gets exactly that one diagnostic, never a 'Cannot select' of an
; unselectable extending load or a crash; llc exits with 1 and the IL on stdout
; still assembles.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=conv.i8 \
; DEFINE:   --implicit-check-not=conv.u8 --implicit-check-not=ldind.i8 --implicit-check-not=ldind.u4 \
; DEFINE:   --implicit-check-not=localloc
; RUN: split-file %s %t
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/loads.ll \
; RUN:   | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/loads.ll \
; RUN:   | %{check}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/loads.ll \
; RUN:   | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/loads.ll \
; RUN:   | %{check}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %t/loads.ll -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/loads.ll -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

; DEFINE: %{diag} = FileCheck %s --check-prefix=ERR --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/half-ext.ll -o /dev/null 2>&1 \
; RUN:   | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/half-ext.ll -o /dev/null 2>&1 \
; RUN:   | %{diag}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/half-ext.ll -o /dev/null \
; RUN:   2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/half-ext.ll -o /dev/null \
; RUN:   2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/half-ext.ll > %t.half.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.half.dll %t.half.il %}

;--- loads.ll
source_filename = "ext-loads.c"

; ---- To int32: one extending load ----

; CHECK-LABEL: .method {{.*}}'sext_i8_to_i32'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_i8_to_i32'{{$}}
define i32 @sext_i8_to_i32(ptr %p) {
  %v = load i8, ptr %p
  %e = sext i8 %v to i32
  ret i32 %e
}

; CHECK-LABEL: .method {{.*}}'zext_i8_to_i32'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.u1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @zext_i8_to_i32(ptr %p) {
  %v = load i8, ptr %p
  %e = zext i8 %v to i32
  ret i32 %e
}

; CHECK-LABEL: .method {{.*}}'sext_i16_to_i32'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @sext_i16_to_i32(ptr %p) {
  %v = load i16, ptr %p
  %e = sext i16 %v to i32
  ret i32 %e
}

; CHECK-LABEL: .method {{.*}}'zext_i16_to_i32'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.u2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @zext_i16_to_i32(ptr %p) {
  %v = load i16, ptr %p
  %e = zext i16 %v to i32
  ret i32 %e
}

; A boolean byte (clang's _Bool in memory, known to be 0 or 1 by !range)
; zero-extended to int.
; CHECK-LABEL: .method {{.*}}'zext_bool_to_i32'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.u1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @zext_bool_to_i32(ptr %p) {
  %v = load i8, ptr %p, !range !0, !noundef !1
  %e = zext i8 %v to i32
  ret i32 %e
}

; The extension is folded into the load even when the loaded value has a
; further use as an int32 (both uses read the extended value).
; CHECK-LABEL: .method {{.*}}'sext_i8_used_twice'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK:       {{^}}  ldind.i1{{$}}
; CHECK-NOT:   ldind
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_i8_used_twice'{{$}}
define i32 @sext_i8_used_twice(ptr %p) {
  %v = load i8, ptr %p
  %e = sext i8 %v to i32
  %m = mul i32 %e, %e
  ret i32 %m
}

; ---- To native int: an int32 load, then conv.i or conv.u ----

; CHECK-LABEL: .method {{.*}}'sext_i8_to_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i1{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sext_i8_to_i64'{{$}}
define i64 @sext_i8_to_i64(ptr %p) {
  %v = load i8, ptr %p
  %e = sext i8 %v to i64
  ret i64 %e
}

; CHECK-LABEL: .method {{.*}}'zext_i8_to_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.u1{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @zext_i8_to_i64(ptr %p) {
  %v = load i8, ptr %p
  %e = zext i8 %v to i64
  ret i64 %e
}

; CHECK-LABEL: .method {{.*}}'sext_i16_to_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i2{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @sext_i16_to_i64(ptr %p) {
  %v = load i16, ptr %p
  %e = sext i16 %v to i64
  ret i64 %e
}

; CHECK-LABEL: .method {{.*}}'zext_i16_to_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.u2{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @zext_i16_to_i64(ptr %p) {
  %v = load i16, ptr %p
  %e = zext i16 %v to i64
  ret i64 %e
}

; CHECK-LABEL: .method {{.*}}'sext_i32_to_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @sext_i32_to_i64(ptr %p) {
  %v = load i32, ptr %p
  %e = sext i32 %v to i64
  ret i64 %e
}

; CHECK-LABEL: .method {{.*}}'zext_i32_to_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @zext_i32_to_i64(ptr %p) {
  %v = load i32, ptr %p
  %e = zext i32 %v to i64
  ret i64 %e
}

; An unsigned int32 loaded into a native int index (a[(unsigned)i] in C).
; CHECK-LABEL: .method {{.*}}'zext_index'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK:       {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK:       {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.u1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define zeroext i8 @zext_index(ptr %base, ptr %pi) {
  %i = load i32, ptr %pi
  %w = zext i32 %i to i64
  %a = getelementptr inbounds i8, ptr %base, i64 %w
  %v = load i8, ptr %a
  ret i8 %v
}

; ---- float to double ----

; CHECK-LABEL: .method {{.*}}'fpext_f32_to_f64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.r4{{$}}
; CHECK-NEXT:  {{^}}  conv.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fpext_f32_to_f64'{{$}}
define double @fpext_f32_to_f64(ptr %p) {
  %v = load float, ptr %p
  %e = fpext float %v to double
  ret double %e
}

!0 = !{i8 0, i8 2}
!1 = !{}

;--- half-ext.ll
source_filename = "ext-loads-half.c"

; ERR: error: {{.*}}in function fpext_half_to_f32 float (ptr): runtime helper '__extendhfsf2' is not available for MSIL yet (phase 3){{$}}
define float @fpext_half_to_f32(ptr %p) {
  %v = load half, ptr %p
  %e = fpext half %v to float
  ret float %e
}

; ERR: error: {{.*}}in function fpext_half_to_f64 double (ptr): runtime helper '__extendhfsf2' is not available for MSIL yet (phase 3){{$}}
define double @fpext_half_to_f64(ptr %p) {
  %v = load half, ptr %p
  %e = fpext half %v to double
  ret double %e
}

; ERR: error: {{.*}}in function fpext_bfloat_to_f32 float (ptr): runtime helper '__extendbfsf2' is not available for MSIL yet (phase 3){{$}}
define float @fpext_bfloat_to_f32(ptr %p) {
  %v = load bfloat, ptr %p
  %e = fpext bfloat %v to float
  ret float %e
}

; ERR: error: {{.*}}in function fpext_bfloat_to_f64 double (ptr): runtime helper '__extendbfsf2' is not available for MSIL yet (phase 3){{$}}
define double @fpext_bfloat_to_f64(ptr %p) {
  %v = load bfloat, ptr %p
  %e = fpext bfloat %v to double
  ret double %e
}

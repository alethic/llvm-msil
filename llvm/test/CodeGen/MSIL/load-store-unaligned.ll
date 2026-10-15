; Misaligned loads and stores (ImplementationPlan.md M7.a and decision D24; plan
; 5.1 'Memory prefixes ... on their own lines before the opcode'; recon isel.md
; 2.14 and 5.5; ECMA-335 III.2.5): allowsMisalignedMemoryAccesses is true, so
; a load or store whose alignment is below its access size stays one access,
; and MSILSetMemFlags gives it the prefix 'unaligned. N', where N is the
; largest of 1, 2 and 4 that is not above the alignment. The prefix is printed
; on its own line right before the opcode, after the address and the value
; have been pushed. An access aligned to its size or more has no prefix, and
; 1-byte accesses never have one. Extending loads and truncating stores
; (including the legalizer's expansions to an int32 access plus a conversion,
; ext-loads.ll and trunc-stores.ll) keep the alignment of their memory operand,
; and so do both halves of an i128. The memflags immediate is the first
; operand of every LDIND/STIND record (plan 5.4), which the MIR lines check
; for one load and one store.
;
; Each function is one access with argument operands, so the CIL is the same at
; -O0 and -O2.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=volatile. --implicit-check-not=localloc \
; DEFINE:   --implicit-check-not=ldind.i8 --implicit-check-not=stind.i8
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -stop-after=msil-set-mem-flags < %s \
; RUN:   | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs -stop-after=msil-set-mem-flags < %s \
; RUN:   | FileCheck %s --check-prefix=MIR
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "load-store-unaligned.c"

; The memflags immediate (bits 0-2: N of 'unaligned. N'; bit 3: volatile) is
; operand 1 of the load and of the store, before the address.
; MIR-LABEL: name: load_i32_a1{{$}}
; MIR:       %{{[0-9]+}}:i32 = LDIND_I4 1, %{{[0-9]+}},
; MIR-LABEL: name: load_i32_a2{{$}}
; MIR:       %{{[0-9]+}}:i32 = LDIND_I4 2, %{{[0-9]+}},
; MIR-LABEL: name: load_i32_a4{{$}}
; MIR:       %{{[0-9]+}}:i32 = LDIND_I4 0, %{{[0-9]+}},
; MIR-LABEL: name: store_i32_a1{{$}}
; MIR:       STIND_I4 1, %{{[0-9]+}}, %{{[0-9]+}},
; MIR-LABEL: name: load_i64_a4{{$}}
; MIR:       %{{[0-9]+}}:i64 = LDIND_I 4, %{{[0-9]+}},
; MIR-LABEL: name: store_i64_a2{{$}}
; MIR:       STIND_I 2, %{{[0-9]+}}, %{{[0-9]+}},
; MIR-LABEL: name: store_f64_a8{{$}}
; MIR:       STIND_R8 0, %{{[0-9]+}}, %{{[0-9]+}},

; ---- int32 ----

; CHECK-LABEL: .method {{.*}}'load_i32_a1'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 1{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'load_i32_a1'{{$}}
define i32 @load_i32_a1(ptr %p) {
  %v = load i32, ptr %p, align 1
  ret i32 %v
}

; CHECK-LABEL: .method {{.*}}'load_i32_a2'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 2{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @load_i32_a2(ptr %p) {
  %v = load i32, ptr %p, align 2
  ret i32 %v
}

; CHECK-LABEL: .method {{.*}}'load_i32_a4'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @load_i32_a4(ptr %p) {
  %v = load i32, ptr %p, align 4
  ret i32 %v
}

; Over-aligned: no prefix.
; CHECK-LABEL: .method {{.*}}'load_i32_a16'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @load_i32_a16(ptr %p) {
  %v = load i32, ptr %p, align 16
  ret i32 %v
}

; CHECK-LABEL: .method {{.*}}'store_i32_a1'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 1{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'store_i32_a1'{{$}}
define void @store_i32_a1(ptr %p, i32 %v) {
  store i32 %v, ptr %p, align 1
  ret void
}

; CHECK-LABEL: .method {{.*}}'store_i32_a2'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 2{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_i32_a2(ptr %p, i32 %v) {
  store i32 %v, ptr %p, align 2
  ret void
}

; CHECK-LABEL: .method {{.*}}'store_i32_a4'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_i32_a4(ptr %p, i32 %v) {
  store i32 %v, ptr %p, align 4
  ret void
}

; ---- native int (i64 and pointers): 'unaligned. 4' is the largest prefix ----

; CHECK-LABEL: .method {{.*}}'load_i64_a1'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 1{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @load_i64_a1(ptr %p) {
  %v = load i64, ptr %p, align 1
  ret i64 %v
}

; CHECK-LABEL: .method {{.*}}'load_i64_a2'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 2{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @load_i64_a2(ptr %p) {
  %v = load i64, ptr %p, align 2
  ret i64 %v
}

; CHECK-LABEL: .method {{.*}}'load_i64_a4'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 4{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @load_i64_a4(ptr %p) {
  %v = load i64, ptr %p, align 4
  ret i64 %v
}

; CHECK-LABEL: .method {{.*}}'load_i64_a8'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @load_i64_a8(ptr %p) {
  %v = load i64, ptr %p, align 8
  ret i64 %v
}

; CHECK-LABEL: .method {{.*}}'load_ptr_a4'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 4{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @load_ptr_a4(ptr %p) {
  %v = load ptr, ptr %p, align 4
  ret ptr %v
}

; CHECK-LABEL: .method {{.*}}'store_i64_a2'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 2{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_i64_a2(ptr %p, i64 %v) {
  store i64 %v, ptr %p, align 2
  ret void
}

; CHECK-LABEL: .method {{.*}}'store_ptr_a4'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 4{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_ptr_a4(ptr %p, ptr %v) {
  store ptr %v, ptr %p, align 4
  ret void
}

; ---- Floating point ----

; CHECK-LABEL: .method {{.*}}'load_f32_a1'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 1{{$}}
; CHECK-NEXT:  {{^}}  ldind.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define float @load_f32_a1(ptr %p) {
  %v = load float, ptr %p, align 1
  ret float %v
}

; CHECK-LABEL: .method {{.*}}'load_f64_a4'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 4{{$}}
; CHECK-NEXT:  {{^}}  ldind.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @load_f64_a4(ptr %p) {
  %v = load double, ptr %p, align 4
  ret double %v
}

; CHECK-LABEL: .method {{.*}}'store_f32_a2'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 2{{$}}
; CHECK-NEXT:  {{^}}  stind.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_f32_a2(ptr %p, float %v) {
  store float %v, ptr %p, align 2
  ret void
}

; CHECK-LABEL: .method {{.*}}'store_f64_a1'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 1{{$}}
; CHECK-NEXT:  {{^}}  stind.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_f64_a1(ptr %p, double %v) {
  store double %v, ptr %p, align 1
  ret void
}

; CHECK-LABEL: .method {{.*}}'store_f64_a8'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  stind.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_f64_a8(ptr %p, double %v) {
  store double %v, ptr %p, align 8
  ret void
}

; ---- Narrow accesses ----

; A byte access is always aligned.
; CHECK-LABEL: .method {{.*}}'load_u8_a1'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.u1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define zeroext i8 @load_u8_a1(ptr %p) {
  %v = load i8, ptr %p, align 1
  ret i8 %v
}

; CHECK-LABEL: .method {{.*}}'store_i8_a1'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_i8_a1(ptr %p, i8 %v) {
  store i8 %v, ptr %p, align 1
  ret void
}

; CHECK-LABEL: .method {{.*}}'load_s16_a1'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 1{{$}}
; CHECK-NEXT:  {{^}}  ldind.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define signext i16 @load_s16_a1(ptr %p) {
  %v = load i16, ptr %p, align 1
  ret i16 %v
}

; CHECK-LABEL: .method {{.*}}'load_u16_a1'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 1{{$}}
; CHECK-NEXT:  {{^}}  ldind.u2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define zeroext i16 @load_u16_a1(ptr %p) {
  %v = load i16, ptr %p, align 1
  ret i16 %v
}

; CHECK-LABEL: .method {{.*}}'load_u16_a2'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.u2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define zeroext i16 @load_u16_a2(ptr %p) {
  %v = load i16, ptr %p, align 2
  ret i16 %v
}

; A truncating store keeps the alignment of the narrow access.
; CHECK-LABEL: .method {{.*}}'store_i16_a1'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 1{{$}}
; CHECK-NEXT:  {{^}}  stind.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_i16_a1(ptr %p, i32 %v) {
  %t = trunc i32 %v to i16
  store i16 %t, ptr %p, align 1
  ret void
}

; A truncating store from native int is conv.i4 and the int32 store, which
; keeps the prefix.
; CHECK-LABEL: .method {{.*}}'store_i32_from_i64_a2'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  conv.i4{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 2{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @store_i32_from_i64_a2(ptr %p, i64 %v) {
  %t = trunc i64 %v to i32
  store i32 %t, ptr %p, align 2
  ret void
}

; An extending load to native int is the int32 load, which keeps the prefix,
; and conv.i.
; CHECK-LABEL: .method {{.*}}'load_s32_to_i64_a1'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 1{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @load_s32_to_i64_a1(ptr %p) {
  %v = load i32, ptr %p, align 1
  %e = sext i32 %v to i64
  ret i64 %e
}

; float -> double extending load: ldind.r4 with the prefix, then conv.r8.
; CHECK-LABEL: .method {{.*}}'load_f32_to_f64_a2'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 2{{$}}
; CHECK-NEXT:  {{^}}  ldind.r4{{$}}
; CHECK-NEXT:  {{^}}  conv.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @load_f32_to_f64_a2(ptr %p) {
  %v = load float, ptr %p, align 2
  %e = fpext float %v to double
  ret double %e
}

; ---- Offsets: the alignment of the access, not of the base ----

; A 2-aligned int32 at a constant offset.
; CHECK-LABEL: .method {{.*}}'load_i32_off2_a2'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.2|ldc\.i4 2}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 2{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @load_i32_off2_a2(ptr %p) {
  %q = getelementptr inbounds i8, ptr %p, i64 2
  %v = load i32, ptr %q, align 2
  ret i32 %v
}

; A field of a packed struct ({ i8, i32 } packed: the i32 at +1, align 1).
; CHECK-LABEL: .method {{.*}}'packed_field'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.1|ldc\.i4 1}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 1{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @packed_field(ptr %s) {
  %f = getelementptr inbounds <{ i8, i32 }>, ptr %s, i64 0, i32 1
  %v = load i32, ptr %f, align 1
  ret i32 %v
}

; ---- i128: both halves carry the alignment of the access ----

; CHECK-LABEL: .method {{.*}}'load_i128_lo_a4'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 4{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @load_i128_lo_a4(ptr %p) {
  %v = load i128, ptr %p, align 4
  %lo = trunc i128 %v to i64
  ret i64 %lo
}

; CHECK-LABEL: .method {{.*}}'store_i128_a2'(
; CHECK-DAG:   {{^  ldarg\.0[[:space:]]+ldarg\.1[[:space:]]+unaligned\. 2[[:space:]]+stind\.i$}}
; CHECK-DAG:   {{^  ldarg\.0[[:space:]]+(ldc\.i4\.8|ldc\.i4 8)[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+ldarg\.2[[:space:]]+unaligned\. 2[[:space:]]+stind\.i$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'store_i128_a2'{{$}}
define void @store_i128_a2(ptr %p, i128 %v) {
  store i128 %v, ptr %p, align 2
  ret void
}

; An 8-aligned i128 has no prefix on either half.
; CHECK-LABEL: .method {{.*}}'store_i128_a8'(
; CHECK-DAG:   {{^  ldarg\.0[[:space:]]+ldarg\.1[[:space:]]+stind\.i$}}
; CHECK-DAG:   {{^  ldarg\.0[[:space:]]+(ldc\.i4\.8|ldc\.i4 8)[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+ldarg\.2[[:space:]]+stind\.i$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'store_i128_a8'{{$}}
define void @store_i128_a8(ptr %p, i128 %v) {
  store i128 %v, ptr %p, align 8
  ret void
}

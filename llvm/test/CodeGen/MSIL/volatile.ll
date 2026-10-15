; Volatile memory accesses (ImplementationPlan.md M7.a and decision D24; plan
; 5.1 'Memory prefixes'; recon isel.md 2.14 and 5.5; ECMA-335 III.2.6): a
; volatile load or store carries the 'volatile.' prefix (bit 3 of the memflags
; immediate), printed on its own line right before the opcode. When the access
; is also misaligned, 'unaligned. N' comes first, then 'volatile.' (both
; prefixes may precede ldind, stind, cpblk and initblk). Volatile accesses are
; never removed, merged or reordered: two volatile loads of one address are
; two ldind, a dead volatile load is still executed (its value is popped), two
; volatile stores to one address both happen, and RegStackify keeps them in
; program order. The volatile bit also applies to memcpy and memset when their
; isvolatile flag is set (cpblk and initblk, which accept both prefixes), while
; a volatile memmove is still the Buffer::MemoryCopy call (decision D15),
; which has no prefix. The x86_64-pc-windows-msvc-msil triple has no
; /volatile:ms semantics in llc: volatile accesses are plain 'load volatile' /
; 'store volatile' there too (plan section 12, row 'M1.c amended').
;
; Every function is straight-line code on arguments, and the CIL is the same
; at -O0 and -O2 (CHECK) except for the memset: its int32 length is computed
; before its fill value, and at -O0 RegStackify only stackifies a def that sits
; right before its use (recon passes-frame.md 5.1), so the constant length
; waits in an int32 local at -O0 (O0) and is pushed right before initblk at
; -O2 (O2). No '.locals' is 'init'.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=localloc --implicit-check-not=ldind.i8 \
; DEFINE:   --implicit-check-not=stind.i8 --implicit-check-not=Interlocked \
; DEFINE:   --implicit-check-not=".locals init"
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -stop-after=msil-set-mem-flags < %s \
; RUN:   | FileCheck %s --check-prefix=MIR
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "volatile.c"

declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)
declare void @llvm.memmove.p0.p0.i64(ptr, ptr, i64, i1)

; memflags: 8 is volatile, 8 + N is volatile and 'unaligned. N'.
; MIR-LABEL: name: vload_i32{{$}}
; MIR:       %{{[0-9]+}}:i32 = LDIND_I4 8, %{{[0-9]+}},
; MIR-LABEL: name: vstore_i32{{$}}
; MIR:       STIND_I4 8, %{{[0-9]+}}, %{{[0-9]+}},
; MIR-LABEL: name: vload_i32_a1{{$}}
; MIR:       %{{[0-9]+}}:i32 = LDIND_I4 9, %{{[0-9]+}},
; MIR-LABEL: name: vstore_i64_a4{{$}}
; MIR:       STIND_I 12, %{{[0-9]+}}, %{{[0-9]+}},

; ---- Loads and stores of every width ----

; CHECK-LABEL: .method {{.*}}'vload_i32'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'vload_i32'{{$}}
define i32 @vload_i32(ptr %p) {
  %v = load volatile i32, ptr %p
  ret i32 %v
}

; CHECK-LABEL: .method {{.*}}'vload_i64'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @vload_i64(ptr %p) {
  %v = load volatile i64, ptr %p
  ret i64 %v
}

; CHECK-LABEL: .method {{.*}}'vload_ptr'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @vload_ptr(ptr %p) {
  %v = load volatile ptr, ptr %p
  ret ptr %v
}

; CHECK-LABEL: .method {{.*}}'vload_f32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  ldind.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define float @vload_f32(ptr %p) {
  %v = load volatile float, ptr %p
  ret float %v
}

; CHECK-LABEL: .method {{.*}}'vload_f64'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  ldind.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @vload_f64(ptr %p) {
  %v = load volatile double, ptr %p
  ret double %v
}

; CHECK-LABEL: .method {{.*}}'vload_u8'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  ldind.u1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define zeroext i8 @vload_u8(ptr %p) {
  %v = load volatile i8, ptr %p
  ret i8 %v
}

; CHECK-LABEL: .method {{.*}}'vload_s16'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  ldind.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define signext i16 @vload_s16(ptr %p) {
  %v = load volatile i16, ptr %p
  ret i16 %v
}

; CHECK-LABEL: .method {{.*}}'vstore_i32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'vstore_i32'{{$}}
define void @vstore_i32(ptr %p, i32 %v) {
  store volatile i32 %v, ptr %p
  ret void
}

; CHECK-LABEL: .method {{.*}}'vstore_i64'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @vstore_i64(ptr %p, i64 %v) {
  store volatile i64 %v, ptr %p
  ret void
}

; CHECK-LABEL: .method {{.*}}'vstore_f32'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  stind.r4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @vstore_f32(ptr %p, float %v) {
  store volatile float %v, ptr %p
  ret void
}

; CHECK-LABEL: .method {{.*}}'vstore_f64'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  stind.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @vstore_f64(ptr %p, double %v) {
  store volatile double %v, ptr %p
  ret void
}

; CHECK-LABEL: .method {{.*}}'vstore_i8'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  stind.i1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @vstore_i8(ptr %p, i8 %v) {
  store volatile i8 %v, ptr %p
  ret void
}

; CHECK-LABEL: .method {{.*}}'vstore_i16'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  stind.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @vstore_i16(ptr %p, i16 %v) {
  store volatile i16 %v, ptr %p
  ret void
}

; ---- Both prefixes: 'unaligned. N' first, then 'volatile.' ----

; CHECK-LABEL: .method {{.*}}'vload_i32_a1'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 1{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @vload_i32_a1(ptr %p) {
  %v = load volatile i32, ptr %p, align 1
  ret i32 %v
}

; CHECK-LABEL: .method {{.*}}'vload_f64_a2'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 2{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  ldind.r8{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @vload_f64_a2(ptr %p) {
  %v = load volatile double, ptr %p, align 2
  ret double %v
}

; CHECK-LABEL: .method {{.*}}'vstore_i64_a4'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 4{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @vstore_i64_a4(ptr %p, i64 %v) {
  store volatile i64 %v, ptr %p, align 4
  ret void
}

; CHECK-LABEL: .method {{.*}}'vstore_i16_a1'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 1{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  stind.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @vstore_i16_a1(ptr %p, i16 %v) {
  store volatile i16 %v, ptr %p, align 1
  ret void
}

; ---- Volatile accesses are kept and stay in order ----

; Two volatile loads of one address are two loads, in program order.
; CHECK-LABEL: .method {{.*}}'vload_twice'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @vload_twice(ptr %p) {
  %a = load volatile i32, ptr %p
  %b = load volatile i32, ptr %p
  %d = sub i32 %a, %b
  ret i32 %d
}

; A volatile load whose value is unused still reads memory; the value is
; popped.
; CHECK-LABEL: .method {{.*}}'vload_dead'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @vload_dead(ptr %p) {
  %v = load volatile i32, ptr %p
  ret void
}

; Two volatile stores to one address both happen, in order.
; CHECK-LABEL: .method {{.*}}'vstore_twice'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.2{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @vstore_twice(ptr %p) {
  store volatile i32 1, ptr %p
  store volatile i32 2, ptr %p
  ret void
}

; A volatile load is not moved below a later volatile store to another
; pointer: its value waits in a local.
; CHECK-LABEL: .method {{.*}}'vload_vstore'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  .locals (int32){{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @vload_vstore(ptr %p, ptr %q) {
  %v = load volatile i32, ptr %p
  store volatile i32 0, ptr %q
  ret i32 %v
}

; Only the volatile access carries the prefix.
; CHECK-LABEL: .method {{.*}}'mixed'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @mixed(ptr %dst, ptr %src) {
  %v = load volatile i32, ptr %src
  store i32 %v, ptr %dst
  ret void
}

; ---- Volatile memory intrinsics ----
; The lengths (1000 bytes) are above any inline expansion, so these are one
; cpblk or initblk each (memintrinsics.ll).

; CHECK-LABEL: .method {{.*}}'vmemcpy'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 1000{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  cpblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @vmemcpy(ptr align 8 %d, ptr align 8 %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %d, ptr align 8 %s, i64 1000, i1 true)
  ret void
}

; CHECK-LABEL: .method {{.*}}'vmemcpy_a1'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 1000{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 1{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  cpblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @vmemcpy_a1(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 1 %d, ptr align 1 %s, i64 1000, i1 true)
  ret void
}

; CHECK-LABEL: .method {{.*}}'vmemset'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; O0-NEXT:     {{^}}  .locals (int32){{$}}
; O0-NEXT:     {{^}}  ldc.i4 1000{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4 1000{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @vmemset(ptr align 8 %d) {
  call void @llvm.memset.p0.i64(ptr align 8 %d, i8 0, i64 1000, i1 true)
  ret void
}

; A volatile memmove is the MemoryCopy call, without a prefix.
; CHECK-LABEL: .method {{.*}}'vmemmove'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 4{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  conv.i8{{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK-NEXT:  {{^}}  call void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @vmemmove(ptr %d, ptr %s, i64 %n) {
  call void @llvm.memmove.p0.p0.i64(ptr %d, ptr %s, i64 %n, i1 true)
  ret void
}

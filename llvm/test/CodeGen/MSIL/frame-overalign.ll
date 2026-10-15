; Over-aligned frames (ImplementationPlan.md decision D13, M7.b; recon
; passes-frame.md 6.3 and 6.5, superseded by D13 as recorded in plan section 12;
; review experiment recon/artifacts/review-cil/align16.il): the backend
; assumes that localloc returns 8-byte aligned memory
; (MSILFrameLowering::getLocallocAlign() is 8 on every Phase-1 triple), because
; a constant-size localloc of 32 bytes or less is only 8-aligned on .NET
; Framework 4.8 and in .NET 8 tiered code. A frame whose largest alignment is 8
; or less is used as localloc returns it. A frame with MaxAlign A > 8
; over-allocates A - 8 bytes and aligns the result up, all inside the
; prologue:
;   ldc.i4 <StackSize + A - 8>; localloc; stloc R
;   ldloc R; ldc.i4 A-1; conv.i; add; ldc.i4 -A; conv.i; and; stloc FP
; (native int arithmetic with native int constants, D2), and every frame
; address is relative to the aligned FP, never to R. This holds for small
; frames too: a 16-byte object aligned to 16 (an __int128 local, an
; _Alignas(16) array) gets the sequence although its frame is only 16 bytes.
; The caller of a function returning i128 keeps the result in a 16-aligned
; slot of its own frame, so it realigns as well.
;
; Every over-aligned frame below uses FP at least twice, so FP lives in a local
; and the prologue is exact at -O2 up to the 'and' (RegStackify may then tee FP
; into its first use, so the store to the FP local may be 'dup; stloc'); at
; -O0 the localloc size is exact and the rest of the sequence is checked by
; opcode. Sizes do not depend on the target's stack alignment: each frame's
; object sizes add up to a multiple of its MaxAlign.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=localloc --implicit-check-not=".locals init" \
; DEFINE:   --implicit-check-not=int64 --implicit-check-not=ldc.i8
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "frame-overalign.c"

define void @sink(ptr %p) {
  ret void
}

define void @sink2(ptr %p, ptr %q) {
  ret void
}

define i128 @rv(i128 %x) {
  ret i128 %x
}

; ---- MaxAlign 8: no realignment ----

; CHECK-LABEL: .method {{.*}}'align8'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  call void 'sink2'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'align8'{{$}}
define void @align8() {
  %x = alloca [16 x i8], align 8
  call void @sink2(ptr %x, ptr %x)
  ret void
}

; A double and an i64 (8-aligned, 16 bytes): d at FP+8, l at FP+0.
; CHECK-LABEL: .method {{.*}}'align8_scalars'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.8|ldc\.i4 8}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  call void 'sink2'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'align8_scalars'{{$}}
define void @align8_scalars() {
  %d = alloca double, align 8
  %l = alloca i64, align 8
  call void @sink2(ptr %d, ptr %l)
  ret void
}

; ---- MaxAlign 16 in frames of 32 bytes or less ----

; One 16-byte object aligned to 16: localloc 16 + 8 bytes, align up to 16.
; CHECK-LABEL: .method {{.*}}'align16_small'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  .locals ({{.*}}){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 24{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.[[R:[0-9]]]{{$}}
; O2-NEXT:     {{^}}  ldloc.[[R]]{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.s 15|ldc\.i4 15}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.s -16|ldc\.i4 -16}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  and{{$}}
; O2-NEXT:     {{^  (dup[[:space:]]+)?stloc\.}}[[F:[0-9]]]{{$}}
; O2-NEXT:     {{^}}  ldloc.[[F]]{{$}}
; O2:          {{^}}  call void 'sink2'(native int, native int){{$}}
; O0-DAG:      {{^}}  {{ldc\.i4\.s 15|ldc\.i4 15}}{{$}}
; O0-DAG:      {{^}}  {{ldc\.i4\.s -16|ldc\.i4 -16}}{{$}}
; O0-DAG:      {{^}}  add{{$}}
; O0-DAG:      {{^}}  and{{$}}
; O0:          {{^}}  call void 'sink2'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'align16_small'{{$}}
define void @align16_small() {
  %x = alloca [16 x i8], align 16
  call void @sink2(ptr %x, ptr %x)
  ret void
}

; An __int128 local (align 16 from the data layout): the two halves are
; stored through the aligned FP. FP+8 may be an 'or' rather than an 'add':
; the DAG combiner turns an add of a 16-aligned frame address and 8 into a
; disjoint or.
; CHECK-LABEL: .method {{.*}}'int128_local'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  .locals ({{.*}}){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 24{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.[[R:[0-9]]]{{$}}
; O2-NEXT:     {{^}}  ldloc.[[R]]{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.s 15|ldc\.i4 15}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.s -16|ldc\.i4 -16}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  and{{$}}
; O2-NEXT:     {{^  (dup[[:space:]]+)?stloc\.}}[[F:[0-9]]]{{$}}
; O0-DAG:      {{^}}  {{ldc\.i4\.s 15|ldc\.i4 15}}{{$}}
; O0-DAG:      {{^}}  {{ldc\.i4\.s -16|ldc\.i4 -16}}{{$}}
; O0-DAG:      {{^}}  and{{$}}
; CHECK-DAG:   {{^  ldarg\.0[[:space:]]+stind\.i$}}
; CHECK-DAG:   {{^  (ldc\.i4\.8|ldc\.i4 8)[[:space:]]+conv\.i[[:space:]]+(add|or)[[:space:]]+ldarg\.1[[:space:]]+stind\.i$}}
; CHECK:       {{^}}  call void 'sink'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @int128_local(i128 %v) {
  %x = alloca i128, align 16
  store i128 %v, ptr %x, align 16
  call void @sink(ptr %x)
  ret void
}

; An int32 (align 4) and a 16-aligned array: the int at offset -4, the array
; at -32, a 32-byte frame: localloc 40, the int at FP+28, the array at FP+0.
; CHECK-LABEL: .method {{.*}}'align16_mixed'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  .locals ({{.*}}){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 40{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.[[R:[0-9]]]{{$}}
; O2-NEXT:     {{^}}  ldloc.[[R]]{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.s 15|ldc\.i4 15}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.s -16|ldc\.i4 -16}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  and{{$}}
; O2-NEXT:     {{^  (dup[[:space:]]+)?stloc\.}}[[F:[0-9]]]{{$}}
; O0-DAG:      {{^}}  {{ldc\.i4\.s 15|ldc\.i4 15}}{{$}}
; O0-DAG:      {{^}}  {{ldc\.i4\.s -16|ldc\.i4 -16}}{{$}}
; O0-DAG:      {{^}}  and{{$}}
; O0:          {{^}}  stloc.[[F:[0-9]]]{{$}}
; CHECK:       {{^}}  {{ldc\.i4\.s 28|ldc\.i4 28}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldloc.[[F]]{{$}}
; CHECK-NEXT:  {{^}}  call void 'sink2'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @align16_mixed() {
  %i = alloca i32, align 4
  %a = alloca [16 x i8], align 16
  call void @sink2(ptr %i, ptr %a)
  ret void
}

; ---- MaxAlign 32 and 64 ----

; CHECK-LABEL: .method {{.*}}'align32'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  .locals ({{.*}}){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 56{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.[[R:[0-9]]]{{$}}
; O2-NEXT:     {{^}}  ldloc.[[R]]{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.s 31|ldc\.i4 31}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.s -32|ldc\.i4 -32}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  and{{$}}
; O2-NEXT:     {{^  (dup[[:space:]]+)?stloc\.}}[[F:[0-9]]]{{$}}
; O2-NEXT:     {{^}}  ldloc.[[F]]{{$}}
; O2:          {{^}}  call void 'sink2'(native int, native int){{$}}
; O0-DAG:      {{^}}  {{ldc\.i4\.s 31|ldc\.i4 31}}{{$}}
; O0-DAG:      {{^}}  {{ldc\.i4\.s -32|ldc\.i4 -32}}{{$}}
; O0-DAG:      {{^}}  add{{$}}
; O0-DAG:      {{^}}  and{{$}}
; O0:          {{^}}  call void 'sink2'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @align32() {
  %x = alloca [32 x i8], align 32
  call void @sink2(ptr %x, ptr %x)
  ret void
}

; An i64 and a 32-aligned array: the i64 at -8, the array at -64, a 64-byte
; frame: localloc 88, the i64 at FP+56.
; CHECK-LABEL: .method {{.*}}'align32_mixed'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  .locals ({{.*}}){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 88{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.[[R:[0-9]]]{{$}}
; O2-NEXT:     {{^}}  ldloc.[[R]]{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.s 31|ldc\.i4 31}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.s -32|ldc\.i4 -32}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  and{{$}}
; O2-NEXT:     {{^  (dup[[:space:]]+)?stloc\.}}[[F:[0-9]]]{{$}}
; O0-DAG:      {{^}}  {{ldc\.i4\.s 31|ldc\.i4 31}}{{$}}
; O0-DAG:      {{^}}  {{ldc\.i4\.s -32|ldc\.i4 -32}}{{$}}
; O0-DAG:      {{^}}  and{{$}}
; O0:          {{^}}  stloc.[[F:[0-9]]]{{$}}
; CHECK:       {{^}}  {{ldc\.i4\.s 56|ldc\.i4 56}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldloc.[[F]]{{$}}
; CHECK-NEXT:  {{^}}  call void 'sink2'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @align32_mixed() {
  %l = alloca i64, align 8
  %a = alloca [32 x i8], align 32
  call void @sink2(ptr %l, ptr %a)
  ret void
}

; An 8-byte object aligned to 64: a 64-byte frame plus 56, mask 63/-64.
; CHECK-LABEL: .method {{.*}}'align64'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  .locals ({{.*}}){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 120{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.[[R:[0-9]]]{{$}}
; O2-NEXT:     {{^}}  ldloc.[[R]]{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.s 63|ldc\.i4 63}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.s -64|ldc\.i4 -64}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  and{{$}}
; O2-NEXT:     {{^  (dup[[:space:]]+)?stloc\.}}[[F:[0-9]]]{{$}}
; O2-NEXT:     {{^}}  ldloc.[[F]]{{$}}
; O2:          {{^}}  call void 'sink2'(native int, native int){{$}}
; O0-DAG:      {{^}}  {{ldc\.i4\.s 63|ldc\.i4 63}}{{$}}
; O0-DAG:      {{^}}  {{ldc\.i4\.s -64|ldc\.i4 -64}}{{$}}
; O0-DAG:      {{^}}  add{{$}}
; O0-DAG:      {{^}}  and{{$}}
; O0:          {{^}}  call void 'sink2'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @align64() {
  %x = alloca [8 x i8], align 64
  call void @sink2(ptr %x, ptr %x)
  ret void
}

; ---- The caller of a function that returns i128 ----

; The demoted result lives in a 16-aligned, 16-byte slot at FP+0: its address
; is the hidden first argument of 'rv', and the low half is read back from it.
; CHECK-LABEL: .method {{.*}}'call_rv'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  .locals ({{.*}}){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 24{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.[[R:[0-9]]]{{$}}
; O2-NEXT:     {{^}}  ldloc.[[R]]{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.s 15|ldc\.i4 15}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.s -16|ldc\.i4 -16}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  and{{$}}
; O2-NEXT:     {{^  (dup[[:space:]]+)?stloc\.}}[[F:[0-9]]]{{$}}
; O0-DAG:      {{^}}  {{ldc\.i4\.s 15|ldc\.i4 15}}{{$}}
; O0-DAG:      {{^}}  {{ldc\.i4\.s -16|ldc\.i4 -16}}{{$}}
; O0-DAG:      {{^}}  and{{$}}
; O0:          {{^}}  stloc.[[F:[0-9]]]{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call void 'rv'(native int, native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ldloc.[[F]]{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_rv'{{$}}
define i64 @call_rv(i128 %x) {
  %r = call i128 @rv(i128 %x)
  %lo = trunc i128 %r to i64
  ret i64 %lo
}

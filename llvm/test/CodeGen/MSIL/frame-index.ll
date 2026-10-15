; The static frame and frame indices (ImplementationPlan.md decision D13, M7.b;
; Design.md 4.2; recon passes-frame.md 6.1-6.7): a function with stack
; objects allocates its whole frame with one 'localloc' at entry. The prologue
; is 'ldc.i4 <frame size>; localloc' (the size alone on the evaluation stack),
; and the result, the frame base FP, is stored to a native int local that is
; never stackified; '.locals' never has 'init' (passes-frame 6.6: C does not
; need zeroed frames). A function without stack objects has no frame. Objects
; are laid out downwards from the top of the frame in IR order (the first
; alloca at the highest address), and an object lives at FP + StackSize +
; ObjectOffset: the object at offset 0 is FP itself, any other is
; 'ldloc FP; ldc.i4 off; conv.i; add' (ADD_I64 of FP and a CONST_I64 offset,
; so no int64 appears). A frame index that is the first operand of an ADD_I64
; whose other operand is a single-use CONST_I64 (a GEP with a constant
; offset) is folded: the constant becomes off + c and FP replaces the frame
; index. A CONST_I64 that has other uses is never changed: the frame address is
; computed separately and the original add stays. Frame indices are values
; like any other: call arguments, stored values, return values, operands of
; variable additions, and the inputs of a select or a phi.
;
; Frame sizes and offsets below do not depend on the stack alignment the
; target chooses: every frame here holds objects whose sizes add up to a
; multiple of 16 bytes and whose alignments are at most 8 (alignment above 8
; is frame-overalign.ll). Straight-line functions whose frame addresses are
; direct operands compile to the same CIL at -O0 and -O2 (CHECK); the folding
; cases are exact at -O2 (O2) and checked for the folded offset at -O0 (O0).
; Branches and selects are checked for the prologue and the uses only.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=localloc --implicit-check-not=".locals init" \
; DEFINE:   --implicit-check-not=int64 --implicit-check-not=ldc.i8 --implicit-check-not="LLVM ERROR"
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

source_filename = "frame-index.c"

define void @sink(ptr %p) {
  ret void
}

define void @sink2(ptr %p, ptr %q) {
  ret void
}

define void @sink4(ptr %p, ptr %q, ptr %r, ptr %s) {
  ret void
}

; No stack objects: no localloc and no frame local.
; CHECK-LABEL: .method {{.*}}'no_frame'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'no_frame'{{$}}
define i32 @no_frame(i32 %a) {
  ret i32 %a
}

; One object at the frame base: the prologue is the first code of the method
; and FP is used as it is.
; CHECK-LABEL: .method {{.*}}'fp_only'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  call void 'sink'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fp_only'{{$}}
define void @fp_only() {
  %x = alloca [16 x i8], align 8
  call void @sink(ptr %x)
  ret void
}

; The first alloca is at the top of the frame: x at FP+8, y at FP+0.
; CHECK-LABEL: .method {{.*}}'two_slots'(
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
; CHECK-NEXT:  {{^}}} // end of method 'two_slots'{{$}}
define void @two_slots() {
  %x = alloca i64, align 8
  %y = alloca i64, align 8
  call void @sink2(ptr %x, ptr %y)
  ret void
}

; Four int32 slots: FP+12, FP+8, FP+4, FP+0.
; CHECK-LABEL: .method {{.*}}'four_slots'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 4{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.s 12|ldc\.i4 12}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.8|ldc\.i4 8}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.4|ldc\.i4 4}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  call void 'sink4'(native int, native int, native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @four_slots() {
  %a = alloca i32, align 4
  %b = alloca i32, align 4
  %c = alloca i32, align 4
  %d = alloca i32, align 4
  call void @sink4(ptr %a, ptr %b, ptr %c, ptr %d)
  ret void
}

; Mixed sizes and alignments, each object aligned: i8 at FP+15, i16 at FP+12,
; i32 at FP+8, double at FP+0.
; CHECK-LABEL: .method {{.*}}'mixed_slots'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 4{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.s 15|ldc\.i4 15}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.s 12|ldc\.i4 12}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.8|ldc\.i4 8}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  call void 'sink4'(native int, native int, native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @mixed_slots() {
  %c = alloca i8, align 1
  %s = alloca i16, align 2
  %i = alloca i32, align 4
  %d = alloca double, align 8
  call void @sink4(ptr %c, ptr %s, ptr %i, ptr %d)
  ret void
}

; Loads and stores address their slots directly: the int32 slot at FP+0 is
; written and read through FP, the other slot through FP+8.
; CHECK-LABEL: .method {{.*}}'slot_load_store'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.8|ldc\.i4 8}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  call void 'sink2'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'slot_load_store'{{$}}
define i32 @slot_load_store(i32 %v) {
  %pad = alloca [8 x i8], align 8
  %x = alloca [8 x i8], align 8
  store i32 %v, ptr %x
  call void @sink2(ptr %pad, ptr %x)
  %r = load i32, ptr %x
  ret i32 %r
}

; The address of a slot stored to memory (the value operand of stind.i).
; CHECK-LABEL: .method {{.*}}'slot_stored'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @slot_stored(ptr %p) {
  %x = alloca [16 x i8], align 8
  store ptr %x, ptr %p
  ret void
}

; The address of a slot as the return value.
; CHECK-LABEL: .method {{.*}}'return_slot'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @return_slot() {
  %x = alloca [16 x i8], align 8
  ret ptr %x
}

; A slot plus a variable index: FP+16, then the index.
; CHECK-LABEL: .method {{.*}}'slot_plus_var'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 32{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.s 16|ldc\.i4 16}}{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  call void 'sink2'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @slot_plus_var(i64 %i) {
  %x = alloca [16 x i8], align 8
  %y = alloca [16 x i8], align 8
  %p = getelementptr inbounds i8, ptr %x, i64 %i
  call void @sink2(ptr %p, ptr %y)
  ret void
}

; A frame larger than ldc.i4.s can express: 100016 bytes, the first object at
; FP+100000.
; CHECK-LABEL: .method {{.*}}'big_frame'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 100016{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 100000{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  call void 'sink2'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @big_frame() {
  %small = alloca [16 x i8], align 8
  %big = alloca [100000 x i8], align 8
  call void @sink2(ptr %small, ptr %big)
  ret void
}

; ---- Folding a GEP's constant offset into the frame offset ----

; a (FP+16) plus 8 is one constant, FP+24. The int32 arrays are only 4-byte
; aligned, so the constant offset stays an ISD::ADD (it shares bits with the
; address).
; CHECK-LABEL: .method {{.*}}'gep_fold'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  .locals (native int){{$}}
; O2-NEXT:     {{^}}  ldc.i4.s 32{{$}}
; O2-NEXT:     {{^}}  localloc{{$}}
; O2-NEXT:     {{^}}  stloc.0{{$}}
; O2-NEXT:     {{^}}  ldloc.0{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.s 24|ldc\.i4 24}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ldloc.0{{$}}
; O2-NEXT:     {{^}}  call void 'sink2'(native int, native int){{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldc.i4.s 32{{$}}
; O0-NEXT:     {{^}}  localloc{{$}}
; O0:          {{^}}  {{ldc\.i4\.s 24|ldc\.i4 24}}{{$}}
; O0-NOT:      {{ldc\.i4\.s 16|ldc\.i4 16|ldc\.i4\.8|ldc\.i4 8}}{{$}}
; O0:          {{^}}  call void 'sink2'(native int, native int){{$}}
define void @gep_fold() {
  %a = alloca [4 x i32], align 4
  %b = alloca [4 x i32], align 4
  %p = getelementptr inbounds i8, ptr %a, i64 8
  call void @sink2(ptr %p, ptr %b)
  ret void
}

; b is at FP+0, so b plus 4 is FP+4.
; CHECK-LABEL: .method {{.*}}'gep_fold_fp0'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  .locals (native int){{$}}
; O2-NEXT:     {{^}}  ldc.i4.s 32{{$}}
; O2-NEXT:     {{^}}  localloc{{$}}
; O2-NEXT:     {{^}}  stloc.0{{$}}
; O2-NEXT:     {{^}}  ldloc.0{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.s 16|ldc\.i4 16}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ldloc.0{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.4|ldc\.i4 4}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  call void 'sink2'(native int, native int){{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldc.i4.s 32{{$}}
; O0-NEXT:     {{^}}  localloc{{$}}
; O0:          {{^}}  {{ldc\.i4\.4|ldc\.i4 4}}{{$}}
; O0:          {{^}}  call void 'sink2'(native int, native int){{$}}
define void @gep_fold_fp0() {
  %a = alloca [4 x i32], align 4
  %b = alloca [4 x i32], align 4
  %p = getelementptr inbounds i8, ptr %b, i64 4
  call void @sink2(ptr %a, ptr %p)
  ret void
}

; The constant 8 is shared by the frame address a+8 and by q+8, so it must not
; be changed: q+8 is still q plus 8. a+8 is FP+24, computed either as one
; constant or as FP+16 followed by the shared +8.
; CHECK-LABEL: .method {{.*}}'gep_shared_const'(
; CHECK-SAME:  native int) cil managed{{$}}
; O2:          {{^}}  ldc.i4.s 32{{$}}
; O2-NEXT:     {{^}}  localloc{{$}}
; O2-NEXT:     {{^}}  stloc.0{{$}}
; O2-NEXT:     {{^  ldloc\.0[[:space:]]+((ldc\.i4\.s 24|ldc\.i4 24)[[:space:]]+conv\.i[[:space:]]+add|(ldc\.i4\.s 16|ldc\.i4 16)[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+(ldc\.i4\.8|ldc\.i4 8)[[:space:]]+conv\.i[[:space:]]+add)$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.8|ldc\.i4 8}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ldloc.0{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  call void 'sink4'(native int, native int, native int, native int){{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  ldc.i4.s 32{{$}}
; O0-NEXT:     {{^}}  localloc{{$}}
; O0:          {{^}}  call void 'sink4'(native int, native int, native int, native int){{$}}
define void @gep_shared_const(ptr %q) {
  %a = alloca [4 x i32], align 4
  %b = alloca [4 x i32], align 4
  %p = getelementptr inbounds i8, ptr %a, i64 8
  %r = getelementptr inbounds i8, ptr %q, i64 8
  call void @sink4(ptr %p, ptr %r, ptr %b, ptr %q)
  ret void
}

; ---- Frame addresses across blocks, in selects and phis ----

; The prologue is in the entry block; a slot used only in another block is
; still FP-relative there.
; CHECK-LABEL: .method {{.*}}'slot_in_branch'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int{{.*}}){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.[[F:[0-9]]]{{$}}
; CHECK:       {{^}}  {{brtrue|brfalse|beq|bne\.un}} @BB{{[0-9_]+$}}
; CHECK:       {{^}}  ldloc.[[F]]{{$}}
; CHECK-NEXT:  {{^}}  call void 'sink'(native int){{$}}
; CHECK:       {{^}}} // end of method 'slot_in_branch'{{$}}
define void @slot_in_branch(i32 %c) {
entry:
  %x = alloca [16 x i8], align 8
  %t = icmp ne i32 %c, 0
  br i1 %t, label %then, label %exit
then:
  call void @sink(ptr %x)
  br label %exit
exit:
  ret void
}

; A select between two slots (x at FP+8, y at FP+0).
; CHECK-LABEL: .method {{.*}}'select_slots'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  .locals ({{.*}}){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.[[F:[0-9]]]{{$}}
; CHECK-DAG:   {{^  ldloc\.}}[[F]]{{[[:space:]]+(ldc\.i4\.8|ldc\.i4 8)[[:space:]]+conv\.i[[:space:]]+add$}}
; CHECK-DAG:   {{^}}  ldloc.[[F]]{{$}}
; CHECK:       {{^}}  call void 'sink'(native int){{$}}
; CHECK:       {{^}}} // end of method 'select_slots'{{$}}
define void @select_slots(i32 %c) {
  %x = alloca i64, align 8
  %y = alloca i64, align 8
  %t = icmp ne i32 %c, 0
  %p = select i1 %t, ptr %x, ptr %y
  call void @sink(ptr %p)
  ret void
}

; A phi of two slots (the frame addresses are computed in the predecessors).
; CHECK-LABEL: .method {{.*}}'phi_slots'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack {{[0-9]+}}{{$}}
; CHECK-NEXT:  {{^}}  .locals ({{.*}}){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.[[F:[0-9]]]{{$}}
; CHECK-DAG:   {{^  ldloc\.}}[[F]]{{[[:space:]]+(ldc\.i4\.8|ldc\.i4 8)[[:space:]]+conv\.i[[:space:]]+add$}}
; CHECK-DAG:   {{^}}  ldloc.[[F]]{{$}}
; CHECK:       {{^}}  call void 'sink'(native int){{$}}
; CHECK:       {{^}}} // end of method 'phi_slots'{{$}}
define void @phi_slots(i32 %c) {
entry:
  %x = alloca i64, align 8
  %y = alloca i64, align 8
  %t = icmp ne i32 %c, 0
  br i1 %t, label %a, label %b
a:
  call void @sink(ptr null)
  br label %m
b:
  call void @sink2(ptr null, ptr null)
  br label %m
m:
  %p = phi ptr [ %x, %a ], [ %y, %b ]
  call void @sink(ptr %p)
  ret void
}

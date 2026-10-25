; byval copies share frame slots (ImplementationPlan.md M12.b (2); the section
; 12 row of 2026-10-21 for M12, 'M9.a byval amended'; recon p2/backlog.md
; 2.6; decision D13). A caller copies each byval argument into its static
; frame before the call (byval.ll), and since M12 the K-th byval argument of
; every call (K counted per call) uses the function's K-th byval slot
; (MSILFunctionInfo::getByvalSlot), created by its first use and grown to the
; largest size and alignment any call asks for, instead of one new frame
; object per argument of every call. The copy is dead when the callee returns,
; so reuse is safe; the copy is still made before every call, so a callee that
; writes to its parameter never changes what the next call sees, and two
; byval arguments of one call get two slots. It works at -O0 as at -O2
; (LowerCall emits no lifetime markers, and StackColoring runs only from
; -O1), on both triples (the IR is what clang emits for SysV x86-64, the only
; ABI with byval aggregates; the backend does not care which triple it is).
; Objects are laid out downwards from the top of the frame in creation order
; and the frame is rounded up to 16 bytes (frame-index.ll): one 64-byte slot
; is FP itself in a 64-byte frame (byval.c's ten calls had 640 bytes), two
; are FP + 64 and FP in a 128-byte frame, a 40-byte slot is FP + 8 in a
; 48-byte frame, and a 24-byte slot aligned to 16 is the aligned base of a
; frame over-allocated by 8 (40 bytes, D13). A function with
; "no-realign-stack" has a frame that cannot be realigned (MachineFrameInfo
; clamps the alignment of a new object to the 16-byte stack alignment and
; asserts when a larger one is set later), and there the slot of an 8-aligned
; argument is not raised for a later 16- or 32-aligned one: the later argument
; gets a second slot, and llc does not assert. With take24 first, its slot is
; the aligned base + 24 and the 16-aligned slot the aligned base of a 56-byte
; frame (48 bytes over-allocated by 8); a 32-byte struct aligned to 32 gets a
; slot aligned to 16 only (the clamp), at the aligned base of a 72-byte frame,
; and take24's slot is the aligned base + 40. The copy is cpblk or loads and
; stores (both are accepted, as in byval.ll); each one is checked by its
; source and, with the call, by the frame address the call receives, as one
; multi-line pattern. --implicit-check-not=localloc checks that every function
; has exactly the one frame checked.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=localloc \
; DEFINE:   --implicit-check-not=".locals init"
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.lnx.o2.il 2>&1 | count 0
; RUN: %{check} --input-file=%t.lnx.o2.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.win.o2.il 2>&1 | count 0
; RUN: %{check} --input-file=%t.win.o2.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.lnx.o0.il 2>&1 | count 0
; RUN: %{check} --input-file=%t.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   -o %t.win.o0.il 2>&1 | count 0
; RUN: %{check} --input-file=%t.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.o2.dll %t.lnx.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.o0.dll %t.win.o0.il %}

source_filename = "byval-slot-reuse.c"

%struct.S64 = type { [8 x i64] }
%struct.S24 = type { i64, i64, i64 }
%struct.S40 = type { [5 x i64] }
%struct.A16 = type { [2 x i64] }
%struct.A32 = type { [4 x i64] }

; ---- callees: they write to their copies; no frame ---------------------------

; CHECK-LABEL: {{^}}.method public static void 'take64'(native int) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'take64'{{$}}
define void @take64(ptr byval(%struct.S64) align 8 %s) {
  %a = load i64, ptr %s, align 8
  %n = add i64 %a, 1
  store i64 %n, ptr %s, align 8
  ret void
}

; CHECK-LABEL: {{^}}.method public static void 'take64x2'(native int, native int) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'take64x2'{{$}}
define void @take64x2(ptr byval(%struct.S64) align 8 %a, ptr byval(%struct.S64) align 8 %b) {
  %va = load i64, ptr %a, align 8
  store i64 %va, ptr %b, align 8
  ret void
}

; CHECK-LABEL: {{^}}.method public static void 'take24'(native int) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'take24'{{$}}
define void @take24(ptr byval(%struct.S24) align 8 %s) {
  store i64 24, ptr %s, align 8
  ret void
}

; CHECK-LABEL: {{^}}.method public static void 'take40'(native int) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'take40'{{$}}
define void @take40(ptr byval(%struct.S40) align 8 %s) {
  store i64 40, ptr %s, align 8
  ret void
}

; CHECK-LABEL: {{^}}.method public static void 'take_a16'(native int) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'take_a16'{{$}}
define void @take_a16(ptr byval(%struct.A16) align 16 %s) {
  store i64 16, ptr %s, align 16
  ret void
}

; CHECK-LABEL: {{^}}.method public static void 'take_a32'(native int) cil managed{{$}}
; CHECK:       {{^}}} // end of method 'take_a32'{{$}}
define void @take_a32(ptr byval(%struct.A32) align 32 %s) {
  store i64 32, ptr %s, align 32
  ret void
}

; ---- callers -----------------------------------------------------------------

; Ten calls with a 64-byte struct: one 64-byte frame, every copy at FP, each
; filled from %p right before its call.
; CHECK-LABEL: {{^}}.method public static void 'ten_calls'(native int) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4.s 64{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+call void 'take64'\(native int\)$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+call void 'take64'\(native int\)$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+call void 'take64'\(native int\)$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+call void 'take64'\(native int\)$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+call void 'take64'\(native int\)$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+call void 'take64'\(native int\)$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+call void 'take64'\(native int\)$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+call void 'take64'\(native int\)$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+call void 'take64'\(native int\)$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+call void 'take64'\(native int\)$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ten_calls'{{$}}
define void @ten_calls(ptr %p) {
  call void @take64(ptr byval(%struct.S64) align 8 %p)
  call void @take64(ptr byval(%struct.S64) align 8 %p)
  call void @take64(ptr byval(%struct.S64) align 8 %p)
  call void @take64(ptr byval(%struct.S64) align 8 %p)
  call void @take64(ptr byval(%struct.S64) align 8 %p)
  call void @take64(ptr byval(%struct.S64) align 8 %p)
  call void @take64(ptr byval(%struct.S64) align 8 %p)
  call void @take64(ptr byval(%struct.S64) align 8 %p)
  call void @take64(ptr byval(%struct.S64) align 8 %p)
  call void @take64(ptr byval(%struct.S64) align 8 %p)
  ret void
}

; Two byval arguments of one call: two slots, %p copied to FP + 64 and %q
; to FP, then both addresses are pushed in order.
; CHECK-LABEL: {{^}}.method public static void 'two_in_one_call'(native int, native int) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4 128{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.1{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+ldc\.i4\.s 64[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+ldloc\.0[[:space:]]+call void 'take64x2'\(native int, native int\)$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @two_in_one_call(ptr %p, ptr %q) {
  call void @take64x2(ptr byval(%struct.S64) align 8 %p, ptr byval(%struct.S64) align 8 %q)
  ret void
}

; One-argument calls before and after a two-argument call: the first slot at
; FP + 64 serves the first argument of all three calls, the second slot at FP
; only the second argument of the middle call.
; CHECK-LABEL: {{^}}.method public static void 'one_and_two'(native int, native int) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4 128{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+ldc\.i4\.s 64[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+call void 'take64'\(native int\)$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.1{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+ldc\.i4\.s 64[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+ldloc\.0[[:space:]]+call void 'take64x2'\(native int, native int\)$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.1{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+ldc\.i4\.s 64[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+call void 'take64'\(native int\)$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @one_and_two(ptr %p, ptr %q) {
  call void @take64(ptr byval(%struct.S64) align 8 %p)
  call void @take64x2(ptr byval(%struct.S64) align 8 %p, ptr byval(%struct.S64) align 8 %q)
  call void @take64(ptr byval(%struct.S64) align 8 %q)
  ret void
}

; Sizes 24 and 40 share one slot, grown to 40 bytes: FP + 8 of a 48-byte
; frame for all three calls.
; CHECK-LABEL: {{^}}.method public static void 'grow'(native int, native int) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4.s 48{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+ldc\.i4\.8[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+call void 'take24'\(native int\)$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.1{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+ldc\.i4\.8[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+call void 'take40'\(native int\)$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+ldc\.i4\.8[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+call void 'take24'\(native int\)$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @grow(ptr %p, ptr %q) {
  call void @take24(ptr byval(%struct.S24) align 8 %p)
  call void @take40(ptr byval(%struct.S40) align 8 %q)
  call void @take24(ptr byval(%struct.S24) align 8 %p)
  ret void
}

; Alignments 8 and 16 share one slot, aligned to 16: the frame is
; over-allocated by 8 and its base aligned up (D13), and the 24-byte slot is
; the aligned base for both calls.
; CHECK-LABEL: {{^}}.method public static void 'realign'(native int, native int) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4.s 40{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 15{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s -16{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.[[:digit:]][[:space:]]+call void 'take24'\(native int\)$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.1{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.[[:digit:]][[:space:]]+call void 'take_a16'\(native int\)$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @realign(ptr %p, ptr %q) {
  call void @take24(ptr byval(%struct.S24) align 8 %p)
  call void @take_a16(ptr byval(%struct.A16) align 16 %q)
  ret void
}

; "no-realign-stack": take24's 8-aligned slot is not raised for the
; 16-aligned argument, which gets a second slot; the frame is 56 bytes.
; CHECK-LABEL: {{^}}.method public static void 'no_realign16'(native int, native int) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4.s 56{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 15{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s -16{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.[[:digit:]][[:space:]]+ldc\.i4\.s 24[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+call void 'take24'\(native int\)$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.1{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.[[:digit:]][[:space:]]+call void 'take_a16'\(native int\)$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @no_realign16(ptr %p, ptr %q) #0 {
  call void @take24(ptr byval(%struct.S24) align 8 %p)
  call void @take_a16(ptr byval(%struct.A16) align 16 %q)
  ret void
}

; The same with a 32-aligned struct, whose slot is aligned to 16 only: a
; 72-byte frame.
; CHECK-LABEL: {{^}}.method public static void 'no_realign32'(native int, native int) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4.s 72{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 15{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s -16{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.[[:digit:]][[:space:]]+ldc\.i4\.s 40[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+call void 'take24'\(native int\)$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.1{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.[[:digit:]][[:space:]]+call void 'take_a32'\(native int\)$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @no_realign32(ptr %p, ptr %q) #0 {
  call void @take24(ptr byval(%struct.S24) align 8 %p)
  call void @take_a32(ptr byval(%struct.A32) align 32 %q)
  ret void
}

attributes #0 = { "no-realign-stack" }

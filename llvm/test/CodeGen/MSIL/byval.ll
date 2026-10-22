; byval arguments and parameters (ImplementationPlan.md M9.a, decision D13;
; recon isel.md 2.4, 2.12: WebAssembly's LowerCall 'copies byval args into a
; new caller stack object with getMemcpy, then passes the frame index').
; A byval parameter is the address of a copy that the caller made: the callee
; sees an ordinary native int parameter, reads and writes through it, and
; gets no diagnostic (M3's 'byval arguments are not supported for MSIL yet'
; is gone for parameters and for call sites). At a call, each byval argument
; is copied into an object of the caller's static frame (one localloc at
; entry, D13), sized and aligned as the byval type, before the call, and the
; call gets the copy's address, never the original pointer: a second call
; makes a second copy, so a callee that writes to its parameter cannot change
; the caller's aggregate. Since M12 (section 12, 'M9.a byval amended') the
; K-th byval argument of every call reuses the function's K-th byval slot,
; grown to the largest size and alignment (byval-slot-reuse.ll), so the two
; calls of call_bump_twice copy into one slot, while the two byval arguments
; of one call (call_two) still get two. The copy is a memcpy, which may be
; cpblk (with 'unaligned. N' below 8-byte alignment) or loads and stores;
; both are accepted, so each copy is checked by its source (the original
; pointer is read) and by the frame address that the call receives, matched
; together with the call as one multi-line pattern. Objects are laid out
; downwards from the top of the frame in creation order (frame-index.ll): one
; 24-byte copy lives at FP + 8 of a 32-byte frame, two at FP + 24 and FP + 0
; of a 48-byte frame; an object aligned to 16 makes the frame over-allocate by 8
; and align its base up (D13). A byval argument of an indirect call is copied
; the same way. A zero-size byval argument is passed as it is, with no copy
; and no frame (WebAssembly's LowerCall copies only a non-zero byval size).
; inalloca and preallocated stay diagnostics (call-errors.ll).

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning: --implicit-check-not=".locals init"
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "byval.c"

%struct.S = type { i64, i64, i64 }
%struct.P = type { i32, i32, i32 }
%struct.A = type { [2 x i64] }
%struct.E = type {}

; ---- byval parameters: plain pointers, no frame -------------------------------

; CHECK-LABEL: .method {{.*}}'sum_s'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NOT:   localloc
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK:       {{^}}} // end of method 'sum_s'{{$}}
define i64 @sum_s(ptr byval(%struct.S) align 8 %s) {
  %a = load i64, ptr %s, align 8
  %pb = getelementptr inbounds i8, ptr %s, i64 8
  %b = load i64, ptr %pb, align 8
  %r = add i64 %a, %b
  ret i64 %r
}

; The callee writes to its own copy.
; CHECK-LABEL: .method {{.*}}'bump_s'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NOT:   localloc
; CHECK:       {{^}}  stind.i{{$}}
; CHECK:       {{^}}} // end of method 'bump_s'{{$}}
define i64 @bump_s(ptr byval(%struct.S) align 8 %s) {
  %a = load i64, ptr %s, align 8
  %n = add i64 %a, 1
  store i64 %n, ptr %s, align 8
  ret i64 %n
}

; CHECK-LABEL: .method {{.*}}'two'(
; CHECK-SAME:  native int, int32, native int) cil managed{{$}}
; CHECK-NOT:   localloc
; CHECK:       {{^}}} // end of method 'two'{{$}}
define i64 @two(ptr byval(%struct.S) align 8 %a, i32 %x, ptr byval(%struct.S) align 8 %b) {
  %va = load i64, ptr %a, align 8
  %vb = load i64, ptr %b, align 8
  %r = sub i64 %va, %vb
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'take_p'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NOT:   localloc
; CHECK:       {{^}}} // end of method 'take_p'{{$}}
define i32 @take_p(ptr byval(%struct.P) align 4 %s) {
  %pc = getelementptr inbounds i8, ptr %s, i64 8
  %c = load i32, ptr %pc, align 4
  ret i32 %c
}

; CHECK-LABEL: .method {{.*}}'take_a'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NOT:   localloc
; CHECK:       {{^}}} // end of method 'take_a'{{$}}
define i64 @take_a(ptr byval(%struct.A) align 16 %s) {
  %v = load i64, ptr %s, align 16
  ret i64 %v
}

; ---- byval arguments: a copy in the caller's frame ------------------------------

; One 24-byte copy at FP + 8 of a 32-byte frame.
; CHECK-LABEL: .method {{.*}}'call_sum'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4.s 32{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+ldc\.i4 8[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+call native int 'sum_s'\(native int\)$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_sum'{{$}}
define i64 @call_sum(ptr %p) {
  %r = call i64 @sum_s(ptr byval(%struct.S) align 8 %p)
  ret i64 %r
}

; Two calls with the same aggregate: one slot since M12, a copy at FP + 8 of
; a 32-byte frame, filled from %p again right before each call, so the second
; callee does not see the first one's write.
; CHECK-LABEL: .method {{.*}}'call_bump_twice'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4.s 32{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+ldc\.i4 8[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+call native int 'bump_s'\(native int\)$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+(ldloc(\.[0-9]|\.s [0-9]+)[[:space:]]+)?ldloc\.0[[:space:]]+ldc\.i4 8[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+call native int 'bump_s'\(native int\)$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @call_bump_twice(ptr %p) {
  %r1 = call i64 @bump_s(ptr byval(%struct.S) align 8 %p)
  %r2 = call i64 @bump_s(ptr byval(%struct.S) align 8 %p)
  %r = add i64 %r1, %r2
  ret i64 %r
}

; A byval parameter passed on as a byval argument is copied again.
; CHECK-LABEL: .method {{.*}}'forward'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4.s 32{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+ldc\.i4 8[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+call native int 'sum_s'\(native int\)$}}
define i64 @forward(ptr byval(%struct.S) align 8 %s) {
  %r = call i64 @sum_s(ptr byval(%struct.S) align 8 %s)
  ret i64 %r
}

; Two byval arguments around a scalar: %p is copied to FP + 24 and %q to
; FP + 0, then the arguments are pushed in order.
; CHECK-LABEL: .method {{.*}}'call_two'(
; CHECK-SAME:  native int, int32, native int) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4.s 48{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+ldc\.i4 24[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+ldarg\.1[[:space:]]+ldloc\.0[[:space:]]+call native int 'two'\(native int, int32, native int\)$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @call_two(ptr %p, i32 %x, ptr %q) {
  %r = call i64 @two(ptr byval(%struct.S) align 8 %p, i32 %x, ptr byval(%struct.S) align 8 %q)
  ret i64 %r
}

; A 12-byte, 4-aligned aggregate: a copy at FP + 4 of a 16-byte frame.
; CHECK-LABEL: .method {{.*}}'call_p'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i4?)[[:space:]]+ldloc\.0[[:space:]]+ldc\.i4 4[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+call int32 'take_p'\(native int\)$}}
define i32 @call_p(ptr %p) {
  %r = call i32 @take_p(ptr byval(%struct.P) align 4 %p)
  ret i32 %r
}

; A 16-aligned aggregate: the frame is over-allocated by 8 and its base is
; aligned up to 16 (D13); the copy is at the aligned base.
; CHECK-LABEL: .method {{.*}}'call_a'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4.s 24{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 15{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 -16{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.[0-9][[:space:]]+call native int 'take_a'\(native int\)$}}
define i64 @call_a(ptr %p) {
  %r = call i64 @take_a(ptr byval(%struct.A) align 16 %p)
  ret i64 %r
}

; An indirect call: the same copy, then the callee pointer, then calli.
; CHECK-LABEL: .method {{.*}}'call_indirect'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK:       {{^}}  ldc.i4.s 32{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NOT:   {{^}}  calli
; CHECK:       {{^}}  ldarg.1{{$}}
; CHECK-NOT:   {{^}}  calli
; CHECK:       {{^  (cpblk|stind\.i)[[:space:]]+ldloc\.0[[:space:]]+ldc\.i4 8[[:space:]]+conv\.i[[:space:]]+add[[:space:]]+ldarg\.0[[:space:]]+calli native int\(native int\)$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @call_indirect(ptr %f, ptr %p) {
  %r = call i64 %f(ptr byval(%struct.S) align 8 %p)
  ret i64 %r
}

; ---- a zero-size byval argument: no copy ------------------------------------

; CHECK-LABEL: .method {{.*}}'take_e'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; CHECK-NOT:   localloc
; CHECK:       {{^}}} // end of method 'take_e'{{$}}
define i32 @take_e(ptr byval(%struct.E) %e, i32 %x) {
  ret i32 %x
}

; CHECK-LABEL: .method {{.*}}'call_e'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; CHECK-NOT:   localloc
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call int32 'take_e'(native int, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_e'{{$}}
define i32 @call_e(ptr %p, i32 %x) {
  %r = call i32 @take_e(ptr byval(%struct.E) %p, i32 %x)
  ret i32 %r
}

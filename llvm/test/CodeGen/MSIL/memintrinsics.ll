; memcpy and memset (ImplementationPlan.md decision D15, M7.a; recon isel.md
; 2.15, 5.8 and 11.3): memory intrinsics never become library calls. A memcpy
; with a constant length of at most 2^32-1 bytes is cpblk (destination,
; source, then the int32 length; ECMA-335 III.3.30), a memset is initblk
; (destination, value, int32 length; III.3.36). A memcpy whose length is not a
; constant, or a constant above 2^32-1, is a call of
; [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64) with the
; source first (MSILISD::MEMMOVE: src, dst, len; then 'conv.i8; dup' supplies
; the length twice, .maxstack 4). A memset whose length is a variable native int
; is truncated with conv.i4 (the documented 4 GiB limit of D15); a constant
; memset length of 2^32 or more is split into initblk blocks of 2^31 bytes with
; the remainder last (set_4g5, set_9g). cpblk and initblk assume addresses
; aligned to the native int size, so an alignment of 1, 2 or 4 is announced
; with 'unaligned. N' (isel.md 11.3 encodes the memflags of cpblk and initblk
; against the natural size 8; ECMA-335 III.2.5), and an 8-aligned (or better)
; operation has no prefix. The only volatile. prefixes are those of the
; volatile memset set_9g. A zero-length memcpy or memset is nothing at all.
;
; The constant lengths that are checked as cpblk/initblk (1000 bytes up to
; 9 GiB) are far above any inline load/store expansion; a short constant
; memcpy or memset may instead be expanded inline into ldind/stind, so for those
; only the absence of a call is checked. Every function is a single
; intrinsic on arguments. The memcpy CIL is the same at -O0 and -O2 (CHECK).
; A memset computes its int32 length before its fill value, and at -O0
; RegStackify only stackifies a def that sits right before its use (recon
; passes-frame.md 5.1), so at -O0 a constant or truncated length waits in an
; int32 local (O0) while at -O2 it is pushed right before initblk (O2); the
; operand order (destination, value, length) is the same. No method has a
; frame, and no '.locals' is 'init'. An undefined external 'memcpy', 'memset'
; or 'memmove' call would be a P/Invoke (and an error until M8), so the output
; must not name them.

; DEFINE: %{check} = FileCheck %s --implicit-check-not="'memcpy'(" --implicit-check-not="'memset'(" \
; DEFINE:   --implicit-check-not="'memmove'(" --implicit-check-not=pinvokeimpl --implicit-check-not=volatile. \
; DEFINE:   --implicit-check-not=localloc --implicit-check-not=".locals init"
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

source_filename = "memintrinsics.c"

declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)
declare void @llvm.memcpy.p0.p0.i32(ptr, ptr, i32, i1)
declare void @llvm.memcpy.inline.p0.p0.i64(ptr, ptr, i64, i1)
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)
declare void @llvm.memset.p0.i32(ptr, i8, i32, i1)

; ---- memcpy: cpblk for constant lengths ----

; CHECK-LABEL: .method {{.*}}'copy_1000'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 1000{{$}}
; CHECK-NEXT:  {{^}}  cpblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'copy_1000'{{$}}
define void @copy_1000(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %d, ptr align 8 %s, i64 1000, i1 false)
  ret void
}

; 16-aligned operands: no prefix either.
; CHECK-LABEL: .method {{.*}}'copy_1000_a16'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 1000{{$}}
; CHECK-NEXT:  {{^}}  cpblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @copy_1000_a16(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 16 %d, ptr align 16 %s, i64 1000, i1 false)
  ret void
}

; The prefix uses the smaller of the two alignments.
; CHECK-LABEL: .method {{.*}}'copy_1000_a1'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 1000{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 1{{$}}
; CHECK-NEXT:  {{^}}  cpblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @copy_1000_a1(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %d, ptr align 1 %s, i64 1000, i1 false)
  ret void
}

; CHECK-LABEL: .method {{.*}}'copy_1000_a2'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 1000{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 2{{$}}
; CHECK-NEXT:  {{^}}  cpblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @copy_1000_a2(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 2 %d, ptr align 8 %s, i64 1000, i1 false)
  ret void
}

; CHECK-LABEL: .method {{.*}}'copy_1000_a4'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 1000{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 4{{$}}
; CHECK-NEXT:  {{^}}  cpblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @copy_1000_a4(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %d, ptr align 4 %s, i64 1000, i1 false)
  ret void
}

; The largest cpblk length, 2^32-1: an int32 -1 (cpblk reads it as unsigned).
; CHECK-LABEL: .method {{.*}}'copy_max_cpblk'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4\.m1|ldc\.i4 -1}}{{$}}
; CHECK-NEXT:  {{^}}  cpblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @copy_max_cpblk(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %d, ptr align 8 %s, i64 4294967295, i1 false)
  ret void
}

; A memcpy with an i32 length operand and a constant length.
; CHECK-LABEL: .method {{.*}}'copy_1000_i32len'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 1000{{$}}
; CHECK-NEXT:  {{^}}  cpblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @copy_1000_i32len(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i32(ptr align 8 %d, ptr align 8 %s, i32 1000, i1 false)
  ret void
}

; ---- memcpy: Buffer::MemoryCopy for other lengths ----

; A variable length: source first, then destination, then the length twice.
; CHECK-LABEL: .method {{.*}}'copy_var'(
; CHECK-SAME:  native int, native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 4{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  conv.i8{{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK-NEXT:  {{^}}  call void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'copy_var'{{$}}
define void @copy_var(ptr %d, ptr %s, i64 %n) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %d, ptr align 8 %s, i64 %n, i1 false)
  ret void
}

; A variable i32 length is zero-extended to native int first.
; CHECK-LABEL: .method {{.*}}'copy_var_i32len'(
; CHECK-SAME:  native int, native int, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  conv.u{{$}}
; CHECK-NEXT:  {{^}}  conv.i8{{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK-NEXT:  {{^}}  call void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @copy_var_i32len(ptr %d, ptr %s, i32 %n) {
  call void @llvm.memcpy.p0.p0.i32(ptr align 8 %d, ptr align 8 %s, i32 %n, i1 false)
  ret void
}

; 2^32 bytes do not fit cpblk's int32 length.
; CHECK-LABEL: .method {{.*}}'copy_4gib'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 4{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i8 4294967296{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  conv.i8{{$}}
; CHECK-NEXT:  {{^}}  dup{{$}}
; CHECK-NEXT:  {{^}}  call void [mscorlib]System.Buffer::MemoryCopy(void*, void*, int64, int64){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @copy_4gib(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 8 %d, ptr align 8 %s, i64 4294967296, i1 false)
  ret void
}

; ---- memcpy: short and empty copies ----

; A short constant copy is cpblk or inline loads and stores, never a call.
; CHECK-LABEL: .method {{.*}}'copy_16'(
; CHECK-NOT:   call
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'copy_16'{{$}}
define void @copy_16(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %d, ptr align 4 %s, i64 16, i1 false)
  ret void
}

; memcpy.inline must not become a call either.
; CHECK-LABEL: .method {{.*}}'copy_inline_64'(
; CHECK-NOT:   call
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'copy_inline_64'{{$}}
define void @copy_inline_64(ptr %d, ptr %s) {
  call void @llvm.memcpy.inline.p0.p0.i64(ptr align 8 %d, ptr align 8 %s, i64 64, i1 false)
  ret void
}

; CHECK-LABEL: .method {{.*}}'copy_0'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'copy_0'{{$}}
define void @copy_0(ptr %d, ptr %s) {
  call void @llvm.memcpy.p0.p0.i64(ptr %d, ptr %s, i64 0, i1 false)
  ret void
}

; ---- memset: initblk ----

; CHECK-LABEL: .method {{.*}}'set_zero_1000'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; O0-NEXT:     {{^}}  .locals (int32){{$}}
; O0-NEXT:     {{^}}  ldc.i4 1000{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4 1000{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'set_zero_1000'{{$}}
define void @set_zero_1000(ptr %d) {
  call void @llvm.memset.p0.i64(ptr align 8 %d, i8 0, i64 1000, i1 false)
  ret void
}

; The fill byte 0xAB as an int32 (initblk stores its low byte).
; CHECK-LABEL: .method {{.*}}'set_ab_1000'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; O0-NEXT:     {{^}}  .locals (int32){{$}}
; O0-NEXT:     {{^}}  ldc.i4 1000{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  {{ldc\.i4 171|ldc\.i4\.s -85}}{{$}}
; O2-NEXT:     {{^}}  ldc.i4 1000{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @set_ab_1000(ptr %d) {
  call void @llvm.memset.p0.i64(ptr align 8 %d, i8 -85, i64 1000, i1 false)
  ret void
}

; A fill byte from an argument.
; CHECK-LABEL: .method {{.*}}'set_arg_1000'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK:       {{^}}  ldc.i4 1000{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @set_arg_1000(ptr %d, i8 %c) {
  call void @llvm.memset.p0.i64(ptr align 8 %d, i8 %c, i64 1000, i1 false)
  ret void
}

; CHECK-LABEL: .method {{.*}}'set_1000_a1'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; O0-NEXT:     {{^}}  .locals (int32){{$}}
; O0-NEXT:     {{^}}  ldc.i4 1000{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4 1000{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 1{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @set_1000_a1(ptr %d) {
  call void @llvm.memset.p0.i64(ptr align 1 %d, i8 0, i64 1000, i1 false)
  ret void
}

; CHECK-LABEL: .method {{.*}}'set_1000_a4'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; O0-NEXT:     {{^}}  .locals (int32){{$}}
; O0-NEXT:     {{^}}  ldc.i4 1000{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4 1000{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 4{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @set_1000_a4(ptr %d) {
  call void @llvm.memset.p0.i64(ptr align 4 %d, i8 0, i64 1000, i1 false)
  ret void
}

; A variable native int length is truncated to int32 (4 GiB limit, D15).
; CHECK-LABEL: .method {{.*}}'set_var'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; O0-NEXT:     {{^}}  .locals (int32){{$}}
; O0-NEXT:     {{^}}  ldarg.1{{$}}
; O0-NEXT:     {{^}}  conv.i4{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  conv.i4{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'set_var'{{$}}
define void @set_var(ptr %d, i64 %n) {
  call void @llvm.memset.p0.i64(ptr align 8 %d, i8 0, i64 %n, i1 false)
  ret void
}

; A variable int32 length is already the size operand of initblk.
; CHECK-LABEL: .method {{.*}}'set_var_i32len'(
; CHECK-SAME:  native int, int32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK:       {{^}}  ldarg.2{{$}}
; CHECK:       {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @set_var_i32len(ptr %d, i8 %c, i32 %n) {
  call void @llvm.memset.p0.i32(ptr align 8 %d, i8 %c, i32 %n, i1 false)
  ret void
}

; ---- memset: constant lengths of 2 GiB and more ----
; initblk reads its int32 length as unsigned, so a constant length from 2^31 to
; 2^32-1 is one initblk with a negative ldc.i4. A constant length that does not
; fit in 32 bits is split into initblk blocks of 2^31 bytes, the remainder
; last; each block has its own address (the base plus 'ldc.i8 <offset>',
; 'conv.i', 'add') and its own prefixes.

; 4294967301 = 2^31 + 2147483653: two blocks, the second at +2^31 with the
; length 2147483653 (-2147483643 as an int32).
; CHECK-LABEL: .method {{.*}}'set_4g5'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; O0-NEXT:     {{^}}  .locals (int32, int32){{$}}
; O0-NEXT:     {{^}}  ldc.i4 -2147483648{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; O0-NEXT:     {{^}}  ldc.i4.7{{$}}
; O0-NEXT:     {{^}}  stloc.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4.7{{$}}
; O2-NEXT:     {{^}}  ldc.i4 -2147483648{{$}}
; O0-NEXT:     {{^}}  ldloc.1{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i8 2147483648{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ldc.i4.7{{$}}
; O0-NEXT:     {{^}}  ldloc.1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 -2147483643{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'set_4g5'{{$}}
define void @set_4g5(ptr %d) {
  call void @llvm.memset.p0.i64(ptr align 8 %d, i8 7, i64 4294967301, i1 false)
  ret void
}

; 3221225472 = 3 * 2^30 fits in 32 bits: one initblk.
; CHECK-LABEL: .method {{.*}}'set_3g'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; O0-NEXT:     {{^}}  .locals (int32){{$}}
; O0-NEXT:     {{^}}  ldc.i4 -1073741824{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.7{{$}}
; O2-NEXT:     {{^}}  ldc.i4 -1073741824{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'set_3g'{{$}}
define void @set_3g(ptr %d) {
  call void @llvm.memset.p0.i64(ptr align 8 %d, i8 7, i64 3221225472, i1 false)
  ret void
}

; 9663676416 = 3 * 2^31 + 3 * 2^30, volatile and 2-aligned: four blocks at +0,
; +2^31, +2^32 and +6442450944, the last of 3221225472 bytes (-1073741824), and
; every initblk has both prefixes.
; CHECK-LABEL: .method {{.*}}'set_9g'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; O0-NEXT:     {{^}}  .locals (int32, int32){{$}}
; O0-NEXT:     {{^}}  ldc.i4 -2147483648{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; O0-NEXT:     {{^}}  ldc.i4.7{{$}}
; O0-NEXT:     {{^}}  stloc.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4.7{{$}}
; O2-NEXT:     {{^}}  ldc.i4 -2147483648{{$}}
; O0-NEXT:     {{^}}  ldloc.1{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 2{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i8 2147483648{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ldc.i4.7{{$}}
; O2-NEXT:     {{^}}  ldc.i4 -2147483648{{$}}
; O0-NEXT:     {{^}}  ldloc.1{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 2{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i8 4294967296{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ldc.i4.7{{$}}
; O2-NEXT:     {{^}}  ldc.i4 -2147483648{{$}}
; O0-NEXT:     {{^}}  ldloc.1{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 2{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i8 6442450944{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; O2-NEXT:     {{^}}  ldc.i4.7{{$}}
; O0-NEXT:     {{^}}  ldloc.1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 -1073741824{{$}}
; CHECK-NEXT:  {{^}}  unaligned. 2{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  initblk{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'set_9g'{{$}}
define void @set_9g(ptr %d) {
  call void @llvm.memset.p0.i64(ptr align 2 %d, i8 7, i64 9663676416, i1 true)
  ret void
}

; A short constant fill is initblk or inline stores, never a call.
; CHECK-LABEL: .method {{.*}}'set_8'(
; CHECK-NOT:   call
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'set_8'{{$}}
define void @set_8(ptr %d) {
  call void @llvm.memset.p0.i64(ptr align 8 %d, i8 0, i64 8, i1 false)
  ret void
}

; CHECK-LABEL: .method {{.*}}'set_0'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'set_0'{{$}}
define void @set_0(ptr %d) {
  call void @llvm.memset.p0.i64(ptr %d, i8 0, i64 0, i1 false)
  ret void
}

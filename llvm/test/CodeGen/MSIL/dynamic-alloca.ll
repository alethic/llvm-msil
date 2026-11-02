; Acyclic dynamic allocas (ImplementationPlan.md 5.15.8 H0, M26; decision D13
; as amended by the section 12 row of 2026-10-28). MSILLowerDynamicAlloca
; (slot P9, 5.13.6) finds the dynamic allocas (!isStaticAlloca: a variable
; count, or any alloca outside the entry block) and, when none of them lies on
; a CFG cycle, leaves them for ISel, which lowers DYNAMIC_STACKALLOC to
; LOCALLOC_I64: CIL 'localloc' with a native-int size, so a 64-bit count is
; never truncated to int32. An alignment above getLocallocAlign() (8)
; over-allocates align - 8 bytes and masks the result with -align (native int
; arithmetic). llvm.stacksave and llvm.stackrestore are deleted in a function
; without a cyclic alloca (they free nothing that a localloc holds), so
; clang's VLA scopes and lone saves and restores compile with no diagnostic.
; A dynamic alloca on a cycle (a VLA or alloca(n) in a loop) is the error
; 'a dynamic stack allocation that can repeat within one call (a VLA or alloca
; in a loop) is not supported for MSIL yet' until M32's shadow stack: the
; function's body is dropped, llc exits with 1 without 'LLVM ERROR' or a crash
; report, and the other functions compile (cyclic.ll; the same text is pinned
; in dynamic-alloca-error.ll).
;
; acyclic.ll compiles without any diagnostic at -O0 and -O2 on both execution
; triples (IL), its IR after P9 keeps the allocas and has no stacksave or
; stackrestore left (IR), and it assembles with ilasm. The IL checks pin the
; opcodes that matter (the localloc of the dynamic size, the mask, no 32-bit
; conversion of a 64-bit size, a program's own rounding kept before the
; localloc, no localloc of 0) and leave the register choices free.

; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{il} = FileCheck %s --check-prefix=IL --implicit-check-not=error: --implicit-check-not=warning: \
; DEFINE:   --implicit-check-not="LLVM ERROR"
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/acyclic.ll \
; RUN:   -o %t/a.lnx.o2.il 2>&1 | count 0
; RUN: %{il} --input-file=%t/a.lnx.o2.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/acyclic.ll \
; RUN:   -o %t/a.win.o2.il 2>&1 | count 0
; RUN: %{il} --input-file=%t/a.win.o2.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/acyclic.ll \
; RUN:   -o %t/a.lnx.o0.il 2>&1 | count 0
; RUN: %{il} --input-file=%t/a.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/acyclic.ll \
; RUN:   -o %t/a.win.o0.il 2>&1 | count 0
; RUN: %{il} --input-file=%t/a.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/a.lnx.o2.dll %t/a.lnx.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/a.win.o0.dll %t/a.win.o0.il %}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -stop-after=msil-lower-dynamic-alloca < %t/acyclic.ll -o - \
; RUN:   | FileCheck %s --check-prefix=IR --implicit-check-not="call ptr @llvm.stacksave" \
; RUN:     --implicit-check-not="call void @llvm.stackrestore"
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O2 -stop-after=msil-lower-dynamic-alloca < %t/acyclic.ll -o - \
; RUN:   | FileCheck %s --check-prefix=IR --implicit-check-not="call ptr @llvm.stacksave" \
; RUN:     --implicit-check-not="call void @llvm.stackrestore"

; DEFINE: %{cyc} = FileCheck %s --check-prefix=CYC --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not="Stack dump" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/cyclic.ll -o /dev/null 2>&1 | %{cyc}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/cyclic.ll -o /dev/null 2>&1 | %{cyc}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/cyclic.ll -o /dev/null 2>&1 \
; RUN:   | %{cyc}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/cyclic.ll -o /dev/null 2>&1 \
; RUN:   | %{cyc}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/cyclic.ll \
; RUN:   > %t/c.lnx.il
; RUN: FileCheck %s --check-prefix=CYC-IL --input-file=%t/c.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/cyclic.ll \
; RUN:   > %t/c.win.il
; RUN: FileCheck %s --check-prefix=CYC-IL --input-file=%t/c.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/c.lnx.dll %t/c.lnx.il %}

;--- acyclic.ll
source_filename = "acyclic.c"

define void @use(ptr %p) {
  ret void
}

; char buf[n] with a 64-bit n: the size goes to localloc as a native int,
; with no conversion to int32 in between.
; IL-LABEL: .method {{.*}}'vla'(
; IL-SAME:  native int) cil managed{{$}}
; IL-NOT:   {{conv\.(i4|u4|ovf)}}
; IL:       {{^}}  localloc{{$}}
; IL-NOT:   {{conv\.(i4|u4|ovf)}}
; IL:       {{^}}  call void 'use'(native int){{$}}
; IL:       {{^}}} // end of method 'vla'{{$}}
; IR-LABEL: define void @vla(
; IR:       alloca i8, i64 %n, align 1
define void @vla(i64 %n) {
  %p = alloca i8, i64 %n, align 1
  call void @use(ptr %p)
  ret void
}

; An element count of another type and a 16-aligned element: the size is
; computed in native int, localloc over-allocates, and the result is masked
; with -16.
; IL-LABEL: .method {{.*}}'vla_i32_align16'(
; IL-SAME:  int32) cil managed{{$}}
; IL:       {{^}}  localloc{{$}}
; IL:       {{^}}  ldc.i4.s -16{{$}}
; IL:       {{^}}  and{{$}}
; IL:       {{^}}  call void 'use'(native int){{$}}
; IL:       {{^}}} // end of method 'vla_i32_align16'{{$}}
; IR-LABEL: define void @vla_i32_align16(
; IR:       alloca double, i32 %n, align 16
define void @vla_i32_align16(i32 %n) {
  %p = alloca double, i32 %n, align 16
  call void @use(ptr %p)
  ret void
}

; A 32-byte alignment masks with -32.
; IL-LABEL: .method {{.*}}'vla_align32'(
; IL-SAME:  native int) cil managed{{$}}
; IL:       {{^}}  localloc{{$}}
; IL:       {{^}}  ldc.i4.s -32{{$}}
; IL:       {{^}}  and{{$}}
; IL:       {{^}}  call void 'use'(native int){{$}}
; IL:       {{^}}} // end of method 'vla_align32'{{$}}
define void @vla_align32(i64 %n) {
  %p = alloca i8, i64 %n, align 32
  call void @use(ptr %p)
  ret void
}

; An alignment of 8 or less needs no mask of the result. The size is still
; rounded up to the stack alignment (an 'and' with -16 before the localloc);
; only the address that localloc returns goes unmasked.
; IL-LABEL: .method {{.*}}'vla_align8'(
; IL-SAME:  native int) cil managed{{$}}
; IL:       {{^}}  localloc{{$}}
; IL-NOT:   {{^  and$}}
; IL:       {{^}}  call void 'use'(native int){{$}}
; IL:       {{^}}} // end of method 'vla_align8'{{$}}
define void @vla_align8(i64 %n) {
  %p = alloca i64, i64 %n, align 8
  call void @use(ptr %p)
  ret void
}

; A size that the program rounded itself, (n + 15) & -16 (review r1 finding
; 3). The IL never allocates less than the alloca's size: the 'and' with -16
; stays before the localloc (with an alignment of 16 the over-allocation is
; added to the rounded size), so the generic rounding to the stack alignment
; must not fold the program's mask away, at -O2 included. The over-allocation
; of 16 - 8 bytes is added to a multiple of 16, so it reaches the IL as 'or 8'
; right before the localloc.
; IL-LABEL: .method {{.*}}'vla_rounded16'(
; IL-SAME:  native int) cil managed{{$}}
; IL-NOT:   localloc
; IL:       {{^  (ldc\.i4\.s|ldc\.i4|ldc\.i8) -16$}}
; IL-NOT:   localloc
; IL:       {{^}}  and{{$}}
; IL-NEXT:  {{^}}  ldc.i4.8{{$}}
; IL-NEXT:  {{^}}  conv.i{{$}}
; IL-NEXT:  {{^}}  or{{$}}
; IL-NEXT:  {{^}}  localloc{{$}}
; IL:       {{^}}  call void 'use'(native int){{$}}
; IL:       {{^}}} // end of method 'vla_rounded16'{{$}}
define void @vla_rounded16(i64 %n) {
  %a = add i64 %n, 15
  %r = and i64 %a, -16
  %p = alloca i8, i64 %r, align 16
  call void @use(ptr %p)
  ret void
}

; The same size with an alignment of 8 (no over-allocation, no result mask).
; IL-LABEL: .method {{.*}}'vla_rounded8'(
; IL-SAME:  native int) cil managed{{$}}
; IL-NOT:   localloc
; IL:       {{^  (ldc\.i4\.s|ldc\.i4|ldc\.i8) -16$}}
; IL-NOT:   localloc
; IL:       {{^}}  and{{$}}
; IL:       {{^}}  localloc{{$}}
; IL:       {{^}}  call void 'use'(native int){{$}}
; IL:       {{^}}} // end of method 'vla_rounded8'{{$}}
define void @vla_rounded8(i64 %n) {
  %a = add i64 %n, 15
  %r = and i64 %a, -16
  %p = alloca i8, i64 %r, align 8
  call void @use(ptr %p)
  ret void
}

; A size that can be 0, without realignment (review r1 finding 4). An alloca
; yields a valid non-null pointer even for 0 bytes, but CIL's 'localloc 0'
; may return null, so the IL never allocates 0: at an alignment of 8 or less
; the size, rounded up to 16 and still 0 when n is 0, is ORed with 1 right
; before the localloc (decision D13 as amended, 5.15.8 H0).
; IL-LABEL: .method {{.*}}'vla_maybe_zero1'(
; IL-SAME:  native int) cil managed{{$}}
; IL-NOT:   localloc
; IL:       {{^}}  ldc.i4.1{{$}}
; IL-NEXT:  {{^}}  conv.i{{$}}
; IL-NEXT:  {{^}}  or{{$}}
; IL-NEXT:  {{^}}  localloc{{$}}
; IL:       {{^}}  call void 'use'(native int){{$}}
; IL:       {{^}}} // end of method 'vla_maybe_zero1'{{$}}
define void @vla_maybe_zero1(i64 %n) {
  %p = alloca i8, i64 %n, align 1
  call void @use(ptr %p)
  ret void
}

; IL-LABEL: .method {{.*}}'vla_maybe_zero8'(
; IL-SAME:  native int) cil managed{{$}}
; IL-NOT:   localloc
; IL:       {{^}}  ldc.i4.1{{$}}
; IL-NEXT:  {{^}}  conv.i{{$}}
; IL-NEXT:  {{^}}  or{{$}}
; IL-NEXT:  {{^}}  localloc{{$}}
; IL:       {{^}}  call void 'use'(native int){{$}}
; IL:       {{^}}} // end of method 'vla_maybe_zero8'{{$}}
define void @vla_maybe_zero8(i64 %n) {
  %p = alloca i8, i64 %n, align 8
  call void @use(ptr %p)
  ret void
}

; A constant-size alloca outside the entry block is dynamic, and acyclic.
; IL-LABEL: .method {{.*}}'alloca_in_block'(
; IL-SAME:  int32) cil managed{{$}}
; IL:       {{^}}  localloc{{$}}
; IL:       {{^}}  call void 'use'(native int){{$}}
; IL:       {{^}}} // end of method 'alloca_in_block'{{$}}
; IR-LABEL: define void @alloca_in_block(
; IR:       then:
; IR-NEXT:  alloca i32, align 4
define void @alloca_in_block(i32 %c) {
entry:
  %t = icmp ne i32 %c, 0
  br i1 %t, label %then, label %exit
then:
  %p = alloca i32, align 4
  call void @use(ptr %p)
  br label %exit
exit:
  ret void
}

; clang's VLA in a block scope: stacksave, the dynamic alloca, stackrestore.
; The save and the restore are deleted; the alloca stays.
; IL-LABEL: .method {{.*}}'vla_scope'(
; IL-SAME:  native int) cil managed{{$}}
; IL:       {{^}}  localloc{{$}}
; IL:       {{^}}  call void 'use'(native int){{$}}
; IL:       {{^}}} // end of method 'vla_scope'{{$}}
; IR-LABEL: define void @vla_scope(
; IR:       alloca i32, i64 %n, align 4
define void @vla_scope(i64 %n) {
  %s = call ptr @llvm.stacksave.p0()
  %p = alloca i32, i64 %n, align 4
  call void @use(ptr %p)
  call void @llvm.stackrestore.p0(ptr %s)
  ret void
}

; Two VLAs one after the other, each in its own scope (no cycle): two
; mid-function localloc instructions.
; IL-LABEL: .method {{.*}}'two_scopes'(
; IL-SAME:  native int, native int) cil managed{{$}}
; IL:       {{^}}  localloc{{$}}
; IL:       {{^}}  call void 'use'(native int){{$}}
; IL:       {{^}}  localloc{{$}}
; IL:       {{^}}  call void 'use'(native int){{$}}
; IL:       {{^}}} // end of method 'two_scopes'{{$}}
define void @two_scopes(i64 %n, i64 %m) {
  %s1 = call ptr @llvm.stacksave.p0()
  %p = alloca i8, i64 %n, align 1
  call void @use(ptr %p)
  call void @llvm.stackrestore.p0(ptr %s1)
  %s2 = call ptr @llvm.stacksave.p0()
  %q = alloca i8, i64 %m, align 1
  call void @use(ptr %q)
  call void @llvm.stackrestore.p0(ptr %s2)
  ret void
}

; A static frame next to a dynamic alloca: the prologue's localloc of the
; frame, then the dynamic one.
; IL-LABEL: .method {{.*}}'static_and_vla'(
; IL-SAME:  native int) cil managed{{$}}
; IL:       {{^}}  localloc{{$}}
; IL:       {{^}}  call void 'use'(native int){{$}}
; IL:       {{^}}  localloc{{$}}
; IL:       {{^}}  call void 'use'(native int){{$}}
; IL:       {{^}}} // end of method 'static_and_vla'{{$}}
define void @static_and_vla(i64 %n) {
  %x = alloca [16 x i8], align 8
  call void @use(ptr %x)
  %p = alloca i8, i64 %n, align 1
  call void @use(ptr %p)
  ret void
}

; A lone stacksave and a lone stackrestore in functions without a dynamic
; alloca are deleted too: no diagnostic (they were ISel errors before M26).
; A saved value that anything other than a stackrestore reads becomes null
; (MSILLowerDynamicAlloca), so save_only returns a null pointer.
; IL-LABEL: .method {{.*}}'save_only'() cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 1{{$}}
; IL-NEXT:  {{^}}  ldc.i4.0{{$}}
; IL-NEXT:  {{^}}  conv.i{{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'save_only'{{$}}
; IL-LABEL: .method {{.*}}'restore_only'(native int) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 0{{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'restore_only'{{$}}
; IR-LABEL: define ptr @save_only(
; IR-NEXT:  {{^ *}}ret ptr null{{$}}
; IR-NEXT:  {{^ *}}}{{$}}
; IR-LABEL: define void @restore_only(
; IR-NEXT:  {{^ *}}ret void{{$}}
; IR-NEXT:  {{^ *}}}{{$}}
define ptr @save_only() {
  %s = call ptr @llvm.stacksave.p0()
  ret ptr %s
}

define void @restore_only(ptr %s) {
  call void @llvm.stackrestore.p0(ptr %s)
  ret void
}

; A branch around a VLA and a merge after it: the alloca's block is on no
; cycle.
; IL-LABEL: .method {{.*}}'vla_diamond'(
; IL-SAME:  native int, int32) cil managed{{$}}
; IL:       {{^}}  localloc{{$}}
; IL:       {{^}}} // end of method 'vla_diamond'{{$}}
define void @vla_diamond(i64 %n, i32 %c) {
entry:
  %t = icmp ne i32 %c, 0
  br i1 %t, label %big, label %join
big:
  %s = call ptr @llvm.stacksave.p0()
  %p = alloca i8, i64 %n, align 16
  call void @use(ptr %p)
  call void @llvm.stackrestore.p0(ptr %s)
  br label %join
join:
  ret void
}

declare ptr @llvm.stacksave.p0()
declare void @llvm.stackrestore.p0(ptr)

;--- cyclic.ll
source_filename = "cyclic.c"

define void @use(ptr %p) {
  ret void
}

; for (i = 0; i < k; i++) { char b[n]; use(b); } as clang emits it: the
; scope's save and restore around the alloca, inside the loop. One error.
; CYC: error: {{.*}}in function vla_loop void (i64, i32): a dynamic stack allocation that can repeat within one call (a VLA or alloca in a loop) is not supported for MSIL yet{{$}}
define void @vla_loop(i64 %n, i32 %k) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %s = call ptr @llvm.stacksave.p0()
  %p = alloca i8, i64 %n, align 1
  call void @use(ptr %p)
  call void @llvm.stackrestore.p0(ptr %s)
  %i.next = add i32 %i, 1
  %done = icmp sge i32 %i.next, %k
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

; A constant-size alloca in a loop (alloca(16)): outside the entry block, on a
; cycle.
; CYC: error: {{.*}}in function alloca_loop void (i32): a dynamic stack allocation that can repeat within one call (a VLA or alloca in a loop) is not supported for MSIL yet{{$}}
define void @alloca_loop(i32 %k) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %p = alloca [16 x i8], align 8
  call void @use(ptr %p)
  %i.next = add i32 %i, 1
  %done = icmp sge i32 %i.next, %k
  br i1 %done, label %exit, label %loop
exit:
  ret void
}

; An irreducible cycle with two entries (entry branches to %a or to %b, %a
; allocates and branches to %b, %b branches back to %a or exits): %a is on a
; cycle that has no loop header, so LoopInfo finds no loop here and only
; CycleInfo reports it. One error.
; CYC: error: {{.*}}in function irreducible void (i64, i1): a dynamic stack allocation that can repeat within one call (a VLA or alloca in a loop) is not supported for MSIL yet{{$}}
define void @irreducible(i64 %n, i1 %c) {
entry:
  br i1 %c, label %a, label %b
a:
  %p = alloca i8, i64 %n, align 1
  call void @use(ptr %p)
  br label %b
b:
  br i1 %c, label %a, label %exit
exit:
  ret void
}

; The functions after the diagnosed ones compile, an acyclic VLA included.
; CYC-IL: .method {{.*}}'after_vla'(native int) cil managed{{$}}
; CYC-IL: {{^}}  localloc{{$}}
; CYC-IL: {{^}}} // end of method 'after_vla'{{$}}
define void @after_vla(i64 %n) {
  %p = alloca i8, i64 %n, align 1
  call void @use(ptr %p)
  ret void
}

; CYC-IL-LABEL: .method {{.*}}'after'(
; CYC-IL-SAME:  int32) cil managed{{$}}
; CYC-IL-NEXT:  {{^\{$}}
; CYC-IL-NEXT:  {{^}}  .maxstack 2{{$}}
; CYC-IL-NEXT:  {{^}}  ldarg.0{{$}}
; CYC-IL-NEXT:  {{^}}  ldc.i4.1{{$}}
; CYC-IL-NEXT:  {{^}}  add{{$}}
; CYC-IL-NEXT:  {{^}}  ret{{$}}
; CYC-IL-NEXT:  {{^}}} // end of method 'after'{{$}}
define i32 @after(i32 %a) {
  %r = add i32 %a, 1
  ret i32 %r
}

declare ptr @llvm.stacksave.p0()
declare void @llvm.stackrestore.p0(ptr)

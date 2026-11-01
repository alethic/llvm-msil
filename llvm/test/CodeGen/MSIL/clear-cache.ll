; llvm.clear_cache emits nothing (ImplementationPlan.md M25.c (4), plan
; section 6 M25 Goal and exit criteria; brief 3.3). CIL code has no
; instruction cache to flush, so MSILTargetLowering expands ISD::CLEAR_CACHE,
; as X86 does, to its input chain: no call of compiler-rt's __clear_cache,
; which the default action (LibCall) would ask for and which no helper archive
; defines (msil/test/M3/03-helpers/archive-contents.test). Before M25 the call
; was the runtime-helper error in text output and an UNDEFINED '__clear_cache'
; symbol in an object.
; cc.ll calls llvm.clear_cache (__builtin___clear_cache) alone and between
; other code whose end pointer only the call uses; ref.ll is the same module
; without the calls. On both x86_64 triples, at -O2 and -O0, llc prints no
; message, and the whole-program text (-S), the object-form text and the
; object (-filetype=obj, its link info and its canonical dump) of cc.ll equal
; ref.ll's: no __clear_cache anywhere, no call and no P/Invoke. The linked
; text (the "msil-linked" flag, cc-linked.ll) has the same methods, and the
; whole-program text assembles.

; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{llc} = llc -verify-machineinstrs -asm-verbose=false
; DEFINE: %{check} = FileCheck %s --implicit-check-not=__clear_cache --implicit-check-not=pinvokeimpl \
; DEFINE:   --implicit-check-not="{{^ +call}}"

; Whole-program text.
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/cc.ll -o %t/cc.lnx.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/cc.lnx.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/ref.ll -o %t/ref.lnx.il 2>&1 | count 0
; RUN: diff %t/cc.lnx.il %t/ref.lnx.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/cc.ll -o %t/cc.lnx.o0.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/cc.lnx.o0.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/ref.ll -o %t/ref.lnx.o0.il 2>&1 | count 0
; RUN: diff %t/cc.lnx.o0.il %t/ref.lnx.o0.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/cc.ll -o %t/cc.win.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/cc.win.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/ref.ll -o %t/ref.win.il 2>&1 | count 0
; RUN: diff %t/cc.win.il %t/ref.win.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/cc.ll -o %t/cc.win.o0.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/cc.win.o0.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/ref.ll -o %t/ref.win.o0.il 2>&1 | count 0
; RUN: diff %t/cc.win.o0.il %t/ref.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/cc.lnx.dll %t/cc.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/cc.win.o0.dll %t/cc.win.o0.il %}

; Linked text.
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/cc-linked.ll -o %t/cc-linked.lnx.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/cc-linked.lnx.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/cc-linked.ll -o %t/cc-linked.win.o0.il 2>&1 \
; RUN:   | count 0
; RUN: %{check} --input-file=%t/cc-linked.win.o0.il

; Object-form text.
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form < %t/cc.ll -o %t/cc.lnx.of.il 2>&1 | count 0
; RUN: %{check} --input-file=%t/cc.lnx.of.il
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form < %t/ref.ll -o %t/ref.lnx.of.il 2>&1 \
; RUN:   | count 0
; RUN: diff %t/cc.lnx.of.il %t/ref.lnx.of.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 -msil-object-form < %t/cc.ll -o %t/cc.win.o0.of.il 2>&1 \
; RUN:   | count 0
; RUN: %{check} --input-file=%t/cc.win.o0.of.il

; Objects.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/cc.ll -o %t/cc.lnx.o 2>&1 \
; RUN:   | count 0
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/ref.ll -o %t/ref.lnx.o 2>&1 \
; RUN:   | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -filetype=obj -verify-machineinstrs < %t/cc.ll \
; RUN:   -o %t/cc.win.o0.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -filetype=obj -verify-machineinstrs < %t/ref.ll \
; RUN:   -o %t/ref.win.o0.o 2>&1 | count 0
; DEFINE: %{obj} = FileCheck %s --implicit-check-not=__clear_cache --implicit-check-not=UNDEFINED \
; DEFINE:   --implicit-check-not="{{L[0-9]+: call}}"
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/cc.lnx.o | %{obj} --check-prefixes=LINK,LINK-LNX %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/cc.win.o0.o | %{obj} --check-prefixes=LINK,LINK-WIN %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --canonical %t/cc.lnx.o > %t/cc.lnx.canon %}
; RUN: %if llvm-cil-dump %{ %{obj} --check-prefix=CANON --input-file=%t/cc.lnx.canon %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --canonical %t/ref.lnx.o > %t/ref.lnx.canon %}
; RUN: %if llvm-cil-dump %{ diff %t/cc.lnx.canon %t/ref.lnx.canon %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --canonical %t/cc.win.o0.o > %t/cc.win.o0.canon %}
; RUN: %if llvm-cil-dump %{ %{obj} --check-prefix=CANON --input-file=%t/cc.win.o0.canon %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --canonical %t/ref.win.o0.o > %t/ref.win.o0.canon %}
; RUN: %if llvm-cil-dump %{ diff %t/cc.win.o0.canon %t/ref.win.o0.canon %}

; CHECK-LABEL: {{^}}.method public static void 'flush'(native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'flush'{{$}}
; CHECK-LABEL: {{^}}.method public static int32 'flush_then_add'(native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'flush_then_add'{{$}}
; CHECK-NOT:   {{^}}.method

; LINK-LNX:  {{^}}LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil {{.*}}
; LINK-WIN:  {{^}}LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil {{.*}}
; LINK-NEXT: {{^}}Symbol 0 FUNCTION GLOBAL DEFAULT 'flush' method=0x06000001{{$}}
; LINK-NEXT: {{^}}Symbol 1 FUNCTION GLOBAL DEFAULT 'flush_then_add' method=0x06000002{{$}}
; LINK-NOT:  {{^}}Symbol

; CANON:      {{^}}  .method 'flush' void(native int, native int) flags=0x0016 impl=0x0000{{$}}
; CANON-NEXT: {{^}}    .maxstack 0{{$}}
; CANON-NEXT: {{^}}    L0: ret{{$}}
; CANON-NEXT: {{^}}  .method 'flush_then_add' int32(native int, int32) flags=0x0016 impl=0x0000{{$}}

;--- cc.ll
source_filename = "clear-cache.c"

declare void @llvm.clear_cache(ptr, ptr)

define void @flush(ptr %b, ptr %e) {
  call void @llvm.clear_cache(ptr %b, ptr %e)
  ret void
}

define i32 @flush_then_add(ptr %b, i32 %n) {
  %e = getelementptr i8, ptr %b, i32 %n
  call void @llvm.clear_cache(ptr %b, ptr %e)
  %r = add i32 %n, 1
  ret i32 %r
}

;--- ref.ll
source_filename = "clear-cache.c"

define void @flush(ptr %b, ptr %e) {
  ret void
}

define i32 @flush_then_add(ptr %b, i32 %n) {
  %r = add i32 %n, 1
  ret i32 %r
}

;--- cc-linked.ll
source_filename = "ld-temp.o"

declare void @llvm.clear_cache(ptr, ptr)

define void @flush(ptr %b, ptr %e) {
  call void @llvm.clear_cache(ptr %b, ptr %e)
  ret void
}

define i32 @flush_then_add(ptr %b, i32 %n) {
  %e = getelementptr i8, ptr %b, i32 %n
  call void @llvm.clear_cache(ptr %b, ptr %e)
  %r = add i32 %n, 1
  ret i32 %r
}

!llvm.module.flags = !{!0}
!0 = !{i32 4, !"msil-linked", i32 1}

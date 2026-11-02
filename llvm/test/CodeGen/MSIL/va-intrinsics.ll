; va_end, va_copy and the va_arg instruction (ImplementationPlan.md 5.15.2
; 'Compile time', M26; decision D19; Design.md 3 and 4.7; plan section 2
; 'Diagnostics leave well-formed output'). A va_list parameter is legal C (a
; vprintf-style helper), and clang lowers va_arg inline for the native
; va_list layout of every triple, so it emits llvm.va_copy and llvm.va_end but
; never a va_arg instruction.
;
; copy.ll: llvm.va_end does nothing on any native ABI, and llvm.va_copy copies
; the native va_list (ExpandVariadics, slot P7, lowers both in every
; function): the 24-byte SysV x86-64 __va_list_tag on
; x86_64-unknown-linux-gnu-msil, the 32-byte AAPCS64 structure on
; aarch64-unknown-linux-gnu-msil, and one pointer on
; x86_64-pc-windows-msvc-msil and aarch64-pc-windows-msvc-msil, where va_list
; is a char*. Nothing is reported at -O0 and -O2, nothing is called (a short
; constant copy is a cpblk or inline loads and stores, memintrinsics.ll), and
; every module assembles. The IR after ExpandVariadics pins va_copy's length
; per ABI (CPIR): a memcpy of 24 bytes (SysV), 32 (AAPCS64) or 8 (both
; windows-msvc triples), and no va_end left.
;
; vaarg.ll: hand-written IR with a va_arg instruction. On the SysV and AAPCS64
; triples MSILPrepareIR reports each one, naming its function, whatever the
; type (legal, promoted, expanded, soft-promoted or vector), with 'the va_arg
; instruction is not supported for MSIL on this triple; clang lowers C's
; va_arg itself', and deletes the function's body (no method); llc exits with
; 1 without 'LLVM ERROR' or a crash report, the function after them compiles,
; and the IL on stdout assembles. On the char* triples the instruction is
; accepted and expanded (va V8): 8-byte slots read through the va_list
; pointer, and on x86_64-pc-windows-msvc-msil a value above 8 bytes (i128,
; <4 x i32>) is passed indirectly, so its slot holds a pointer that is loaded
; first. Nothing is reported and the module assembles.
;
; mixed.ll: a variadic definition that uses va_start, va_copy and va_end, next
; to a non-variadic va_copy and va_end user. Since M26 both compile on every
; triple: the definition's method takes its fixed parameter and the va_list
; pointer, (int32, native int).

; DEFINE: %{llc} = llc -asm-verbose=false -verify-machineinstrs
; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; RUN: split-file %s %t

; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/copy.ll 2>&1 \
; RUN:   | %{diag} --check-prefix=COPY
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O2 < %t/copy.ll 2>&1 \
; RUN:   | %{diag} --check-prefix=COPY
; RUN: %{llc} -mtriple=aarch64-unknown-linux-gnu-msil -O0 < %t/copy.ll 2>&1 \
; RUN:   | %{diag} --check-prefix=COPY
; RUN: %{llc} -mtriple=aarch64-unknown-linux-gnu-msil -O2 < %t/copy.ll 2>&1 \
; RUN:   | %{diag} --check-prefix=COPY
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/copy.ll 2>&1 \
; RUN:   | %{diag} --check-prefix=COPY
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O2 < %t/copy.ll 2>&1 \
; RUN:   | %{diag} --check-prefix=COPY
; RUN: %{llc} -mtriple=aarch64-pc-windows-msvc-msil -O0 < %t/copy.ll 2>&1 \
; RUN:   | %{diag} --check-prefix=COPY
; RUN: %{llc} -mtriple=aarch64-pc-windows-msvc-msil -O2 < %t/copy.ll 2>&1 \
; RUN:   | %{diag} --check-prefix=COPY
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=expand-variadics < %t/copy.ll -o - \
; RUN:   | FileCheck %s --check-prefixes=CPIR,CPIR-SYSV --implicit-check-not=llvm.va_end --implicit-check-not=llvm.va_copy
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -stop-after=expand-variadics < %t/copy.ll -o - \
; RUN:   | FileCheck %s --check-prefixes=CPIR,CPIR-AAPCS --implicit-check-not=llvm.va_end --implicit-check-not=llvm.va_copy
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -stop-after=expand-variadics < %t/copy.ll -o - \
; RUN:   | FileCheck %s --check-prefixes=CPIR,CPIR-WIN --implicit-check-not=llvm.va_end --implicit-check-not=llvm.va_copy
; RUN: llc -mtriple=aarch64-pc-windows-msvc-msil -stop-after=expand-variadics < %t/copy.ll -o - \
; RUN:   | FileCheck %s --check-prefixes=CPIR,CPIR-WIN --implicit-check-not=llvm.va_end --implicit-check-not=llvm.va_copy
; RUN: %if ilasm %{ %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/copy.ll -o %t/copy.x64-gnu.o0.il \
; RUN:   && %ilasm-verify -output=%t/copy.x64-gnu.o0.dll %t/copy.x64-gnu.o0.il %}
; RUN: %if ilasm %{ %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O2 < %t/copy.ll -o %t/copy.x64-gnu.o2.il \
; RUN:   && %ilasm-verify -output=%t/copy.x64-gnu.o2.dll %t/copy.x64-gnu.o2.il %}
; RUN: %if ilasm %{ %{llc} -mtriple=aarch64-unknown-linux-gnu-msil -O0 < %t/copy.ll -o %t/copy.a64-gnu.o0.il \
; RUN:   && %ilasm-verify -arm64 -output=%t/copy.a64-gnu.o0.dll %t/copy.a64-gnu.o0.il %}
; RUN: %if ilasm %{ %{llc} -mtriple=aarch64-unknown-linux-gnu-msil -O2 < %t/copy.ll -o %t/copy.a64-gnu.o2.il \
; RUN:   && %ilasm-verify -arm64 -output=%t/copy.a64-gnu.o2.dll %t/copy.a64-gnu.o2.il %}
; RUN: %if ilasm %{ %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/copy.ll -o %t/copy.x64-msvc.o0.il \
; RUN:   && %ilasm-verify -output=%t/copy.x64-msvc.o0.dll %t/copy.x64-msvc.o0.il %}
; RUN: %if ilasm %{ %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O2 < %t/copy.ll -o %t/copy.x64-msvc.o2.il \
; RUN:   && %ilasm-verify -output=%t/copy.x64-msvc.o2.dll %t/copy.x64-msvc.o2.il %}
; RUN: %if ilasm %{ %{llc} -mtriple=aarch64-pc-windows-msvc-msil -O0 < %t/copy.ll -o %t/copy.a64-msvc.o0.il \
; RUN:   && %ilasm-verify -arm64 -output=%t/copy.a64-msvc.o0.dll %t/copy.a64-msvc.o0.il %}
; RUN: %if ilasm %{ %{llc} -mtriple=aarch64-pc-windows-msvc-msil -O2 < %t/copy.ll -o %t/copy.a64-msvc.o2.il \
; RUN:   && %ilasm-verify -arm64 -output=%t/copy.a64-msvc.o2.dll %t/copy.a64-msvc.o2.il %}

; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/vaarg.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VAARG
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/vaarg.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VAARG
; RUN: not %{llc} -mtriple=aarch64-unknown-linux-gnu-msil < %t/vaarg.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VAARG
; RUN: not %{llc} -mtriple=aarch64-unknown-linux-gnu-msil -O0 < %t/vaarg.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VAARG
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/vaarg.ll > %t/vaarg.x64-gnu.il
; RUN: FileCheck %s --check-prefix=VAARG-IL --input-file=%t/vaarg.x64-gnu.il --implicit-check-not="'arg_"
; RUN: not %{llc} -mtriple=aarch64-unknown-linux-gnu-msil -O0 < %t/vaarg.ll > %t/vaarg.a64-gnu.o0.il
; RUN: FileCheck %s --check-prefix=VAARG-IL --input-file=%t/vaarg.a64-gnu.o0.il --implicit-check-not="'arg_"
; RUN: %if ilasm %{ %ilasm-verify -output=%t/vaarg.x64-gnu.dll %t/vaarg.x64-gnu.il %}
; RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t/vaarg.a64-gnu.o0.dll %t/vaarg.a64-gnu.o0.il %}
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/vaarg.ll -o %t/vaarg.x64-msvc.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefixes=WIN-IL,X64WIN-IL --implicit-check-not=ldind.i8 --input-file=%t/vaarg.x64-msvc.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/vaarg.ll -o %t/vaarg.x64-msvc.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefixes=WIN-IL,X64WIN-IL --implicit-check-not=ldind.i8 --input-file=%t/vaarg.x64-msvc.o0.il
; RUN: %{llc} -mtriple=aarch64-pc-windows-msvc-msil -O0 < %t/vaarg.ll -o %t/vaarg.a64-msvc.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=WIN-IL --input-file=%t/vaarg.a64-msvc.o0.il
; RUN: %{llc} -mtriple=aarch64-pc-windows-msvc-msil < %t/vaarg.ll -o %t/vaarg.a64-msvc.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=WIN-IL --input-file=%t/vaarg.a64-msvc.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/vaarg.x64-msvc.dll %t/vaarg.x64-msvc.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/vaarg.x64-msvc.o0.dll %t/vaarg.x64-msvc.o0.il %}
; RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t/vaarg.a64-msvc.o0.dll %t/vaarg.a64-msvc.o0.il %}

; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/mixed.ll -o %t/mixed.x64-gnu.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=MIXED-IL --input-file=%t/mixed.x64-gnu.il
; RUN: %{llc} -mtriple=aarch64-unknown-linux-gnu-msil -O0 < %t/mixed.ll -o %t/mixed.a64-gnu.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=MIXED-IL --input-file=%t/mixed.a64-gnu.il
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/mixed.ll -o %t/mixed.x64-msvc.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=MIXED-IL --input-file=%t/mixed.x64-msvc.il
; RUN: %{llc} -mtriple=aarch64-pc-windows-msvc-msil < %t/mixed.ll -o %t/mixed.a64-msvc.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=MIXED-IL --input-file=%t/mixed.a64-msvc.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/mixed.x64-gnu.dll %t/mixed.x64-gnu.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/mixed.x64-msvc.dll %t/mixed.x64-msvc.il %}
; RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t/mixed.a64-msvc.dll %t/mixed.a64-msvc.il %}

;--- copy.ll
; COPY-LABEL:   .method {{.*}}'end_only'(
; COPY-SAME:    native int) cil managed{{$}}
; COPY-NEXT:    {{^\{$}}
; COPY-NEXT:    {{^}}  .maxstack 0{{$}}
; COPY-NEXT:    {{^}}  ret{{$}}
; COPY-NEXT:    {{^}}} // end of method 'end_only'{{$}}

; COPY-LABEL:   .method {{.*}}'copy_end'(
; COPY-SAME:    native int, native int) cil managed{{$}}
; COPY-NEXT:    {{^\{$}}
; COPY-NOT:     call
; COPY:         {{^}}  ldarg.{{[01]}}{{$}}
; COPY-NOT:     call
; COPY:         {{^}}  ret{{$}}
; COPY-NEXT:    {{^}}} // end of method 'copy_end'{{$}}
; CPIR-LABEL:  define void @end_only(ptr %ap)
; CPIR-NEXT:   ret void
; CPIR-LABEL:  define void @copy_end(ptr %dst, ptr %src)
; CPIR-SYSV-NEXT:  call void @llvm.memcpy.p0.p0.i32(ptr %dst, ptr %src, i32 24, i1 false)
; CPIR-AAPCS-NEXT: call void @llvm.memcpy.p0.p0.i32(ptr %dst, ptr %src, i32 32, i1 false)
; CPIR-WIN-NEXT:   call void @llvm.memcpy.p0.p0.i32(ptr %dst, ptr %src, i32 8, i1 false)
; CPIR-NEXT:   ret void
source_filename = "copy.c"

; void end_only(va_list ap) { va_end(ap); }
define void @end_only(ptr %ap) {
  call void @llvm.va_end.p0(ptr %ap)
  ret void
}

; va_copy into the caller's va_list, then va_end of the copy.
define void @copy_end(ptr %dst, ptr %src) {
  call void @llvm.va_copy.p0(ptr %dst, ptr %src)
  call void @llvm.va_end.p0(ptr %dst)
  ret void
}

declare void @llvm.va_copy.p0(ptr, ptr)
declare void @llvm.va_end.p0(ptr)

;--- vaarg.ll
; VAARG: {{^}}error: {{.*}}in function arg_i32 {{.*}}: the va_arg instruction is not supported for MSIL on this triple; clang lowers C's va_arg itself{{$}}
; VAARG: {{^}}error: {{.*}}in function arg_ptr {{.*}}: the va_arg instruction is not supported for MSIL on this triple; clang lowers C's va_arg itself{{$}}
; VAARG: {{^}}error: {{.*}}in function arg_double {{.*}}: the va_arg instruction is not supported for MSIL on this triple; clang lowers C's va_arg itself{{$}}
; VAARG: {{^}}error: {{.*}}in function arg_i8 {{.*}}: the va_arg instruction is not supported for MSIL on this triple; clang lowers C's va_arg itself{{$}}
; VAARG: {{^}}error: {{.*}}in function arg_i128 {{.*}}: the va_arg instruction is not supported for MSIL on this triple; clang lowers C's va_arg itself{{$}}
; VAARG: {{^}}error: {{.*}}in function arg_half {{.*}}: the va_arg instruction is not supported for MSIL on this triple; clang lowers C's va_arg itself{{$}}
; VAARG: {{^}}error: {{.*}}in function arg_vector {{.*}}: the va_arg instruction is not supported for MSIL on this triple; clang lowers C's va_arg itself{{$}}

; SysV and AAPCS64: only after_vaarg has a method.
; VAARG-IL-LABEL: .method {{.*}}'after_vaarg'(
; VAARG-IL-SAME:  int32, int32) cil managed{{$}}
; VAARG-IL:       {{^}}  ldarg.0{{$}}
; VAARG-IL-NEXT:  {{^}}  ldarg.1{{$}}
; VAARG-IL-NEXT:  {{^}}  add{{$}}
; VAARG-IL-NEXT:  {{^}}  ret{{$}}

; char* triples: every function compiles; arg_i32 reads the va_list pointer
; and then the int32 in its slot, and on x86-64 Windows arg_i128 and
; arg_vector load the slot's pointer before the value.
; WIN-IL-LABEL:   .method {{.*}}'arg_i32'(
; WIN-IL-SAME:    native int) cil managed{{$}}
; WIN-IL:         {{^}}  ldind.i{{$}}
; WIN-IL:         {{^}}  ldind.i4{{$}}
; WIN-IL:         {{^}}} // end of method 'arg_i32'{{$}}
; WIN-IL-LABEL:   .method {{.*}}'arg_ptr'(
; WIN-IL-SAME:    native int) cil managed{{$}}
; WIN-IL-LABEL:   .method {{.*}}'arg_double'(
; WIN-IL-SAME:    native int) cil managed{{$}}
; WIN-IL-LABEL:   .method {{.*}}'arg_i8'(
; WIN-IL-SAME:    native int) cil managed{{$}}
; WIN-IL-LABEL:   .method {{.*}}'arg_i128'(
; WIN-IL-SAME:    native int, native int) cil managed{{$}}
; X64WIN-IL:      {{^}}  ldind.i{{$}}
; X64WIN-IL:      {{^}}  ldind.i{{$}}
; X64WIN-IL:      {{^}}  ldind.i{{$}}
; WIN-IL:         {{^}}} // end of method 'arg_i128'{{$}}
; WIN-IL-LABEL:   .method {{.*}}'arg_half'(
; WIN-IL-SAME:    native int, native int) cil managed{{$}}
; WIN-IL-LABEL:   .method {{.*}}'arg_vector'(
; WIN-IL-SAME:    native int, native int) cil managed{{$}}
; X64WIN-IL:      {{^}}  ldind.i{{$}}
; X64WIN-IL:      {{^}}  ldind.i{{$}}
; WIN-IL:         {{^}}} // end of method 'arg_vector'{{$}}
; WIN-IL-LABEL:   .method {{.*}}'after_vaarg'(
; WIN-IL-SAME:    int32, int32) cil managed{{$}}

source_filename = "vaarg.c"

define i32 @arg_i32(ptr %ap) {
  %v = va_arg ptr %ap, i32
  ret i32 %v
}

define ptr @arg_ptr(ptr %ap) {
  %v = va_arg ptr %ap, ptr
  ret ptr %v
}

define double @arg_double(ptr %ap) {
  %v = va_arg ptr %ap, double
  ret double %v
}

define i8 @arg_i8(ptr %ap) {
  %v = va_arg ptr %ap, i8
  ret i8 %v
}

define void @arg_i128(ptr %ap, ptr %out) {
  %v = va_arg ptr %ap, i128
  store i128 %v, ptr %out
  ret void
}

define void @arg_half(ptr %ap, ptr %out) {
  %v = va_arg ptr %ap, half
  store half %v, ptr %out
  ret void
}

define void @arg_vector(ptr %ap, ptr %out) {
  %v = va_arg ptr %ap, <4 x i32>
  store <4 x i32> %v, ptr %out
  ret void
}

define i32 @after_vaarg(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

;--- mixed.ll
; MIXED-IL-LABEL:    .method {{.*}}'vdef'(
; MIXED-IL-SAME:     int32, native int) cil managed{{$}}
; MIXED-IL-NOT:      {{^ +call}}
; MIXED-IL:          {{^}}} // end of method 'vdef'{{$}}
; MIXED-IL-LABEL:    .method {{.*}}'copy_user'(
; MIXED-IL-SAME:     native int, native int) cil managed{{$}}
; MIXED-IL-NOT:      {{^ +call}}
; MIXED-IL:          {{^}}  ret{{$}}
; MIXED-IL-NEXT:     {{^}}} // end of method 'copy_user'{{$}}
source_filename = "mixed.c"

define i32 @vdef(i32 %n, ...) {
  %ap = alloca [32 x i8], align 8
  %cp = alloca [32 x i8], align 8
  call void @llvm.va_start.p0(ptr %ap)
  call void @llvm.va_copy.p0(ptr %cp, ptr %ap)
  call void @llvm.va_end.p0(ptr %cp)
  call void @llvm.va_end.p0(ptr %ap)
  ret i32 %n
}

define void @copy_user(ptr %dst, ptr %src) {
  call void @llvm.va_copy.p0(ptr %dst, ptr %src)
  call void @llvm.va_end.p0(ptr %src)
  ret void
}

declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_copy.p0(ptr, ptr)
declare void @llvm.va_end.p0(ptr)

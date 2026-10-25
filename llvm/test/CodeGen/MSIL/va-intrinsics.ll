; va_end, va_copy and the va_arg instruction (ImplementationPlan.md decision
; D19; Design.md 3 and 4.7; plan section 2 'Diagnostics leave well-formed
; output'). Only variadic definitions and calls are Phase-1 errors: a va_list
; parameter is legal C (a vprintf-style helper), and clang lowers va_arg
; inline for the native va_list layout of every triple, so it emits
; llvm.va_copy and llvm.va_end but never a va_arg instruction.
;
; copy.ll: llvm.va_end does nothing on any native ABI, and llvm.va_copy copies
; the native va_list: a cpblk of the 24-byte SysV x86-64 __va_list_tag on
; x86_64-unknown-linux-gnu-msil, of the 32-byte AAPCS64 structure on
; aarch64-unknown-linux-gnu-msil, and one pointer (ldind.i, stind.i) on
; x86_64-pc-windows-msvc-msil and aarch64-pc-windows-msvc-msil, where va_list
; is a char*. Nothing is reported at -O0 and -O2, and every module assembles.
;
; vaarg.ll: hand-written IR with a va_arg instruction is reported once per
; instruction, naming its function, whatever the type (legal, promoted,
; expanded, soft-promoted or vector), and leaves an undefined value (zero for
; an integer). llc exits with 1 without 'LLVM ERROR' or a crash report, the
; function after them compiles, and the IL on stdout assembles.
;
; mixed.ll: a variadic definition that uses va_start, va_copy and va_end, next
; to a non-variadic va_copy and va_end user. MSILPrepareIR reports the
; definition and deletes its body, so that is the only error, and the user
; compiles as in copy.ll.

; DEFINE: %{llc} = llc -asm-verbose=false -verify-machineinstrs
; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; RUN: split-file %s %t

; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/copy.ll 2>&1 \
; RUN:   | %{diag} --check-prefixes=COPY,STRUCT -DSIZE=24
; RUN: %{llc} -mtriple=x86_64-unknown-linux-gnu-msil -O2 < %t/copy.ll 2>&1 \
; RUN:   | %{diag} --check-prefixes=COPY,STRUCT -DSIZE=24
; RUN: %{llc} -mtriple=aarch64-unknown-linux-gnu-msil -O0 < %t/copy.ll 2>&1 \
; RUN:   | %{diag} --check-prefixes=COPY,STRUCT -DSIZE=32
; RUN: %{llc} -mtriple=aarch64-unknown-linux-gnu-msil -O2 < %t/copy.ll 2>&1 \
; RUN:   | %{diag} --check-prefixes=COPY,STRUCT -DSIZE=32
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/copy.ll 2>&1 \
; RUN:   | %{diag} --check-prefixes=COPY,PTR
; RUN: %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O2 < %t/copy.ll 2>&1 \
; RUN:   | %{diag} --check-prefixes=COPY,PTR
; RUN: %{llc} -mtriple=aarch64-pc-windows-msvc-msil -O0 < %t/copy.ll 2>&1 \
; RUN:   | %{diag} --check-prefixes=COPY,PTR
; RUN: %{llc} -mtriple=aarch64-pc-windows-msvc-msil -O2 < %t/copy.ll 2>&1 \
; RUN:   | %{diag} --check-prefixes=COPY,PTR
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
; RUN: not %{llc} -mtriple=x86_64-pc-windows-msvc-msil < %t/vaarg.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VAARG
; RUN: not %{llc} -mtriple=aarch64-pc-windows-msvc-msil -O0 < %t/vaarg.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VAARG
; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/vaarg.ll > %t/vaarg.x64-gnu.il
; RUN: FileCheck %s --check-prefix=VAARG-IL --input-file=%t/vaarg.x64-gnu.il
; RUN: not %{llc} -mtriple=aarch64-pc-windows-msvc-msil -O0 < %t/vaarg.ll > %t/vaarg.a64-msvc.o0.il
; RUN: FileCheck %s --check-prefix=VAARG-IL --input-file=%t/vaarg.a64-msvc.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/vaarg.x64-gnu.dll %t/vaarg.x64-gnu.il %}
; RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t/vaarg.a64-msvc.o0.dll %t/vaarg.a64-msvc.o0.il %}

; RUN: not %{llc} -mtriple=x86_64-unknown-linux-gnu-msil < %t/mixed.ll > %t/mixed.x64-gnu.il 2> %t/mixed.x64-gnu.err
; RUN: %{diag} --check-prefix=MIXED --input-file=%t/mixed.x64-gnu.err
; RUN: FileCheck %s --check-prefixes=MIXED-IL,MIXED-STRUCT -DSIZE=24 --input-file=%t/mixed.x64-gnu.il
; RUN: not %{llc} -mtriple=aarch64-unknown-linux-gnu-msil -O0 < %t/mixed.ll > %t/mixed.a64-gnu.il 2> %t/mixed.a64-gnu.err
; RUN: %{diag} --check-prefix=MIXED --input-file=%t/mixed.a64-gnu.err
; RUN: FileCheck %s --check-prefixes=MIXED-IL,MIXED-STRUCT -DSIZE=32 --input-file=%t/mixed.a64-gnu.il
; RUN: not %{llc} -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/mixed.ll > %t/mixed.x64-msvc.il 2> %t/mixed.x64-msvc.err
; RUN: %{diag} --check-prefix=MIXED --input-file=%t/mixed.x64-msvc.err
; RUN: FileCheck %s --check-prefixes=MIXED-IL,MIXED-PTR --input-file=%t/mixed.x64-msvc.il
; RUN: not %{llc} -mtriple=aarch64-pc-windows-msvc-msil < %t/mixed.ll > %t/mixed.a64-msvc.il 2> %t/mixed.a64-msvc.err
; RUN: %{diag} --check-prefix=MIXED --input-file=%t/mixed.a64-msvc.err
; RUN: FileCheck %s --check-prefixes=MIXED-IL,MIXED-PTR --input-file=%t/mixed.a64-msvc.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/mixed.x64-gnu.dll %t/mixed.x64-gnu.il %}
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
; STRUCT-NEXT:  {{^}}  .maxstack 3{{$}}
; STRUCT-NEXT:  {{^}}  ldarg.0{{$}}
; STRUCT-NEXT:  {{^}}  ldarg.1{{$}}
; STRUCT-NEXT:  {{^}}  ldc.i4.s [[SIZE]]{{$}}
; STRUCT-NEXT:  {{^}}  cpblk{{$}}
; PTR-NEXT:     {{^}}  .maxstack 2{{$}}
; PTR-NEXT:     {{^}}  ldarg.0{{$}}
; PTR-NEXT:     {{^}}  ldarg.1{{$}}
; PTR-NEXT:     {{^}}  ldind.i{{$}}
; PTR-NEXT:     {{^}}  stind.i{{$}}
; COPY-NEXT:    {{^}}  ret{{$}}
; COPY-NEXT:    {{^}}} // end of method 'copy_end'{{$}}
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
; VAARG: error: {{.*}}arg_i32{{.*}}the va_arg instruction is not supported for MSIL yet (phase 3 variadics)
; VAARG: error: {{.*}}arg_ptr{{.*}}the va_arg instruction is not supported for MSIL yet (phase 3 variadics)
; VAARG: error: {{.*}}arg_double{{.*}}the va_arg instruction is not supported for MSIL yet (phase 3 variadics)
; VAARG: error: {{.*}}arg_i8{{.*}}the va_arg instruction is not supported for MSIL yet (phase 3 variadics)
; VAARG: error: {{.*}}arg_i128{{.*}}the va_arg instruction is not supported for MSIL yet (phase 3 variadics)
; VAARG: error: {{.*}}arg_half{{.*}}the va_arg instruction is not supported for MSIL yet (phase 3 variadics)
; VAARG: error: {{.*}}arg_vector{{.*}}the va_arg instruction is not supported for MSIL yet (phase 3 variadics)

; VAARG-IL-LABEL: .method {{.*}}'arg_i32'(
; VAARG-IL-SAME:  native int) cil managed{{$}}
; VAARG-IL-NEXT:  {{^\{$}}
; VAARG-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; VAARG-IL-NEXT:  {{^}}  ldc.i4.0{{$}}
; VAARG-IL-NEXT:  {{^}}  ret{{$}}
; VAARG-IL-NEXT:  {{^}}} // end of method 'arg_i32'{{$}}
; VAARG-IL-LABEL: .method {{.*}}'arg_ptr'(
; VAARG-IL-SAME:  native int) cil managed{{$}}
; VAARG-IL-NEXT:  {{^\{$}}
; VAARG-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; VAARG-IL-NEXT:  {{^}}  ldc.i4.0{{$}}
; VAARG-IL-NEXT:  {{^}}  conv.i{{$}}
; VAARG-IL-NEXT:  {{^}}  ret{{$}}
; VAARG-IL-NEXT:  {{^}}} // end of method 'arg_ptr'{{$}}
; VAARG-IL-LABEL: .method {{.*}}'arg_double'(
; VAARG-IL-NOT:   call
; VAARG-IL:       {{^}}} // end of method 'arg_double'{{$}}
; VAARG-IL-LABEL: .method {{.*}}'arg_i8'(
; VAARG-IL-NOT:   call
; VAARG-IL:       {{^}}} // end of method 'arg_i8'{{$}}
; VAARG-IL-LABEL: .method {{.*}}'arg_i128'(
; VAARG-IL-NOT:   call
; VAARG-IL:       {{^}}} // end of method 'arg_i128'{{$}}
; VAARG-IL-LABEL: .method {{.*}}'arg_half'(
; VAARG-IL-NOT:   call
; VAARG-IL:       {{^}}} // end of method 'arg_half'{{$}}
; VAARG-IL-LABEL: .method {{.*}}'arg_vector'(
; VAARG-IL-NOT:   call
; VAARG-IL:       {{^}}} // end of method 'arg_vector'{{$}}
; VAARG-IL-LABEL: .method {{.*}}'after_vaarg'(
; VAARG-IL-SAME:  int32, int32) cil managed{{$}}
; VAARG-IL:       {{^}}  ldarg.0{{$}}
; VAARG-IL-NEXT:  {{^}}  ldarg.1{{$}}
; VAARG-IL-NEXT:  {{^}}  add{{$}}
; VAARG-IL-NEXT:  {{^}}  ret{{$}}
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
; MIXED: error: {{.*}}vdef{{.*}}variadic function definitions are not supported for MSIL yet

; MIXED-IL-NOT:      {{^}}.method {{.*}}'vdef'(
; MIXED-IL-LABEL:    .method {{.*}}'copy_user'(
; MIXED-IL-SAME:     native int, native int) cil managed{{$}}
; MIXED-STRUCT:      {{^}}  ldarg.0{{$}}
; MIXED-STRUCT-NEXT: {{^}}  ldarg.1{{$}}
; MIXED-STRUCT-NEXT: {{^}}  ldc.i4.s [[SIZE]]{{$}}
; MIXED-STRUCT-NEXT: {{^}}  cpblk{{$}}
; MIXED-PTR:         {{^}}  ldarg.0{{$}}
; MIXED-PTR-NEXT:    {{^}}  ldarg.1{{$}}
; MIXED-PTR-NEXT:    {{^}}  ldind.i{{$}}
; MIXED-PTR-NEXT:    {{^}}  stind.i{{$}}
; MIXED-IL-NEXT:     {{^}}  ret{{$}}
; MIXED-IL-NEXT:     {{^}}} // end of method 'copy_user'{{$}}
; MIXED-IL-NOT:      {{^}}.method {{.*}}'vdef'(
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

; Calls of variadic functions stay errors in Phase 1 (ImplementationPlan.md
; decision D19, M9 goal 'Calls to variadic functions stay diagnosed (phase
; 3)'; Design.md 4.7; plan section 2 'Diagnostics leave well-formed output').
; Every call whose function type is variadic is reported once, naming the
; caller: "calls to variadic functions are not supported for MSIL yet". This
; holds for declared callees with and without variadic arguments, for a
; variadic import (msil-import-module) and for puts declared with '...'
; (without "no-prototype", so it is not a K&R call: add-prototypes.ll and
; function-bitcasts.ll have the K&R calls, which are fixed-arity), and, new
; with indirect calls in M9, for calls through a function pointer with a
; variadic call type: no calli is built for them, while a non-variadic call
; through a pointer in the same module is an ordinary calli. A rejected call
; builds nothing (its result is undefined), its callee gets no P/Invoke and no
; 'undefined function' error, llc exits with 1 without 'LLVM ERROR' or a crash
; report, and the IL on stdout assembles. A variadic definition is reported
; as such by MSILPrepareIR, which deletes its body and erases its calls, so
; that is its only error (unsupported-ir-errors.ll).

; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; RUN: split-file %s %t

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/direct.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=DIRECT
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/direct.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=DIRECT
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/direct.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=DIRECT
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/direct.ll \
; RUN:   > %t/direct.lnx.il
; RUN: FileCheck %s --check-prefix=DIRECT-IL --implicit-check-not=pinvokeimpl --input-file=%t/direct.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/direct.ll \
; RUN:   > %t/direct.win.il
; RUN: FileCheck %s --check-prefix=DIRECT-IL --implicit-check-not=pinvokeimpl --input-file=%t/direct.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/direct.dll %t/direct.win.il %}

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/indirect.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=INDIRECT
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/indirect.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=INDIRECT
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/indirect.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=INDIRECT
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/indirect.ll \
; RUN:   > %t/indirect.lnx.il
; RUN: FileCheck %s --check-prefix=INDIRECT-IL --input-file=%t/indirect.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/indirect.ll \
; RUN:   > %t/indirect.win.il
; RUN: FileCheck %s --check-prefix=INDIRECT-IL --input-file=%t/indirect.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/indirect.dll %t/indirect.lnx.il %}

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/definition.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=DEFN
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/definition.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=DEFN
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/definition.ll \
; RUN:   > %t/definition.win.il
; RUN: FileCheck %s --check-prefix=DEFN-IL --input-file=%t/definition.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/definition.dll %t/definition.win.il %}

;--- direct.ll
; DIRECT:      error: {{.*}}call_vf{{.*}}calls to variadic functions are not supported for MSIL yet
; DIRECT:      error: {{.*}}call_vf_fixed_only{{.*}}calls to variadic functions are not supported for MSIL yet
; DIRECT:      error: {{.*}}call_printf{{.*}}calls to variadic functions are not supported for MSIL yet
; DIRECT:      error: {{.*}}call_puts_variadic{{.*}}calls to variadic functions are not supported for MSIL yet

; DIRECT-IL-LABEL: .method {{.*}}'call_vf'(
; DIRECT-IL-NOT:   call
; DIRECT-IL:       {{^}}  ret{{$}}
; DIRECT-IL-NEXT:  {{^}}} // end of method 'call_vf'{{$}}
; DIRECT-IL-LABEL: .method {{.*}}'call_vf_fixed_only'(
; DIRECT-IL-NOT:   call
; DIRECT-IL:       {{^}}  ret{{$}}
; DIRECT-IL-NEXT:  {{^}}} // end of method 'call_vf_fixed_only'{{$}}
; DIRECT-IL-LABEL: .method {{.*}}'call_printf'(
; DIRECT-IL-NOT:   call
; DIRECT-IL:       {{^}}  ret{{$}}
; DIRECT-IL-NEXT:  {{^}}} // end of method 'call_printf'{{$}}
; DIRECT-IL-LABEL: .method {{.*}}'call_puts_variadic'(
; DIRECT-IL-NOT:   call
; DIRECT-IL:       {{^}}  ret{{$}}
; DIRECT-IL-NEXT:  {{^}}} // end of method 'call_puts_variadic'{{$}}
; DIRECT-IL-LABEL: .method {{.*}}'after_direct'(
; DIRECT-IL:       {{^}}  ldarg.0{{$}}
; DIRECT-IL-NEXT:  {{^}}  ldarg.1{{$}}
; DIRECT-IL-NEXT:  {{^}}  add{{$}}
; DIRECT-IL-NEXT:  {{^}}  ret{{$}}
; DIRECT-IL-NOT:   .method
source_filename = "direct.c"

declare i32 @vf(i32, ...)
declare i32 @printf(ptr, ...) #0
declare i32 @puts(ptr, ...)

define i32 @call_vf(i32 %x) {
  %r = call i32 (i32, ...) @vf(i32 %x, i32 2, double 3.0)
  ret i32 %r
}

define i32 @call_vf_fixed_only(i32 %x) {
  %r = call i32 (i32, ...) @vf(i32 %x)
  ret i32 %r
}

define void @call_printf(ptr %fmt, i32 %x) {
  %r = call i32 (ptr, ...) @printf(ptr %fmt, i32 %x)
  ret void
}

define i32 @call_puts_variadic(ptr %s) {
  %r = call i32 (ptr, ...) @puts(ptr %s)
  ret i32 %r
}

define i32 @after_direct(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

attributes #0 = { "msil-import-module"="libc" }

;--- indirect.ll
; Variadic call types through function pointers: with fixed and variadic
; arguments, with no fixed parameter at all, and with a floating-point result;
; then a non-variadic call through a pointer, which compiles.
; INDIRECT:    error: {{.*}}call_ptr_variadic{{.*}}calls to variadic functions are not supported for MSIL yet
; INDIRECT:    error: {{.*}}call_ptr_varargs_only{{.*}}calls to variadic functions are not supported for MSIL yet
; INDIRECT:    error: {{.*}}call_ptr_variadic_double{{.*}}calls to variadic functions are not supported for MSIL yet

; INDIRECT-IL-LABEL: .method {{.*}}'call_ptr_variadic'(
; INDIRECT-IL-NOT:   call
; INDIRECT-IL:       {{^}}  ret{{$}}
; INDIRECT-IL-NEXT:  {{^}}} // end of method 'call_ptr_variadic'{{$}}
; INDIRECT-IL-LABEL: .method {{.*}}'call_ptr_varargs_only'(
; INDIRECT-IL-NOT:   call
; INDIRECT-IL:       {{^}}  ret{{$}}
; INDIRECT-IL-NEXT:  {{^}}} // end of method 'call_ptr_varargs_only'{{$}}
; INDIRECT-IL-LABEL: .method {{.*}}'call_ptr_variadic_double'(
; INDIRECT-IL-NOT:   call
; INDIRECT-IL:       {{^}}  ret{{$}}
; INDIRECT-IL-NEXT:  {{^}}} // end of method 'call_ptr_variadic_double'{{$}}
; INDIRECT-IL-LABEL: .method {{.*}}'call_ptr_fixed'(
; INDIRECT-IL-SAME:  native int, native int) cil managed{{$}}
; INDIRECT-IL:       {{^}}  ldarg.1{{$}}
; INDIRECT-IL-NEXT:  {{^}}  ldarg.0{{$}}
; INDIRECT-IL-NEXT:  {{^}}  calli int32(native int){{$}}
; INDIRECT-IL-NEXT:  {{^}}  ret{{$}}
source_filename = "indirect.c"

define i32 @call_ptr_variadic(ptr %fp, ptr %s, i32 %x) {
  %r = call i32 (ptr, ...) %fp(ptr %s, i32 %x)
  ret i32 %r
}

define void @call_ptr_varargs_only(ptr %fp) {
  call void (...) %fp()
  ret void
}

define double @call_ptr_variadic_double(ptr %fp, double %d) {
  %r = call double (double, ...) %fp(double %d, double %d)
  ret double %r
}

define i32 @call_ptr_fixed(ptr %fp, ptr %s) {
  %r = call i32 %fp(ptr %s)
  ret i32 %r
}

;--- definition.ll
; DEFN:        error: {{.*}}vdef{{.*}}variadic function definitions are not supported for MSIL yet
; DEFN-NOT:    calls to variadic functions

; DEFN-IL-NOT:     {{^}}.method {{.*}}'vdef'(
; DEFN-IL-LABEL:   .method {{.*}}'call_vdef'(
; DEFN-IL-NOT:     call
; DEFN-IL:         {{^}}  ret{{$}}
; DEFN-IL-NEXT:    {{^}}} // end of method 'call_vdef'{{$}}
; DEFN-IL-LABEL:   .method {{.*}}'after_definition'(
; DEFN-IL:         {{^}}  ldarg.0{{$}}
; DEFN-IL-NEXT:    {{^}}  ldarg.1{{$}}
; DEFN-IL-NEXT:    {{^}}  sub{{$}}
; DEFN-IL-NEXT:    {{^}}  ret{{$}}
; DEFN-IL-NOT:     {{^}}.method {{.*}}'vdef
source_filename = "definition.c"

define i32 @vdef(i32 %n, ...) {
  ret i32 %n
}

define i32 @call_vdef(i32 %x) {
  %r = call i32 (i32, ...) @vdef(i32 %x, i32 1)
  ret i32 %r
}

define i32 @after_definition(i32 %a, i32 %b) {
  %r = sub i32 %a, %b
  ret i32 %r
}

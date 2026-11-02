; Calls of variadic functions (ImplementationPlan.md 5.15.2, M26; decision D19
; as M26 amends it; Design.md 4.7). Until M26 every call with a variadic
; function type was the error 'calls to variadic functions are not supported
; for MSIL yet'. Since M26 MSILPrepareVariadics (slot P6) and ExpandVariadics
; (P7) expand them in whole-program mode as in object mode: the caller packs
; the variadic arguments into a buffer in its frame and passes a va_list
; pointer after the fixed arguments, so the callee's IL signature is
; (<fixed>, native int) (variadic-call.ll pins the buffers). The text remains
; the backstop for the calls that ExpandVariadics leaves variadic: every
; variadic call on a triple without an MSIL variadic ABI (the aarch64 Darwin
; triples) and a variadic call with a calling convention other than C, once
; per call (variadic-errors.ll pins it: darwin-call.ll, cc.ll, cc-w64.ll).
;
; direct.ll: calls of a declared variadic function with and without variadic
; arguments are expanded calls of the same declaration; since 'vf' is defined
; nowhere and is no import, D14's standalone 'undefined function' error names
; it once, at the end of the module, and the calls are still built (so llc
; exits with 1 and the IL is not assembled). An explicit import
; (msil-import-module, here printf from "libc") counts as listed (5.15.2
; MSILPrepareVariadics step 2): its call becomes a fixed-arity P/Invoke with
; the call site's own signature (variadic-fixed-arity.ll has the lists).
; indirect.ll: calls through function pointers with a variadic call type are
; expanded calli instructions, (<fixed>, native int), with a floating-point
; result and with no fixed parameter at all; a non-variadic call through a
; pointer is an ordinary calli. Nothing is reported and the IL assembles.
; definition.ll: a variadic definition and its caller compile (5.15.2's
; definitions; variadic-def.ll pins the bodies); nothing is reported.

; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; RUN: split-file %s %t

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/direct.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=DIRECT
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/direct.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=DIRECT
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/direct.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=DIRECT
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/direct.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=DIRECT
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/direct.ll \
; RUN:   > %t/direct.lnx.il
; RUN: FileCheck %s --check-prefix=DIRECT-IL --input-file=%t/direct.lnx.il
; RUN: FileCheck %s --check-prefix=DIRECT-PINV --input-file=%t/direct.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/direct.ll \
; RUN:   > %t/direct.win.il
; RUN: FileCheck %s --check-prefix=DIRECT-IL --input-file=%t/direct.win.il
; RUN: FileCheck %s --check-prefix=DIRECT-PINV --input-file=%t/direct.win.il

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/indirect.ll \
; RUN:   -o %t/indirect.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=INDIRECT-IL --input-file=%t/indirect.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/indirect.ll \
; RUN:   -o %t/indirect.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=INDIRECT-IL --input-file=%t/indirect.win.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/indirect.ll \
; RUN:   -o %t/indirect.lnx.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=INDIRECT-IL --input-file=%t/indirect.lnx.o0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/indirect.ll \
; RUN:   -o %t/indirect.win.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=INDIRECT-IL --input-file=%t/indirect.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/indirect.dll %t/indirect.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/indirect.win.o0.dll %t/indirect.win.o0.il %}

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/definition.ll \
; RUN:   -o %t/definition.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=DEFN-IL --input-file=%t/definition.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/definition.ll \
; RUN:   -o %t/definition.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=DEFN-IL --input-file=%t/definition.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/definition.dll %t/definition.win.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/definition.lnx.dll %t/definition.lnx.il %}

;--- direct.ll
; DIRECT:      {{^}}error: {{.*}}undefined function 'vf' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}

; DIRECT-IL-LABEL: .method {{.*}}'call_vf'(
; DIRECT-IL-SAME:  int32) cil managed{{$}}
; DIRECT-IL:       {{^}}  call int32 'vf'(int32, native int){{$}}
; DIRECT-IL:       {{^}}  ret{{$}}
; DIRECT-IL-NEXT:  {{^}}} // end of method 'call_vf'{{$}}
; DIRECT-IL-LABEL: .method {{.*}}'call_vf_fixed_only'(
; DIRECT-IL-SAME:  int32) cil managed{{$}}
; DIRECT-IL:       {{^}}  call int32 'vf'(int32, native int){{$}}
; DIRECT-IL:       {{^}}  ret{{$}}
; DIRECT-IL-NEXT:  {{^}}} // end of method 'call_vf_fixed_only'{{$}}
; DIRECT-IL-LABEL: .method {{.*}}'call_printf'(
; DIRECT-IL-SAME:  native int, int32) cil managed{{$}}
; DIRECT-IL:       {{^}}  call int32 'printf'(native int, int32){{$}}
; DIRECT-IL:       {{^}}  ret{{$}}
; DIRECT-IL-NEXT:  {{^}}} // end of method 'call_printf'{{$}}
; DIRECT-IL-LABEL: .method {{.*}}'after_direct'(
; DIRECT-IL:       {{^}}  ldarg.0{{$}}
; DIRECT-IL-NEXT:  {{^}}  ldarg.1{{$}}
; DIRECT-IL-NEXT:  {{^}}  add{{$}}
; DIRECT-IL-NEXT:  {{^}}  ret{{$}}
; DIRECT-PINV:     {{^}}.method {{.*}}pinvokeimpl("libc" {{.*}}) int32 'printf'(native int, int32) cil managed preservesig {}{{$}}
source_filename = "direct.c"

declare i32 @vf(i32, ...)
declare i32 @printf(ptr, ...) #0

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

define i32 @after_direct(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

attributes #0 = { "msil-import-module"="libc" }

;--- indirect.ll
; Variadic call types through function pointers: with fixed and variadic
; arguments, with no fixed parameter and no argument at all, and with a
; floating-point result; then a non-variadic call through a pointer.
; INDIRECT-IL-LABEL: .method {{.*}}'call_ptr_variadic'(
; INDIRECT-IL-SAME:  native int, native int, int32) cil managed{{$}}
; INDIRECT-IL:       {{^}}  calli int32(native int, native int){{$}}
; INDIRECT-IL:       {{^}}} // end of method 'call_ptr_variadic'{{$}}
; INDIRECT-IL-LABEL: .method {{.*}}'call_ptr_varargs_only'(
; INDIRECT-IL-SAME:  native int) cil managed{{$}}
; INDIRECT-IL:       {{^}}  calli void(native int){{$}}
; INDIRECT-IL:       {{^}}} // end of method 'call_ptr_varargs_only'{{$}}
; INDIRECT-IL-LABEL: .method {{.*}}'call_ptr_variadic_double'(
; INDIRECT-IL-SAME:  native int, float64) cil managed{{$}}
; INDIRECT-IL:       {{^}}  calli float64(float64, native int){{$}}
; INDIRECT-IL:       {{^}}} // end of method 'call_ptr_variadic_double'{{$}}
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
; DEFN-IL:         {{^}}.method public static int32 'vdef'(int32, native int) cil managed{{$}}
; DEFN-IL-LABEL:   .method {{.*}}'call_vdef'(
; DEFN-IL:         {{^}}  call int32 'vdef'(int32, native int){{$}}
; DEFN-IL:         {{^}}  ret{{$}}
; DEFN-IL-NEXT:    {{^}}} // end of method 'call_vdef'{{$}}
; DEFN-IL-LABEL:   .method {{.*}}'after_definition'(
; DEFN-IL:         {{^}}  ldarg.0{{$}}
; DEFN-IL-NEXT:    {{^}}  ldarg.1{{$}}
; DEFN-IL-NEXT:    {{^}}  sub{{$}}
; DEFN-IL-NEXT:    {{^}}  ret{{$}}
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

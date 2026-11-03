; Variadic code on msil64-unknown-dotnet (ImplementationPlan.md 5.18.2 PC5 and
; PB1, milestone P-1a). The portable va_list is a char*, expanded by
; ExpandVariadics' MSILPortable class (8-byte slots, aggregates indirect;
; llvm/test/Transforms/ExpandVariadics/msil-abi.ll pins the layout), so:
; - an IR va_arg instruction compiles (MSILPrepareIR treats dotnet like the
;   windows-msvc char* triples instead of reporting it): each function
;   loads the va_list pointer from its argument, aligns it, stores the
;   advanced pointer back and reads the value with the type's load
;   (ldind.i4, ldind.i, ldind.r8);
; - a variadic definition with va_start compiles: its IL signature gets the
;   caller's va_list pointer last, '(int32, native int)', va_start stores
;   that pointer in the function's own va_list (the IR after
;   ExpandVariadics; -O2 then reads through the argument), and a call
;   passes its variadic arguments through a buffer whose address is that
;   last argument;
; nothing is reported at -O0 and -O2, in text and in objects, and the text
; assembles.

; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{t} = -mtriple=msil64-unknown-dotnet -verify-machineinstrs
; RUN: llc %{t} -asm-verbose=false < %t/va.ll -o %t/va.o2.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=IL --input-file=%t/va.o2.il --implicit-check-not=pinvokeimpl
; RUN: llc %{t} -O0 -asm-verbose=false < %t/va.ll -o %t/va.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=IL --input-file=%t/va.o0.il --implicit-check-not=pinvokeimpl
; RUN: llc %{t} -filetype=obj < %t/va.ll -o %t/va.o 2>&1 | count 0
; RUN: llc %{t} -O0 -filetype=obj < %t/va.ll -o %t/va.o0.o 2>&1 | count 0
; RUN: llc %{t} -stop-after=expand-variadics < %t/va.ll -o - | FileCheck %s --check-prefix=IR \
; RUN:   --implicit-check-not="call void @llvm.va_"
; RUN: %if ilasm %{ %ilasm-verify -output=%t/va.o2.dll %t/va.o2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/va.o0.dll %t/va.o0.il %}

;--- va.ll
source_filename = "portable-varargs.c"

; IL-LABEL: {{^}}.method public static int32 'get_i32'(native int) cil managed{{$}}
; IL:       {{^}}  ldarg.0{{$}}
; IL:       {{^}}  ldind.i{{$}}
; IL:       {{^}}  stind.i{{$}}
; IL:       {{^}}  ldind.i4{{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'get_i32'{{$}}
define i32 @get_i32(ptr %ap) {
  %v = va_arg ptr %ap, i32
  ret i32 %v
}

; IL-LABEL: {{^}}.method public static native int 'get_ptr'(native int) cil managed{{$}}
; IL:       {{^}}  ldind.i{{$}}
; IL:       {{^}}  stind.i{{$}}
; IL:       {{^}}  ldind.i{{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'get_ptr'{{$}}
define ptr @get_ptr(ptr %ap) {
  %v = va_arg ptr %ap, ptr
  ret ptr %v
}

; IL-LABEL: {{^}}.method public static float64 'get_double'(native int) cil managed{{$}}
; IL:       {{^}}  ldind.i{{$}}
; IL:       {{^}}  stind.i{{$}}
; IL:       {{^}}  ldind.r8{{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'get_double'{{$}}
define double @get_double(ptr %ap) {
  %v = va_arg ptr %ap, double
  ret double %v
}

; IL-LABEL: {{^}}.method public static int32 'vsum'(int32, native int) cil managed{{$}}
; IL:       {{^}}  ldind.r8{{$}}
; IL:       {{^}}} // end of method 'vsum'{{$}}
; IR-LABEL: define i32 @vsum(i32 %n, ptr %varargs)
; IR:       store ptr %varargs, ptr %ap
define i32 @vsum(i32 %n, ...) {
entry:
  %ap = alloca ptr, align 8
  call void @llvm.va_start.p0(ptr %ap)
  %a = va_arg ptr %ap, i32
  %b = va_arg ptr %ap, double
  %bi = fptosi double %b to i32
  call void @llvm.va_end.p0(ptr %ap)
  %s = add i32 %a, %bi
  %t = add i32 %s, %n
  ret i32 %t
}

; IL-LABEL: {{^}}.method public static int32 'call_vsum'() cil managed{{$}}
; IL:       {{^}}  call int32 'vsum'(int32, native int){{$}}
; IL:       {{^}}} // end of method 'call_vsum'{{$}}
; IR-LABEL: define i32 @call_vsum()
; IR:       call i32 @vsum(i32 2, ptr {{.*}})
define i32 @call_vsum() {
  %r = call i32 (i32, ...) @vsum(i32 2, i32 7, double 1.5)
  ret i32 %r
}

declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_end.p0(ptr)

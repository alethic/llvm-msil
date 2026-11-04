; The errors of MSILLowerSjLj and the reworded __builtin_setjmp and
; __builtin_longjmp texts (ImplementationPlan.md 5.15.4 'Names recognized',
; M27.b 'Diagnostics'; recon p3/sjlj.md S8; plan section 2 'Diagnostics leave
; well-formed output'), each reported with its whole text:
; - a non-call use of a setjmp name, in a global initializer ('setjmp') and
;   in a function ('_setjmp'): 'taking the address of '<name>' is not
;   supported for MSIL';
; - a call of any other returns_twice function: 'calling the returns_twice
;   function '<name>' is not supported for MSIL' for 'vfork' and 'getcontext',
;   also in a module without any setjmp name, in object mode;
; - llvm.eh.sjlj.setjmp and llvm.eh.sjlj.longjmp, still diagnosed by ISel:
;   '__builtin_setjmp ('llvm.eh.sjlj.setjmp') is not supported for MSIL; use
;   setjmp' and '__builtin_longjmp ('llvm.eh.sjlj.longjmp') is not supported
;   for MSIL; use longjmp'.
; Separately, each in a module of its own, with only its own errors (exit 1):
; - on both triples, a variadic declaration of a setjmp name without a fixed
;   buffer parameter ('setjmp(...)') and of a longjmp name without a fixed
;   value parameter ('_longjmp(ptr, ...)'), diagnosed by MSILPrepareVariadics
;   (P6) before ExpandVariadics rewrites the calls: 'a variadic declaration of
;   '<name>' is not supported for MSIL; declare it with its C library
;   prototype' (section 12 row '5.15.4 'Names recognized' and M27.b extended
;   (variadic setjmp-family declarations)'). It is reported once per use:
;   once per call (two setjmp calls in one function give two errors), once
;   for a function that takes the address ('addr'), and once per global
;   variable whose initializer holds it ('global variable @g: ...', without a
;   location). The output stays well formed: after P6 the initializer's use
;   is null (zeroinitializer for the array), each call is gone and its value
;   poison, 'addr' returns null, the bodies are kept, and the function after
;   them ('after') compiles and the IL assembles (review r2 item 7);
; - on both triples, a variadic declaration '__sigsetjmp(ptr, ...)' called
;   with a savemask: the same P6 error, since sigsetjmp and __sigsetjmp also
;   need the savemask fixed (the section 12 row that extends '5.15.4 'Names
;   recognized' and M27.b extended (variadic setjmp-family declarations)' by
;   one clause; review r2 item 4);
; - a module that calls longjmp and holds a global named after one of
;   MSILLowerSjLj's helpers with another type ('declare void
;   @__msil_longjmp()', '@__msil_sjlj_throw = global i32 0'): ''<name>' is
;   reserved for the MSIL toolchain (a setjmp and longjmp helper)', once,
;   without a location, and never 'LLVM ERROR' (section 12 row '5.14.6 I2 and
;   5.15.4 amended (a user global with a helper's name)'; review r2 item 5);
; - a VLA inside 'if (_setjmp(b) == 0) { ... }' with no loop, before a
;   wrapped call: the dispatch's path back to the site puts the alloca on a
;   cycle, so P9 (after P8, 5.13.6) reports its existing text 'a dynamic
;   stack allocation that can repeat within one call (a VLA or alloca in a
;   loop) is not supported for MSIL yet' until M32 H1; the same body without
;   the site compiles without it (review r2 item 3);
; - in whole-program text on the Linux triple (no -msil-object-form), a
;   '__sigsetjmp(b, 1)' site (glibc's sigsetjmp), whose 'sigprocmask' call
;   meets D14's undefined-function error (M27.b 'Linux sigsetjmp').
; MSILLowerSjLj's errors leave valid IR. The initializer's use becomes null,
; and a function holding a diagnosed use or call keeps its definition with the
; body a lone 'unreachable' (IL 'ldnull; throw'); ISel's dummy setjmp result
; is 0. llc reports every error once, exits with 1 (`not`), never prints
; 'LLVM ERROR' or a crash report, and keeps compiling: the function after
; them compiles normally and the IL on stdout assembles. MSILLowerSjLj (P8)
; runs before ISel, so its four errors (in an order that is not pinned) come
; before ISel's two (in function order). Every case at -O0 and -O2 on both
; triples (diag.ll; the separate modules above run at least at -O0 on one
; triple and -O2 on the other, sigmask.ll on the Linux triple only, at both).
;
; llc deletes its -o file after an error, so the IR and IL that are checked
; are taken from stdout.

; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{diag} = FileCheck %s --check-prefix=ERR --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not="Stack dump" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{run} = not llc -mtriple=%{t} %{o} -verify-machineinstrs < %t/diag.ll -o /dev/null 2>&1 | %{diag} && \
; DEFINE:   not llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %t/diag.ll \
; DEFINE:     > %t/diag.%{t}%{o}.il 2>/dev/null && \
; DEFINE:   FileCheck %s --check-prefix=IL --input-file=%t/diag.%{t}%{o}.il && \
; DEFINE:   not llc -mtriple=%{t} %{o} -stop-after=msil-lower-sjlj < %t/diag.ll -o - 2>/dev/null \
; DEFINE:   | FileCheck %s --check-prefix=IR && \
; DEFINE:   not llc -mtriple=%{t} %{o} -filetype=obj -verify-machineinstrs < %t/vfork.ll -o %t/vfork.o 2>&1 \
; DEFINE:   | FileCheck %s --check-prefix=VF --implicit-check-not="LLVM ERROR" \
; DEFINE:       --implicit-check-not="PLEASE submit" --implicit-check-not=error: --implicit-check-not=warning:
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %{run}
; REDEFINE: %{o} = -O0
; RUN: %{run}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; RUN: %{run}
; REDEFINE: %{o} = -O2
; RUN: %{run}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/diag.lnx.dll %t/diag.x86_64-unknown-linux-gnu-msil-O2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/diag.win.dll %t/diag.x86_64-pc-windows-msvc-msil-O0.il %}

; The variadic setjmp-family declarations and the standalone sigprocmask.
; DEFINE: %{one} = --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not="Stack dump" --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O2 < %t/va-setjmp.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=VAS %{one}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/va-setjmp.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=VAS %{one}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/va-longjmp.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=VAL %{one}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O2 < %t/va-longjmp.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=VAL %{one}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O2 -stop-after=msil-prepare-variadics \
; RUN:   < %t/va-setjmp.ll -o - 2>/dev/null \
; RUN:   | FileCheck %s --check-prefix=VAS-IR --implicit-check-not="{{(call .*|ptr )@setjmp}}"
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -stop-after=msil-prepare-variadics \
; RUN:   < %t/va-longjmp.ll -o - 2>/dev/null \
; RUN:   | FileCheck %s --check-prefix=VAL-IR --implicit-check-not="{{(call .*|ptr )@_longjmp}}"
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs \
; RUN:   < %t/va-setjmp.ll > %t/va-setjmp.il 2>/dev/null
; RUN: FileCheck %s --check-prefix=VA-IL --input-file=%t/va-setjmp.il
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O2 -asm-verbose=false -verify-machineinstrs \
; RUN:   < %t/va-longjmp.ll > %t/va-longjmp.il 2>/dev/null
; RUN: FileCheck %s --check-prefix=VA-IL --input-file=%t/va-longjmp.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/va-setjmp.dll %t/va-setjmp.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/va-longjmp.dll %t/va-longjmp.il %}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O2 < %t/va-sigsetjmp.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=VSS %{one}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 < %t/va-sigsetjmp.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=VSS %{one}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O2 < %t/sigmask.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=SIGP %{one}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %t/sigmask.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=SIGP %{one}

; The reserved helper names, in object-form text (the longjmp call is left
; unlowered, a call of an undefined function that the object may reference).
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O2 -msil-object-form < %t/rsv-fn.ll \
; RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=RSVF %{one} --implicit-check-not="is reserved"
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -msil-object-form < %t/rsv-fn.ll \
; RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=RSVF %{one} --implicit-check-not="is reserved"
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -msil-object-form < %t/rsv-var.ll \
; RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=RSVV %{one} --implicit-check-not="is reserved"
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O2 -msil-object-form < %t/rsv-var.ll \
; RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=RSVV %{one} --implicit-check-not="is reserved"

; A VLA after a setjmp site, in whole-program text.
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/vla.ll \
; RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=VLA %{one}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O2 -verify-machineinstrs < %t/vla.ll \
; RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=VLA %{one}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O2 -asm-verbose=false -verify-machineinstrs \
; RUN:   < %t/vla.ll > %t/vla.lnx.il 2>/dev/null
; RUN: FileCheck %s --check-prefix=VLA-IL --input-file=%t/vla.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs \
; RUN:   < %t/vla.ll > %t/vla.win.il 2>/dev/null
; RUN: FileCheck %s --check-prefix=VLA-IL --input-file=%t/vla.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/vla.lnx.dll %t/vla.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/vla.win.dll %t/vla.win.il %}

; ERR-DAG: error: {{.*}}taking the address of 'setjmp' is not supported for MSIL{{$}}
; ERR-DAG: error: {{.*}}in function take_setjmp {{.*}}: taking the address of '_setjmp' is not supported for MSIL{{$}}
; ERR-DAG: error: {{.*}}in function use_vfork {{.*}}: calling the returns_twice function 'vfork' is not supported for MSIL{{$}}
; ERR-DAG: error: {{.*}}in function use_getcontext {{.*}}: calling the returns_twice function 'getcontext' is not supported for MSIL{{$}}
; ERR:     error: {{.*}}in function builtin_setjmp i32 (ptr): __builtin_setjmp ('llvm.eh.sjlj.setjmp') is not supported for MSIL; use setjmp{{$}}
; ERR:     error: {{.*}}in function builtin_longjmp void (ptr): __builtin_longjmp ('llvm.eh.sjlj.longjmp') is not supported for MSIL; use longjmp{{$}}

; The IR after P8: the initializer's use is null; the bodies of the functions
; P8 diagnosed are a lone 'unreachable'; the builtins are left to ISel.
; IR:       @sj_ptr = global ptr null
; IR-LABEL: define ptr @take_setjmp() {
; IR-NOT:   {{call|ret |store}}
; IR:       unreachable
; IR-NEXT:  {{^ *}}}{{$}}
; IR-LABEL: define i32 @use_vfork() {
; IR-NOT:   {{call|ret |store}}
; IR:       unreachable
; IR-NEXT:  {{^ *}}}{{$}}
; IR-LABEL: define i32 @use_getcontext(ptr %ctx) {
; IR-NOT:   {{call|ret |store}}
; IR:       unreachable
; IR-NEXT:  {{^ *}}}{{$}}
; IR-LABEL: define i32 @builtin_setjmp(ptr %buf) {
; IR:       call i32 @llvm.eh.sjlj.setjmp(ptr %buf)
; IR-LABEL: define void @builtin_longjmp(ptr %buf) {
; IR:       call void @llvm.eh.sjlj.longjmp(ptr %buf)
; IR-LABEL: define i32 @after(i32 %x) {
; IR:       add i32 %x, 1

; In the IL every method is printed, and the function after them compiles.
; IL-LABEL: .method {{.*}}'take_setjmp'(
; IL-SAME:  ) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 1{{$}}
; IL-NEXT:  {{^}}  ldnull{{$}}
; IL-NEXT:  {{^}}  throw{{$}}
; IL-NEXT:  {{^}}} // end of method 'take_setjmp'{{$}}
; IL-LABEL: .method {{.*}}'use_vfork'(
; IL-SAME:  ) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 1{{$}}
; IL-NEXT:  {{^}}  ldnull{{$}}
; IL-NEXT:  {{^}}  throw{{$}}
; IL-NEXT:  {{^}}} // end of method 'use_vfork'{{$}}
; IL-LABEL: .method {{.*}}'use_getcontext'(
; IL-SAME:  native int) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 1{{$}}
; IL-NEXT:  {{^}}  ldnull{{$}}
; IL-NEXT:  {{^}}  throw{{$}}
; IL-NEXT:  {{^}}} // end of method 'use_getcontext'{{$}}
; IL-LABEL: .method {{.*}}'builtin_setjmp'(
; IL-SAME:  native int) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 1{{$}}
; IL-NEXT:  {{^}}  ldc.i4.0{{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'builtin_setjmp'{{$}}
; IL-LABEL: .method {{.*}}'builtin_longjmp'(
; IL-SAME:  native int) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 1{{$}}
; IL-NEXT:  {{^}}  ldnull{{$}}
; IL-NEXT:  {{^}}  throw{{$}}
; IL-NEXT:  {{^}}} // end of method 'builtin_longjmp'{{$}}
; IL-LABEL: .method {{.*}}'after'(
; IL-SAME:  int32) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 2{{$}}
; IL-NEXT:  {{^}}  ldarg.0{{$}}
; IL-NEXT:  {{^}}  ldc.i4.1{{$}}
; IL-NEXT:  {{^}}  add{{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'after'{{$}}

; A module without setjmp names, in object mode.
; VF: error: {{.*}}in function only_vfork {{.*}}: calling the returns_twice function 'vfork' is not supported for MSIL{{$}}

; One error per call, per function taking the address and per global
; variable (two identical DAG lines need two distinct lines of output).
; VAS-DAG: error: {{.*}}in function va_setjmp i32 (ptr): a variadic declaration of 'setjmp' is not supported for MSIL; declare it with its C library prototype{{$}}
; VAS-DAG: error: {{.*}}in function va_setjmp i32 (ptr): a variadic declaration of 'setjmp' is not supported for MSIL; declare it with its C library prototype{{$}}
; VAS-DAG: {{^}}error: global variable @g: a variadic declaration of 'setjmp' is not supported for MSIL; declare it with its C library prototype{{$}}
; VAL-DAG: error: {{.*}}in function va_longjmp void (ptr): a variadic declaration of '_longjmp' is not supported for MSIL; declare it with its C library prototype{{$}}
; VAL-DAG: error: {{.*}}in function addr ptr (): a variadic declaration of '_longjmp' is not supported for MSIL; declare it with its C library prototype{{$}}
; VAL-DAG: {{^}}error: global variable @h: a variadic declaration of '_longjmp' is not supported for MSIL; declare it with its C library prototype{{$}}

; The IR after P6: null initializers, the calls gone with poison values, the
; address null, the bodies kept.
; VAS-IR:       @g = global ptr null
; VAS-IR-LABEL: define i32 @va_setjmp(ptr %b) {
; VAS-IR-NEXT:  entry:
; VAS-IR-NEXT:    call void @ext(i32 poison)
; VAS-IR-NEXT:    call void @ext(i32 poison)
; VAS-IR-NEXT:    ret i32 poison
; VAS-IR-NEXT:  {{^ *}}}{{$}}
; VAS-IR-LABEL: define i32 @after(i32 %x) {
; VAS-IR-NEXT:  entry:
; VAS-IR-NEXT:    %y = add i32 %x, 1
; VAL-IR:       @h = global [2 x ptr] {{zeroinitializer|\[ptr null, ptr null\]}}
; VAL-IR-LABEL: define void @va_longjmp(ptr %b) {
; VAL-IR-NEXT:  entry:
; VAL-IR-NEXT:    unreachable
; VAL-IR-NEXT:  {{^ *}}}{{$}}
; VAL-IR-LABEL: define ptr @addr() {
; VAL-IR-NEXT:  entry:
; VAL-IR-NEXT:    ret ptr null
; VAL-IR-NEXT:  {{^ *}}}{{$}}
; VAL-IR-LABEL: define i32 @after(i32 %x) {
; VAL-IR-NEXT:  entry:
; VAL-IR-NEXT:    %y = add i32 %x, 1

; The function after them compiles.
; VA-IL-LABEL: .method {{.*}}'after'(
; VA-IL-SAME:  int32) cil managed{{$}}
; VA-IL-NEXT:  {{^\{$}}
; VA-IL-NEXT:  {{^}}  .maxstack 2{{$}}
; VA-IL-NEXT:  {{^}}  ldarg.0{{$}}
; VA-IL-NEXT:  {{^}}  ldc.i4.1{{$}}
; VA-IL-NEXT:  {{^}}  add{{$}}
; VA-IL-NEXT:  {{^}}  ret{{$}}
; VA-IL-NEXT:  {{^}}} // end of method 'after'{{$}}

; VSS: error: {{.*}}in function va_sigsetjmp i32 (ptr): a variadic declaration of '__sigsetjmp' is not supported for MSIL; declare it with its C library prototype{{$}}

; RSVF: {{^}}error: '__msil_longjmp' is reserved for the MSIL toolchain (a setjmp and longjmp helper){{$}}
; RSVV: {{^}}error: '__msil_sjlj_throw' is reserved for the MSIL toolchain (a setjmp and longjmp helper){{$}}

; The VLA after the site is reported; the same body without it is not.
; VLA: error: {{.*}}in function vla_site i32 (i32): a dynamic stack allocation that can repeat within one call (a VLA or alloca in a loop) is not supported for MSIL yet{{$}}
; VLA-IL-LABEL: .method {{.*}}'vla_site'(
; VLA-IL-SAME:  int32) cil managed{{$}}
; VLA-IL-NEXT:  {{^\{$}}
; VLA-IL-NEXT:  {{^}}  .maxstack 1{{$}}
; VLA-IL-NEXT:  {{^}}  ldnull{{$}}
; VLA-IL-NEXT:  {{^}}  throw{{$}}
; VLA-IL-NEXT:  {{^}}} // end of method 'vla_site'{{$}}
; VLA-IL-LABEL: .method {{.*}}'vla_nosite'(
; VLA-IL-SAME:  int32) cil managed{{$}}
; VLA-IL-NOT:   {{throw|end of method}}
; VLA-IL:       {{^}}  localloc{{$}}
; VLA-IL-NOT:   {{throw|end of method}}
; VLA-IL:       {{^}}  call void 'use'(native int, int32){{$}}
; VLA-IL-NOT:   throw
; VLA-IL:       {{^}}} // end of method 'vla_nosite'{{$}}
; SIGP: {{^}}error: {{.*}}undefined function 'sigprocmask' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}

;--- diag.ll
source_filename = "sjlj-diag.c"

@sj_ptr = global ptr @setjmp

declare i32 @setjmp(ptr) #0
declare i32 @_setjmp(ptr) #0
declare i32 @vfork() #0
declare i32 @getcontext(ptr) #0
declare i32 @llvm.eh.sjlj.setjmp(ptr)
declare void @llvm.eh.sjlj.longjmp(ptr)

define ptr @take_setjmp() {
entry:
  ret ptr @_setjmp
}

define i32 @use_vfork() {
entry:
  %p = call i32 @vfork() #1
  ret i32 %p
}

define i32 @use_getcontext(ptr %ctx) {
entry:
  %r = call i32 @getcontext(ptr %ctx) #1
  ret i32 %r
}

define i32 @builtin_setjmp(ptr %buf) {
entry:
  %r = call i32 @llvm.eh.sjlj.setjmp(ptr %buf)
  ret i32 %r
}

define void @builtin_longjmp(ptr %buf) {
entry:
  call void @llvm.eh.sjlj.longjmp(ptr %buf)
  unreachable
}

define i32 @after(i32 %x) {
entry:
  %y = add i32 %x, 1
  ret i32 %y
}

attributes #0 = { nounwind returns_twice }
attributes #1 = { returns_twice }

;--- vfork.ll
source_filename = "sjlj-vfork.c"

declare i32 @vfork() #0
declare void @ext(i32)

define void @only_vfork() {
entry:
  %p = call i32 @vfork() #1
  call void @ext(i32 %p)
  ret void
}

attributes #0 = { nounwind returns_twice }
attributes #1 = { returns_twice }

;--- va-setjmp.ll
source_filename = "sjlj-va-setjmp.c"

@g = global ptr @setjmp

declare i32 @setjmp(...) #0

define void @ext(i32 %v) {
entry:
  ret void
}

define i32 @va_setjmp(ptr %b) {
entry:
  %r = call i32 (...) @setjmp(ptr %b) #1
  call void @ext(i32 %r)
  %s = call i32 (...) @setjmp(ptr %b) #1
  call void @ext(i32 %s)
  ret i32 %r
}

define i32 @after(i32 %x) {
entry:
  %y = add i32 %x, 1
  ret i32 %y
}

attributes #0 = { nounwind returns_twice }
attributes #1 = { returns_twice }

;--- va-longjmp.ll
source_filename = "sjlj-va-longjmp.c"

@h = global [2 x ptr] [ptr @_longjmp, ptr null]

declare void @_longjmp(ptr, ...) #0

define void @va_longjmp(ptr %b) {
entry:
  call void (ptr, ...) @_longjmp(ptr %b, i32 1) #1
  unreachable
}

define ptr @addr() {
entry:
  ret ptr @_longjmp
}

define i32 @after(i32 %x) {
entry:
  %y = add i32 %x, 1
  ret i32 %y
}

attributes #0 = { noreturn nounwind }
attributes #1 = { noreturn }

;--- va-sigsetjmp.ll
source_filename = "sjlj-va-sigsetjmp.c"

declare i32 @__sigsetjmp(ptr, ...) #0

define void @ext(i32 %v) {
entry:
  ret void
}

define i32 @va_sigsetjmp(ptr %b) {
entry:
  %r = call i32 (ptr, ...) @__sigsetjmp(ptr %b, i32 1) #1
  call void @ext(i32 %r)
  ret i32 %r
}

attributes #0 = { nounwind returns_twice }
attributes #1 = { returns_twice }

;--- sigmask.ll
source_filename = "sjlj-sigmask.c"

declare i32 @__sigsetjmp(ptr, i32) #0

define void @ext(i32 %v) noinline {
entry:
  ret void
}

define i32 @sig(ptr %b) {
entry:
  %r = call i32 @__sigsetjmp(ptr %b, i32 1) #1
  call void @ext(i32 %r)
  ret i32 %r
}

attributes #0 = { nounwind returns_twice }
attributes #1 = { returns_twice }

;--- rsv-fn.ll
source_filename = "sjlj-reserved-fn.c"

@b = internal global [25 x i64] zeroinitializer

declare void @__msil_longjmp()
declare void @longjmp(ptr, i32) #0

define void @jump() {
entry:
  call void @longjmp(ptr @b, i32 1) #1
  unreachable
}

attributes #0 = { noreturn nounwind }
attributes #1 = { noreturn }

;--- rsv-var.ll
source_filename = "sjlj-reserved-var.c"

@__msil_sjlj_throw = global i32 0
@b = internal global [25 x i64] zeroinitializer

declare void @longjmp(ptr, i32) #0

define void @jump() {
entry:
  call void @longjmp(ptr @b, i32 1) #1
  unreachable
}

attributes #0 = { noreturn nounwind }
attributes #1 = { noreturn }

;--- vla.ll
source_filename = "sjlj-vla.c"

@jb = global [64 x i32] zeroinitializer, align 16

declare i32 @_setjmp(ptr) #0

define void @use(ptr %p, i32 %n) noinline {
entry:
  ret void
}

; int vla_site(int n) {
;   if (_setjmp(jb) == 0) { char buf[n]; use(buf, n); return 0; }
;   return 1;
; }
define i32 @vla_site(i32 %n) {
entry:
  %r = call i32 @_setjmp(ptr @jb) #1
  %c = icmp eq i32 %r, 0
  br i1 %c, label %then, label %out
then:
  %sz = zext i32 %n to i64
  %s = call ptr @llvm.stacksave.p0()
  %buf = alloca i8, i64 %sz, align 16
  call void @use(ptr %buf, i32 %n)
  call void @llvm.stackrestore.p0(ptr %s)
  ret i32 0
out:
  ret i32 1
}

; The same body without the site.
define i32 @vla_nosite(i32 %n) {
entry:
  %sz = zext i32 %n to i64
  %s = call ptr @llvm.stacksave.p0()
  %buf = alloca i8, i64 %sz, align 16
  call void @use(ptr %buf, i32 %n)
  call void @llvm.stackrestore.p0(ptr %s)
  ret i32 0
}

declare ptr @llvm.stacksave.p0()
declare void @llvm.stackrestore.p0(ptr)

attributes #0 = { nounwind returns_twice }
attributes #1 = { returns_twice }

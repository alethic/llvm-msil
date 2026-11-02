; Fixed-arity natives (ImplementationPlan.md 5.15.2 'MSILPrepareVariadics'
; steps 1 and 2, 'Explicit imports' and 'The tables'; 5.14.8 FIXED_ARITY;
; M26). MSILPrepareVariadics (slot P6, -stop-after=msil-prepare-variadics)
; rewrites each call of a declaration on the fixed-arity list of the triple's
; C runtime (Glibc on linux-gnu, UCRT on windows-msvc, Msvcrt on windows-gnu)
; into a call of the same declaration with the non-variadic function type of
; the call's arguments and the call-site attribute "msil-fixed-arity"="<n>"
; (n named parameters), gives the declaration "msil-fixed-arity", so that
; ExpandVariadics leaves it alone, and gives every other variadic function
; that ExpandVariadics expands "msil-variadic". A declaration with an import
; module counts as listed on every triple.
;
; lists.ll calls every name of the three lists (Glibc 25, UCRT 30, Msvcrt 62)
; and three names on no list (printf, clone, makecontext), each declared as
; 'i32 (ptr, ...)' and called with one variadic int: on each triple exactly
; its own list's calls are rewritten (one attribute group "msil-fixed-arity"=
; "1" for all of them, one "msil-fixed-arity" group for the declarations), and
; every other call keeps its variadic type and its declaration gets
; "msil-variadic". Names of another runtime's list are not special.
; imports.ll: an explicit import ("msil-import-module"="libc") called with
; two signatures: whole-program text gives one P/Invoke per call-site
; signature, each called with its own signature; nothing is reported, and the
; module assembles.
; windbl.ll: on the Windows triples every double variadic argument of a
; fixed-arity call is passed as 'bitcast double to i64' (va 2.2, winfp.c), so
; the P/Invoke takes a native int (an i64 is native int, decision D2); AAPCS64
; Linux passes it unchanged (float64).
; sysvfp.ll: on x86_64-unknown-linux-gnu-msil a floating-point variadic
; argument of a fixed-arity call (two explicit imports, one of them a Glibc
; name) is an error, each with its whole text; llc exits with 1 and prints neither 'LLVM
; ERROR' nor a crash report.
; byval.ll: on every triple a byval variadic argument of a fixed-arity call is
; an error with its whole text.
; obj.ll: object mode (5.8.8, 5.14.7 V3): each fixed-arity call site is a
; '<msil>Symbols' MemberRef with its own signature (5.8.3 R3) and a
; SIGNATURES entry with FIXED_ARITY and FixedParams = n, next to an expanded
; call's VARARG entry; a record with FIXED_ARITY is v1.1. The explicit import
; gets FIXED_ARITY on its import stubs.

; RUN: rm -rf %t && split-file %s %t

; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{p} =
; DEFINE: %{lists} = llc -mtriple=%{t} %{o} -stop-after=msil-prepare-variadics < %t/lists.ll -o - \
; DEFINE:   | FileCheck %s --check-prefix=%{p}
; REDEFINE: %{p} = GLIBC
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O0
; RUN: %{lists}
; REDEFINE: %{o} = -O2
; RUN: %{lists}
; REDEFINE: %{t} = aarch64-unknown-linux-gnu-msil
; RUN: %{lists}
; REDEFINE: %{p} = UCRT
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; RUN: %{lists}
; REDEFINE: %{o} = -O0
; RUN: %{lists}
; REDEFINE: %{t} = aarch64-pc-windows-msvc-msil
; RUN: %{lists}
; REDEFINE: %{p} = MSVCRT
; REDEFINE: %{t} = x86_64-w64-windows-gnu-msil
; RUN: %{lists}

; DEFINE: %{imp} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %t/imports.ll \
; DEFINE:   -o %t/imports.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefix=IMP --input-file=%t/imports.%{t}%{o}.il && \
; DEFINE:   FileCheck %s --check-prefix=IMP-PINV --input-file=%t/imports.%{t}%{o}.il
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O0
; RUN: %{imp}
; REDEFINE: %{o} = -O2
; RUN: %{imp}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/imports.lnx.dll %t/imports.x86_64-unknown-linux-gnu-msil-O2.il %}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; RUN: %{imp}
; REDEFINE: %{o} = -O0
; RUN: %{imp}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/imports.win.dll %t/imports.x86_64-pc-windows-msvc-msil-O0.il %}
; REDEFINE: %{t} = aarch64-unknown-linux-gnu-msil
; RUN: %{imp}
; REDEFINE: %{t} = x86_64-w64-windows-gnu-msil
; RUN: %{imp}

; DEFINE: %{dbl} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %t/windbl.ll \
; DEFINE:   -o %t/windbl.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefix=%{p} --input-file=%t/windbl.%{t}%{o}.il
; REDEFINE: %{p} = WINDBL
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %{dbl}
; REDEFINE: %{o} = -O2
; RUN: %{dbl}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/windbl.dll %t/windbl.x86_64-pc-windows-msvc-msil-O2.il %}
; REDEFINE: %{t} = aarch64-pc-windows-msvc-msil
; RUN: %{dbl}
; REDEFINE: %{t} = x86_64-w64-windows-gnu-msil
; RUN: %{dbl}
; REDEFINE: %{p} = A64DBL
; REDEFINE: %{t} = aarch64-unknown-linux-gnu-msil
; RUN: %{dbl}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -stop-after=msil-prepare-variadics < %t/windbl.ll -o - \
; RUN:   | FileCheck %s --check-prefix=WINDBL-IR

; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/sysvfp.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=SYSVFP
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/sysvfp.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=SYSVFP
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/byval.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=BYVAL
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/byval.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=BYVAL
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/byval.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=BYVAL
; RUN: not llc -mtriple=aarch64-unknown-linux-gnu-msil -verify-machineinstrs < %t/byval.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=BYVAL
; RUN: not llc -mtriple=aarch64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/byval.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=BYVAL

; RUN: %if llvm-cil-dump %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs \
; RUN:   < %t/obj-lnx.ll -o %t/obj.lnx.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/obj.lnx.o | FileCheck %s --check-prefix=OBJ-LNX %}
; RUN: %if llvm-cil-dump %{ llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -filetype=obj -verify-machineinstrs \
; RUN:   < %t/obj-win.ll -o %t/obj.win.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/obj.win.o | FileCheck %s --check-prefix=OBJ-WIN %}
; RUN: %if llvm-cil-dump %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs \
; RUN:   < %t/obj-imp.ll -o %t/obj.imp.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/obj.imp.o | FileCheck %s --check-prefix=OBJ-IMP %}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form -verify-machineinstrs < %t/obj-lnx.ll \
; RUN:   -o %t/obj.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=OBJ-LNX-IL --input-file=%t/obj.lnx.il

;--- imports.ll
source_filename = "imports.c"

; IMP-LABEL:  {{^}}.method public static int32 'two'(native int, int32, native int, native int) cil managed{{$}}
; IMP:        {{^}}  call int32 'myprintf'(native int, int32){{$}}
; IMP:        {{^}}  call int32 'myprintf'(native int, native int, native int){{$}}
; IMP:        {{^}}} // end of method 'two'{{$}}
; IMP-PINV-DAG: {{^}}.method {{.*}}pinvokeimpl("libc" {{.*}}) int32 'myprintf'(native int, int32) cil managed preservesig {}{{$}}
; IMP-PINV-DAG: {{^}}.method {{.*}}pinvokeimpl("libc" {{.*}}) int32 'myprintf'(native int, native int, native int) cil managed preservesig {}{{$}}
declare i32 @myprintf(ptr, ...) #0

define i32 @two(ptr %f, i32 %x, ptr %q, i64 %l) {
  %a = call i32 (ptr, ...) @myprintf(ptr %f, i32 %x)
  %b = call i32 (ptr, ...) @myprintf(ptr %f, ptr %q, i64 %l)
  %s = add i32 %a, %b
  ret i32 %s
}

attributes #0 = { "msil-import-module"="libc" }

;--- windbl.ll
source_filename = "windbl.c"

; WINDBL:      {{^}}  call int32 'myprintf'(native int, native int){{$}}
; WINDBL:      {{^}}.method {{.*}}pinvokeimpl("libc" {{.*}}) int32 'myprintf'(native int, native int) cil managed preservesig {}{{$}}
; A64DBL:      {{^}}  call int32 'myprintf'(native int, float64){{$}}
; A64DBL:      {{^}}.method {{.*}}pinvokeimpl("libc" {{.*}}) int32 'myprintf'(native int, float64) cil managed preservesig {}{{$}}
; WINDBL-IR-LABEL: define i32 @dbl(ptr %f, double %d)
; WINDBL-IR:       [[I:%[^ ]+]] = bitcast double %d to i64
; WINDBL-IR:       call i32 @myprintf(ptr %f, i64 [[I]]) #{{[0-9]+}}
declare i32 @myprintf(ptr, ...) #0

define i32 @dbl(ptr %f, double %d) {
  %r = call i32 (ptr, ...) @myprintf(ptr %f, double %d)
  ret i32 %r
}

attributes #0 = { "msil-import-module"="libc" }

;--- sysvfp.ll
source_filename = "sysvfp.c"

; SYSVFP: {{^}}error: {{.*}}passing a floating-point value to native variadic function 'myprintf' is not supported for MSIL: it has no va_list variant, and .NET cannot set the vector-register count (AL) of the x86-64 System V variadic convention{{$}}
; SYSVFP: {{^}}error: {{.*}}passing a floating-point value to native variadic function 'open' is not supported for MSIL: it has no va_list variant, and .NET cannot set the vector-register count (AL) of the x86-64 System V variadic convention{{$}}
declare i32 @myprintf(ptr, ...) #0
declare i32 @open(ptr, i32, ...) #0

define i32 @fp_import(ptr %f, double %d) {
  %r = call i32 (ptr, ...) @myprintf(ptr %f, double %d)
  ret i32 %r
}

define i32 @fp_listed(ptr %p, double %d) {
  %r = call i32 (ptr, i32, ...) @open(ptr %p, i32 0, double %d)
  ret i32 %r
}

attributes #0 = { "msil-import-module"="libc" }

;--- byval.ll
source_filename = "byval.c"

; BYVAL: {{^}}error: {{.*}}passing a structure by value to native variadic function 'myprintf' is not supported for MSIL{{$}}
%S = type { i64, i64 }

declare i32 @myprintf(ptr, ...) #0

define i32 @bv(ptr %f, ptr %s) {
  %r = call i32 (ptr, ...) @myprintf(ptr %f, ptr byval(%S) align 8 %s)
  ret i32 %r
}

attributes #0 = { "msil-import-module"="libc" }

;--- obj-lnx.ll
source_filename = "fa.c"

; OBJ-LNX:     LinkInfo v1.1 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; OBJ-LNX-DAG: Signature 0x0A000001 'open' FIXED_ARITY fixed=2
; OBJ-LNX-DAG: Signature 0x0A000002 'open' FIXED_ARITY fixed=2
; OBJ-LNX-DAG: Signature 0x0A000003 'printf' VARARG fixed=1
; OBJ-LNX-NOT: Signature
; OBJ-LNX-IL:  {{^}}  call int32 [msil.extern]'<msil>Symbols'::'open'(native int, int32){{$}}
; OBJ-LNX-IL:  {{^}}  call int32 [msil.extern]'<msil>Symbols'::'open'(native int, int32, int32){{$}}
; OBJ-LNX-IL:  {{^}}  call int32 [msil.extern]'<msil>Symbols'::'printf'(native int, native int){{$}}
declare i32 @open(ptr, i32, ...)
declare i32 @printf(ptr, ...)

define i32 @use(ptr %p) {
  %a = call i32 (ptr, i32, ...) @open(ptr %p, i32 0)
  %b = call i32 (ptr, i32, ...) @open(ptr %p, i32 64, i32 420)
  %c = call i32 (ptr, ...) @printf(ptr %p, i32 %a)
  %s = add i32 %b, %c
  ret i32 %s
}

;--- obj-win.ll
source_filename = "fa.c"

; OBJ-WIN:     LinkInfo v1.1 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; OBJ-WIN-DAG: Signature 0x0A000001 '_open' FIXED_ARITY fixed=2
; OBJ-WIN-DAG: Signature 0x0A000002 '_open' FIXED_ARITY fixed=2
; OBJ-WIN-DAG: Signature 0x0A000003 'printf' VARARG fixed=1
; OBJ-WIN-NOT: Signature
declare i32 @_open(ptr, i32, ...)
declare i32 @printf(ptr, ...)

define i32 @use(ptr %p) {
  %a = call i32 (ptr, i32, ...) @_open(ptr %p, i32 0)
  %b = call i32 (ptr, i32, ...) @_open(ptr %p, i32 64, i32 420)
  %c = call i32 (ptr, ...) @printf(ptr %p, i32 %a)
  %s = add i32 %b, %c
  ret i32 %s
}

;--- obj-imp.ll
source_filename = "faimp.c"

; OBJ-IMP:          LinkInfo v1.1 triple=x86_64-unknown-linux-gnu-msil
; OBJ-IMP-COUNT-2:  Signature 0x{{0[6A]}}{{[0-9A-F]+}} 'myprintf' FIXED_ARITY fixed=1
; OBJ-IMP-NOT:      Signature
declare i32 @myprintf(ptr, ...) #0

define i32 @two(ptr %f, i32 %x, ptr %q) {
  %a = call i32 (ptr, ...) @myprintf(ptr %f, i32 %x)
  %b = call i32 (ptr, ...) @myprintf(ptr %f, ptr %q, i32 %x)
  %s = add i32 %a, %b
  ret i32 %s
}

attributes #0 = { "msil-import-module"="libc" }

;--- lists.ll
source_filename = "lists.c"

; GLIBC-LABEL: define void @calls(ptr %p)
; UCRT-LABEL: define void @calls(ptr %p)
; MSVCRT-LABEL: define void @calls(ptr %p)
; GLIBC-NEXT:  call i32 (ptr, ...) @_CrtDbgReport(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_CrtDbgReport(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_CrtDbgReport(ptr %p, i32 7) #[[MSVCRT_FA:[0-9]+]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_CrtDbgReportW(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_CrtDbgReportW(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_CrtDbgReportW(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 @__asprintf(ptr %p, i32 7) #[[GLIBC_FA:[0-9]+]]
; UCRT-NEXT:  call i32 (ptr, ...) @__asprintf(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @__asprintf(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 (ptr, ...) @_cscanf(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_cscanf(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_cscanf(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_cscanf_l(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_cscanf_l(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_cscanf_l(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_cscanf_s(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_cscanf_s(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_cscanf_s(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_cscanf_s_l(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_cscanf_s_l(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_cscanf_s_l(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_cwscanf(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_cwscanf(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_cwscanf(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_cwscanf_l(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_cwscanf_l(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_cwscanf_l(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_cwscanf_s(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_cwscanf_s(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_cwscanf_s(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_cwscanf_s_l(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_cwscanf_s_l(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_cwscanf_s_l(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_execl(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @_execl(ptr %p, i32 7) #[[UCRT_FA:[0-9]+]]
; MSVCRT-NEXT:  call i32 @_execl(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_execle(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @_execle(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 @_execle(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_execlp(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @_execlp(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 @_execlp(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_execlpe(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @_execlpe(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 @_execlpe(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_fscanf_l(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_fscanf_l(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_fscanf_l(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_fscanf_s_l(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_fscanf_s_l(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_fscanf_s_l(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_fwscanf_l(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_fwscanf_l(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_fwscanf_l(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_fwscanf_s_l(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_fwscanf_s_l(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_fwscanf_s_l(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_open(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @_open(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 @_open(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_scanf_l(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_scanf_l(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_scanf_l(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_scanf_s_l(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_scanf_s_l(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_scanf_s_l(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_snscanf(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_snscanf(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_snscanf(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_snscanf_l(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_snscanf_l(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_snscanf_l(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_snscanf_s(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_snscanf_s(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_snscanf_s(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_snscanf_s_l(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_snscanf_s_l(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_snscanf_s_l(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_snwscanf(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_snwscanf(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_snwscanf(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_snwscanf_l(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_snwscanf_l(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_snwscanf_l(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_snwscanf_s(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_snwscanf_s(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_snwscanf_s(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_snwscanf_s_l(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_snwscanf_s_l(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_snwscanf_s_l(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_sopen(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @_sopen(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 @_sopen(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_spawnl(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @_spawnl(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 @_spawnl(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_spawnle(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @_spawnle(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 @_spawnle(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_spawnlp(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @_spawnlp(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 @_spawnlp(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_spawnlpe(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @_spawnlpe(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 @_spawnlpe(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_sscanf_l(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_sscanf_l(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_sscanf_l(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_sscanf_s_l(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_sscanf_s_l(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_sscanf_s_l(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_swscanf_l(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_swscanf_l(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_swscanf_l(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_swscanf_s_l(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_swscanf_s_l(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_swscanf_s_l(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_wexecl(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @_wexecl(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 @_wexecl(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_wexecle(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @_wexecle(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 @_wexecle(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_wexeclp(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @_wexeclp(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 @_wexeclp(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_wexeclpe(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @_wexeclpe(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 @_wexeclpe(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_wopen(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @_wopen(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 @_wopen(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_wscanf_l(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_wscanf_l(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_wscanf_l(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_wscanf_s_l(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @_wscanf_s_l(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @_wscanf_s_l(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_wsopen(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @_wsopen(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 @_wsopen(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_wspawnl(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @_wspawnl(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 @_wspawnl(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_wspawnle(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @_wspawnle(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 @_wspawnle(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_wspawnlp(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @_wspawnlp(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 @_wspawnlp(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @_wspawnlpe(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @_wspawnlpe(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 @_wspawnlpe(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 @argp_error(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 (ptr, ...) @argp_error(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @argp_error(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 @argp_failure(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 (ptr, ...) @argp_failure(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @argp_failure(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 @error(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 (ptr, ...) @error(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @error(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 @error_at_line(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 (ptr, ...) @error_at_line(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @error_at_line(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 @execl(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 @execl(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 (ptr, ...) @execl(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 @execle(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 @execle(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 (ptr, ...) @execle(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 @execlp(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 @execlp(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 (ptr, ...) @execlp(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 (ptr, ...) @execlpe(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @execlpe(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 (ptr, ...) @execlpe(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 @fcntl(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 (ptr, ...) @fcntl(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @fcntl(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 @fcntl64(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 (ptr, ...) @fcntl64(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @fcntl64(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 (ptr, ...) @fscanf(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @fscanf(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @fscanf(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @fscanf_s(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @fscanf_s(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @fscanf_s(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @fwscanf(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @fwscanf(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @fwscanf(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @fwscanf_s(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @fwscanf_s(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @fwscanf_s(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 @ioctl(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 (ptr, ...) @ioctl(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @ioctl(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 @mq_open(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 (ptr, ...) @mq_open(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @mq_open(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 @mremap(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 (ptr, ...) @mremap(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @mremap(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 @open(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 @open(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 (ptr, ...) @open(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 @open64(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 (ptr, ...) @open64(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @open64(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 @openat(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 (ptr, ...) @openat(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @openat(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 @openat64(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 (ptr, ...) @openat64(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @openat64(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 @prctl(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 (ptr, ...) @prctl(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @prctl(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 @ptrace(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 (ptr, ...) @ptrace(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @ptrace(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 (ptr, ...) @scanf(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @scanf(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @scanf(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @scanf_s(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @scanf_s(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @scanf_s(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 @sem_open(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 (ptr, ...) @sem_open(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @sem_open(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 @semctl(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 (ptr, ...) @semctl(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @semctl(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 (ptr, ...) @sopen(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @sopen(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 (ptr, ...) @sopen(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 (ptr, ...) @spawnl(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @spawnl(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 (ptr, ...) @spawnl(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 (ptr, ...) @spawnle(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @spawnle(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 (ptr, ...) @spawnle(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 (ptr, ...) @spawnlp(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @spawnlp(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 (ptr, ...) @spawnlp(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 (ptr, ...) @spawnlpe(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 @spawnlpe(ptr %p, i32 7) #[[UCRT_FA]]
; MSVCRT-NEXT:  call i32 (ptr, ...) @spawnlpe(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 (ptr, ...) @sscanf(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @sscanf(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @sscanf(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @sscanf_s(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @sscanf_s(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @sscanf_s(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 @strfmon(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 (ptr, ...) @strfmon(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @strfmon(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 @strfmon_l(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 (ptr, ...) @strfmon_l(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @strfmon_l(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 (ptr, ...) @swscanf(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @swscanf(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @swscanf(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @swscanf_s(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @swscanf_s(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @swscanf_s(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 @syscall(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 (ptr, ...) @syscall(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @syscall(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 @ulimit(ptr %p, i32 7) #[[GLIBC_FA]]
; UCRT-NEXT:  call i32 (ptr, ...) @ulimit(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @ulimit(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 (ptr, ...) @wscanf(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @wscanf(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @wscanf(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @wscanf_s(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @wscanf_s(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 @wscanf_s(ptr %p, i32 7) #[[MSVCRT_FA]]
; GLIBC-NEXT:  call i32 (ptr, ...) @printf(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @printf(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @printf(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 (ptr, ...) @clone(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @clone(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @clone(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  call i32 (ptr, ...) @makecontext(ptr %p, i32 7){{$}}
; UCRT-NEXT:  call i32 (ptr, ...) @makecontext(ptr %p, i32 7){{$}}
; MSVCRT-NEXT:  call i32 (ptr, ...) @makecontext(ptr %p, i32 7){{$}}
; GLIBC-NEXT:  ret void
; UCRT-NEXT:  ret void
; MSVCRT-NEXT:  ret void
define void @calls(ptr %p) {
  call i32 (ptr, ...) @_CrtDbgReport(ptr %p, i32 7)
  call i32 (ptr, ...) @_CrtDbgReportW(ptr %p, i32 7)
  call i32 (ptr, ...) @__asprintf(ptr %p, i32 7)
  call i32 (ptr, ...) @_cscanf(ptr %p, i32 7)
  call i32 (ptr, ...) @_cscanf_l(ptr %p, i32 7)
  call i32 (ptr, ...) @_cscanf_s(ptr %p, i32 7)
  call i32 (ptr, ...) @_cscanf_s_l(ptr %p, i32 7)
  call i32 (ptr, ...) @_cwscanf(ptr %p, i32 7)
  call i32 (ptr, ...) @_cwscanf_l(ptr %p, i32 7)
  call i32 (ptr, ...) @_cwscanf_s(ptr %p, i32 7)
  call i32 (ptr, ...) @_cwscanf_s_l(ptr %p, i32 7)
  call i32 (ptr, ...) @_execl(ptr %p, i32 7)
  call i32 (ptr, ...) @_execle(ptr %p, i32 7)
  call i32 (ptr, ...) @_execlp(ptr %p, i32 7)
  call i32 (ptr, ...) @_execlpe(ptr %p, i32 7)
  call i32 (ptr, ...) @_fscanf_l(ptr %p, i32 7)
  call i32 (ptr, ...) @_fscanf_s_l(ptr %p, i32 7)
  call i32 (ptr, ...) @_fwscanf_l(ptr %p, i32 7)
  call i32 (ptr, ...) @_fwscanf_s_l(ptr %p, i32 7)
  call i32 (ptr, ...) @_open(ptr %p, i32 7)
  call i32 (ptr, ...) @_scanf_l(ptr %p, i32 7)
  call i32 (ptr, ...) @_scanf_s_l(ptr %p, i32 7)
  call i32 (ptr, ...) @_snscanf(ptr %p, i32 7)
  call i32 (ptr, ...) @_snscanf_l(ptr %p, i32 7)
  call i32 (ptr, ...) @_snscanf_s(ptr %p, i32 7)
  call i32 (ptr, ...) @_snscanf_s_l(ptr %p, i32 7)
  call i32 (ptr, ...) @_snwscanf(ptr %p, i32 7)
  call i32 (ptr, ...) @_snwscanf_l(ptr %p, i32 7)
  call i32 (ptr, ...) @_snwscanf_s(ptr %p, i32 7)
  call i32 (ptr, ...) @_snwscanf_s_l(ptr %p, i32 7)
  call i32 (ptr, ...) @_sopen(ptr %p, i32 7)
  call i32 (ptr, ...) @_spawnl(ptr %p, i32 7)
  call i32 (ptr, ...) @_spawnle(ptr %p, i32 7)
  call i32 (ptr, ...) @_spawnlp(ptr %p, i32 7)
  call i32 (ptr, ...) @_spawnlpe(ptr %p, i32 7)
  call i32 (ptr, ...) @_sscanf_l(ptr %p, i32 7)
  call i32 (ptr, ...) @_sscanf_s_l(ptr %p, i32 7)
  call i32 (ptr, ...) @_swscanf_l(ptr %p, i32 7)
  call i32 (ptr, ...) @_swscanf_s_l(ptr %p, i32 7)
  call i32 (ptr, ...) @_wexecl(ptr %p, i32 7)
  call i32 (ptr, ...) @_wexecle(ptr %p, i32 7)
  call i32 (ptr, ...) @_wexeclp(ptr %p, i32 7)
  call i32 (ptr, ...) @_wexeclpe(ptr %p, i32 7)
  call i32 (ptr, ...) @_wopen(ptr %p, i32 7)
  call i32 (ptr, ...) @_wscanf_l(ptr %p, i32 7)
  call i32 (ptr, ...) @_wscanf_s_l(ptr %p, i32 7)
  call i32 (ptr, ...) @_wsopen(ptr %p, i32 7)
  call i32 (ptr, ...) @_wspawnl(ptr %p, i32 7)
  call i32 (ptr, ...) @_wspawnle(ptr %p, i32 7)
  call i32 (ptr, ...) @_wspawnlp(ptr %p, i32 7)
  call i32 (ptr, ...) @_wspawnlpe(ptr %p, i32 7)
  call i32 (ptr, ...) @argp_error(ptr %p, i32 7)
  call i32 (ptr, ...) @argp_failure(ptr %p, i32 7)
  call i32 (ptr, ...) @error(ptr %p, i32 7)
  call i32 (ptr, ...) @error_at_line(ptr %p, i32 7)
  call i32 (ptr, ...) @execl(ptr %p, i32 7)
  call i32 (ptr, ...) @execle(ptr %p, i32 7)
  call i32 (ptr, ...) @execlp(ptr %p, i32 7)
  call i32 (ptr, ...) @execlpe(ptr %p, i32 7)
  call i32 (ptr, ...) @fcntl(ptr %p, i32 7)
  call i32 (ptr, ...) @fcntl64(ptr %p, i32 7)
  call i32 (ptr, ...) @fscanf(ptr %p, i32 7)
  call i32 (ptr, ...) @fscanf_s(ptr %p, i32 7)
  call i32 (ptr, ...) @fwscanf(ptr %p, i32 7)
  call i32 (ptr, ...) @fwscanf_s(ptr %p, i32 7)
  call i32 (ptr, ...) @ioctl(ptr %p, i32 7)
  call i32 (ptr, ...) @mq_open(ptr %p, i32 7)
  call i32 (ptr, ...) @mremap(ptr %p, i32 7)
  call i32 (ptr, ...) @open(ptr %p, i32 7)
  call i32 (ptr, ...) @open64(ptr %p, i32 7)
  call i32 (ptr, ...) @openat(ptr %p, i32 7)
  call i32 (ptr, ...) @openat64(ptr %p, i32 7)
  call i32 (ptr, ...) @prctl(ptr %p, i32 7)
  call i32 (ptr, ...) @ptrace(ptr %p, i32 7)
  call i32 (ptr, ...) @scanf(ptr %p, i32 7)
  call i32 (ptr, ...) @scanf_s(ptr %p, i32 7)
  call i32 (ptr, ...) @sem_open(ptr %p, i32 7)
  call i32 (ptr, ...) @semctl(ptr %p, i32 7)
  call i32 (ptr, ...) @sopen(ptr %p, i32 7)
  call i32 (ptr, ...) @spawnl(ptr %p, i32 7)
  call i32 (ptr, ...) @spawnle(ptr %p, i32 7)
  call i32 (ptr, ...) @spawnlp(ptr %p, i32 7)
  call i32 (ptr, ...) @spawnlpe(ptr %p, i32 7)
  call i32 (ptr, ...) @sscanf(ptr %p, i32 7)
  call i32 (ptr, ...) @sscanf_s(ptr %p, i32 7)
  call i32 (ptr, ...) @strfmon(ptr %p, i32 7)
  call i32 (ptr, ...) @strfmon_l(ptr %p, i32 7)
  call i32 (ptr, ...) @swscanf(ptr %p, i32 7)
  call i32 (ptr, ...) @swscanf_s(ptr %p, i32 7)
  call i32 (ptr, ...) @syscall(ptr %p, i32 7)
  call i32 (ptr, ...) @ulimit(ptr %p, i32 7)
  call i32 (ptr, ...) @wscanf(ptr %p, i32 7)
  call i32 (ptr, ...) @wscanf_s(ptr %p, i32 7)
  call i32 (ptr, ...) @printf(ptr %p, i32 7)
  call i32 (ptr, ...) @clone(ptr %p, i32 7)
  call i32 (ptr, ...) @makecontext(ptr %p, i32 7)
  ret void
}

; GLIBC: declare i32 @_CrtDbgReport(ptr, ...) #[[GLIBC_VD:[0-9]+]]
; UCRT: declare i32 @_CrtDbgReport(ptr, ...) #[[UCRT_VD:[0-9]+]]
; MSVCRT: declare i32 @_CrtDbgReport(ptr, ...) #[[MSVCRT_FD:[0-9]+]]
declare i32 @_CrtDbgReport(ptr, ...)
; GLIBC: declare i32 @_CrtDbgReportW(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_CrtDbgReportW(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_CrtDbgReportW(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_CrtDbgReportW(ptr, ...)
; GLIBC: declare i32 @__asprintf(ptr, ...) #[[GLIBC_FD:[0-9]+]]
; UCRT: declare i32 @__asprintf(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @__asprintf(ptr, ...) #[[MSVCRT_VD:[0-9]+]]
declare i32 @__asprintf(ptr, ...)
; GLIBC: declare i32 @_cscanf(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_cscanf(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_cscanf(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_cscanf(ptr, ...)
; GLIBC: declare i32 @_cscanf_l(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_cscanf_l(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_cscanf_l(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_cscanf_l(ptr, ...)
; GLIBC: declare i32 @_cscanf_s(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_cscanf_s(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_cscanf_s(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_cscanf_s(ptr, ...)
; GLIBC: declare i32 @_cscanf_s_l(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_cscanf_s_l(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_cscanf_s_l(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_cscanf_s_l(ptr, ...)
; GLIBC: declare i32 @_cwscanf(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_cwscanf(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_cwscanf(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_cwscanf(ptr, ...)
; GLIBC: declare i32 @_cwscanf_l(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_cwscanf_l(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_cwscanf_l(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_cwscanf_l(ptr, ...)
; GLIBC: declare i32 @_cwscanf_s(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_cwscanf_s(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_cwscanf_s(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_cwscanf_s(ptr, ...)
; GLIBC: declare i32 @_cwscanf_s_l(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_cwscanf_s_l(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_cwscanf_s_l(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_cwscanf_s_l(ptr, ...)
; GLIBC: declare i32 @_execl(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_execl(ptr, ...) #[[UCRT_FD:[0-9]+]]
; MSVCRT: declare i32 @_execl(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_execl(ptr, ...)
; GLIBC: declare i32 @_execle(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_execle(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @_execle(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_execle(ptr, ...)
; GLIBC: declare i32 @_execlp(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_execlp(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @_execlp(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_execlp(ptr, ...)
; GLIBC: declare i32 @_execlpe(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_execlpe(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @_execlpe(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_execlpe(ptr, ...)
; GLIBC: declare i32 @_fscanf_l(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_fscanf_l(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_fscanf_l(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_fscanf_l(ptr, ...)
; GLIBC: declare i32 @_fscanf_s_l(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_fscanf_s_l(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_fscanf_s_l(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_fscanf_s_l(ptr, ...)
; GLIBC: declare i32 @_fwscanf_l(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_fwscanf_l(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_fwscanf_l(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_fwscanf_l(ptr, ...)
; GLIBC: declare i32 @_fwscanf_s_l(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_fwscanf_s_l(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_fwscanf_s_l(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_fwscanf_s_l(ptr, ...)
; GLIBC: declare i32 @_open(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_open(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @_open(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_open(ptr, ...)
; GLIBC: declare i32 @_scanf_l(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_scanf_l(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_scanf_l(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_scanf_l(ptr, ...)
; GLIBC: declare i32 @_scanf_s_l(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_scanf_s_l(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_scanf_s_l(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_scanf_s_l(ptr, ...)
; GLIBC: declare i32 @_snscanf(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_snscanf(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_snscanf(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_snscanf(ptr, ...)
; GLIBC: declare i32 @_snscanf_l(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_snscanf_l(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_snscanf_l(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_snscanf_l(ptr, ...)
; GLIBC: declare i32 @_snscanf_s(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_snscanf_s(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_snscanf_s(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_snscanf_s(ptr, ...)
; GLIBC: declare i32 @_snscanf_s_l(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_snscanf_s_l(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_snscanf_s_l(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_snscanf_s_l(ptr, ...)
; GLIBC: declare i32 @_snwscanf(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_snwscanf(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_snwscanf(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_snwscanf(ptr, ...)
; GLIBC: declare i32 @_snwscanf_l(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_snwscanf_l(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_snwscanf_l(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_snwscanf_l(ptr, ...)
; GLIBC: declare i32 @_snwscanf_s(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_snwscanf_s(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_snwscanf_s(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_snwscanf_s(ptr, ...)
; GLIBC: declare i32 @_snwscanf_s_l(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_snwscanf_s_l(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_snwscanf_s_l(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_snwscanf_s_l(ptr, ...)
; GLIBC: declare i32 @_sopen(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_sopen(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @_sopen(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_sopen(ptr, ...)
; GLIBC: declare i32 @_spawnl(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_spawnl(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @_spawnl(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_spawnl(ptr, ...)
; GLIBC: declare i32 @_spawnle(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_spawnle(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @_spawnle(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_spawnle(ptr, ...)
; GLIBC: declare i32 @_spawnlp(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_spawnlp(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @_spawnlp(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_spawnlp(ptr, ...)
; GLIBC: declare i32 @_spawnlpe(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_spawnlpe(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @_spawnlpe(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_spawnlpe(ptr, ...)
; GLIBC: declare i32 @_sscanf_l(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_sscanf_l(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_sscanf_l(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_sscanf_l(ptr, ...)
; GLIBC: declare i32 @_sscanf_s_l(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_sscanf_s_l(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_sscanf_s_l(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_sscanf_s_l(ptr, ...)
; GLIBC: declare i32 @_swscanf_l(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_swscanf_l(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_swscanf_l(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_swscanf_l(ptr, ...)
; GLIBC: declare i32 @_swscanf_s_l(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_swscanf_s_l(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_swscanf_s_l(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_swscanf_s_l(ptr, ...)
; GLIBC: declare i32 @_wexecl(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_wexecl(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @_wexecl(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_wexecl(ptr, ...)
; GLIBC: declare i32 @_wexecle(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_wexecle(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @_wexecle(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_wexecle(ptr, ...)
; GLIBC: declare i32 @_wexeclp(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_wexeclp(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @_wexeclp(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_wexeclp(ptr, ...)
; GLIBC: declare i32 @_wexeclpe(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_wexeclpe(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @_wexeclpe(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_wexeclpe(ptr, ...)
; GLIBC: declare i32 @_wopen(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_wopen(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @_wopen(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_wopen(ptr, ...)
; GLIBC: declare i32 @_wscanf_l(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_wscanf_l(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_wscanf_l(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_wscanf_l(ptr, ...)
; GLIBC: declare i32 @_wscanf_s_l(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_wscanf_s_l(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @_wscanf_s_l(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_wscanf_s_l(ptr, ...)
; GLIBC: declare i32 @_wsopen(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_wsopen(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @_wsopen(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_wsopen(ptr, ...)
; GLIBC: declare i32 @_wspawnl(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_wspawnl(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @_wspawnl(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_wspawnl(ptr, ...)
; GLIBC: declare i32 @_wspawnle(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_wspawnle(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @_wspawnle(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_wspawnle(ptr, ...)
; GLIBC: declare i32 @_wspawnlp(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_wspawnlp(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @_wspawnlp(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_wspawnlp(ptr, ...)
; GLIBC: declare i32 @_wspawnlpe(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @_wspawnlpe(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @_wspawnlpe(ptr, ...) #[[MSVCRT_FD]]
declare i32 @_wspawnlpe(ptr, ...)
; GLIBC: declare i32 @argp_error(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @argp_error(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @argp_error(ptr, ...) #[[MSVCRT_VD]]
declare i32 @argp_error(ptr, ...)
; GLIBC: declare i32 @argp_failure(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @argp_failure(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @argp_failure(ptr, ...) #[[MSVCRT_VD]]
declare i32 @argp_failure(ptr, ...)
; GLIBC: declare i32 @error(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @error(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @error(ptr, ...) #[[MSVCRT_VD]]
declare i32 @error(ptr, ...)
; GLIBC: declare i32 @error_at_line(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @error_at_line(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @error_at_line(ptr, ...) #[[MSVCRT_VD]]
declare i32 @error_at_line(ptr, ...)
; GLIBC: declare i32 @execl(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @execl(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @execl(ptr, ...) #[[MSVCRT_VD]]
declare i32 @execl(ptr, ...)
; GLIBC: declare i32 @execle(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @execle(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @execle(ptr, ...) #[[MSVCRT_VD]]
declare i32 @execle(ptr, ...)
; GLIBC: declare i32 @execlp(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @execlp(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @execlp(ptr, ...) #[[MSVCRT_VD]]
declare i32 @execlp(ptr, ...)
; GLIBC: declare i32 @execlpe(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @execlpe(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @execlpe(ptr, ...) #[[MSVCRT_VD]]
declare i32 @execlpe(ptr, ...)
; GLIBC: declare i32 @fcntl(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @fcntl(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @fcntl(ptr, ...) #[[MSVCRT_VD]]
declare i32 @fcntl(ptr, ...)
; GLIBC: declare i32 @fcntl64(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @fcntl64(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @fcntl64(ptr, ...) #[[MSVCRT_VD]]
declare i32 @fcntl64(ptr, ...)
; GLIBC: declare i32 @fscanf(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @fscanf(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @fscanf(ptr, ...) #[[MSVCRT_FD]]
declare i32 @fscanf(ptr, ...)
; GLIBC: declare i32 @fscanf_s(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @fscanf_s(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @fscanf_s(ptr, ...) #[[MSVCRT_FD]]
declare i32 @fscanf_s(ptr, ...)
; GLIBC: declare i32 @fwscanf(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @fwscanf(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @fwscanf(ptr, ...) #[[MSVCRT_FD]]
declare i32 @fwscanf(ptr, ...)
; GLIBC: declare i32 @fwscanf_s(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @fwscanf_s(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @fwscanf_s(ptr, ...) #[[MSVCRT_FD]]
declare i32 @fwscanf_s(ptr, ...)
; GLIBC: declare i32 @ioctl(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @ioctl(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @ioctl(ptr, ...) #[[MSVCRT_VD]]
declare i32 @ioctl(ptr, ...)
; GLIBC: declare i32 @mq_open(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @mq_open(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @mq_open(ptr, ...) #[[MSVCRT_VD]]
declare i32 @mq_open(ptr, ...)
; GLIBC: declare i32 @mremap(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @mremap(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @mremap(ptr, ...) #[[MSVCRT_VD]]
declare i32 @mremap(ptr, ...)
; GLIBC: declare i32 @open(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @open(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @open(ptr, ...) #[[MSVCRT_VD]]
declare i32 @open(ptr, ...)
; GLIBC: declare i32 @open64(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @open64(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @open64(ptr, ...) #[[MSVCRT_VD]]
declare i32 @open64(ptr, ...)
; GLIBC: declare i32 @openat(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @openat(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @openat(ptr, ...) #[[MSVCRT_VD]]
declare i32 @openat(ptr, ...)
; GLIBC: declare i32 @openat64(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @openat64(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @openat64(ptr, ...) #[[MSVCRT_VD]]
declare i32 @openat64(ptr, ...)
; GLIBC: declare i32 @prctl(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @prctl(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @prctl(ptr, ...) #[[MSVCRT_VD]]
declare i32 @prctl(ptr, ...)
; GLIBC: declare i32 @ptrace(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @ptrace(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @ptrace(ptr, ...) #[[MSVCRT_VD]]
declare i32 @ptrace(ptr, ...)
; GLIBC: declare i32 @scanf(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @scanf(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @scanf(ptr, ...) #[[MSVCRT_FD]]
declare i32 @scanf(ptr, ...)
; GLIBC: declare i32 @scanf_s(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @scanf_s(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @scanf_s(ptr, ...) #[[MSVCRT_FD]]
declare i32 @scanf_s(ptr, ...)
; GLIBC: declare i32 @sem_open(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @sem_open(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @sem_open(ptr, ...) #[[MSVCRT_VD]]
declare i32 @sem_open(ptr, ...)
; GLIBC: declare i32 @semctl(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @semctl(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @semctl(ptr, ...) #[[MSVCRT_VD]]
declare i32 @semctl(ptr, ...)
; GLIBC: declare i32 @sopen(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @sopen(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @sopen(ptr, ...) #[[MSVCRT_VD]]
declare i32 @sopen(ptr, ...)
; GLIBC: declare i32 @spawnl(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @spawnl(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @spawnl(ptr, ...) #[[MSVCRT_VD]]
declare i32 @spawnl(ptr, ...)
; GLIBC: declare i32 @spawnle(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @spawnle(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @spawnle(ptr, ...) #[[MSVCRT_VD]]
declare i32 @spawnle(ptr, ...)
; GLIBC: declare i32 @spawnlp(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @spawnlp(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @spawnlp(ptr, ...) #[[MSVCRT_VD]]
declare i32 @spawnlp(ptr, ...)
; GLIBC: declare i32 @spawnlpe(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @spawnlpe(ptr, ...) #[[UCRT_FD]]
; MSVCRT: declare i32 @spawnlpe(ptr, ...) #[[MSVCRT_VD]]
declare i32 @spawnlpe(ptr, ...)
; GLIBC: declare i32 @sscanf(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @sscanf(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @sscanf(ptr, ...) #[[MSVCRT_FD]]
declare i32 @sscanf(ptr, ...)
; GLIBC: declare i32 @sscanf_s(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @sscanf_s(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @sscanf_s(ptr, ...) #[[MSVCRT_FD]]
declare i32 @sscanf_s(ptr, ...)
; GLIBC: declare i32 @strfmon(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @strfmon(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @strfmon(ptr, ...) #[[MSVCRT_VD]]
declare i32 @strfmon(ptr, ...)
; GLIBC: declare i32 @strfmon_l(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @strfmon_l(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @strfmon_l(ptr, ...) #[[MSVCRT_VD]]
declare i32 @strfmon_l(ptr, ...)
; GLIBC: declare i32 @swscanf(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @swscanf(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @swscanf(ptr, ...) #[[MSVCRT_FD]]
declare i32 @swscanf(ptr, ...)
; GLIBC: declare i32 @swscanf_s(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @swscanf_s(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @swscanf_s(ptr, ...) #[[MSVCRT_FD]]
declare i32 @swscanf_s(ptr, ...)
; GLIBC: declare i32 @syscall(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @syscall(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @syscall(ptr, ...) #[[MSVCRT_VD]]
declare i32 @syscall(ptr, ...)
; GLIBC: declare i32 @ulimit(ptr, ...) #[[GLIBC_FD]]
; UCRT: declare i32 @ulimit(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @ulimit(ptr, ...) #[[MSVCRT_VD]]
declare i32 @ulimit(ptr, ...)
; GLIBC: declare i32 @wscanf(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @wscanf(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @wscanf(ptr, ...) #[[MSVCRT_FD]]
declare i32 @wscanf(ptr, ...)
; GLIBC: declare i32 @wscanf_s(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @wscanf_s(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @wscanf_s(ptr, ...) #[[MSVCRT_FD]]
declare i32 @wscanf_s(ptr, ...)
; GLIBC: declare i32 @printf(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @printf(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @printf(ptr, ...) #[[MSVCRT_VD]]
declare i32 @printf(ptr, ...)
; GLIBC: declare i32 @clone(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @clone(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @clone(ptr, ...) #[[MSVCRT_VD]]
declare i32 @clone(ptr, ...)
; GLIBC: declare i32 @makecontext(ptr, ...) #[[GLIBC_VD]]
; UCRT: declare i32 @makecontext(ptr, ...) #[[UCRT_VD]]
; MSVCRT: declare i32 @makecontext(ptr, ...) #[[MSVCRT_VD]]
declare i32 @makecontext(ptr, ...)

; GLIBC-DAG: attributes #[[GLIBC_FA]] = { "msil-fixed-arity"="1" }
; GLIBC-DAG: attributes #[[GLIBC_FD]] = { "msil-fixed-arity" }
; GLIBC-DAG: attributes #[[GLIBC_VD]] = { "msil-variadic" }
; UCRT-DAG: attributes #[[UCRT_FA]] = { "msil-fixed-arity"="1" }
; UCRT-DAG: attributes #[[UCRT_FD]] = { "msil-fixed-arity" }
; UCRT-DAG: attributes #[[UCRT_VD]] = { "msil-variadic" }
; MSVCRT-DAG: attributes #[[MSVCRT_FA]] = { "msil-fixed-arity"="1" }
; MSVCRT-DAG: attributes #[[MSVCRT_FD]] = { "msil-fixed-arity" }
; MSVCRT-DAG: attributes #[[MSVCRT_VD]] = { "msil-variadic" }

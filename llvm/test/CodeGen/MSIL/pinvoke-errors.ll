; Undefined functions outside the Phase-1 import table are errors
; (ImplementationPlan.md decision D14 'Every other undefined function is an
; error: Phase 1 has no linker', M8.c; plan section 2 'Diagnostics leave
; well-formed output'). The end of the module reports, once per function and
; whatever the number of references, every undefined function that the module
; calls, takes the address of in code, or stores in an initializer, and that
; is not an import:
;   undefined function '<name>' is not defined in this module and is not a
;   Phase-1 import (puts, putchar, the C math library, msil-import-module);
;   Phase 1 has no linker
; and declares no P/Invoke for it, while the imports of the same module get
; theirs. A declaration that is never used is not reported. The order of the
; errors is not pinned: they are matched as a group, and a second FileCheck
; run counts them. llc exits with 1 and prints neither 'LLVM ERROR' nor a
; crash report, and the IL (taken from stdout, as llc deletes its -o file
; after an error) still has every method. A variadic function cannot be a
; P/Invoke (D14, D19; Design.md 4.7): an import with "msil-import-module" that
; is variadic is an error naming it when its address is used (its calls are
; already errors of LowerCall, call-errors.ll), also without a declaration.
; An undefined function that is only a global constructor is an error too:
; the .cctor would call a method that does not exist. On the Windows triples a
; math function that the C runtime DLL does not export (D14 as amended in plan
; section 12, 'Windows import sets follow the DLL exports') is not an import
; either: calling it is the same error, without a declaration. On
; windows-msvc these are hypotf, ldexpf and frexpf (ucrtbase.dll exports only
; the double functions, which get their P/Invokes into "ucrtbase" in the same
; module), on x86_64 and aarch64 alike; on windows-gnu they are hypot, the C99
; functions such as round, trunc and fmin, and ldexpf and frexpf, while sqrt,
; which msvcrt.dll exports, gets its P/Invoke into "msvcrt".
;
; An import whose signature a P/Invoke cannot pass natively (Phase 1 marshals
; only int32, native int, float32 and float64 values and void results) is an
; error naming it, once, whether code calls it or an initializer takes its
; address, and it gets no declaration:
;   imported function '<name>' has a parameter or result that a P/Invoke
;   cannot pass natively (aggregates, vectors, 128-bit integers and half are
;   not supported for MSIL yet)
; Here: i128, { i64, i64 }, { double, double } and <2 x float> results of
; called imports, and a half result, a byval parameter and an i128 parameter
; of imports referenced only from data; the scalar import of the same module
; still gets its P/Invoke. An sret parameter is a hidden pointer that
; the native conventions do not pass as an ordinary first argument (AAPCS64
; passes it in X8), so an import with one is the same error, on aarch64 as on
; x86_64 (sret.ll).

; DEFINE: %{nocrash} = --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; RUN: split-file %s %t
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/undef.ll -o /dev/null \
; RUN:   2> %t/undef.lnx.err
; RUN: FileCheck %s --check-prefix=UNDEF %{nocrash} --implicit-check-not=warning: --input-file=%t/undef.lnx.err
; RUN: FileCheck %s --check-prefix=NERR %{nocrash} --input-file=%t/undef.lnx.err
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/undef.ll -o /dev/null \
; RUN:   2> %t/undef.win.err
; RUN: FileCheck %s --check-prefix=UNDEF %{nocrash} --implicit-check-not=warning: --input-file=%t/undef.win.err
; RUN: FileCheck %s --check-prefix=NERR %{nocrash} --input-file=%t/undef.win.err
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/undef.ll -o /dev/null \
; RUN:   2> %t/undef.win.o0.err
; RUN: FileCheck %s --check-prefix=UNDEF %{nocrash} --implicit-check-not=warning: --input-file=%t/undef.win.o0.err
; RUN: FileCheck %s --check-prefix=NERR %{nocrash} --input-file=%t/undef.win.o0.err
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/undef.ll \
; RUN:   > %t/undef.lnx.il
; RUN: FileCheck %s --check-prefixes=UNDEF-IL,UNDEF-LNX --input-file=%t/undef.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/undef.ll \
; RUN:   > %t/undef.win.il
; RUN: FileCheck %s --check-prefixes=UNDEF-IL,UNDEF-WIN --input-file=%t/undef.win.il

; DEFINE: %{diag} = FileCheck %s %{nocrash} --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/variadic-data.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VARDATA
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/variadic-data.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VARDATA
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/variadic-data.ll \
; RUN:   | FileCheck %s --check-prefix=NODECL
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/variadic-code.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VARCODE
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/variadic-code.ll -o /dev/null \
; RUN:   2>&1 | %{diag} --check-prefix=VARCODE
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/variadic-code.ll \
; RUN:   | FileCheck %s --check-prefix=NODECL

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/ctor.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=CTOR %{nocrash}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/ctor.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=CTOR %{nocrash}

; RUN: not llc -mtriple=x86_64-w64-windows-gnu-msil -verify-machineinstrs < %t/mingw-math.ll -o /dev/null \
; RUN:   2> %t/mingw-math.err
; RUN: FileCheck %s --check-prefix=MINGW %{nocrash} --implicit-check-not=warning: --input-file=%t/mingw-math.err
; RUN: FileCheck %s --check-prefix=NERRGNU %{nocrash} --input-file=%t/mingw-math.err
; RUN: not llc -mtriple=x86_64-w64-windows-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/mingw-math.ll \
; RUN:   > %t/mingw-math.il
; RUN: FileCheck %s --check-prefix=MINGW-IL --implicit-check-not=pinvokeimpl --input-file=%t/mingw-math.il

; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/msvc-math.ll \
; RUN:   > %t/msvc-math.il 2> %t/msvc-math.err
; RUN: FileCheck %s --check-prefix=MSVC %{nocrash} --implicit-check-not=warning: --input-file=%t/msvc-math.err
; RUN: FileCheck %s --check-prefix=NERRMSVC %{nocrash} --input-file=%t/msvc-math.err
; RUN: FileCheck %s --check-prefix=MSVC-IL --implicit-check-not=pinvokeimpl --input-file=%t/msvc-math.il
; RUN: FileCheck %s --check-prefix=NPINVMSVC --input-file=%t/msvc-math.il
; RUN: not llc -mtriple=aarch64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/msvc-math.ll \
; RUN:   > %t/msvc-math.a64.il 2> %t/msvc-math.a64.err
; RUN: FileCheck %s --check-prefix=MSVC %{nocrash} --implicit-check-not=warning: --input-file=%t/msvc-math.a64.err
; RUN: FileCheck %s --check-prefix=NERRMSVC %{nocrash} --input-file=%t/msvc-math.a64.err
; RUN: FileCheck %s --check-prefix=MSVC-IL --implicit-check-not=pinvokeimpl --input-file=%t/msvc-math.a64.il
; RUN: FileCheck %s --check-prefix=NPINVMSVC --input-file=%t/msvc-math.a64.il

; DEFINE: %{sigcheck} = FileCheck %s --check-prefix=SIG %{nocrash} --implicit-check-not=warning:
; DEFINE: %{sigcount} = FileCheck %s --check-prefix=NERRSIG %{nocrash}
; DEFINE: %{sigil} = FileCheck %s --check-prefix=SIG-IL --implicit-check-not=pinvokeimpl
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/sig.ll \
; RUN:   > %t/sig.lnx.il 2> %t/sig.lnx.err
; RUN: %{sigcheck} --input-file=%t/sig.lnx.err
; RUN: %{sigcount} --input-file=%t/sig.lnx.err
; RUN: %{sigil} --input-file=%t/sig.lnx.il
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/sig.ll \
; RUN:   > %t/sig.lnx.o0.il 2> %t/sig.lnx.o0.err
; RUN: %{sigcheck} --input-file=%t/sig.lnx.o0.err
; RUN: %{sigcount} --input-file=%t/sig.lnx.o0.err
; RUN: %{sigil} --input-file=%t/sig.lnx.o0.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/sig.ll \
; RUN:   > %t/sig.win.il 2> %t/sig.win.err
; RUN: %{sigcheck} --input-file=%t/sig.win.err
; RUN: %{sigcount} --input-file=%t/sig.win.err
; RUN: %{sigil} --input-file=%t/sig.win.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/sig.ll \
; RUN:   > %t/sig.win.o0.il 2> %t/sig.win.o0.err
; RUN: %{sigcheck} --input-file=%t/sig.win.o0.err
; RUN: %{sigcount} --input-file=%t/sig.win.o0.err
; RUN: %{sigil} --input-file=%t/sig.win.o0.il

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/sret.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=SRET
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/sret.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=SRET
; RUN: not llc -mtriple=aarch64-unknown-linux-gnu-msil -verify-machineinstrs < %t/sret.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=SRET
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/sret.ll \
; RUN:   | FileCheck %s --check-prefix=NODECL
; RUN: not llc -mtriple=aarch64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/sret.ll \
; RUN:   | FileCheck %s --check-prefix=NODECL

; NERR-COUNT-4: {{^}}error:
; NERR-NOT:     {{^}}error:
; NERRGNU-COUNT-6: {{^}}error:
; NERRGNU-NOT:     {{^}}error:
; NERRMSVC-COUNT-3: {{^}}error:
; NERRMSVC-NOT:     {{^}}error:
; The P/Invokes of msvc-math.ll are matched as a group, so they are counted.
; NPINVMSVC-COUNT-3: pinvokeimpl
; NPINVMSVC-NOT:     pinvokeimpl
; NERRSIG-COUNT-7: {{^}}error:
; NERRSIG-NOT:     {{^}}error:

;--- undef.ll
; malloc is called, strlen is called three times from two functions, free's
; address is taken in code, abort's address is stored in a global; puts is an
; import and unused_decl is never used.
source_filename = "undef.c"

; UNDEF-DAG: {{^}}error: {{.*}}undefined function 'malloc' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; UNDEF-DAG: {{^}}error: {{.*}}undefined function 'strlen' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; UNDEF-DAG: {{^}}error: {{.*}}undefined function 'free' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; UNDEF-DAG: {{^}}error: {{.*}}undefined function 'abort' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}

; UNDEF-IL-LABEL: .method {{.*}}'alloc'(
; UNDEF-IL:       {{^}}  call native int 'malloc'(native int){{$}}
; UNDEF-IL-LABEL: .method {{.*}}'len2'(
; UNDEF-IL:       {{^}}  call native int 'strlen'(native int){{$}}
; UNDEF-IL:       {{^}}  call native int 'strlen'(native int){{$}}
; UNDEF-IL-LABEL: .method {{.*}}'len1'(
; UNDEF-IL:       {{^}}  call native int 'strlen'(native int){{$}}
; UNDEF-IL-LABEL: .method {{.*}}'free_addr'(
; UNDEF-IL:       {{^}}  ldftn void 'free'(native int){{$}}
; UNDEF-IL-LABEL: .method {{.*}}'say'(
; UNDEF-IL:       {{^}}  call int32 'puts'(native int){{$}}
; UNDEF-IL-NOT:   pinvokeimpl
; UNDEF-LNX:      {{^}}.method public hidebysig static pinvokeimpl("libc" cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; UNDEF-WIN:      {{^}}.method public hidebysig static pinvokeimpl("ucrtbase" cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; UNDEF-IL-NOT:   pinvokeimpl
; UNDEF-IL:       {{^}}.field public static initonly native int 'on_error'{{( |$)}}
; UNDEF-IL-NOT:   pinvokeimpl

declare ptr @malloc(i64)
declare i64 @strlen(ptr)
declare void @free(ptr)
declare void @abort()
declare i32 @unused_decl(i32)
declare i32 @puts(ptr)

@on_error = global ptr @abort, align 8

define ptr @alloc(i64 %n) {
  %p = call ptr @malloc(i64 %n)
  ret ptr %p
}

define i64 @len2(ptr %a, ptr %b) {
  %x = call i64 @strlen(ptr %a)
  %y = call i64 @strlen(ptr %b)
  %r = add i64 %x, %y
  ret i64 %r
}

define i64 @len1(ptr %a) {
  %x = call i64 @strlen(ptr %a)
  ret i64 %x
}

define ptr @free_addr() {
  ret ptr @free
}

define i32 @say(ptr %s) {
  %r = call i32 @puts(ptr %s)
  ret i32 %r
}

;--- variadic-data.ll
; int (*vp)(const char *, ...) = vimp;  (vimp imported from "libc")
source_filename = "variadic-data.c"

; VARDATA: {{^}}error: {{.*(vimp.*variadic|variadic.*vimp)}}
; NODECL-NOT: pinvokeimpl

declare i32 @vimp(ptr, ...) #0

@vp = global ptr @vimp, align 8

attributes #0 = { "msil-import-module"="libc" }

;--- variadic-code.ll
; The address of the variadic import taken in code.
source_filename = "variadic-code.c"

; VARCODE: {{^}}error: {{.*(vimp.*variadic|variadic.*vimp)}}

declare i32 @vimp(ptr, ...) #0

define ptr @get_vimp() {
  ret ptr @vimp
}

attributes #0 = { "msil-import-module"="libc" }

;--- ctor.ll
; A constructor that another module would define.
source_filename = "ctor.c"

; CTOR: {{^}}error: {{.*}}ext_init

@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 65535, ptr @ext_init, ptr null }]

declare void @ext_init()

;--- mingw-math.ll
; hypot, the C99 functions round, trunc and fmin, and ldexpf and frexpf are
; not exported by msvcrt.dll; sqrt is.
source_filename = "mingw-math.c"

; MINGW-DAG: {{^}}error: {{.*}}undefined function 'hypot' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; MINGW-DAG: {{^}}error: {{.*}}undefined function 'round' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; MINGW-DAG: {{^}}error: {{.*}}undefined function 'trunc' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; MINGW-DAG: {{^}}error: {{.*}}undefined function 'fmin' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; MINGW-DAG: {{^}}error: {{.*}}undefined function 'ldexpf' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; MINGW-DAG: {{^}}error: {{.*}}undefined function 'frexpf' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}

; MINGW-IL-LABEL: .method {{.*}}'use_hypot'(
; MINGW-IL:       {{^}}  call float64 'hypot'(float64, float64){{$}}
; MINGW-IL-LABEL: .method {{.*}}'use_round'(
; MINGW-IL:       {{^}}  call float64 'round'(float64){{$}}
; MINGW-IL-LABEL: .method {{.*}}'use_trunc'(
; MINGW-IL:       {{^}}  call float64 'trunc'(float64){{$}}
; MINGW-IL-LABEL: .method {{.*}}'use_fmin'(
; MINGW-IL:       {{^}}  call float64 'fmin'(float64, float64){{$}}
; MINGW-IL-LABEL: .method {{.*}}'use_ldexpf'(
; MINGW-IL:       {{^}}  call float32 'ldexpf'(float32, int32){{$}}
; MINGW-IL-LABEL: .method {{.*}}'use_frexpf'(
; MINGW-IL:       {{^}}  call float32 'frexpf'(float32, native int){{$}}
; MINGW-IL-LABEL: .method {{.*}}'use_sqrt'(
; MINGW-IL:       {{^}}  call float64 'sqrt'(float64){{$}}
; MINGW-IL:       {{^}}.method public hidebysig static pinvokeimpl("msvcrt" cdecl) float64 'sqrt'(float64) cil managed preservesig {}{{$}}

declare double @hypot(double, double)
declare double @round(double)
declare double @trunc(double)
declare double @fmin(double, double)
declare float @ldexpf(float, i32)
declare float @frexpf(float, ptr)
declare double @sqrt(double)

define double @use_hypot(double %x, double %y) {
  %r = call double @hypot(double %x, double %y)
  ret double %r
}

define double @use_round(double %x) {
  %r = call double @round(double %x)
  ret double %r
}

define double @use_trunc(double %x) {
  %r = call double @trunc(double %x)
  ret double %r
}

define double @use_fmin(double %x, double %y) {
  %r = call double @fmin(double %x, double %y)
  ret double %r
}

define float @use_ldexpf(float %x, i32 %e) {
  %r = call float @ldexpf(float %x, i32 %e)
  ret float %r
}

define float @use_frexpf(float %x, ptr %e) {
  %r = call float @frexpf(float %x, ptr %e)
  ret float %r
}

define double @use_sqrt(double %x) {
  %r = call double @sqrt(double %x)
  ret double %r
}

;--- msvc-math.ll
; ucrtbase.dll does not export hypotf, ldexpf and frexpf; it exports hypot,
; ldexp and frexp, which the same module calls.
source_filename = "msvc-math.c"

; MSVC-DAG: {{^}}error: {{.*}}undefined function 'hypotf' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; MSVC-DAG: {{^}}error: {{.*}}undefined function 'ldexpf' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; MSVC-DAG: {{^}}error: {{.*}}undefined function 'frexpf' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}

; MSVC-IL-LABEL: .method {{.*}}'use_hypotf'(
; MSVC-IL:       {{^}}  call float32 'hypotf'(float32, float32){{$}}
; MSVC-IL-LABEL: .method {{.*}}'use_ldexpf'(
; MSVC-IL:       {{^}}  call float32 'ldexpf'(float32, int32){{$}}
; MSVC-IL-LABEL: .method {{.*}}'use_frexpf'(
; MSVC-IL:       {{^}}  call float32 'frexpf'(float32, native int){{$}}
; MSVC-IL-LABEL: .method {{.*}}'use_double'(
; MSVC-IL:       {{^}}  call float64 'hypot'(float64, float64){{$}}
; MSVC-IL:       {{^}}  call float64 'ldexp'(float64, int32){{$}}
; MSVC-IL:       {{^}}  call float64 'frexp'(float64, native int){{$}}
; MSVC-IL:       {{^}}} // end of method 'use_double'{{$}}
; MSVC-IL-DAG:   {{^}}.method public hidebysig static pinvokeimpl("ucrtbase" cdecl) float64 'hypot'(float64, float64) cil managed preservesig {}{{$}}
; MSVC-IL-DAG:   {{^}}.method public hidebysig static pinvokeimpl("ucrtbase" cdecl) float64 'ldexp'(float64, int32) cil managed preservesig {}{{$}}
; MSVC-IL-DAG:   {{^}}.method public hidebysig static pinvokeimpl("ucrtbase" cdecl) float64 'frexp'(float64, native int) cil managed preservesig {}{{$}}

declare float @hypotf(float, float)
declare float @ldexpf(float, i32)
declare float @frexpf(float, ptr)
declare double @hypot(double, double)
declare double @ldexp(double, i32)
declare double @frexp(double, ptr)

define float @use_hypotf(float %x, float %y) {
  %r = call float @hypotf(float %x, float %y)
  ret float %r
}

define float @use_ldexpf(float %x, i32 %e) {
  %r = call float @ldexpf(float %x, i32 %e)
  ret float %r
}

define float @use_frexpf(float %x, ptr %e) {
  %r = call float @frexpf(float %x, ptr %e)
  ret float %r
}

define double @use_double(double %x, double %y, i32 %n, ptr %e) {
  %h = call double @hypot(double %x, double %y)
  %l = call double @ldexp(double %h, i32 %n)
  %f = call double @frexp(double %l, ptr %e)
  ret double %f
}

;--- sig.ll
; Imports with the "msil-import-module" attribute whose signatures have no
; native P/Invoke form: four called from code, three referenced only from
; the initializer of tbl; okfn is the scalar control.
source_filename = "sig.c"

; SIG-DAG: {{^}}error: imported function 'imp128' has a parameter or result that a P/Invoke cannot pass natively (aggregates, vectors, 128-bit integers and half are not supported for MSIL yet){{$}}
; SIG-DAG: {{^}}error: imported function 'imppair' has a parameter or result that a P/Invoke cannot pass natively (aggregates, vectors, 128-bit integers and half are not supported for MSIL yet){{$}}
; SIG-DAG: {{^}}error: imported function 'cplx' has a parameter or result that a P/Invoke cannot pass natively (aggregates, vectors, 128-bit integers and half are not supported for MSIL yet){{$}}
; SIG-DAG: {{^}}error: imported function 'vec2' has a parameter or result that a P/Invoke cannot pass natively (aggregates, vectors, 128-bit integers and half are not supported for MSIL yet){{$}}
; SIG-DAG: {{^}}error: imported function 'hret' has a parameter or result that a P/Invoke cannot pass natively (aggregates, vectors, 128-bit integers and half are not supported for MSIL yet){{$}}
; SIG-DAG: {{^}}error: imported function 'takebyval' has a parameter or result that a P/Invoke cannot pass natively (aggregates, vectors, 128-bit integers and half are not supported for MSIL yet){{$}}
; SIG-DAG: {{^}}error: imported function 'argi128' has a parameter or result that a P/Invoke cannot pass natively (aggregates, vectors, 128-bit integers and half are not supported for MSIL yet){{$}}

; SIG-IL-LABEL: .method {{.*}}'use_okfn'(
; SIG-IL:       {{^}}  call int32 'okfn'(int32){{$}}
; SIG-IL:       {{^}}.method public hidebysig static pinvokeimpl("mylib" cdecl) int32 'okfn'(int32) cil managed preservesig {}{{$}}

%S = type { i64, i64 }

declare i128 @imp128(i64) #0
declare { i64, i64 } @imppair(i64) #0
declare { double, double } @cplx(double) #0
declare <2 x float> @vec2(float) #0
declare half @hret(half) #0
declare void @takebyval(ptr byval(%S)) #0
declare i32 @argi128(i128) #0
declare i32 @okfn(i32) #0

@tbl = global [3 x ptr] [ptr @hret, ptr @takebyval, ptr @argi128], align 8

define i64 @use_imp128(i64 %x) {
  %r = call i128 @imp128(i64 %x)
  %t = trunc i128 %r to i64
  ret i64 %t
}

define i64 @use_imppair(i64 %x) {
  %r = call { i64, i64 } @imppair(i64 %x)
  %e = extractvalue { i64, i64 } %r, 1
  ret i64 %e
}

define double @use_cplx(double %x) {
  %r = call { double, double } @cplx(double %x)
  %e = extractvalue { double, double } %r, 0
  ret double %e
}

define float @use_vec2(float %x) {
  %r = call <2 x float> @vec2(float %x)
  %e = extractelement <2 x float> %r, i64 0
  ret float %e
}

define i32 @use_okfn(i32 %x) {
  %r = call i32 @okfn(i32 %x)
  ret i32 %r
}

attributes #0 = { "msil-import-module"="mylib" }

;--- sret.ll
; struct S { long long a, b, c, d; } mk(int); imported from "x": the struct is
; returned through an sret pointer.
source_filename = "sret.c"

; SRET: {{^}}error: imported function 'mk' has a parameter or result that a P/Invoke cannot pass natively (aggregates, vectors, 128-bit integers and half are not supported for MSIL yet){{$}}

%struct.S = type { i64, i64, i64, i64 }

declare void @mk(ptr sret(%struct.S) align 8, i32) #0

define i64 @use_mk(i32 %n) {
  %s = alloca %struct.S, align 8
  call void @mk(ptr sret(%struct.S) align 8 %s, i32 %n)
  %v = load i64, ptr %s, align 8
  ret i64 %v
}

attributes #0 = { "msil-import-module"="x" }

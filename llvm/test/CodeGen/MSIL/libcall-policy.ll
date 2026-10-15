; The library-call policy of the Phase-1 import table (ImplementationPlan.md
; decision D14, M8.a and M8.c): a call that the legalizer or the type
; legalizer turns into a library call (an ExternalSymbol callee) is handled by
; name.
; - A name of the import table becomes a call of that external symbol and a
;   P/Invoke, exactly like a call of the declared C function: sqrt from C (a
;   plain call of the declaration, as clang emits it with math-errno) and from
;   llvm.sqrt (an FSQRT library call, as clang emits it without math-errno)
;   share one declaration in the math library ('libm.so.6' on
;   x86_64-unknown-linux-gnu-msil, 'ucrtbase' on x86_64-pc-windows-msvc-msil),
;   and sqrtf has its own. puts is in the C library ('libc', 'ucrtbase'). A
;   function with "msil-import-module" and "msil-import-name" is imported
;   from that library under that entry point.
; - A runtime helper, whose name starts with '__' (compiler-rt; __atomic_*
;   helpers in atomic-errors.ll), is an error until phase 3: "runtime helper
;   '<name>' is not available for MSIL yet (phase 3)", reported by LowerCall
;   (__floattidf for sitofp i128 to double) or, for i128 division, by the type
;   legalizer with the same text (__divti3). powi is __powidf2 on the Linux
;   triple (an error) and pow of the converted exponent on windows-msvc (a
;   P/Invoke).
; - Any other library function is an error: "library call '<name>' is not a
;   Phase-1 import" (lround, exp10). On x86_64-w64-windows-gnu-msil the import
;   set is msvcrt.dll's (D14 as amended in plan section 12, 'Windows import
;   sets follow the DLL exports'): llvm.round becomes the library call 'round',
;   which msvcrt.dll does not export, so it is that error, while llvm.ldexp on
;   float has no ldexpf library call there and is promoted to ldexp on double
;   (conv.r8, call, conv.r4; plan section 12, 'M5.a extended (float32 frexp
;   and ldexp on windows-msvc)'), a P/Invoke into "msvcrt".
; Each error names its function, is reported once, builds no call (the IL
; never names the helper), and compilation goes on; llc exits with 1 and prints
; neither 'LLVM ERROR' nor a crash report. Both triples, -O2 and -O0.
;
; A library call is bound by its name and signature alone:
; - The same library function called with two signatures gets one P/Invoke
;   per signature, which CIL overloads by signature: ldexp with an i32 and
;   with an i64 exponent (llvm.ldexp.f64.i32 and llvm.ldexp.f64.i64), and a
;   trap function named sqrt ("trap-func-name"="sqrt", void()) next to the
;   FSQRT library call sqrt (float64(float64)).
; - A declaration with "msil-import-module" that happens to have the name of a
;   library function, but another type, keeps its own P/Invoke (the library
;   and entry point of its attributes), and the library call of the same name
;   gets the import table's P/Invoke into the math library.
; - A library call whose name is defined in this module with the same type
;   calls that definition, and nothing is imported; a definition of another
;   type is the error "library call '<name>' does not match the type of the
;   function of that name in this module". The name is the IL name of the
;   definition (plan section 12, 'M8.a and M8.c amended (IL names of
;   `<Module>` members)' part (1)): a function defined as "\01sqrt" (C's
;   'double my_sqrt(double) __asm__("sqrt")') is the method 'sqrt', which the
;   FSQRT library call sqrt calls, and no P/Invoke 'sqrt' is declared next to
;   it (escdef.ll).
; The modules of these rules without an error (twosigs.ll, importmix.ll,
; defined.ll and escdef.ll) compile without any message: llc exits with 0 and
; prints nothing (in particular neither 'LLVM ERROR' nor a crash report), and
; they assemble.

; RUN: split-file %s %t
; DEFINE: %{count} = FileCheck %s --check-prefix=NPINV
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/imports.ll \
; RUN:   -o %t/imports.lnx.il 2>&1 | count 0
; RUN: FileCheck %s -DCLIB=libc -DMLIB=libm.so.6 --input-file=%t/imports.lnx.il
; RUN: %{count} --input-file=%t/imports.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/imports.ll \
; RUN:   -o %t/imports.win.il 2>&1 | count 0
; RUN: FileCheck %s -DCLIB=ucrtbase -DMLIB=ucrtbase --input-file=%t/imports.win.il
; RUN: %{count} --input-file=%t/imports.win.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/imports.ll \
; RUN:   -o %t/imports.lnx.o0.il 2>&1 | count 0
; RUN: FileCheck %s -DCLIB=libc -DMLIB=libm.so.6 --input-file=%t/imports.lnx.o0.il
; RUN: %{count} --input-file=%t/imports.lnx.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/imports.lnx.dll %t/imports.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/imports.win.dll %t/imports.win.il %}

; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; DEFINE: %{il} = FileCheck %s --check-prefix=HELPER-IL --implicit-check-not=__divti3 \
; DEFINE:   --implicit-check-not=__floattidf --implicit-check-not=lround --implicit-check-not=exp10 \
; DEFINE:   --implicit-check-not=pinvokeimpl
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/helpers.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=HELPER
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/helpers.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=HELPER
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/helpers.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=HELPER
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/helpers.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=HELPER
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/helpers.ll \
; RUN:   > %t/helpers.lnx.il
; RUN: %{il} --input-file=%t/helpers.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/helpers.ll \
; RUN:   > %t/helpers.win.il
; RUN: %{il} --input-file=%t/helpers.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/helpers.dll %t/helpers.win.il %}

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/powi.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=POWI-LNX
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/powi.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=POWI-LNX
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/powi.ll \
; RUN:   -o %t/powi.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=POWI-WIN --input-file=%t/powi.win.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/powi.ll \
; RUN:   -o %t/powi.win.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=POWI-WIN --input-file=%t/powi.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/powi.dll %t/powi.win.il %}

; DEFINE: %{nocrash} = --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit"
; DEFINE: %{nodiag} = FileCheck %s --check-prefix=NODIAG --allow-empty %{nocrash}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/twosigs.ll \
; RUN:   -o %t/twosigs.lnx.il 2>&1 | %{nodiag}
; RUN: FileCheck %s --check-prefix=TWO -DMLIB=libm.so.6 --input-file=%t/twosigs.lnx.il
; RUN: FileCheck %s --check-prefix=NTWO --input-file=%t/twosigs.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/twosigs.ll \
; RUN:   -o %t/twosigs.win.il 2>&1 | %{nodiag}
; RUN: FileCheck %s --check-prefix=TWO -DMLIB=ucrtbase --input-file=%t/twosigs.win.il
; RUN: FileCheck %s --check-prefix=NTWO --input-file=%t/twosigs.win.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/twosigs.ll \
; RUN:   -o %t/twosigs.win.o0.il 2>&1 | %{nodiag}
; RUN: FileCheck %s --check-prefix=TWO -DMLIB=ucrtbase --input-file=%t/twosigs.win.o0.il
; RUN: FileCheck %s --check-prefix=NTWO --input-file=%t/twosigs.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/twosigs.lnx.dll %t/twosigs.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/twosigs.win.dll %t/twosigs.win.il %}

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/importmix.ll \
; RUN:   -o %t/importmix.lnx.il 2>&1 | %{nodiag}
; RUN: FileCheck %s --check-prefix=MIX -DMLIB=libm.so.6 --input-file=%t/importmix.lnx.il
; RUN: FileCheck %s --check-prefix=NMIX --input-file=%t/importmix.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/importmix.ll \
; RUN:   -o %t/importmix.win.il 2>&1 | %{nodiag}
; RUN: FileCheck %s --check-prefix=MIX -DMLIB=ucrtbase --input-file=%t/importmix.win.il
; RUN: FileCheck %s --check-prefix=NMIX --input-file=%t/importmix.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/importmix.lnx.dll %t/importmix.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/importmix.win.dll %t/importmix.win.il %}

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/defined.ll \
; RUN:   -o %t/defined.lnx.il 2>&1 | %{nodiag}
; RUN: FileCheck %s --check-prefix=DEF --implicit-check-not=pinvokeimpl --input-file=%t/defined.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/defined.ll \
; RUN:   -o %t/defined.win.il 2>&1 | %{nodiag}
; RUN: FileCheck %s --check-prefix=DEF --implicit-check-not=pinvokeimpl --input-file=%t/defined.win.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/defined.ll \
; RUN:   -o %t/defined.lnx.o0.il 2>&1 | %{nodiag}
; RUN: FileCheck %s --check-prefix=DEF --implicit-check-not=pinvokeimpl --input-file=%t/defined.lnx.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/defined.lnx.dll %t/defined.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/defined.win.dll %t/defined.win.il %}

; DEFINE: %{escdef} = FileCheck %s --check-prefix=ESC --implicit-check-not=pinvokeimpl \
; DEFINE:   --implicit-check-not="'sqrt'"
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/escdef.ll \
; RUN:   -o %t/escdef.lnx.il 2>&1 | %{nodiag}
; RUN: %{escdef} --input-file=%t/escdef.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/escdef.ll \
; RUN:   -o %t/escdef.win.il 2>&1 | %{nodiag}
; RUN: %{escdef} --input-file=%t/escdef.win.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/escdef.ll \
; RUN:   -o %t/escdef.win.o0.il 2>&1 | %{nodiag}
; RUN: %{escdef} --input-file=%t/escdef.win.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/escdef.lnx.dll %t/escdef.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/escdef.win.dll %t/escdef.win.il %}

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/mismatch.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=MISMATCH
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/mismatch.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=MISMATCH
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/mismatch.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=MISMATCH

; RUN: not llc -mtriple=x86_64-w64-windows-gnu-msil -verify-machineinstrs < %t/mingw.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=MINGW
; RUN: not llc -mtriple=x86_64-w64-windows-gnu-msil -O0 -verify-machineinstrs < %t/mingw.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=MINGW
; RUN: not llc -mtriple=x86_64-w64-windows-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/mingw.ll \
; RUN:   > %t/mingw.il
; RUN: FileCheck %s --check-prefix=MINGW-IL --implicit-check-not=pinvokeimpl --implicit-check-not="'round'" \
; RUN:   --implicit-check-not="'ldexpf'" --input-file=%t/mingw.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/mingw.dll %t/mingw.il %}

; NODIAG-NOT: {{.}}

;--- imports.ll
source_filename = "libcall-imports.c"

; double sqrt(double): a call of the declaration.
; CHECK-LABEL: {{^}}.method public static float64 'sqrt_c'(float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call float64 'sqrt'(float64){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sqrt_c'{{$}}
define double @sqrt_c(double %x) {
  %r = call double @sqrt(double %x)
  ret double %r
}

; llvm.sqrt.f64: the FSQRT library call 'sqrt', the same callee.
; CHECK-LABEL: {{^}}.method public static float64 'sqrt_intrinsic'(float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call float64 'sqrt'(float64){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sqrt_intrinsic'{{$}}
define double @sqrt_intrinsic(double %x) {
  %r = call double @llvm.sqrt.f64(double %x)
  ret double %r
}

; CHECK-LABEL: {{^}}.method public static float32 'sqrtf_intrinsic'(float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call float32 'sqrtf'(float32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'sqrtf_intrinsic'{{$}}
define float @sqrtf_intrinsic(float %x) {
  %r = call float @llvm.sqrt.f32(float %x)
  ret float %r
}

; CHECK-LABEL: {{^}}.method public static int32 'hello'(native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'puts'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'hello'{{$}}
define i32 @hello(ptr %s) {
  %r = call i32 @puts(ptr %s)
  ret i32 %r
}

; CHECK-LABEL: {{^}}.method public static int32 'custom'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'imp'(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'custom'{{$}}
define i32 @custom(i32 %x) {
  %r = call i32 @imp(i32 %x)
  ret i32 %r
}

declare double @sqrt(double)
declare double @llvm.sqrt.f64(double)
declare float @llvm.sqrt.f32(float)
declare i32 @puts(ptr)
declare i32 @imp(i32) #0

attributes #0 = { "msil-import-module"="custom.dll" "msil-import-name"="custom_entry" }

; CHECK-NOT:   {{^}}.method {{(public|assembly) static}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("[[MLIB]]" cdecl) float64 'sqrt'(float64) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("[[MLIB]]" cdecl) float32 'sqrtf'(float32) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("[[CLIB]]" cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("custom.dll" as "custom_entry" cdecl) int32 'imp'(int32) cil managed preservesig {}{{$}}
; CHECK-NOT:   {{^}}.method

; NPINV-COUNT-4: pinvokeimpl
; NPINV-NOT:     pinvokeimpl

;--- helpers.ll
source_filename = "libcall-helpers.c"

; HELPER: {{^}}error: {{.*}}in function div128 {{.*}}: runtime helper '__divti3' is not available for MSIL yet (phase 3){{$}}
; HELPER: {{^}}error: {{.*}}in function sitofp128 {{.*}}: runtime helper '__floattidf' is not available for MSIL yet (phase 3){{$}}
; HELPER: {{^}}error: {{.*}}in function lround64 {{.*}}: library call 'lround' is not a Phase-1 import{{$}}
; HELPER: {{^}}error: {{.*}}in function exp10_f64 {{.*}}: library call 'exp10' is not a Phase-1 import{{$}}

; HELPER-IL-LABEL: .method {{.*}}'div128'(
; HELPER-IL-NOT:   call
; HELPER-IL:       {{^}}} // end of method 'div128'{{$}}
; HELPER-IL-LABEL: .method {{.*}}'sitofp128'(
; HELPER-IL-NOT:   call
; HELPER-IL:       {{^}}} // end of method 'sitofp128'{{$}}
; HELPER-IL-LABEL: .method {{.*}}'lround64'(
; HELPER-IL-NOT:   call
; HELPER-IL:       {{^}}} // end of method 'lround64'{{$}}
; HELPER-IL-LABEL: .method {{.*}}'exp10_f64'(
; HELPER-IL-NOT:   call
; HELPER-IL:       {{^}}} // end of method 'exp10_f64'{{$}}
; HELPER-IL-LABEL: .method {{.*}}'after_helpers'(
; HELPER-IL-NEXT:  {{^\{$}}
; HELPER-IL-NEXT:  {{^}}  .maxstack 2{{$}}
; HELPER-IL-NEXT:  {{^}}  ldarg.0{{$}}
; HELPER-IL-NEXT:  {{^}}  ldarg.1{{$}}
; HELPER-IL-NEXT:  {{^}}  mul{{$}}
; HELPER-IL-NEXT:  {{^}}  ret{{$}}
; HELPER-IL-NEXT:  {{^}}} // end of method 'after_helpers'{{$}}

define i64 @div128(i128 %a, i128 %b) {
  %q = sdiv i128 %a, %b
  %t = trunc i128 %q to i64
  ret i64 %t
}

define double @sitofp128(i128 %x) {
  %r = sitofp i128 %x to double
  ret double %r
}

define i64 @lround64(double %x) {
  %r = call i64 @llvm.lround.i64.f64(double %x)
  ret i64 %r
}

define double @exp10_f64(double %x) {
  %r = call double @llvm.exp10.f64(double %x)
  ret double %r
}

define i32 @after_helpers(i32 %a, i32 %b) {
  %r = mul i32 %a, %b
  ret i32 %r
}

declare i64 @llvm.lround.i64.f64(double)
declare double @llvm.exp10.f64(double)

;--- powi.ll
; __builtin_powi: __powidf2 (compiler-rt) where the C runtime has it, pow of
; the converted exponent on windows-msvc.
source_filename = "libcall-powi.c"

; POWI-LNX: {{^}}error: {{.*}}in function powi_f64 {{.*}}: runtime helper '__powidf2' is not available for MSIL yet (phase 3){{$}}

; POWI-WIN-LABEL: {{^}}.method public static float64 'powi_f64'(float64, int32) cil managed{{$}}
; POWI-WIN:       {{^}}  conv.r8{{$}}
; POWI-WIN:       {{^}}  call float64 'pow'(float64, float64){{$}}
; POWI-WIN:       {{^}}} // end of method 'powi_f64'{{$}}
; POWI-WIN:       {{^}}.method public hidebysig static pinvokeimpl("ucrtbase" cdecl) float64 'pow'(float64, float64) cil managed preservesig {}{{$}}
; POWI-WIN-NOT:   {{pinvokeimpl|__powidf2}}

define double @powi_f64(double %a, i32 %n) {
  %r = call double @llvm.powi.f64.i32(double %a, i32 %n)
  ret double %r
}

declare double @llvm.powi.f64.i32(double, i32)

;--- twosigs.ll
; ldexp with an int and with a 64-bit exponent, and sqrt as the FSQRT library
; call and as the trap function: four P/Invokes, two per name.
source_filename = "libcall-twosigs.c"

; TWO-LABEL: {{^}}.method public static float64 'ldexp_i32'(float64, int32) cil managed{{$}}
; TWO-NEXT:  {{^\{$}}
; TWO-NEXT:  {{^}}  .maxstack 2{{$}}
; TWO-NEXT:  {{^}}  ldarg.0{{$}}
; TWO-NEXT:  {{^}}  ldarg.1{{$}}
; TWO-NEXT:  {{^}}  call float64 'ldexp'(float64, int32){{$}}
; TWO-NEXT:  {{^}}  ret{{$}}
; TWO-NEXT:  {{^}}} // end of method 'ldexp_i32'{{$}}
define double @ldexp_i32(double %x, i32 %e) {
  %r = call double @llvm.ldexp.f64.i32(double %x, i32 %e)
  ret double %r
}

; TWO-LABEL: {{^}}.method public static float64 'ldexp_i64'(float64, native int) cil managed{{$}}
; TWO-NEXT:  {{^\{$}}
; TWO-NEXT:  {{^}}  .maxstack 2{{$}}
; TWO-NEXT:  {{^}}  ldarg.0{{$}}
; TWO-NEXT:  {{^}}  ldarg.1{{$}}
; TWO-NEXT:  {{^}}  call float64 'ldexp'(float64, native int){{$}}
; TWO-NEXT:  {{^}}  ret{{$}}
; TWO-NEXT:  {{^}}} // end of method 'ldexp_i64'{{$}}
define double @ldexp_i64(double %x, i64 %e) {
  %r = call double @llvm.ldexp.f64.i64(double %x, i64 %e)
  ret double %r
}

; TWO-LABEL: {{^}}.method public static float64 'sqrt_intrinsic'(float64) cil managed{{$}}
; TWO-NEXT:  {{^\{$}}
; TWO-NEXT:  {{^}}  .maxstack 1{{$}}
; TWO-NEXT:  {{^}}  ldarg.0{{$}}
; TWO-NEXT:  {{^}}  call float64 'sqrt'(float64){{$}}
; TWO-NEXT:  {{^}}  ret{{$}}
; TWO-NEXT:  {{^}}} // end of method 'sqrt_intrinsic'{{$}}
define double @sqrt_intrinsic(double %x) {
  %r = call double @llvm.sqrt.f64(double %x)
  ret double %r
}

; TWO-LABEL: {{^}}.method public static void 'trap_sqrt'() cil managed{{$}}
; TWO-NEXT:  {{^\{$}}
; TWO-NEXT:  {{^}}  .maxstack 1{{$}}
; TWO-NEXT:  {{^}}  call void 'sqrt'(){{$}}
; TWO-NEXT:  {{^}}  ldnull{{$}}
; TWO-NEXT:  {{^}}  throw{{$}}
; TWO-NEXT:  {{^}}} // end of method 'trap_sqrt'{{$}}
define void @trap_sqrt() {
  call void @llvm.trap() #0
  unreachable
}

; TWO-NOT:   {{^}}.method {{(public|assembly) static}}
; TWO-DAG:   {{^}}.method public hidebysig static pinvokeimpl("[[MLIB]]" cdecl) float64 'ldexp'(float64, int32) cil managed preservesig {}{{$}}
; TWO-DAG:   {{^}}.method public hidebysig static pinvokeimpl("[[MLIB]]" cdecl) float64 'ldexp'(float64, native int) cil managed preservesig {}{{$}}
; TWO-DAG:   {{^}}.method public hidebysig static pinvokeimpl("[[MLIB]]" cdecl) float64 'sqrt'(float64) cil managed preservesig {}{{$}}
; TWO-DAG:   {{^}}.method public hidebysig static pinvokeimpl("[[MLIB]]" cdecl) void 'sqrt'() cil managed preservesig {}{{$}}
; TWO-NOT:   {{^}}.method

; NTWO-COUNT-4: pinvokeimpl
; NTWO-NOT:     pinvokeimpl

declare double @llvm.ldexp.f64.i32(double, i32)
declare double @llvm.ldexp.f64.i64(double, i64)
declare double @llvm.sqrt.f64(double)
declare void @llvm.trap()

attributes #0 = { "trap-func-name"="sqrt" }

;--- importmix.ll
; float sqrt(float) imported from "mylib" under the entry point "mysqrtf",
; and the FSQRT library call 'sqrt' (float64) of llvm.sqrt.f64.
source_filename = "libcall-importmix.c"

; MIX-LABEL: {{^}}.method public static float64 'sqrt_intrinsic'(float64) cil managed{{$}}
; MIX-NEXT:  {{^\{$}}
; MIX-NEXT:  {{^}}  .maxstack 1{{$}}
; MIX-NEXT:  {{^}}  ldarg.0{{$}}
; MIX-NEXT:  {{^}}  call float64 'sqrt'(float64){{$}}
; MIX-NEXT:  {{^}}  ret{{$}}
; MIX-NEXT:  {{^}}} // end of method 'sqrt_intrinsic'{{$}}
define double @sqrt_intrinsic(double %x) {
  %r = call double @llvm.sqrt.f64(double %x)
  ret double %r
}

; MIX-LABEL: {{^}}.method public static float32 'sqrt_import'(float32) cil managed{{$}}
; MIX-NEXT:  {{^\{$}}
; MIX-NEXT:  {{^}}  .maxstack 1{{$}}
; MIX-NEXT:  {{^}}  ldarg.0{{$}}
; MIX-NEXT:  {{^}}  call float32 'sqrt'(float32){{$}}
; MIX-NEXT:  {{^}}  ret{{$}}
; MIX-NEXT:  {{^}}} // end of method 'sqrt_import'{{$}}
define float @sqrt_import(float %x) {
  %r = call float @sqrt(float %x)
  ret float %r
}

; MIX-NOT:   {{^}}.method {{(public|assembly) static}}
; MIX-DAG:   {{^}}.method public hidebysig static pinvokeimpl("mylib" as "mysqrtf" cdecl) float32 'sqrt'(float32) cil managed preservesig {}{{$}}
; MIX-DAG:   {{^}}.method public hidebysig static pinvokeimpl("[[MLIB]]" cdecl) float64 'sqrt'(float64) cil managed preservesig {}{{$}}
; MIX-NOT:   {{^}}.method

; NMIX-COUNT-2: pinvokeimpl
; NMIX-NOT:     pinvokeimpl

declare float @sqrt(float) #0
declare double @llvm.sqrt.f64(double)

attributes #0 = { "msil-import-module"="mylib" "msil-import-name"="mysqrtf" }

;--- defined.ll
; sqrt, sqrtf and puts are defined here: the library calls of llvm.sqrt.f64
; and llvm.sqrt.f32 and the call of puts call these methods.
source_filename = "libcall-defined.c"

; DEF-LABEL: {{^}}.method public static float64 'sqrt'(float64) cil managed{{$}}
; DEF-LABEL: {{^}}.method public static float32 'sqrtf'(float32) cil managed{{$}}
; DEF-LABEL: {{^}}.method public static int32 'puts'(native int) cil managed{{$}}
define double @sqrt(double %x) {
  ret double %x
}

define float @sqrtf(float %x) {
  ret float %x
}

define i32 @puts(ptr %s) {
  ret i32 7
}

; DEF-LABEL: {{^}}.method public static float64 'use_sqrt'(float64) cil managed{{$}}
; DEF-NEXT:  {{^\{$}}
; DEF-NEXT:  {{^}}  .maxstack 1{{$}}
; DEF-NEXT:  {{^}}  ldarg.0{{$}}
; DEF-NEXT:  {{^}}  call float64 'sqrt'(float64){{$}}
; DEF-NEXT:  {{^}}  ret{{$}}
; DEF-NEXT:  {{^}}} // end of method 'use_sqrt'{{$}}
define double @use_sqrt(double %x) {
  %r = call double @llvm.sqrt.f64(double %x)
  ret double %r
}

; DEF-LABEL: {{^}}.method public static float32 'use_sqrtf'(float32) cil managed{{$}}
; DEF-NEXT:  {{^\{$}}
; DEF-NEXT:  {{^}}  .maxstack 1{{$}}
; DEF-NEXT:  {{^}}  ldarg.0{{$}}
; DEF-NEXT:  {{^}}  call float32 'sqrtf'(float32){{$}}
; DEF-NEXT:  {{^}}  ret{{$}}
; DEF-NEXT:  {{^}}} // end of method 'use_sqrtf'{{$}}
define float @use_sqrtf(float %x) {
  %r = call float @llvm.sqrt.f32(float %x)
  ret float %r
}

; DEF-LABEL: {{^}}.method public static int32 'use_puts'(native int) cil managed{{$}}
; DEF-NEXT:  {{^\{$}}
; DEF-NEXT:  {{^}}  .maxstack 1{{$}}
; DEF-NEXT:  {{^}}  ldarg.0{{$}}
; DEF-NEXT:  {{^}}  call int32 'puts'(native int){{$}}
; DEF-NEXT:  {{^}}  ret{{$}}
; DEF-NEXT:  {{^}}} // end of method 'use_puts'{{$}}
; DEF-NOT:   {{^}}.method
define i32 @use_puts(ptr %s) {
  %r = call i32 @puts(ptr %s)
  ret i32 %r
}

declare double @llvm.sqrt.f64(double)
declare float @llvm.sqrt.f32(float)

;--- mismatch.ll
; float sqrtf(double) is defined here, so the sqrtf library call of
; llvm.sqrt.f32 (float32(float32)) cannot call it.
source_filename = "libcall-mismatch.c"

; MISMATCH: {{^}}error: {{.*}}in function use_sqrtf {{.*}}: library call 'sqrtf' does not match the type of the function of that name in this module{{$}}

define float @sqrtf(double %y) {
  %x = fptrunc double %y to float
  ret float %x
}

define float @use_sqrtf(float %x) {
  %r = call float @llvm.sqrt.f32(float %x)
  ret float %r
}

declare float @llvm.sqrt.f32(float)

;--- mingw.ll
; x86_64-w64-windows-gnu-msil: msvcrt.dll has no round, and no ldexpf.
source_filename = "libcall-mingw.c"

; MINGW: {{^}}error: {{.*}}in function round_f64 {{.*}}: library call 'round' is not a Phase-1 import{{$}}

; MINGW-IL-LABEL: {{^}}.method public static float64 'round_f64'(float64) cil managed{{$}}
; MINGW-IL-NOT:   call
; MINGW-IL:       {{^}}} // end of method 'round_f64'{{$}}
; MINGW-IL-LABEL: {{^}}.method public static float32 'ldexp_f32'(float32, int32) cil managed{{$}}
; MINGW-IL-NEXT:  {{^\{$}}
; MINGW-IL-NEXT:  {{^}}  .maxstack 2{{$}}
; MINGW-IL-NEXT:  {{^}}  ldarg.0{{$}}
; MINGW-IL-NEXT:  {{^}}  conv.r8{{$}}
; MINGW-IL-NEXT:  {{^}}  ldarg.1{{$}}
; MINGW-IL-NEXT:  {{^}}  call float64 'ldexp'(float64, int32){{$}}
; MINGW-IL-NEXT:  {{^}}  conv.r4{{$}}
; MINGW-IL-NEXT:  {{^}}  ret{{$}}
; MINGW-IL-NEXT:  {{^}}} // end of method 'ldexp_f32'{{$}}
; MINGW-IL:       {{^}}.method public hidebysig static pinvokeimpl("msvcrt" cdecl) float64 'ldexp'(float64, int32) cil managed preservesig {}{{$}}

define double @round_f64(double %x) {
  %r = call double @llvm.round.f64(double %x)
  ret double %r
}

define float @ldexp_f32(float %x, i32 %e) {
  %r = call float @llvm.ldexp.f32.i32(float %x, i32 %e)
  ret float %r
}

declare double @llvm.round.f64(double)
declare float @llvm.ldexp.f32.i32(float, i32)

;--- escdef.ll
; double my_sqrt(double) __asm__("sqrt") is defined here: its IR name is
; "\01sqrt" and its IL name 'sqrt', so the FSQRT library call of llvm.sqrt.f64
; calls this method.
source_filename = "libcall-escdef.c"

; ESC:      {{^}}.method public static float64 'sqrt'(float64) cil managed{{$}}
; ESC:      {{^}}} // end of method 'sqrt'{{$}}
; ESC:      {{^}}.method public static float64 'use_sqrt'(float64) cil managed{{$}}
; ESC-NEXT: {{^\{$}}
; ESC-NEXT: {{^}}  .maxstack 1{{$}}
; ESC-NEXT: {{^}}  ldarg.0{{$}}
; ESC-NEXT: {{^}}  call float64 'sqrt'(float64){{$}}
; ESC-NEXT: {{^}}  ret{{$}}
; ESC-NEXT: {{^}}} // end of method 'use_sqrt'{{$}}
; ESC:      {{^}}.method public static int32 'main'() cil managed{{$}}
; ESC:      {{^}}  call float64 'use_sqrt'(float64){{$}}
; ESC:      {{^}}} // end of method 'main'{{$}}

define double @"\01sqrt"(double %x) {
  %r = fadd double %x, 1.000000e+00
  ret double %r
}

define double @use_sqrt(double %x) {
  %r = call double @llvm.sqrt.f64(double %x)
  ret double %r
}

define i32 @main() {
  %d = call double @use_sqrt(double 1.500000e+01)
  %r = fptosi double %d to i32
  ret i32 %r
}

declare double @llvm.sqrt.f64(double)

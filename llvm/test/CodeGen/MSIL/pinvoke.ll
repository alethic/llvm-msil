; P/Invoke declarations for the Phase-1 import table (ImplementationPlan.md
; decision D14, M8.a and M8.c, plan 5.1; recon cil-runtime.md e08): every
; undefined function that the module calls and that is a Phase-1 import gets
; one declaration after the methods,
;   .method public hidebysig static pinvokeimpl("<lib>" nomangle cdecl) <ret> '<name>'(<params>) cil managed preservesig {}
; with the same blittable signature as its call sites ('call <ret>
; '<name>'(<params>)'): int32, native int, float32, float64 or void, and i8 or
; i16 arguments and results widened to int32. The library depends on the
; triple:
;   C library (puts, putchar):       linux-gnu, linux-musl "libc",
;                                    windows-msvc "ucrtbase", windows-gnu "msvcrt"
;   C math library (D14's list and   linux-gnu "libm.so.6", linux-musl "libc",
;   the f variants):                 windows-msvc "ucrtbase", windows-gnu "msvcrt"
; on x86_64 and aarch64 alike. On the Windows triples a math function is an
; import only when the C runtime DLL exports it under that name (D14 as
; amended in plan section 12, 'Windows import sets follow the DLL exports';
; Design.md 5.2): windows-msvc imports all of them but frexpf, ldexpf and
; hypotf, and windows-gnu (msvcrt.dll) imports sqrt, floor, ceil, fmod, the
; trigonometric, hyperbolic, exponential and logarithmic functions of C89,
; pow, ldexp, frexp and modf and their f variants but ldexpf and frexpf, and
; neither hypot nor any C99 function (cbrt, trunc, round, ..., log1p) nor
; their f variants. The calls of hypot are therefore in their own part
; (hypot.ll), which the windows-gnu run leaves out, and the call of hypotf in
; another (hypotf.ll), which only the Linux runs include; that calling them
; on the Windows triples is the 'not a Phase-1 import' error is checked in
; pinvoke-errors.ll. A function with the "msil-import-module" attribute is
; imported from that library, and "msil-import-name" names the entry point
; ('as "<entry>"'); without it the entry point is the function's own name and
; no 'as' is printed. Both are printed as ILAsm string literals with D11's
; octal escapes, a backslash as '\134' and a double quote as '\042', so a
; Windows path keeps its backslashes (ilasm would silently read a raw '\l' or
; '\f' as an escape, so only FileCheck catches that). A function that is
; called several times, or from several functions, is declared once; an
; import that is declared but never used is not declared. There is no
; DisableRuntimeMarshalling (D10), no module initializer (no globals), nothing
; is reported, and every module assembles (ilasm does not resolve the
; libraries).
;
; The native calling conventions leave the upper bits of an i8, i16 or i1
; result unspecified (the MSVC x64 convention does not extend them), so the
; signext and zeroext attributes of an import's result promise nothing: the
; caller extends the int32 that the P/Invoke returns itself, as it does for a
; call result without the attribute (narrow-ints.ll): 'conv.i2' after the
; signext i16 result of narrow, 'conv.u1' (the mask 255) after the zeroext i8
; result of nbyte, 'conv.i1' after the signext i8 result of nsbyte, and
; 'ldc.i4.1; and' after the zeroext i1 result of nflag, both when it is
; returned and when it decides a branch. A zeroext or signext argument is
; extended by the caller as for any call ('conv.u1' before the call of
; narrow). The windows-msvc and linux-gnu runs are repeated at -O0, where the
; same extensions follow the calls.
;
; The P/Invoke declarations are matched as a group (their order is not
; pinned), and a second FileCheck run counts them: 17 with hypot, 18 with
; hypot and hypotf (Linux), 16 on windows-gnu.
;
; Two more parts, on x86_64-unknown-linux-gnu-msil and
; x86_64-pc-windows-msvc-msil, each compiled without any message and
; assembled:
; - nonascii.ll: an import whose library and entry point are not ASCII in a
;   module whose IR names and source file name are (plan section 12, 'D11 and
;   M8.c amended (byte order mark for import strings)'). The quoted strings of
;   'pinvokeimpl' copy the bytes 0x80 and above raw, as identifiers do, so the
;   file starts with the UTF-8 byte order mark, directly followed by the
;   header comment; without it Framework ilasm reads the file in the ANSI code
;   page and stores the library and entry names double-encoded, which only
;   fails at the first call.
; - bind.ll: puts is declared, and a function whose IL name is 'puts' (the
;   IR name "\01puts") is defined with the same CIL signature (plan section
;   12, 'M8.a and M8.c amended (IL names of `<Module>` members)' part (2)).
;   The declaration gets no P/Invoke: the calls of both bind to the one
;   method 'puts', as a native link binds the symbol.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=DisableRuntimeMarshalling --implicit-check-not=.cctor \
; DEFINE:   --implicit-check-not="'cbrt'" --implicit-check-not=int64
; DEFINE: %{count} = FileCheck %s --check-prefix=NPINV
; DEFINE: %{countlnx} = FileCheck %s --check-prefix=NPINVLNX
; RUN: split-file %s %t
; RUN: cat %t/common.ll %t/hypot.ll > %t/all.ll
; RUN: cat %t/common.ll %t/hypot.ll %t/hypotf.ll > %t/all-lnx.ll
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/all-lnx.ll \
; RUN:   -o %t.lnx.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,HYPOT,HYPOTF -DCLIB=libc -DMLIB=libm.so.6 --input-file=%t.lnx.il
; RUN: %{countlnx} --input-file=%t.lnx.il
; RUN: llc -mtriple=x86_64-unknown-linux-musl-msil -asm-verbose=false -verify-machineinstrs < %t/all-lnx.ll \
; RUN:   -o %t.musl.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,HYPOT,HYPOTF -DCLIB=libc -DMLIB=libc --input-file=%t.musl.il
; RUN: %{countlnx} --input-file=%t.musl.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/all.ll -o %t.win.il \
; RUN:   2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,HYPOT -DCLIB=ucrtbase -DMLIB=ucrtbase --input-file=%t.win.il
; RUN: %{count} --input-file=%t.win.il
; RUN: llc -mtriple=x86_64-w64-windows-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/common.ll \
; RUN:   -o %t.mingw.il 2>&1 | count 0
; RUN: %{check} -DCLIB=msvcrt -DMLIB=msvcrt --input-file=%t.mingw.il
; RUN: FileCheck %s --check-prefix=NPINVGNU --input-file=%t.mingw.il
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/all-lnx.ll \
; RUN:   -o %t.a64lnx.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,HYPOT,HYPOTF -DCLIB=libc -DMLIB=libm.so.6 --input-file=%t.a64lnx.il
; RUN: %{countlnx} --input-file=%t.a64lnx.il
; RUN: llc -mtriple=aarch64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/all.ll \
; RUN:   -o %t.a64win.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,HYPOT -DCLIB=ucrtbase -DMLIB=ucrtbase --input-file=%t.a64win.il
; RUN: %{count} --input-file=%t.a64win.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/all.ll \
; RUN:   -o %t.win.o0.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,HYPOT -DCLIB=ucrtbase -DMLIB=ucrtbase --input-file=%t.win.o0.il
; RUN: %{count} --input-file=%t.win.o0.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/all-lnx.ll \
; RUN:   -o %t.lnx.o0.il 2>&1 | count 0
; RUN: %{check} --check-prefixes=CHECK,HYPOT,HYPOTF -DCLIB=libc -DMLIB=libm.so.6 --input-file=%t.lnx.o0.il
; RUN: %{countlnx} --input-file=%t.lnx.o0.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.lnx.dll %t.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.dll %t.win.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.mingw.dll %t.mingw.il %}

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/nonascii.ll \
; RUN:   -o %t/nonascii.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=NONASCII --implicit-check-not=pinvokeimpl --input-file=%t/nonascii.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/nonascii.ll \
; RUN:   -o %t/nonascii.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=NONASCII --implicit-check-not=pinvokeimpl --input-file=%t/nonascii.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/nonascii.lnx.dll %t/nonascii.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/nonascii.win.dll %t/nonascii.win.il %}

; DEFINE: %{bind} = FileCheck %s --check-prefix=BIND --implicit-check-not=pinvokeimpl \
; DEFINE:   --implicit-check-not="'puts'"
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/bind.ll \
; RUN:   -o %t/bind.lnx.il 2>&1 | count 0
; RUN: %{bind} --input-file=%t/bind.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/bind.ll \
; RUN:   -o %t/bind.win.il 2>&1 | count 0
; RUN: %{bind} --input-file=%t/bind.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/bind.lnx.dll %t/bind.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/bind.win.dll %t/bind.win.il %}

;--- common.ll
source_filename = "pinvoke.c"

declare i32 @puts(ptr)
declare i32 @putchar(i32)
declare double @sqrt(double)
declare double @frexp(double, ptr)
declare double @ldexp(double, i32)
declare float @floorf(float)
declare float @fmodf(float, float)
; A math function that is never called.
declare double @cbrt(double)
declare i32 @mylib_add(i32, i32) #0
declare i64 @renamed(i64) #1
declare void @log_msg(ptr) #2
declare signext i16 @narrow(i8 zeroext) #0
declare zeroext i8 @nbyte(i32) #0
declare signext i8 @nsbyte(i32) #0
declare zeroext i1 @nflag(i32) #0
declare i32 @winpath(i32) #3
declare void @quoted(ptr) #4

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

; CHECK-LABEL: {{^}}.method public static void 'hello_twice'(native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'puts'(native int){{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'puts'(native int){{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'hello_twice'{{$}}
define void @hello_twice(ptr %s) {
  %a = call i32 @puts(ptr %s)
  %b = call i32 @puts(ptr %s)
  ret void
}

; CHECK-LABEL: {{^}}.method public static int32 'put'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'putchar'(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'put'{{$}}
define i32 @put(i32 %c) {
  %r = call i32 @putchar(i32 %c)
  ret i32 %r
}

; CHECK-LABEL: {{^}}.method public static float64 'math'(float64, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call float64 'sqrt'(float64){{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call float64 'frexp'(float64, native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.3{{$}}
; CHECK-NEXT:  {{^}}  call float64 'ldexp'(float64, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'math'{{$}}
define double @math(double %x, ptr %e) {
  %a = call double @sqrt(double %x)
  %c = call double @frexp(double %a, ptr %e)
  %d = call double @ldexp(double %c, i32 3)
  ret double %d
}

; CHECK-LABEL: {{^}}.method public static float32 'mathf'(float32, float32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call float32 'floorf'(float32){{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call float32 'fmodf'(float32, float32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'mathf'{{$}}
define float @mathf(float %x, float %y) {
  %a = call float @floorf(float %x)
  %b = call float @fmodf(float %a, float %y)
  ret float %b
}

; CHECK-LABEL: {{^}}.method public static int32 'custom'(int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call int32 'mylib_add'(int32, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'custom'{{$}}
define i32 @custom(i32 %a, i32 %b) {
  %r = call i32 @mylib_add(i32 %a, i32 %b)
  ret i32 %r
}

; The call names the IR function; only the declaration names the entry point.
; CHECK-LABEL: {{^}}.method public static native int 'custom_renamed'(native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call native int 'renamed'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'custom_renamed'{{$}}
define i64 @custom_renamed(i64 %x) {
  %r = call i64 @renamed(i64 %x)
  ret i64 %r
}

; CHECK-LABEL: {{^}}.method public static void 'custom_void'(native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call void 'log_msg'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'custom_void'{{$}}
define void @custom_void(ptr %p) {
  call void @log_msg(ptr %p)
  ret void
}

; The zeroext argument is extended before the call, and the signext i16
; result after it.
; CHECK-LABEL: {{^}}.method public static int32 'call_narrow'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.u1{{$}}
; CHECK-NEXT:  {{^}}  call int32 'narrow'(int32){{$}}
; CHECK-NEXT:  {{^}}  conv.i2{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_narrow'{{$}}
define i32 @call_narrow(i32 %x) {
  %t = trunc i32 %x to i8
  %r = call signext i16 @narrow(i8 zeroext %t)
  %e = sext i16 %r to i32
  ret i32 %e
}

; CHECK-LABEL: {{^}}.method public static int32 'call_nbyte'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'nbyte'(int32){{$}}
; CHECK-NEXT:  {{^}}  conv.u1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_nbyte'{{$}}
define i32 @call_nbyte(i32 %x) {
  %r = call zeroext i8 @nbyte(i32 %x)
  %e = zext i8 %r to i32
  ret i32 %e
}

; CHECK-LABEL: {{^}}.method public static int32 'call_nsbyte'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'nsbyte'(int32){{$}}
; CHECK-NEXT:  {{^}}  conv.i1{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_nsbyte'{{$}}
define i32 @call_nsbyte(i32 %x) {
  %r = call signext i8 @nsbyte(i32 %x)
  %e = sext i8 %r to i32
  ret i32 %e
}

; CHECK-LABEL: {{^}}.method public static int32 'call_nflag'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'nflag'(int32){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_nflag'{{$}}
define i32 @call_nflag(i32 %x) {
  %r = call zeroext i1 @nflag(i32 %x)
  %e = zext i1 %r to i32
  ret i32 %e
}

; CHECK-LABEL: {{^}}.method public static int32 'branch_nflag'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'nflag'(int32){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  and{{$}}
; CHECK-NEXT:  {{^}}  {{(brfalse|brtrue)\.s @BB[0-9]+_[0-9]+$}}
; CHECK:       {{^}}} // end of method 'branch_nflag'{{$}}
define i32 @branch_nflag(i32 %x) {
entry:
  %r = call zeroext i1 @nflag(i32 %x)
  br i1 %r, label %yes, label %no
yes:
  ret i32 7
no:
  ret i32 9
}

; Library and entry strings with a backslash and a double quote (attributes
; #3 and #4).
; CHECK-LABEL: {{^}}.method public static int32 'call_winpath'(int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'winpath'(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_winpath'{{$}}
define i32 @call_winpath(i32 %x) {
  %r = call i32 @winpath(i32 %x)
  ret i32 %r
}

; CHECK-LABEL: {{^}}.method public static void 'call_quoted'(native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call void 'quoted'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_quoted'{{$}}
define void @call_quoted(ptr %p) {
  call void @quoted(ptr %p)
  ret void
}

; The method of hypot.ll, which follows this part on every triple but
; windows-gnu.
; HYPOT-LABEL: {{^}}.method public static float64 'math_hypot'(float64, float64) cil managed{{$}}
; HYPOT-NEXT:  {{^\{$}}
; HYPOT-NEXT:  {{^}}  .maxstack 2{{$}}
; HYPOT-NEXT:  {{^}}  ldarg.0{{$}}
; HYPOT-NEXT:  {{^}}  ldarg.1{{$}}
; HYPOT-NEXT:  {{^}}  call float64 'hypot'(float64, float64){{$}}
; HYPOT-NEXT:  {{^}}  ret{{$}}
; HYPOT-NEXT:  {{^}}} // end of method 'math_hypot'{{$}}

; The method of hypotf.ll, which follows hypot.ll on the Linux triples.
; HYPOTF-LABEL: {{^}}.method public static float32 'math_hypotf'(float32, float32) cil managed{{$}}
; HYPOTF-NEXT:  {{^\{$}}
; HYPOTF-NEXT:  {{^}}  .maxstack 2{{$}}
; HYPOTF-NEXT:  {{^}}  ldarg.0{{$}}
; HYPOTF-NEXT:  {{^}}  ldarg.1{{$}}
; HYPOTF-NEXT:  {{^}}  call float32 'hypotf'(float32, float32){{$}}
; HYPOTF-NEXT:  {{^}}  ret{{$}}
; HYPOTF-NEXT:  {{^}}} // end of method 'math_hypotf'{{$}}

; CHECK-NOT:   {{^}}.method {{(public|assembly) static}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("[[CLIB]]" nomangle cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("[[CLIB]]" nomangle cdecl) int32 'putchar'(int32) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("[[MLIB]]" nomangle cdecl) float64 'sqrt'(float64) cil managed preservesig {}{{$}}
; HYPOT-DAG:   {{^}}.method public hidebysig static pinvokeimpl("[[MLIB]]" nomangle cdecl) float64 'hypot'(float64, float64) cil managed preservesig {}{{$}}
; HYPOTF-DAG:  {{^}}.method public hidebysig static pinvokeimpl("[[MLIB]]" nomangle cdecl) float32 'hypotf'(float32, float32) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("[[MLIB]]" nomangle cdecl) float64 'frexp'(float64, native int) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("[[MLIB]]" nomangle cdecl) float64 'ldexp'(float64, int32) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("[[MLIB]]" nomangle cdecl) float32 'floorf'(float32) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("[[MLIB]]" nomangle cdecl) float32 'fmodf'(float32, float32) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("mylib" nomangle cdecl) int32 'mylib_add'(int32, int32) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("mylib" as "real_name" nomangle cdecl) native int 'renamed'(native int) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("native.lib.so.2" nomangle cdecl) void 'log_msg'(native int) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("mylib" nomangle cdecl) int32 'narrow'(int32) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("mylib" nomangle cdecl) int32 'nbyte'(int32) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("mylib" nomangle cdecl) int32 'nsbyte'(int32) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("mylib" nomangle cdecl) int32 'nflag'(int32) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("C:\134libs\134foo.dll" nomangle cdecl) int32 'winpath'(int32) cil managed preservesig {}{{$}}
; CHECK-DAG:   {{^}}.method public hidebysig static pinvokeimpl("my\042lib" as "entry\134name" nomangle cdecl) void 'quoted'(native int) cil managed preservesig {}{{$}}
; CHECK-NOT:   {{^}}.method

; NPINV-COUNT-17:    pinvokeimpl
; NPINV-NOT:         pinvokeimpl
; NPINVLNX-COUNT-18: pinvokeimpl
; NPINVLNX-NOT:      pinvokeimpl
; NPINVGNU-COUNT-16: pinvokeimpl
; NPINVGNU-NOT:      pinvokeimpl

attributes #0 = { "msil-import-module"="mylib" }
attributes #1 = { "msil-import-module"="mylib" "msil-import-name"="real_name" }
attributes #2 = { "msil-import-module"="native.lib.so.2" }
; The library C:\libs\foo.dll; the library my"lib with the entry point
; entry\name.
attributes #3 = { "msil-import-module"="C:\5Clibs\5Cfoo.dll" }
attributes #4 = { "msil-import-module"="my\22lib" "msil-import-name"="entry\5Cname" }

;--- hypot.ll
; hypot, an import on every triple but windows-gnu; appended to common.ll.
declare double @hypot(double, double)

define double @math_hypot(double %x, double %y) {
  %r = call double @hypot(double %x, double %y)
  ret double %r
}

;--- hypotf.ll
; hypotf, an import on the Linux triples only; appended after hypot.ll.
declare float @hypotf(float, float)

define float @math_hypotf(float %x, float %y) {
  %r = call float @hypotf(float %x, float %y)
  ret float %r
}

;--- nonascii.ll
; The library 'cafe' and the entry point 'entree', both with an acute accent
; (IR escapes \C3\A9); every IR name and the source file name are ASCII.
source_filename = "pinvoke-nonascii.c"

; NONASCII-NOT:  {{.}}
; NONASCII:      {{^}}{{\xEF\xBB\xBF}}// MSIL module generated by LLVM{{$}}
; NONASCII:      {{^}}.method public static int32 'call_myabs'(int32) cil managed{{$}}
; NONASCII-NEXT: {{^\{$}}
; NONASCII-NEXT: {{^}}  .maxstack 1{{$}}
; NONASCII-NEXT: {{^}}  ldarg.0{{$}}
; NONASCII-NEXT: {{^}}  call int32 'myabs'(int32){{$}}
; NONASCII-NEXT: {{^}}  ret{{$}}
; NONASCII-NEXT: {{^}}} // end of method 'call_myabs'{{$}}
; NONASCII:      {{^}}.method public hidebysig static pinvokeimpl("caf{{\xC3\xA9}}" as "entr{{\xC3\xA9}}e" nomangle cdecl) int32 'myabs'(int32) cil managed preservesig {}{{$}}

declare i32 @myabs(i32) #0

define i32 @call_myabs(i32 %x) {
  %r = call i32 @myabs(i32 %x)
  ret i32 %r
}

attributes #0 = { "msil-import-module"="caf\C3\A9" "msil-import-name"="entr\C3\A9e" }

;--- bind.ll
; int puts(const char *); and a definition named 'puts' in the object file,
; as int my_puts(const char *) __asm__("puts") spells it.
source_filename = "pinvoke-bind.c"

; BIND:      {{^}}.method public static int32 'puts'(native int) cil managed{{$}}
; BIND:      {{^}}} // end of method 'puts'{{$}}
; BIND:      {{^}}.method public static int32 'main'() cil managed{{$}}
; BIND:      {{^}}  call int32 'puts'(native int){{$}}
; BIND:      {{^}}  call int32 'puts'(native int){{$}}
; BIND:      {{^}}} // end of method 'main'{{$}}

declare i32 @puts(ptr)

define i32 @"\01puts"(ptr %s) {
  ret i32 5
}

define i32 @main() {
  %a = call i32 @puts(ptr null)
  %b = call i32 @"\01puts"(ptr null)
  %r = add i32 %a, %b
  ret i32 %r
}

; The variadic errors of MSILPrepareIR (ImplementationPlan.md 5.15.2 'Compile
; time', M26; plan section 2 'Diagnostics leave well-formed output').
; MSILPrepareIR (slot P1) reports, with DiagnosticInfoUnsupported naming the
; function, and deletes the function's body (the function then has no
; method):
; - every musttail call: 'musttail calls are not supported for MSIL'
;   (ExpandVariadics would stop with report_fatal_error on a variadic
;   musttail forwarder), on every triple, in a variadic function and in an
;   ordinary one;
; - on the SysV (x86_64-unknown-linux-gnu-msil) and AAPCS64
;   (aarch64-unknown-linux-gnu-msil) triples every va_arg instruction: 'the
;   va_arg instruction is not supported for MSIL on this triple; clang lowers
;   C's va_arg itself'. On the char* triples (windows-msvc) an IR va_arg is
;   accepted and expanded (va-intrinsics.ll has the types).
; - on the aarch64 Darwin triples (aarch64-apple-macosx-msil,
;   aarch64-apple-ios-msil), which have no MSIL variadic ABI yet (5.15.2's
;   table: Darwin later), every variadic definition: 'variadic function
;   definitions are not supported for MSIL yet' (review r1 finding 5: the
;   definition's va_start reached ISel and crashed).
; - on every triple, a variadic definition with a calling convention other
;   than C (ExpandVariadics expands only C's and leaves the others alone):
;   'variadic function definitions are not supported for MSIL yet' (cc.ll:
;   preserve_mostcc on x86_64-pc-windows-msvc-msil; cc-w64.ll: win64cc on
;   x86_64-unknown-linux-gnu-msil; review r2: ExpandVariadics stopped with
;   'LLVM ERROR: Cannot lower callbase instruction').
; The calls that ExpandVariadics leaves variadic, every variadic call on the
; aarch64 Darwin triples (darwin-call.ll: a declared variadic function and a
; printf call) and a variadic call with a calling convention other than C
; (cc.ll, cc-w64.ll), each give 'calls to variadic functions are not
; supported for MSIL yet', once per call, in the calling function. cc-w64.ll
; may also give 'calling convention is not supported for MSIL' (win64cc is
; no MSIL convention), so it checks the variadic texts in either order.
; In linked text (the module flag "msil-linked" 1), a call of a native
; variadic function that msil-ld bound (its !msil.imports operand) and that
; ExpandVariadics left variadic, as every variadic call on the aarch64 Darwin
; triples, gives 5.15.2's backstop 'call of native variadic function '<f>'
; (<module>) was not expanded for MSIL', a plain error without an enclosing
; function's location (darwin-linked.ll: printf from libc.so.6 on
; aarch64-apple-macosx-msil).
; In whole-program mode the address of an undefined variadic function that is
; on the fixed-arity list (open) or an explicit import (an
; "msil-import-module" declaration) gives 'undefined variadic function '<f>'
; is not supported for MSIL yet', once per function (vaddr.ll; the object
; mode compiles both, variadic-call.ll's vaddr-*.ll).
; A K&R call of a variadic definition whose type cannot be made the
; definition's own (MSILFixFunctionBitcasts' makeKnRCallsVariadic needs
; arguments that cover the fixed parameters, each castable to its type; a
; double or an i64 for an i32, or no argument at all, is not) is reported on
; every triple by makeKnRCallsVariadic itself (slot P5), with LowerCall's text
; 'mismatched call signature' in the caller, and erased, so ExpandVariadics
; never sees it (knr.ll; review r1 finding 6: ExpandVariadics asserted on such
; a call).
; Each function gives exactly one error, in function order, the functions
; after them compile, llc exits with 1 and never prints 'LLVM ERROR' or a
; crash report, at -O0 and -O2, and the IL on stdout assembles.

; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{diag} = FileCheck %s --implicit-check-not="LLVM ERROR" --implicit-check-not="PLEASE submit" \
; DEFINE:   --implicit-check-not="Stack dump" --implicit-check-not=error: --implicit-check-not=warning:

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/musttail.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=MT
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/musttail.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=MT
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/musttail.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=MT
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/musttail.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=MT
; RUN: not llc -mtriple=aarch64-unknown-linux-gnu-msil -verify-machineinstrs < %t/musttail.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=MT
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/musttail.ll \
; RUN:   > %t/musttail.lnx.il
; RUN: FileCheck %s --check-prefix=MT-IL --input-file=%t/musttail.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/musttail.ll \
; RUN:   > %t/musttail.win.il
; RUN: FileCheck %s --check-prefix=MT-IL --input-file=%t/musttail.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/musttail.lnx.dll %t/musttail.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/musttail.win.dll %t/musttail.win.il %}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-prepare-ir < %t/musttail.ll -o - 2>/dev/null \
; RUN:   | FileCheck %s --check-prefix=MT-IR

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/vaarg.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VAARG
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/vaarg.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VAARG
; RUN: not llc -mtriple=aarch64-unknown-linux-gnu-msil -verify-machineinstrs < %t/vaarg.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VAARG
; RUN: not llc -mtriple=aarch64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/vaarg.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VAARG
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/vaarg.ll \
; RUN:   > %t/vaarg.lnx.il
; RUN: FileCheck %s --check-prefix=VAARG-IL --input-file=%t/vaarg.lnx.il --implicit-check-not="'next_int'" \
; RUN:   --implicit-check-not="'sum_va'"
; RUN: not llc -mtriple=aarch64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/vaarg.ll \
; RUN:   > %t/vaarg.a64.il
; RUN: FileCheck %s --check-prefix=VAARG-IL --input-file=%t/vaarg.a64.il --implicit-check-not="'next_int'" \
; RUN:   --implicit-check-not="'sum_va'"
; RUN: %if ilasm %{ %ilasm-verify -output=%t/vaarg.lnx.dll %t/vaarg.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t/vaarg.a64.dll %t/vaarg.a64.il %}
; The char* triples accept the same module.
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/vaarg.ll \
; RUN:   -o %t/vaarg.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=VAARG-WIN --input-file=%t/vaarg.win.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/vaarg.ll \
; RUN:   -o %t/vaarg.win.o0.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=VAARG-WIN --input-file=%t/vaarg.win.o0.il
; RUN: llc -mtriple=aarch64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/vaarg.ll \
; RUN:   -o %t/vaarg.a64win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=VAARG-WIN --input-file=%t/vaarg.a64win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/vaarg.win.dll %t/vaarg.win.il %}

; RUN: not llc -mtriple=aarch64-apple-macosx-msil -verify-machineinstrs < %t/darwin.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=DARWIN
; RUN: not llc -mtriple=aarch64-apple-macosx-msil -O0 -verify-machineinstrs < %t/darwin.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=DARWIN
; RUN: not llc -mtriple=aarch64-apple-ios-msil -verify-machineinstrs < %t/darwin.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=DARWIN
; RUN: not llc -mtriple=aarch64-apple-ios-msil -O0 -verify-machineinstrs < %t/darwin.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=DARWIN
; RUN: not llc -mtriple=aarch64-apple-macosx-msil -asm-verbose=false -verify-machineinstrs < %t/darwin.ll \
; RUN:   > %t/darwin.mac.il
; RUN: FileCheck %s --check-prefix=DARWIN-IL --input-file=%t/darwin.mac.il
; RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t/darwin.mac.dll %t/darwin.mac.il %}

; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{in} =
; DEFINE: %{p} =
; DEFINE: %{errs} = not llc -mtriple=%{t} %{o} -verify-machineinstrs < %t/%{in}.ll -o /dev/null 2>&1 \
; DEFINE:   | %{diag} --check-prefix=%{p} && \
; DEFINE:   not llc -mtriple=%{t} %{o} -filetype=obj -verify-machineinstrs < %t/%{in}.ll -o /dev/null 2>&1 \
; DEFINE:   | %{diag} --check-prefix=%{p}

; REDEFINE: %{in} = darwin-call
; REDEFINE: %{p} = DCALL
; REDEFINE: %{t} = aarch64-apple-macosx-msil
; REDEFINE: %{o} = -O0
; RUN: %{errs}
; REDEFINE: %{o} = -O2
; RUN: %{errs}
; REDEFINE: %{t} = aarch64-apple-ios-msil
; RUN: %{errs}
; REDEFINE: %{o} = -O0
; RUN: %{errs}
; RUN: not llc -mtriple=aarch64-apple-macosx-msil -asm-verbose=false -verify-machineinstrs < %t/darwin-call.ll \
; RUN:   > %t/darwin-call.mac.il
; RUN: FileCheck %s --check-prefix=DCALL-IL --input-file=%t/darwin-call.mac.il
; RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t/darwin-call.mac.dll %t/darwin-call.mac.il %}

; REDEFINE: %{in} = cc
; REDEFINE: %{p} = CC
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; RUN: %{errs}
; REDEFINE: %{o} = -O2
; RUN: %{errs}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %t/cc.ll \
; RUN:   > %t/cc.win.il
; RUN: FileCheck %s --check-prefix=CC-IL --input-file=%t/cc.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/cc.win.dll %t/cc.win.il %}

; cc-w64.ll may also give the calling-convention error, so its runs do not
; reject other errors.
; DEFINE: %{ccw} = FileCheck %s --check-prefix=CCW --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not="Stack dump"
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/cc-w64.ll -o /dev/null 2>&1 \
; RUN:   | %{ccw}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/cc-w64.ll -o /dev/null 2>&1 \
; RUN:   | %{ccw}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/cc-w64.ll \
; RUN:   -o /dev/null 2>&1 | %{ccw}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -filetype=obj -verify-machineinstrs < %t/cc-w64.ll \
; RUN:   -o /dev/null 2>&1 | %{ccw}

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/vaddr.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VADDR
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/vaddr.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VADDR
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/vaddr.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VADDR
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/vaddr.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=VADDR
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/vaddr.ll \
; RUN:   > %t/vaddr.lnx.il
; RUN: FileCheck %s --check-prefix=VADDR-IL --input-file=%t/vaddr.lnx.il

; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/knr.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=KNR
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %t/knr.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=KNR
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/knr.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=KNR
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %t/knr.ll -o /dev/null 2>&1 \
; RUN:   | %{diag} --check-prefix=KNR
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %t/knr.ll \
; RUN:   > %t/knr.lnx.il
; RUN: FileCheck %s --check-prefix=KNR-IL --input-file=%t/knr.lnx.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %t/knr.ll \
; RUN:   > %t/knr.win.il
; RUN: FileCheck %s --check-prefix=KNR-IL --input-file=%t/knr.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t/knr.lnx.dll %t/knr.lnx.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/knr.win.dll %t/knr.win.il %}

; REDEFINE: %{in} = darwin-linked
; REDEFINE: %{p} = DLINK
; REDEFINE: %{t} = aarch64-apple-macosx-msil
; REDEFINE: %{o} = -O0
; RUN: %{errs}
; REDEFINE: %{o} = -O2
; RUN: %{errs}
; RUN: not llc -mtriple=aarch64-apple-macosx-msil -asm-verbose=false -verify-machineinstrs < %t/darwin-linked.ll \
; RUN:   > %t/darwin-linked.mac.il
; RUN: FileCheck %s --check-prefix=DLINK-IL --input-file=%t/darwin-linked.mac.il
; RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t/darwin-linked.mac.dll %t/darwin-linked.mac.il %}

;--- musttail.ll
source_filename = "musttail.c"

; MT:    {{^}}error: {{.*}}in function fwd i32 (i32, ...): musttail calls are not supported for MSIL{{$}}
; MT:    {{^}}error: {{.*}}in function tail_self i64 (i64): musttail calls are not supported for MSIL{{$}}

; MT-IR: define i32 @take(i32 %n, ...)
; MT-IR: declare i32 @fwd(i32, ...)
; MT-IR: declare i64 @tail_self(i64)
; MT-IR: define i32 @after_musttail(

; The method of take (a variadic definition) and of the last function remain;
; fwd and tail_self have none.
; MT-IL:     {{^}}.method public static int32 'take'(int32, native int) cil managed{{$}}
; MT-IL-NOT: 'fwd'
; MT-IL-NOT: 'tail_self'
; MT-IL:     {{^}}.method public static int32 'after_musttail'(int32, int32) cil managed{{$}}
; MT-IL-NOT: 'fwd'
; MT-IL-NOT: 'tail_self'

define i32 @take(i32 %n, ...) {
  ret i32 %n
}

; A variadic forwarder: musttail passes its own variadic arguments on.
define i32 @fwd(i32 %n, ...) {
  %r = musttail call i32 (i32, ...) @take(i32 %n, ...)
  ret i32 %r
}

; An ordinary musttail call.
define i64 @tail_self(i64 %x) {
  %r = musttail call i64 @tail_self(i64 %x)
  ret i64 %r
}

define i32 @after_musttail(i32 %a, i32 %b) {
  %r = add i32 %a, %b
  ret i32 %r
}

;--- vaarg.ll
source_filename = "vaarg.c"

; VAARG:     {{^}}error: {{.*}}in function next_int i32 (ptr): the va_arg instruction is not supported for MSIL on this triple; clang lowers C's va_arg itself{{$}}
; VAARG:     {{^}}error: {{.*}}in function sum_va i32 (i32, ...): the va_arg instruction is not supported for MSIL on this triple; clang lowers C's va_arg itself{{$}}

; VAARG-IL:  {{^}}.method public static int32 'after_vaarg'(int32) cil managed{{$}}

; VAARG-WIN: {{^}}.method public static int32 'next_int'(native int) cil managed{{$}}
; VAARG-WIN: {{^}}.method public static int32 'sum_va'(int32, native int) cil managed{{$}}
; VAARG-WIN: {{^}}.method public static int32 'after_vaarg'(int32) cil managed{{$}}

; A va_list parameter read with an IR va_arg.
define i32 @next_int(ptr %ap) {
  %v = va_arg ptr %ap, i32
  ret i32 %v
}

; A variadic definition that reads its arguments with IR va_arg (the shape of
; IR written by hand or by a front end other than clang).
define i32 @sum_va(i32 %n, ...) {
entry:
  %ap = alloca [32 x i8], align 16
  call void @llvm.va_start.p0(ptr %ap)
  %a = va_arg ptr %ap, i32
  %b = va_arg ptr %ap, i32
  call void @llvm.va_end.p0(ptr %ap)
  %s = add i32 %a, %b
  ret i32 %s
}

define i32 @after_vaarg(i32 %a) {
  %r = add i32 %a, 1
  ret i32 %r
}

declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_end.p0(ptr)

;--- darwin.ll
source_filename = "darwin.c"

; DARWIN: {{^}}error: {{.*}}in function vdef i32 (i32, ...): variadic function definitions are not supported for MSIL yet{{$}}

; DARWIN-IL:     {{^}}.method public static int32 'after_darwin'(int32) cil managed{{$}}

; A variadic definition with va_start and va_end (clang's char* va_list).
define i32 @vdef(i32 %n, ...) {
  %ap = alloca ptr, align 8
  call void @llvm.va_start.p0(ptr %ap)
  call void @llvm.va_end.p0(ptr %ap)
  ret i32 %n
}

define i32 @after_darwin(i32 %a) {
  %r = add i32 %a, 1
  ret i32 %r
}

declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_end.p0(ptr)

;--- knr.ll
source_filename = "knr.c"

; KNR: {{^}}error: {{.*}}in function call_double i32 (): mismatched call signature{{$}}
; KNR: {{^}}error: {{.*}}in function call_none i32 (): mismatched call signature{{$}}
; KNR: {{^}}error: {{.*}}in function call_i64 i32 (): mismatched call signature{{$}}

; KNR-IL: {{^}}.method public static int32 'f'(int32, native int) cil managed{{$}}
; KNR-IL: {{^}}.method public static int32 'after_knr'(int32) cil managed{{$}}

define i32 @f(i32 %n, ...) {
  ret i32 %n
}

; int f(); ... f(2.5), f() and f(1L) against 'int f(int n, ...)'.
define i32 @call_double() {
  %r = call i32 @f(double 2.5)
  ret i32 %r
}

define i32 @call_none() {
  %r = call i32 @f()
  ret i32 %r
}

define i32 @call_i64() {
  %r = call i32 @f(i64 1)
  ret i32 %r
}

define i32 @after_knr(i32 %a) {
  %r = add i32 %a, 1
  ret i32 %r
}

;--- darwin-call.ll
source_filename = "darwin-call.c"

; Neither call is expanded on the Darwin triples (no MSIL variadic ABI).
; DCALL: {{^}}error: {{.*}}in function main i32 (): calls to variadic functions are not supported for MSIL yet{{$}}
; DCALL: {{^}}error: {{.*}}in function print_it void (i32): calls to variadic functions are not supported for MSIL yet{{$}}

; DCALL-IL: {{^}}.method public static int32 'after_darwin_call'(int32) cil managed{{$}}

@.str = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1

declare i32 @vf(i32, ...)
declare i32 @printf(ptr, ...)

define i32 @main() {
  %r = call i32 (i32, ...) @vf(i32 1, i32 2)
  ret i32 %r
}

define void @print_it(i32 %x) {
  %r = call i32 (ptr, ...) @printf(ptr @.str, i32 %x, double 1.5)
  ret void
}

define i32 @after_darwin_call(i32 %a) {
  %r = add i32 %a, 1
  ret i32 %r
}

;--- cc.ll
source_filename = "cc.c"

; A preserve_mostcc variadic definition and a preserve_mostcc variadic call
; through a pointer: ExpandVariadics expands neither.
; CC: {{^}}error: {{.*}}in function f i32 (i32, ...): variadic function definitions are not supported for MSIL yet{{$}}
; CC: {{^}}error: {{.*}}in function g i32 (ptr): calls to variadic functions are not supported for MSIL yet{{$}}

; CC-IL: {{^}}.method public static int32 'after_cc'(int32) cil managed{{$}}

define preserve_mostcc i32 @f(i32 %n, ...) {
  %ap = alloca ptr, align 8
  call void @llvm.va_start.p0(ptr %ap)
  call void @llvm.va_end.p0(ptr %ap)
  ret i32 %n
}

define i32 @g(ptr %fp) {
  %r = call preserve_mostcc i32 (i32, ...) %fp(i32 1, i32 2)
  ret i32 %r
}

define i32 @after_cc(i32 %a) {
  %r = add i32 %a, 1
  ret i32 %r
}

declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_end.p0(ptr)

;--- cc-w64.ll
source_filename = "cc-w64.c"

; The same with win64cc on the SysV triple.
; CCW-DAG: {{^}}error: {{.*}}in function f i32 (i32, ...): variadic function definitions are not supported for MSIL yet{{$}}
; CCW-DAG: {{^}}error: {{.*}}in function g i32 (ptr): calls to variadic functions are not supported for MSIL yet{{$}}

define win64cc i32 @f(i32 %n, ...) {
  %ap = alloca [24 x i8], align 8
  call void @llvm.va_start.p0(ptr %ap)
  call void @llvm.va_end.p0(ptr %ap)
  ret i32 %n
}

define i32 @g(ptr %fp) {
  %r = call win64cc i32 (i32, ...) %fp(i32 1, i32 2)
  ret i32 %r
}

define i32 @after_cc(i32 %a) {
  %r = add i32 %a, 1
  ret i32 %r
}

declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_end.p0(ptr)

;--- vaddr.ll
source_filename = "vaddr.c"

; Whole-program mode: the address of open (on the fixed-arity list) in an
; initializer and the address of an explicit variadic import in code. The
; order of the two errors is not the order of the declarations.
; VADDR-DAG: {{^}}error: undefined variadic function 'open' is not supported for MSIL yet{{$}}
; VADDR-DAG: {{^}}error: undefined variadic function 'myimp' is not supported for MSIL yet{{$}}

; VADDR-IL: {{^}}.method public static int32 'after_vaddr'(int32) cil managed{{$}}

@p = global ptr @open, align 8

declare i32 @open(ptr, i32, ...)
declare i32 @myimp(ptr, ...) #0

define ptr @get_myimp() {
  ret ptr @myimp
}

define i32 @after_vaddr(i32 %a) {
  %r = add i32 %a, 1
  ret i32 %r
}

attributes #0 = { "msil-import-module"="libc.so.6" "msil-import-name"="myimp" }

;--- darwin-linked.ll
source_filename = "darwin-linked.c"

; Linked text: msil-ld bound printf to libc.so.6, and the call is not
; expanded on the Darwin triples.
; DLINK: {{^}}error: call of native variadic function 'printf' (libc.so.6) was not expanded for MSIL{{$}}

; DLINK-IL: {{^}}.method public static int32 'after_darwin_linked'(int32) cil managed{{$}}

declare i32 @printf(ptr, ...)

define i32 @main() {
  %r = call i32 (ptr, ...) @printf(ptr null, i32 1)
  ret i32 %r
}

define i32 @after_darwin_linked(i32 %a) {
  %r = add i32 %a, 1
  ret i32 %r
}

!llvm.module.flags = !{!0}
!msil.imports = !{!1}

!0 = !{i32 4, !"msil-linked", i32 1}
!1 = !{!"printf", !"libc.so.6", !"printf", i32 0}

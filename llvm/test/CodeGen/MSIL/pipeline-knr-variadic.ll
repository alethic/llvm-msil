; A K&R call into a variadic definition of the same module (ImplementationPlan.md
; 5.13.6 P-4, M26; brief 3.1, CF's alternative). clang for MSIL triples emits
; fixed-arity K&R calls (function-bitcasts.ll), so 'int vdef(); ... vdef(1, 2,
; 3.0)' with a later definition 'int vdef(int n, ...)' is a call of @vdef with
; the call type i32 (i32, i32, double). MSILFixFunctionBitcasts (P5) runs
; before MSILPrepareVariadics (P6) and ExpandVariadics (P7), so whatever P5
; makes of the mismatched call (a wrapper whose body calls @vdef as a variadic
; call, or the call itself) is expanded by P7: after P9 no variadic function
; type is left in a definition or a call, and every IL call of 'vdef' uses its
; expanded signature (int32, native int), the method's own, so the IL
; assembles. The caller calls either a '_bitcast' wrapper with its own call
; type or 'vdef' with the expanded signature, and no call of 'vdef' passes its
; arguments one by one (the 'not grep' lines). Whole-program mode at -O0 and
; -O2 on both execution triples (with an ilasm round trip), and object mode,
; where calls of definitions keep Phase 1's wrappers (5.8.10): the record
; carries 'vdef' as a definition with a VARARG SIGNATURES entry (5.8.8,
; fixed=1) and no FIXED_ARITY, so it stays a v1.0 record (5.14.7). If this
; test shows the order wrong, the orchestrator moves the slot by a section 12
; row.

; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{wp} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %t/knr.ll \
; DEFINE:   -o %t/knr.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefix=WP --input-file=%t/knr.%{t}%{o}.il \
; DEFINE:     --implicit-check-not=error: --implicit-check-not="LLVM ERROR" && \
; DEFINE:   not grep -E "'vdef'\([^)]*(float64|int32, int32|int32\))" %t/knr.%{t}%{o}.il

; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O0
; RUN: %{wp}
; REDEFINE: %{o} = -O2
; RUN: %{wp}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/knr.lnx.dll %t/knr.x86_64-unknown-linux-gnu-msil-O2.il %}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %{wp}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/knr.win.dll %t/knr.x86_64-pc-windows-msvc-msil-O0.il %}
; REDEFINE: %{o} = -O2
; RUN: %{wp}

; The IR after the last M26 slot (P9): nothing variadic is left.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -stop-after=msil-lower-dynamic-alloca < %t/knr.ll -o - \
; RUN:   | FileCheck %s --check-prefix=IR
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O2 -stop-after=msil-lower-dynamic-alloca < %t/knr.ll -o - \
; RUN:   | FileCheck %s --check-prefix=IR

; Object mode.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form -verify-machineinstrs < %t/knr.ll \
; RUN:   -o %t/knr.obj.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=WP --input-file=%t/knr.obj.lnx.il
; RUN: not grep -E "'vdef'\([^)]*(float64|int32, int32|int32\))" %t/knr.obj.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -msil-object-form -verify-machineinstrs < %t/knr.ll \
; RUN:   -o %t/knr.obj.win.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=WP --input-file=%t/knr.obj.win.il
; RUN: not grep -E "'vdef'\([^)]*(float64|int32, int32|int32\))" %t/knr.obj.win.il
; RUN: %if llvm-cil-dump %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs \
; RUN:   < %t/knr.ll -o %t/knr.lnx.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/knr.lnx.o \
; RUN:   | FileCheck %s --check-prefixes=OBJ,OBJ-LNX --implicit-check-not=FIXED_ARITY %}
; RUN: %if llvm-cil-dump %{ llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -filetype=obj -verify-machineinstrs \
; RUN:   < %t/knr.ll -o %t/knr.win.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/knr.win.o \
; RUN:   | FileCheck %s --check-prefixes=OBJ,OBJ-WIN --implicit-check-not=FIXED_ARITY %}

; WP-DAG: {{^.method .*static int32 'vdef'\(int32, native int\) cil managed$}}
; WP-DAG: {{^  call int32 '(vdef_bitcast(\.[0-9]+)?'\(int32, int32, float64\)|vdef'\(int32, native int\))$}}

; IR-NOT:   {{^ *define .*\.\.\.\)}}
; IR-NOT:   {{call .*\(.*\.\.\.\)}}
; IR-LABEL: define i32 @vdef(i32 {{.*}}%n, ptr {{.*}}%varargs)
; IR-NOT:   {{^ *define .*\.\.\.\)}}
; IR-NOT:   {{call .*\(.*\.\.\.\)}}

; OBJ-LNX: LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil
; OBJ-WIN: LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil
; OBJ-DAG: Symbol {{[0-9]+}} FUNCTION GLOBAL DEFAULT 'vdef' method=0x06{{[0-9A-F]+}}
; OBJ-DAG: Symbol {{[0-9]+}} FUNCTION GLOBAL DEFAULT 'main' method=0x06{{[0-9A-F]+}}
; OBJ-DAG: Signature 0x06{{[0-9A-F]+}} 'vdef' VARARG fixed=1

;--- knr.ll
source_filename = "knr.c"

define i32 @main() {
entry:
  %r = call i32 @vdef(i32 noundef 1, i32 noundef 2, double noundef 3.000000e+00)
  ret i32 %r
}

define i32 @vdef(i32 noundef %n, ...) {
entry:
  %ap = alloca [24 x i8], align 8
  call void @llvm.va_start.p0(ptr %ap)
  call void @llvm.va_end.p0(ptr %ap)
  ret i32 %n
}

declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_end.p0(ptr)

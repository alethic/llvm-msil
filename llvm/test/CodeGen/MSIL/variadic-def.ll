; Variadic definitions (ImplementationPlan.md 5.15.2 'Expanded functions',
; M26; decision D19 as the section 12 row of 2026-10-28 amends it).
; ExpandVariadics (slot P7, Lowering mode, the MSIL ABIs of 5.13.5) replaces
; each variadic definition with 'define @f(<fixed>, ptr %varargs)', whose IL
; signature is (<fixed>, native int): the caller's va_list pointer comes last.
; va_start copies the caller's va_list into the function's own: on the SysV
; x86-64 triple a copy of the 24-byte __va_list_tag, on the AAPCS64 triple a
; copy of the 32-byte structure, and on the char* triples (windows-msvc) a
; store of the incoming pointer; va_copy copies the va_list type's size, and
; va_end is nothing. No call is left (no library call, no llvm.va_* call),
; nothing is reported, at -O0 and -O2, and every module assembles.
;
; defs.ll: a definition with one fixed parameter that uses va_start, va_copy
; and va_end (vsum), one with no fixed parameter (vnone), one with two fixed
; parameters of other types and no va_start (vdbl), and one with a hidden
; sret parameter (vsret), which counts as a fixed IL parameter.
; obj.ll: object mode (5.8.8, 5.14.7): each definition's SIGNATURES entry is
; VARARG with FixedParams = the IL parameters before the va_list (hidden sret
; included), no definition carries FIXED_ARITY (C8), and the record stays
; v1.0 because nothing in it uses FIXED_ARITY.

; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{p} =
; DEFINE: %{il} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %t/defs.ll \
; DEFINE:   -o %t/defs.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,%{p} --input-file=%t/defs.%{t}%{o}.il \
; DEFINE:     --implicit-check-not="{{^ +call}}" --implicit-check-not=pinvokeimpl

; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{p} = COPY
; REDEFINE: %{o} = -O0
; RUN: %{il}
; REDEFINE: %{o} = -O2
; RUN: %{il}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/defs.lnx.dll %t/defs.x86_64-unknown-linux-gnu-msil-O2.il %}
; REDEFINE: %{t} = aarch64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O0
; RUN: %{il}
; RUN: %if ilasm %{ %ilasm-verify -arm64 -output=%t/defs.a64.dll %t/defs.aarch64-unknown-linux-gnu-msil-O0.il %}
; REDEFINE: %{o} = -O2
; RUN: %{il}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{p} = STORE
; REDEFINE: %{o} = -O0
; RUN: %{il}
; RUN: %if ilasm %{ %ilasm-verify -output=%t/defs.win.dll %t/defs.x86_64-pc-windows-msvc-msil-O0.il %}
; REDEFINE: %{o} = -O2
; RUN: %{il}
; REDEFINE: %{t} = aarch64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O2
; RUN: %{il}

; The IR after the M26 slots: the expanded definitions and va_start's form.
; DEFINE: %{ir} = llc -mtriple=%{t} %{o} -stop-after=msil-lower-dynamic-alloca < %t/defs.ll -o - \
; DEFINE:   | FileCheck %s --check-prefixes=IR,%{p} --implicit-check-not="call void @llvm.va_"
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O0
; REDEFINE: %{p} = IR-SYSV
; RUN: %{ir}
; REDEFINE: %{t} = aarch64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; REDEFINE: %{p} = IR-AAPCS
; RUN: %{ir}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{p} = IR-WIN
; RUN: %{ir}
; REDEFINE: %{t} = aarch64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %{ir}

; RUN: %if llvm-cil-dump %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs \
; RUN:   < %t/obj.ll -o %t/obj.lnx.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/obj.lnx.o \
; RUN:   | FileCheck %s --check-prefixes=OBJ,OBJ-LNX --implicit-check-not=FIXED_ARITY %}
; RUN: %if llvm-cil-dump %{ llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -filetype=obj -verify-machineinstrs \
; RUN:   < %t/obj.ll -o %t/obj.win.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t/obj.win.o \
; RUN:   | FileCheck %s --check-prefixes=OBJ,OBJ-WIN --implicit-check-not=FIXED_ARITY %}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form -verify-machineinstrs < %t/obj.ll \
; RUN:   -o %t/obj.lnx.il 2>&1 | count 0
; RUN: FileCheck %s --check-prefix=OBJ-IL --input-file=%t/obj.lnx.il

;--- defs.ll
source_filename = "defs.c"

; The SysV and AAPCS64 va_start is a block copy from the incoming va_list
; (the cpblk's source is argument 1); the char* va_start stores argument 1.
; CHECK-LABEL: {{^}}.method public static int32 'vsum'(int32, native int) cil managed{{$}}
; COPY:        {{^}}  ldarg.1{{$}}
; COPY:        {{^}}  cpblk{{$}}
; STORE:       {{^}}  ldarg.1{{$}}
; STORE-NEXT:  {{^}}  stind.i{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'vsum'{{$}}
; IR-LABEL:    define i32 @vsum(i32 %n, ptr %varargs)
; IR-SYSV:     call void @llvm.memcpy.p0.p0.i{{(32|64)}}(ptr {{.*}}%ap, ptr {{.*}}%varargs, i{{(32|64)}} 24, i1 false)
; IR-AAPCS:    call void @llvm.memcpy.p0.p0.i{{(32|64)}}(ptr {{.*}}%ap, ptr {{.*}}%varargs, i{{(32|64)}} 32, i1 false)
; IR-WIN:      store ptr %varargs, ptr %ap
; IR:          ret i32 %n
define i32 @vsum(i32 %n, ...) {
entry:
  %ap = alloca [32 x i8], align 16
  %cp = alloca [32 x i8], align 16
  call void @llvm.va_start.p0(ptr %ap)
  call void @llvm.va_copy.p0(ptr %cp, ptr %ap)
  call void @llvm.va_end.p0(ptr %cp)
  call void @llvm.va_end.p0(ptr %ap)
  ret i32 %n
}

; No fixed parameter: the va_list pointer is argument 0.
; CHECK-LABEL: {{^}}.method public static void 'vnone'(native int) cil managed{{$}}
; COPY:        {{^}}  ldarg.0{{$}}
; COPY:        {{^}}  cpblk{{$}}
; STORE:       {{^}}  ldarg.0{{$}}
; STORE-NEXT:  {{^}}  stind.i{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'vnone'{{$}}
; IR-LABEL:    define void @vnone(ptr %varargs)
define void @vnone(...) {
entry:
  %ap = alloca [32 x i8], align 16
  call void @llvm.va_start.p0(ptr %ap)
  call void @llvm.va_end.p0(ptr %ap)
  ret void
}

; Two fixed parameters and no va_start: the va_list pointer is unused.
; CHECK-LABEL: {{^}}.method public static float64 'vdbl'(float64, native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'vdbl'{{$}}
; IR-LABEL:    define double @vdbl(double %x, ptr %p, ptr %varargs)
define double @vdbl(double %x, ptr %p, ...) {
  ret double %x
}

; A hidden sret parameter comes first, then the fixed parameter, then the
; va_list pointer.
; CHECK-LABEL: {{^}}.method public static void 'vsret'(native int, int32, native int) cil managed{{$}}
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'vsret'{{$}}
; IR-LABEL:    define void @vsret(ptr {{.*}}sret(%struct.big) {{.*}}%out, i32 %n, ptr %varargs)
%struct.big = type { i64, i64, i64 }
define void @vsret(ptr sret(%struct.big) align 8 %out, i32 %n, ...) {
  store i64 0, ptr %out, align 8
  ret void
}

declare void @llvm.va_start.p0(ptr)
declare void @llvm.va_copy.p0(ptr, ptr)
declare void @llvm.va_end.p0(ptr)

;--- obj.ll
source_filename = "vardef.c"

%struct.big = type { i64, i64, i64 }

define i32 @vdef(i32 %n, ...) {
  ret i32 %n
}

define void @vnone(...) {
  ret void
}

define double @vdbl(double %x, ptr %p, ...) {
  ret double %x
}

define void @vsret(ptr sret(%struct.big) align 8 %out, i32 %n, ...) {
  store i64 0, ptr %out, align 8
  ret void
}

; OBJ-LNX:     LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; OBJ-WIN:     LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; OBJ-NEXT:    Symbol 0 FUNCTION GLOBAL DEFAULT 'vdef' method=0x06000001
; OBJ-NEXT:    Symbol 1 FUNCTION GLOBAL DEFAULT 'vnone' method=0x06000002
; OBJ-NEXT:    Symbol 2 FUNCTION GLOBAL DEFAULT 'vdbl' method=0x06000003
; OBJ-NEXT:    Symbol 3 FUNCTION GLOBAL DEFAULT 'vsret' method=0x06000004
; OBJ-DAG:     Signature 0x06000001 'vdef' VARARG fixed=1
; OBJ-DAG:     Signature 0x06000002 'vnone' VARARG fixed=0
; OBJ-DAG:     Signature 0x06000003 'vdbl' VARARG fixed=2
; OBJ-DAG:     Signature 0x06000004 'vsret' {{(.* )?}}VARARG fixed=2{{( .*)?$}}

; The object-form text carries the same expanded signatures.
; OBJ-IL-DAG:  {{^}}.method public static int32 'vdef'(int32, native int) cil managed{{$}}
; OBJ-IL-DAG:  {{^}}.method public static void 'vnone'(native int) cil managed{{$}}
; OBJ-IL-DAG:  {{^}}.method public static float64 'vdbl'(float64, native int, native int) cil managed{{$}}
; OBJ-IL-DAG:  {{^}}.method public static void 'vsret'(native int, int32, native int) cil managed{{$}}

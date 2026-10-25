; The COFF symbol view of a CIL object (ImplementationPlan.md 5.8.6; M19;
; judge-dotnet-value J2): one 18-byte COFF symbol per SYMTAB entry, in SYMTAB
; order, no auxiliary records, so llvm-nm and llvm-ar read an object as they
; read a native one:
; - a defined function, aliases included: section 1 (.text), type 0x20 ->
;   'T' / 't' when LOCAL (storage class STATIC);
; - defined data in a READONLY segment: .rdata -> 'R' / 'r'; in a ZERO
;   segment: .bss -> 'B' / 'b' (zeroinitializer, and also initializers that
;   mix zeros with undef or poison, zero_undef and zero_undef_local, which
;   are zero-initialized for getKindForGlobal and native objects alike,
;   5.8.8); any other: .data -> 'D' / 'd';
; - COMMON: section 0, Value = its size -> 'C';
; - UNDEFINED, weak or not: section 0 -> 'U'.
; Weak definitions and weak undefined symbols are plain EXTERNAL records,
; never IMAGE_SYM_CLASS_WEAK_EXTERNAL, so they print 'T'/'D' and 'U' (not
; 'W'/'w'), and an 'llvm-ar rcs' archive indexes exactly the defined
; non-local symbols and the common, never a local or an undefined one
; (weak undefined symbols must not extract members). llvm-nm -p prints the
; symbols in symbol-table order, which is SYMTAB order (variables, functions,
; aliases).
; dalias.ll: a data alias takes its aliasee's section (5.8.6, M17's
; buildCOFFSymbolView) and its own binding: 'R' for an alias into a
; READONLY aliasee (ro_a), 'B' / 'b' for one into a ZERO aliasee (z_ga, and
; the LOCAL z_a), 'D' for one into another data segment (the weak d_a: a
; plain record, not 'W' or 'w'). The archive index lists the non-local data
; aliases with the other defined symbols and leaves out the LOCAL one.
; Both x86_64 triples.

; REQUIRES: llvm-cil-dump
; RUN: rm -rf %t && split-file %s %t
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/coff.ll -o %t/lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/coff.ll -o %t/coff.o 2>&1 | count 0
; RUN: llvm-nm -p %t/lnx.o | FileCheck %s --check-prefix=NM
; RUN: llvm-nm -p %t/coff.o | FileCheck %s --check-prefix=NM
; RUN: llvm-cil-dump --headers %t/coff.o | FileCheck %s --check-prefix=HDR
; RUN: llvm-ar rcs %t/lib.a %t/coff.o
; RUN: llvm-nm --print-armap %t/lib.a | FileCheck %s --check-prefix=ARMAP \
; RUN:   --implicit-check-not="ro_local in" --implicit-check-not="zero_local in" \
; RUN:   --implicit-check-not="zero_undef_local in" \
; RUN:   --implicit-check-not="data_local in" --implicit-check-not="func_local in" \
; RUN:   --implicit-check-not="local_alias in" --implicit-check-not="ext_data in" \
; RUN:   --implicit-check-not="ext_func in"

; NM:      {{^[0-9a-f]+}} R ro{{$}}
; NM-NEXT: {{^[0-9a-f]+}} r ro_local{{$}}
; NM-NEXT: {{^[0-9a-f]+}} B zero{{$}}
; NM-NEXT: {{^[0-9a-f]+}} b zero_local{{$}}
; NM-NEXT: {{^[0-9a-f]+}} B zero_undef{{$}}
; NM-NEXT: {{^[0-9a-f]+}} b zero_undef_local{{$}}
; NM-NEXT: {{^[0-9a-f]+}} D data{{$}}
; NM-NEXT: {{^[0-9a-f]+}} d data_local{{$}}
; NM-NEXT: {{^[0-9a-f]+}} D weak_data{{$}}
; NM-NEXT: {{^0*}}8 C common{{$}}
; NM-NEXT: {{^ +}}U ext_data{{$}}
; NM-NEXT: {{^ +}}U weak_ext_data{{$}}
; NM-NEXT: {{^[0-9a-f]+}} T func{{$}}
; NM-NEXT: {{^[0-9a-f]+}} t func_local{{$}}
; NM-NEXT: {{^[0-9a-f]+}} T weak_func{{$}}
; NM-NEXT: {{^ +}}U ext_func{{$}}
; NM-NEXT: {{^ +}}U weak_ext_func{{$}}
; NM-NEXT: {{^[0-9a-f]+}} T use{{$}}
; NM-NEXT: {{^[0-9a-f]+}} T func_alias{{$}}
; NM-NEXT: {{^[0-9a-f]+}} t local_alias{{$}}
; NM-NOT:  {{.}}

; HDR: COFF: sections=4 timestamp=0x{{[0-9A-F]+}} characteristics=0x2022 symbols=20

; ARMAP:     Archive map
; ARMAP-DAG: {{^}}common in coff.o{{$}}
; ARMAP-DAG: {{^}}data in coff.o{{$}}
; ARMAP-DAG: {{^}}func in coff.o{{$}}
; ARMAP-DAG: {{^}}func_alias in coff.o{{$}}
; ARMAP-DAG: {{^}}ro in coff.o{{$}}
; ARMAP-DAG: {{^}}use in coff.o{{$}}
; ARMAP-DAG: {{^}}weak_data in coff.o{{$}}
; ARMAP-DAG: {{^}}weak_func in coff.o{{$}}
; ARMAP-DAG: {{^}}zero in coff.o{{$}}
; ARMAP-DAG: {{^}}zero_undef in coff.o{{$}}
; ARMAP:     {{^}}coff.o:{{$}}

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/dalias.ll \
; RUN:   -o %t/dalias.lnx.o 2>&1 | count 0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/dalias.ll \
; RUN:   -o %t/dalias.o 2>&1 | count 0
; RUN: llvm-nm -p %t/dalias.lnx.o | FileCheck %s --check-prefix=DNM
; RUN: llvm-nm -p %t/dalias.o | FileCheck %s --check-prefix=DNM
; RUN: llvm-ar rcs %t/dalias.a %t/dalias.o
; RUN: llvm-nm --print-armap %t/dalias.a | FileCheck %s --check-prefix=DARMAP --implicit-check-not="z_a in"

; DNM:      {{^[0-9a-f]+}} R ro{{$}}
; DNM-NEXT: {{^[0-9a-f]+}} B z{{$}}
; DNM-NEXT: {{^[0-9a-f]+}} D d{{$}}
; DNM-NEXT: {{^[0-9a-f]+}} T use{{$}}
; DNM-NEXT: {{^[0-9a-f]+}} R ro_a{{$}}
; DNM-NEXT: {{^[0-9a-f]+}} B z_ga{{$}}
; DNM-NEXT: {{^[0-9a-f]+}} b z_a{{$}}
; DNM-NEXT: {{^[0-9a-f]+}} D d_a{{$}}
; DNM-NOT:  {{.}}

; DARMAP:     Archive map
; DARMAP-DAG: {{^}}d in dalias.o{{$}}
; DARMAP-DAG: {{^}}d_a in dalias.o{{$}}
; DARMAP-DAG: {{^}}ro in dalias.o{{$}}
; DARMAP-DAG: {{^}}ro_a in dalias.o{{$}}
; DARMAP-DAG: {{^}}use in dalias.o{{$}}
; DARMAP-DAG: {{^}}z in dalias.o{{$}}
; DARMAP-DAG: {{^}}z_ga in dalias.o{{$}}
; DARMAP:     {{^}}dalias.o:{{$}}

;--- coff.ll
source_filename = "coff.c"

@ro = constant i32 1, align 4
@ro_local = internal constant i32 2, align 4
@zero = global i32 0, align 4
@zero_local = internal global i32 0, align 4
@zero_undef = global { i8, [3 x i8] } { i8 0, [3 x i8] undef }, align 4
@zero_undef_local = internal global [2 x i32] [i32 0, i32 poison], align 4
@data = global i32 3, align 4
@data_local = internal global i32 4, align 4
@weak_data = weak global i32 5, align 4
@common = common global i64 0, align 8
@ext_data = external global i32, align 4
@weak_ext_data = extern_weak global i32, align 4

@func_alias = alias i32 (), ptr @func
@local_alias = internal alias i32 (), ptr @func_local

define i32 @func() {
  ret i32 1
}

define internal i32 @func_local() {
  ret i32 2
}

define weak i32 @weak_func() {
  ret i32 3
}

declare i32 @ext_func(i32)

declare extern_weak i32 @weak_ext_func(i32)

; Stores every local and undefined variable's address and calls every local
; and undefined function, so that none is unreferenced.
define i32 @use(ptr %out) {
  store ptr @ro_local, ptr %out, align 8
  %p1 = getelementptr inbounds ptr, ptr %out, i64 1
  store ptr @zero_local, ptr %p1, align 8
  %p2 = getelementptr inbounds ptr, ptr %out, i64 2
  store ptr @data_local, ptr %p2, align 8
  %p3 = getelementptr inbounds ptr, ptr %out, i64 3
  store ptr @ext_data, ptr %p3, align 8
  %p4 = getelementptr inbounds ptr, ptr %out, i64 4
  store ptr @weak_ext_data, ptr %p4, align 8
  %p5 = getelementptr inbounds ptr, ptr %out, i64 5
  store ptr @zero_undef_local, ptr %p5, align 8
  %a = call i32 @func_local()
  %b = call i32 @local_alias()
  %c = call i32 @ext_func(i32 %a)
  %d = call i32 @weak_ext_func(i32 %b)
  %r = add i32 %c, %d
  ret i32 %r
}

;--- dalias.ll
source_filename = "dalias.c"

@ro = constant [2 x i32] [i32 1, i32 2], align 4
@z = global [2 x i32] zeroinitializer, align 4
@d = global [2 x i32] [i32 3, i32 4], align 4

@ro_a = alias i32, getelementptr (i8, ptr @ro, i64 4)
@z_ga = alias i32, ptr @z
@z_a = internal alias i32, getelementptr (i8, ptr @z, i64 4)
@d_a = weak alias i32, ptr @d

define i32 @use() {
  %a = load i32, ptr @ro_a, align 4
  %b = load i32, ptr @z_a, align 4
  %c = load i32, ptr @d_a, align 4
  %ab = add i32 %a, %b
  %r = add i32 %ab, %c
  ret i32 %r
}

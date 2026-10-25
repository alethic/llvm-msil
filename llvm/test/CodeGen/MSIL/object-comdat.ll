; COMDAT groups in a CIL object (ImplementationPlan.md 5.8.8 COMDATS and
; INIT_FINI, 5.8.10; M19; judge-c-fidelity J7): every IR comdat with
; selection Any or NoDeduplicate becomes a group with its name, its
; selection (ANY / NO_DEDUPLICATE) and its members, FUNCTION_SYMBOL (the
; function's symbol index) for a function and SEGMENT (the variable's
; segment index) for a variable. The members keep their own bindings: a
; linkonce_odr member is WEAK, an external one GLOBAL (lld/ELF's
; ungrouped-section rule for NoDeduplicate is msil-ld's business, M20).
; groups.ll has the MSVC string literal that C produces (a linkonce_odr
; unnamed_addr constant in its own Any comdat, so WEAK, STRINGS|READONLY), a
; linkonce_odr function in its own comdat, and a NoDeduplicate group with a
; variable and a function. The order of the groups and of the members in a
; group is not fixed by 5.8.8, so those lines are matched in any order and
; counted (NGRP), and nothing may stand between the last record line before
; them and the first of them (GRP-NOT). use's call of the COMDAT member
; any_fn names any_fn's own MethodDef (R1: a copy that msil-ld discards is
; redirected to the winner), never a '<msil>Symbols' placeholder: the
; canonical listing names '<Module>'::'any_fn' and the only MemberRef is the
; carrier's .ctor (R1-GRP).
; init.ll: an llvm.global_ctors entry whose associated global is in a comdat
; records 1 + that group's index (printed as comdat=<index>), so the entry is
; dropped with the group; an entry without one records 0 (no comdat=).
; Comdats with other selections are errors (object-errors.ll). Both x86_64
; triples at -O2, windows-msvc also at -O0.

; REQUIRES: llvm-cil-dump
; RUN: rm -rf %t && split-file %s %t
; DEFINE: %{rec} = FileCheck %s --match-full-lines --strict-whitespace
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/groups.ll \
; RUN:   -o %t/groups.lnx.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/groups.lnx.o | %{rec} --check-prefixes=GRP,GRP-LNX
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/groups.ll \
; RUN:   -o %t/groups.win.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/groups.win.o | %{rec} --check-prefixes=GRP,GRP-WIN
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -O0 -verify-machineinstrs < %t/groups.ll \
; RUN:   -o %t/groups.win.o0.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/groups.win.o0.o | %{rec} --check-prefixes=GRP,GRP-WIN
; RUN: llvm-cil-dump --link-info %t/groups.lnx.o | %{rec} --check-prefix=NGRP
; RUN: llvm-cil-dump --link-info %t/groups.win.o | %{rec} --check-prefix=NGRP
; RUN: llvm-cil-dump --link-info %t/groups.win.o0.o | %{rec} --check-prefix=NGRP
; RUN: llvm-cil-dump --canonical %t/groups.lnx.o | %{rec} --check-prefix=R1-GRP
; RUN: llvm-cil-dump --canonical %t/groups.win.o0.o | %{rec} --check-prefix=R1-GRP
; RUN: llvm-cil-dump --tables %t/groups.lnx.o | %{rec} --check-prefix=R1-GRP-TBL
; RUN: llvm-cil-dump --tables %t/groups.win.o0.o | %{rec} --check-prefix=R1-GRP-TBL

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs < %t/init.ll \
; RUN:   -o %t/init.lnx.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/init.lnx.o | %{rec} --check-prefixes=INIT,INIT-LNX
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs < %t/init.ll \
; RUN:   -o %t/init.win.o 2>&1 | count 0
; RUN: llvm-cil-dump --link-info %t/init.win.o | %{rec} --check-prefixes=INIT,INIT-WIN

; The object oracle (5.12.6, as in object-form-text.ll): ilasm's object of
; the object-form text is canonically equal to llc's object, record and
; tokens included.
; DEFINE: %{canon} = llvm-cil-dump --canonical --params=drop --initlocals=ignore-without-locals --snan=quiet
; DEFINE: %{o} =
; DEFINE: %{oracle} = %ilasm-verify -output=%{o}.ilasm.o %{o}.il && \
; DEFINE:   %{canon} %{o}.o > %{o}.canon1 && %{canon} %{o}.ilasm.o > %{o}.canon2 && diff %{o}.canon1 %{o}.canon2 && \
; DEFINE:   llvm-cil-dump --link-info %{o}.o > %{o}.link1 && llvm-cil-dump --link-info %{o}.ilasm.o > %{o}.link2 && \
; DEFINE:   diff %{o}.link1 %{o}.link2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -msil-object-form -verify-machineinstrs < %t/groups.ll -o %t/groups.win.il 2>&1 | count 0
; REDEFINE: %{o} = %t/groups.win
; RUN: %if ilasm %{ %{oracle} %}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -msil-object-form -verify-machineinstrs < %t/init.ll -o %t/init.lnx.il 2>&1 | count 0
; REDEFINE: %{o} = %t/init.lnx
; RUN: %if ilasm %{ %{oracle} %}

; GRP-LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; GRP-WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; GRP-NEXT:Symbol 0 DATA WEAK DEFAULT '??_C@_05CJBACGMB@hello?$AA@' field=0x04000001 segment=0 offset=0 size=6
; GRP-NEXT:Symbol 1 DATA GLOBAL DEFAULT 'nd_data' field=0x04000002 segment=1 offset=0 size=4
; GRP-NEXT:Symbol 2 DATA GLOBAL DEFAULT 'msg' field=0x04000003 segment=2 offset=0 size=8
; GRP-NEXT:Symbol 3 FUNCTION GLOBAL DEFAULT 'nd_fn' method=0x06000001
; GRP-NEXT:Symbol 4 FUNCTION WEAK DEFAULT 'any_fn' method=0x06000002
; GRP-NEXT:Symbol 5 FUNCTION GLOBAL DEFAULT 'use' method=0x06000003
; GRP-NEXT:Segment 0 '.rodata' align=1 size=6 flags=STRINGS|READONLY
; GRP-NEXT:  68 65 6C 6C 6F 00
; GRP-NEXT:Segment 1 '.data' align=4 size=4 flags=0
; GRP-NEXT:  02 00 00 00
; GRP-NEXT:Segment 2 '.data' align=8 size=8 flags=0
; GRP-NEXT:  00 00 00 00 00 00 00 00
; GRP-NEXT:Reloc ADDR64 segment=2 offset=0 symbol=0 '??_C@_05CJBACGMB@hello?$AA@' addend=0
; GRP-NOT:{{.}}
; GRP-DAG:Comdat '??_C@_05CJBACGMB@hello?$AA@' ANY segment=0
; GRP-DAG:Comdat 'nd' NO_DEDUPLICATE {{(segment=1 function=3|function=3 segment=1)}}
; GRP-DAG:Comdat 'any_fn' ANY function=4
; GRP-NOT:{{.}}
; NGRP-COUNT-3:{{^Comdat .*}}
; NGRP-NOT:{{^Comdat }}

; R1-GRP:  .method 'use' int32() flags=0x0016 impl=0x0000
; R1-GRP-NOT:  .method
; R1-GRP:    L{{[0-9]+}}: call int32() '<Module>'::'any_fn'
; R1-GRP-TBL:Table: TypeRef rows=1 rowsize={{[0-9]+}}
; R1-GRP-TBL-NEXT:  TypeRef[1]: ResolutionScope=0x23000003 TypeName='<msil>LinkInfo' TypeNamespace=''
; R1-GRP-TBL:Table: MemberRef rows=1 rowsize={{[0-9]+}}
; R1-GRP-TBL-NEXT:  MemberRef[1]: Class=0x01000001 Name='.ctor' Signature=20 01 01 1D 05
; R1-GRP-TBL-NEXT:Table: {{.*}}

; INIT-LNX:LinkInfo v1.0 triple=x86_64-unknown-linux-gnu-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,8,4,8
; INIT-WIN:LinkInfo v1.0 triple=x86_64-pc-windows-msvc-msil abi=1 abiflags=0x0 features=0x0 datamodel=8,4,2,8
; INIT-NEXT:Symbol 0 DATA WEAK DEFAULT 'grp_data' field=0x04000001 segment=0 offset=0 size=4
; INIT-NEXT:Symbol 1 DATA GLOBAL DEFAULT 'plain' field=0x04000002 segment=1 offset=0 size=4
; INIT-NEXT:Symbol 2 FUNCTION LOCAL DEFAULT 'grp_init' method=0x06000001
; INIT-NEXT:Symbol 3 FUNCTION LOCAL DEFAULT 'plain_init' method=0x06000002
; INIT-NEXT:Segment 0 '.data' align=4 size=4 flags=0
; INIT-NEXT:  03 00 00 00
; INIT-NEXT:Segment 1 '.bss' align=4 size=4 flags=ZERO
; INIT-NEXT:Comdat 'grp' ANY {{(segment=0 function=2|function=2 segment=0)}}
; INIT-NEXT:Init priority=200 symbol=2 'grp_init' comdat=0
; INIT-NEXT:Init priority=65535 symbol=3 'plain_init'
; INIT-NOT:{{.}}

;--- groups.ll
source_filename = "comdat.c"

$"??_C@_05CJBACGMB@hello?$AA@" = comdat any
$nd = comdat nodeduplicate
$any_fn = comdat any

@"??_C@_05CJBACGMB@hello?$AA@" = linkonce_odr unnamed_addr constant [6 x i8] c"hello\00", comdat, align 1
@nd_data = global i32 2, comdat($nd), align 4
@msg = global ptr @"??_C@_05CJBACGMB@hello?$AA@", align 8

define i32 @nd_fn() comdat($nd) {
  ret i32 2
}

define linkonce_odr i32 @any_fn() comdat {
  ret i32 1
}

define i32 @use() {
  %r = call i32 @any_fn()
  ret i32 %r
}

;--- init.ll
source_filename = "comdat-init.c"

$grp = comdat any

@grp_data = linkonce_odr global i32 3, comdat($grp), align 4
@plain = global i32 0, align 4
@llvm.global_ctors = appending global [2 x { i32, ptr, ptr }] [{ i32, ptr, ptr } { i32 200, ptr @grp_init, ptr @grp_data }, { i32, ptr, ptr } { i32 65535, ptr @plain_init, ptr null }]

define internal void @grp_init() comdat($grp) {
  store i32 4, ptr @grp_data, align 4
  ret void
}

define internal void @plain_init() {
  store i32 1, ptr @plain, align 4
  ret void
}

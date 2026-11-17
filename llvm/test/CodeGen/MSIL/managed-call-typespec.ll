; The parent of a managed call's static member (ImplementationPlan.md 5.20.2
; MA1 and 5.20.5 ML3 as amended by the MC-1 fix loop, contract MK3;
; milestone MC-1). In an object and in object-form text (OF), the parent of
; a static method or static field whose TYPE is written 'valuetype T' is the
; TypeSpec 'valuetype [msil.bcl]T', so that msil-ld sees how the declaration
; named the type and reports a class that is named 'valuetype' (T-M16):
; m_now (DateTime::get_Now), m_dtmax (the field DateTime::MaxValue),
; m_fromticks (TimeSpan::FromTicks(int64)) and m_tszero (the field
; TimeSpan::Zero). These keep the TypeRef parent '[msil.bcl]T':
; - a value type that a signature spells in one byte (m_i32max, the field
;   Int32::MaxValue of 'valuetype System.Int32') and GCHandle (m_toptr,
;   GCHandle::ToIntPtr): their TypeSpec would be the long form, which
;   PEVerify /md refuses;
; - instance members and constructors, whose 'this' or result already shows
;   the kind (m_ticks, DateTime::get_Ticks on a 'this-val' handle, with the
;   'constrained.' placeholder; m_mkdate, DateTime::.ctor(int64));
; - a value type named without 'valuetype' (m_now_ref, m_now's mirror:
;   'System.DateTime' as TYPE stays accepted, with no new check).
; Whole-program text (WP, llc -S) keeps the plain '[mscorlib]T::M' for all of
; them, as images and --lto-emit-asm text do (msil-ld's tests), and it
; assembles; there, m_ticks's 'unbox' of the 'this' handle and m_mkdate's
; 'box' of its constructed object name the TypeSpec 'valuetype
; [mscorlib]System.DateTime' (the run-time kind checks of text, which no
; link checks; managed-call-bodies.ll and managed-call-text-checks.ll),
; while object-form text and the object keep the TypeRef. In the object
; (llvm-cil-dump's canonical listing, DUMP, and its tables, TABLES), m_now's
; and m_now_ref's references to DateTime::get_Now are two MemberRef rows
; with one signature, one on the TypeSpec (token 0x1B) and one on the
; TypeRef (0x01): the parent is part of a reference's identity (R6). The
; TypeSpec table has exactly the rows of DateTime and TimeSpan. The object
; oracle (5.12.6): llc's object and the object that ilasm makes from the
; object-form text have the same canonical listing and link info.
; Whole-program and object-form text on both host x86_64 triples and
; msil64-unknown-dotnet at -O0 and -O2; objects at one level each.
; Not pinned: .maxstack values, label names and the null test of the handle.

; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{il} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %s \
; DEFINE:   -o %t.wp.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,WP -DCORE=mscorlib --input-file=%t.wp.%{t}%{o}.il \
; DEFINE:     --implicit-check-not=msil.bcl --implicit-check-not="valuetype [mscorlib]System.DateTime::" \
; DEFINE:     --implicit-check-not="valuetype [mscorlib]System.TimeSpan::" && \
; DEFINE:   llc -mtriple=%{t} %{o} -msil-object-form -asm-verbose=false -verify-machineinstrs < %s \
; DEFINE:   -o %t.of.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s --check-prefixes=CHECK,OF -DCORE=msil.bcl --input-file=%t.of.%{t}%{o}.il \
; DEFINE:     --implicit-check-not=mscorlib

; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %{il}
; REDEFINE: %{o} = -O0
; RUN: %{il}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; RUN: %{il}
; REDEFINE: %{o} = -O2
; RUN: %{il}
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %{il}
; REDEFINE: %{o} = -O0
; RUN: %{il}

; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.lnx.dll %t.wp.x86_64-unknown-linux-gnu-msil-O2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.win.dll %t.wp.x86_64-pc-windows-msvc-msil-O0.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.p64.dll %t.wp.msil64-unknown-dotnet-O2.il %}

; DEFINE: %{canon} = llvm-cil-dump --canonical --params=drop --initlocals=ignore-without-locals --snan=quiet
; DEFINE: %{obj} = llc -mtriple=%{t} %{o} -filetype=obj -verify-machineinstrs < %s \
; DEFINE:   -o %t.%{t}%{o}.o 2>&1 | count 0 && \
; DEFINE:   llvm-cil-dump --canonical --order=decl --params=drop %t.%{t}%{o}.o \
; DEFINE:   | FileCheck %s --check-prefix=DUMP --implicit-check-not=mscorlib && \
; DEFINE:   llvm-cil-dump --tables %t.%{t}%{o}.o > %t.%{t}%{o}.tables && \
; DEFINE:   FileCheck %s --check-prefix=TABLES --input-file=%t.%{t}%{o}.tables && \
; DEFINE:   FileCheck %s --check-prefix=NGETNOW --input-file=%t.%{t}%{o}.tables
; DEFINE: %{oracle} = %ilasm-verify -output=%t.of.%{t}%{o}.ilasm.o %t.of.%{t}%{o}.il && \
; DEFINE:   %{canon} %t.%{t}%{o}.o | sed -e "/^pe /d" > %t.%{t}%{o}.canon1 && \
; DEFINE:   %{canon} %t.of.%{t}%{o}.ilasm.o | sed -e "/^pe /d" > %t.%{t}%{o}.canon2 && \
; DEFINE:   diff %t.%{t}%{o}.canon1 %t.%{t}%{o}.canon2 && \
; DEFINE:   llvm-cil-dump --link-info %t.%{t}%{o}.o > %t.%{t}%{o}.link1 && \
; DEFINE:   llvm-cil-dump --link-info %t.of.%{t}%{o}.ilasm.o > %t.%{t}%{o}.link2 && \
; DEFINE:   diff %t.%{t}%{o}.link1 %t.%{t}%{o}.link2
; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; REDEFINE: %{o} = -O0
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}
; REDEFINE: %{t} = msil64-unknown-dotnet
; REDEFINE: %{o} = -O2
; RUN: %if llvm-cil-dump %{ %{obj} %}
; RUN: %if ilasm && llvm-cil-dump %{ %{oracle} %}

source_filename = "managed-call-typespec.c"

; Static members of a TYPE written 'valuetype T': the TypeSpec parent in
; object form, the TypeRef in whole-program text.
; CHECK-LABEL: .method assembly static native int '__msil_mc.m_now'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; WP-NEXT:     {{^}}  call valuetype {{\[}}mscorlib]System.DateTime {{\[}}mscorlib]System.DateTime::get_Now(){{$}}
; OF-NEXT:     {{^}}  call valuetype {{\[}}msil.bcl]System.DateTime valuetype {{\[}}msil.bcl]System.DateTime::get_Now(){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.DateTime{{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_now'{{$}}
; DUMP-LABEL:  {{^}}  .method '__msil_mc.m_now' native int() flags=
; DUMP-NEXT:   {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:   {{^}}    L0: call valuetype [msil.bcl]System.DateTime() valuetype [msil.bcl]System.DateTime::'get_Now'{{$}}
; DUMP-NEXT:   {{^}}    L1: box [msil.bcl]System.DateTime{{$}}
declare ptr @__msil_mc.m_now() #0

; CHECK-LABEL: .method assembly static native int '__msil_mc.m_dtmax'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; WP-NEXT:     {{^}}  ldsfld valuetype {{\[}}mscorlib]System.DateTime {{\[}}mscorlib]System.DateTime::MaxValue{{$}}
; OF-NEXT:     {{^}}  ldsfld valuetype {{\[}}msil.bcl]System.DateTime valuetype {{\[}}msil.bcl]System.DateTime::MaxValue{{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.DateTime{{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_dtmax'{{$}}
; DUMP-LABEL:  {{^}}  .method '__msil_mc.m_dtmax' native int() flags=
; DUMP-NEXT:   {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:   {{^}}    L0: ldsfld valuetype [msil.bcl]System.DateTime valuetype [msil.bcl]System.DateTime::'MaxValue'{{$}}
declare ptr @__msil_mc.m_dtmax() #1

; CHECK-LABEL: .method assembly static native int '__msil_mc.m_fromticks'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i8{{$}}
; WP-NEXT:     {{^}}  call valuetype {{\[}}mscorlib]System.TimeSpan {{\[}}mscorlib]System.TimeSpan::FromTicks(int64){{$}}
; OF-NEXT:     {{^}}  call valuetype {{\[}}msil.bcl]System.TimeSpan valuetype {{\[}}msil.bcl]System.TimeSpan::FromTicks(int64){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.TimeSpan{{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_fromticks'{{$}}
; DUMP-LABEL:  {{^}}  .method '__msil_mc.m_fromticks' native int(native int) flags=
; DUMP:        {{^}}    L{{[0-9]+}}: call valuetype [msil.bcl]System.TimeSpan(int64) valuetype [msil.bcl]System.TimeSpan::'FromTicks'{{$}}
declare ptr @__msil_mc.m_fromticks(i64 noundef) #2

; CHECK-LABEL: .method assembly static native int '__msil_mc.m_tszero'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; WP-NEXT:     {{^}}  ldsfld valuetype {{\[}}mscorlib]System.TimeSpan {{\[}}mscorlib]System.TimeSpan::Zero{{$}}
; OF-NEXT:     {{^}}  ldsfld valuetype {{\[}}msil.bcl]System.TimeSpan valuetype {{\[}}msil.bcl]System.TimeSpan::Zero{{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.TimeSpan{{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_tszero'{{$}}
; DUMP-LABEL:  {{^}}  .method '__msil_mc.m_tszero' native int() flags=
; DUMP-NEXT:   {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:   {{^}}    L0: ldsfld valuetype [msil.bcl]System.TimeSpan valuetype [msil.bcl]System.TimeSpan::'Zero'{{$}}
declare ptr @__msil_mc.m_tszero() #3

; A one-byte value type and GCHandle keep the TypeRef.
; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_i32max'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldsfld int32 {{\[}}[[CORE]]]System.Int32::MaxValue{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_i32max'{{$}}
; DUMP-LABEL:  {{^}}  .method '__msil_mc.m_i32max' int32() flags=
; DUMP-NEXT:   {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:   {{^}}    L0: ldsfld int32 [msil.bcl]System.Int32::'MaxValue'{{$}}
declare i32 @__msil_mc.m_i32max() #4

; CHECK-LABEL: .method assembly static native int '__msil_mc.m_toptr'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::FromIntPtr(native int){{$}}
; CHECK-NEXT:  {{^}}  call native int {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle::ToIntPtr(valuetype {{\[}}[[CORE]]]System.Runtime.InteropServices.GCHandle){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_toptr'{{$}}
; DUMP-LABEL:  {{^}}  .method '__msil_mc.m_toptr' native int(native int) flags=
; DUMP:        {{^}}    L{{[0-9]+}}: call native int(valuetype [msil.bcl]System.Runtime.InteropServices.GCHandle) [msil.bcl]System.Runtime.InteropServices.GCHandle::'ToIntPtr'{{$}}
declare i64 @__msil_mc.m_toptr(i64 noundef) #5

; An instance member and a constructor of 'valuetype System.DateTime' keep
; the TypeRef.
; CHECK-LABEL: .method assembly static native int '__msil_mc.m_ticks'(native int) cil managed
; WP:          {{^}}  unbox valuetype {{\[}}[[CORE]]]System.DateTime{{$}}
; OF:          {{^}}  unbox {{\[}}[[CORE]]]System.DateTime{{$}}
; WP-NEXT:     {{^}}  call instance int64 {{\[}}mscorlib]System.DateTime::get_Ticks(){{$}}
; OF-NEXT:     {{^}}  constrained. {{\[}}msil.bcl]System.DateTime{{$}}
; OF-NEXT:     {{^}}  callvirt instance int64 {{\[}}msil.bcl]System.DateTime::get_Ticks(){{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_mc.m_ticks'{{$}}
; DUMP-LABEL:  {{^}}  .method '__msil_mc.m_ticks' native int(native int) flags=
; DUMP:        {{^}}    L{{[0-9]+}}: unbox [msil.bcl]System.DateTime{{$}}
; DUMP-NEXT:   {{^}}    L{{[0-9]+}}: constrained. [msil.bcl]System.DateTime{{$}}
; DUMP-NEXT:   {{^}}    L{{[0-9]+}}: callvirt instance int64() [msil.bcl]System.DateTime::'get_Ticks'{{$}}
declare i64 @__msil_mc.m_ticks(ptr noundef) #6

; CHECK-LABEL: .method assembly static native int '__msil_mc.m_mkdate'(native int) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  conv.i8{{$}}
; CHECK-NEXT:  {{^}}  newobj instance void {{\[}}[[CORE]]]System.DateTime::.ctor(int64){{$}}
; WP-NEXT:     {{^}}  box valuetype {{\[}}mscorlib]System.DateTime{{$}}
; OF-NEXT:     {{^}}  box {{\[}}msil.bcl]System.DateTime{{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_mkdate'{{$}}
; DUMP-LABEL:  {{^}}  .method '__msil_mc.m_mkdate' native int(native int) flags=
; DUMP:        {{^}}    L{{[0-9]+}}: newobj instance void(int64) [msil.bcl]System.DateTime::'.ctor'{{$}}
declare ptr @__msil_mc.m_mkdate(i64 noundef) #7

; The mirror case: 'System.DateTime' without 'valuetype' keeps the TypeRef.
; CHECK-LABEL: .method assembly static native int '__msil_mc.m_now_ref'() cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  call valuetype {{\[}}[[CORE]]]System.DateTime {{\[}}[[CORE]]]System.DateTime::get_Now(){{$}}
; CHECK-NEXT:  {{^}}  box {{\[}}[[CORE]]]System.DateTime{{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_now_ref'{{$}}
; DUMP-LABEL:  {{^}}  .method '__msil_mc.m_now_ref' native int() flags=
; DUMP-NEXT:   {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:   {{^}}    L0: call valuetype [msil.bcl]System.DateTime() [msil.bcl]System.DateTime::'get_Now'{{$}}
declare ptr @__msil_mc.m_now_ref() #8

; Two MemberRef rows for DateTime::get_Now, one per parent kind, with one
; signature; the TypeSpecs of DateTime and TimeSpan only.
; TABLES:      {{^}}Table: MemberRef rows={{[0-9]+}} rowsize={{[0-9]+}}{{$}}
; TABLES-DAG:  {{^}}  MemberRef[{{[0-9]+}}]: Class=0x1B{{[0-9A-F]+}} Name='get_Now' Signature=[[SIG:[0-9A-F ]+]]{{$}}
; TABLES-DAG:  {{^}}  MemberRef[{{[0-9]+}}]: Class=0x01{{[0-9A-F]+}} Name='get_Now' Signature=[[SIG]]{{$}}
; TABLES-DAG:  {{^}}  MemberRef[{{[0-9]+}}]: Class=0x1B{{[0-9A-F]+}} Name='MaxValue' Signature={{[0-9A-F ]+}}{{$}}
; TABLES-DAG:  {{^}}  MemberRef[{{[0-9]+}}]: Class=0x01{{[0-9A-F]+}} Name='MaxValue' Signature=06 08{{$}}
; TABLES-DAG:  {{^}}  MemberRef[{{[0-9]+}}]: Class=0x1B{{[0-9A-F]+}} Name='FromTicks' Signature={{[0-9A-F ]+}}{{$}}
; TABLES-DAG:  {{^}}  MemberRef[{{[0-9]+}}]: Class=0x1B{{[0-9A-F]+}} Name='Zero' Signature={{[0-9A-F ]+}}{{$}}
; TABLES-DAG:  {{^}}  MemberRef[{{[0-9]+}}]: Class=0x01{{[0-9A-F]+}} Name='ToIntPtr' Signature={{[0-9A-F ]+}}{{$}}
; TABLES-DAG:  {{^}}  MemberRef[{{[0-9]+}}]: Class=0x01{{[0-9A-F]+}} Name='get_Ticks' Signature={{[0-9A-F ]+}}{{$}}
; TABLES-DAG:  {{^}}  MemberRef[{{[0-9]+}}]: Class=0x01{{[0-9A-F]+}} Name='.ctor' Signature=20 01 01 0A{{$}}
; TABLES:      {{^}}Table: TypeSpec rows=2 rowsize={{[0-9]+}}{{$}}
; Exactly two rows name get_Now. FileCheck counts them, not 'grep -c': on
; Windows, lit's internal shell passes an argument without blanks to Git's
; grep unquoted, and the MSYS runtime strips its single quotes.
; NGETNOW-COUNT-2: Name='get_Now'
; NGETNOW-NOT:     Name='get_Now'

; CHECK-LABEL: .method public static native int 'use_all'(native int) cil managed
; CHECK:       {{^}}  call native int '__msil_mc.m_now'(){{$}}
; CHECK:       {{^}}  call native int '__msil_mc.m_now_ref'(){{$}}
define i64 @use_all(ptr %h) {
  %a = call ptr @__msil_mc.m_now()
  %b = call ptr @__msil_mc.m_dtmax()
  %c = call ptr @__msil_mc.m_fromticks(i64 noundef 10)
  %d = call ptr @__msil_mc.m_tszero()
  %e = call i32 @__msil_mc.m_i32max()
  %f = call i64 @__msil_mc.m_toptr(i64 noundef 9)
  %g = call i64 @__msil_mc.m_ticks(ptr noundef %h)
  %i = call ptr @__msil_mc.m_mkdate(i64 noundef 7)
  %j = call ptr @__msil_mc.m_now_ref()
  %e64 = sext i32 %e to i64
  %s = add i64 %f, %g
  %s2 = add i64 %s, %e64
  ret i64 %s2
}

attributes #0 = { "msil-managed-conv"=";boxed" "msil-managed-member"="get_Now" "msil-managed-sig"="valuetype System.DateTime()" "msil-managed-type"="valuetype System.DateTime" }
attributes #1 = { "msil-managed-conv"=";boxed" "msil-managed-member"="MaxValue" "msil-managed-sig"="field valuetype System.DateTime" "msil-managed-type"="valuetype System.DateTime" }
attributes #2 = { "msil-managed-conv"="val;boxed" "msil-managed-member"="FromTicks" "msil-managed-sig"="valuetype System.TimeSpan(int64)" "msil-managed-type"="valuetype System.TimeSpan" }
attributes #3 = { "msil-managed-conv"=";boxed" "msil-managed-member"="Zero" "msil-managed-sig"="field valuetype System.TimeSpan" "msil-managed-type"="valuetype System.TimeSpan" }
attributes #4 = { "msil-managed-conv"=";val" "msil-managed-member"="MaxValue" "msil-managed-sig"="field int32" "msil-managed-type"="valuetype System.Int32" }
attributes #5 = { "msil-managed-conv"="gch;val" "msil-managed-member"="ToIntPtr" "msil-managed-sig"="native int(valuetype System.Runtime.InteropServices.GCHandle)" "msil-managed-type"="valuetype System.Runtime.InteropServices.GCHandle" }
attributes #6 = { "msil-managed-conv"="this-val;val" "msil-managed-member"="get_Ticks" "msil-managed-sig"="instance int64()" "msil-managed-type"="valuetype System.DateTime" }
attributes #7 = { "msil-managed-conv"="val;boxed" "msil-managed-member"=".ctor" "msil-managed-sig"="instance void(int64)" "msil-managed-type"="valuetype System.DateTime" }
attributes #8 = { "msil-managed-conv"=";boxed" "msil-managed-member"="get_Now" "msil-managed-sig"="valuetype System.DateTime()" "msil-managed-type"="System.DateTime" }

!msil.managed.calls = !{!0, !1, !2, !3, !4, !5, !6, !7, !8}
!0 = !{!"__msil_mc.m_now", !"valuetype System.DateTime", !"get_Now", !"valuetype System.DateTime()", !"", !";boxed", !""}
!1 = !{!"__msil_mc.m_dtmax", !"valuetype System.DateTime", !"MaxValue", !"field valuetype System.DateTime", !"", !";boxed", !""}
!2 = !{!"__msil_mc.m_fromticks", !"valuetype System.TimeSpan", !"FromTicks", !"valuetype System.TimeSpan(int64)", !"", !"val;boxed", !""}
!3 = !{!"__msil_mc.m_tszero", !"valuetype System.TimeSpan", !"Zero", !"field valuetype System.TimeSpan", !"", !";boxed", !""}
!4 = !{!"__msil_mc.m_i32max", !"valuetype System.Int32", !"MaxValue", !"field int32", !"", !";val", !""}
!5 = !{!"__msil_mc.m_toptr", !"valuetype System.Runtime.InteropServices.GCHandle", !"ToIntPtr", !"native int(valuetype System.Runtime.InteropServices.GCHandle)", !"", !"gch;val", !""}
!6 = !{!"__msil_mc.m_ticks", !"valuetype System.DateTime", !"get_Ticks", !"instance int64()", !"", !"this-val;val", !""}
!7 = !{!"__msil_mc.m_mkdate", !"valuetype System.DateTime", !".ctor", !"instance void(int64)", !"", !"val;boxed", !""}
!8 = !{!"__msil_mc.m_now_ref", !"System.DateTime", !"get_Now", !"valuetype System.DateTime()", !"", !";boxed", !""}

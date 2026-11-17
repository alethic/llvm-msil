; Managed-call declarations in '-g -O2' IR (ImplementationPlan.md 5.20.5 ML2
; (1), milestone MC-1). clang attaches to a called declaration, with -g at
; -O1 and above, a non-distinct DISubprogram without DISPFlagDefinition
; ('declare !dbg !N', a call-site description). MSIL::createManagedCallStubs
; turns the declaration in place into an internal definition, and a
; definition may only have a distinct !dbg attachment, so the stub drops the
; declaration's subprogram (F.setSubprogram(nullptr)). Without that, the IR
; verifier that llc runs on its input fails ('function definition may only
; have a distinct !dbg attachment') and the machine passes crash on the
; stub. The callers keep their own distinct subprograms and locations.
; Checked on x86_64-unknown-linux-gnu-msil, msil64-unknown-dotnet and
; x86_64-pc-windows-msvc-msil (the Windows host triple) at -O2 and -O0,
; through -S (whole-program and object-form text) and -filetype=obj
; (llvm-cil-dump): llc prints nothing and exits 0, and every stub's body is
; written (exactly one member access; the 'utf8' row's length loop). With
; debug info the text may have '@func_begin<n>:' and '@func_end<n>:' labels
; around a method's code, so the checks step over them.
; clang -g writes the module flag "Dwarf Version" 4 on the Windows triple
; (5 on the other two); this IR has 5, and llc's text and object for the
; Windows triple are byte-identical with 4, so the one input covers all three.

; DEFINE: %{t} =
; DEFINE: %{o} =
; DEFINE: %{run} = llc -mtriple=%{t} %{o} -asm-verbose=false -verify-machineinstrs < %s \
; DEFINE:   -o %t.wp.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s -DCORE=mscorlib --input-file=%t.wp.%{t}%{o}.il && \
; DEFINE:   llc -mtriple=%{t} %{o} -msil-object-form -asm-verbose=false -verify-machineinstrs < %s \
; DEFINE:   -o %t.of.%{t}%{o}.il 2>&1 | count 0 && \
; DEFINE:   FileCheck %s -DCORE=msil.bcl --input-file=%t.of.%{t}%{o}.il
; DEFINE: %{obj} = llc -mtriple=%{t} %{o} -filetype=obj -verify-machineinstrs < %s \
; DEFINE:   -o %t.%{t}%{o}.o 2>&1 | count 0 && \
; DEFINE:   llvm-cil-dump --canonical --order=decl --params=drop %t.%{t}%{o}.o \
; DEFINE:   | FileCheck %s --check-prefix=DUMP

; REDEFINE: %{t} = x86_64-unknown-linux-gnu-msil
; REDEFINE: %{o} = -O2
; RUN: %{run}
; RUN: %if llvm-cil-dump %{ %{obj} %}
; REDEFINE: %{o} = -O0
; RUN: %{run}
; RUN: %if llvm-cil-dump %{ %{obj} %}
; REDEFINE: %{t} = msil64-unknown-dotnet
; RUN: %{run}
; RUN: %if llvm-cil-dump %{ %{obj} %}
; REDEFINE: %{o} = -O2
; RUN: %{run}
; RUN: %if llvm-cil-dump %{ %{obj} %}
; REDEFINE: %{t} = x86_64-pc-windows-msvc-msil
; RUN: %{run}
; RUN: %if llvm-cil-dump %{ %{obj} %}
; REDEFINE: %{o} = -O0
; RUN: %{run}
; RUN: %if llvm-cil-dump %{ %{obj} %}

; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.lnx.dll %t.wp.x86_64-unknown-linux-gnu-msil-O2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.p64.dll %t.wp.msil64-unknown-dotnet-O2.il %}
; RUN: %if ilasm %{ %ilasm-verify -output=%t.wp.win.dll %t.wp.x86_64-pc-windows-msvc-msil-O0.il %}

source_filename = "managed-call-debuginfo.c"

; CHECK-LABEL: .method public static int32 'main'() cil managed
; CHECK:       call int32 '__msil_mc.m_abs'(int32)
; CHECK:       call int32 '__msil_mc.m_exists'(native int)
; CHECK:       {{^}}} // end of method 'main'{{$}}
define i32 @main() local_unnamed_addr !dbg !14 {
entry:
  %a = tail call i32 @__msil_mc.m_abs(i32 noundef -5), !dbg !16
  %e = tail call zeroext i1 @__msil_mc.m_exists(ptr noundef @path), !dbg !17
  %z = zext i1 %e to i32, !dbg !17
  %r = add nsw i32 %a, %z, !dbg !18
  ret i32 %r, !dbg !18
}

@path = private unnamed_addr constant [4 x i8] c"a.c\00"

; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_abs'(int32) cil managed
; CHECK-NEXT:  {{^\{$}}
; CHECK:       {{^  \.maxstack [0-9]+$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 {{\[}}[[CORE]]]System.Math::Abs(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_abs'{{$}}
declare !dbg !20 i32 @__msil_mc.m_abs(i32 noundef) local_unnamed_addr #0

; CHECK-LABEL: .method assembly static int32 '__msil_mc.m_exists'(native int) cil managed
; CHECK:       {{^}}  ldind.{{[iu]}}1{{$}}
; CHECK:       {{^}}  brtrue{{(\.s)?}} {{@MC[0-9]+_[0-9]+$}}
; CHECK:       {{^}}  newobj instance void {{\[}}[[CORE]]]System.Text.UTF8Encoding::.ctor(bool, bool){{$}}
; CHECK:       {{^}}  callvirt instance string {{\[}}[[CORE]]]System.Text.Encoding::GetString(uint8*, int32){{$}}
; CHECK:       {{^}}  call bool {{\[}}[[CORE]]]System.IO.File::Exists(string){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  cgt.un{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK:       {{^}}} // end of method '__msil_mc.m_exists'{{$}}
declare !dbg !23 zeroext i1 @__msil_mc.m_exists(ptr noundef) local_unnamed_addr #1

; DUMP-LABEL: {{^}}  .method 'main' int32() flags=
; DUMP:       {{^}}    L{{[0-9]+}}: call int32(int32) '<Module>'::'__msil_mc.m_abs'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call int32(native int) '<Module>'::'__msil_mc.m_exists'{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_abs' int32(int32) flags=
; DUMP-NEXT:  {{^}}    .maxstack {{[0-9]+$}}
; DUMP-NEXT:  {{^}}    L0: ldarg.0{{$}}
; DUMP-NEXT:  {{^}}    L1: call int32(int32) [msil.bcl]System.Math::'Abs'{{$}}
; DUMP-NEXT:  {{^}}    L2: ret{{$}}
; DUMP-LABEL: {{^}}  .method '__msil_mc.m_exists' int32(native int) flags=
; DUMP:       {{^}}    L{{[0-9]+}}: ldind.{{[iu]}}1{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: newobj instance void(bool, bool) [msil.bcl]System.Text.UTF8Encoding::'.ctor'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: callvirt instance string(uint8*, int32) [msil.bcl]System.Text.Encoding::'GetString'{{$}}
; DUMP:       {{^}}    L{{[0-9]+}}: call bool(string) [msil.bcl]System.IO.File::'Exists'{{$}}

attributes #0 = { "msil-managed-conv"="val;val" "msil-managed-member"="Abs" "msil-managed-sig"="int32(int32)" "msil-managed-type"="System.Math" }
attributes #1 = { "msil-managed-conv"="utf8;bool" "msil-managed-member"="Exists" "msil-managed-sig"="bool(string)" "msil-managed-type"="System.IO.File" }

!llvm.dbg.cu = !{!0}
!msil.managed.calls = !{!4, !5}
!llvm.module.flags = !{!6, !7}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, producer: "clang", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, retainedTypes: !2, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "managed-call-debuginfo.c", directory: "/")
!2 = !{!3}
!3 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!4 = !{!"__msil_mc.m_abs", !"System.Math", !"Abs", !"int32(int32)", !"", !"val;val", !""}
!5 = !{!"__msil_mc.m_exists", !"System.IO.File", !"Exists", !"bool(string)", !"", !"utf8;bool", !""}
!6 = !{i32 7, !"Dwarf Version", i32 5}
!7 = !{i32 2, !"Debug Info Version", i32 3}
!14 = distinct !DISubprogram(name: "main", scope: !1, file: !1, line: 9, type: !15, scopeLine: 9, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0)
!15 = !DISubroutineType(types: !2)
!16 = !DILocation(line: 10, column: 11, scope: !14)
!17 = !DILocation(line: 10, column: 25, scope: !14)
!18 = !DILocation(line: 10, column: 3, scope: !14)
; The declarations' subprograms: not distinct, no DISPFlagDefinition.
!20 = !DISubprogram(name: "m_abs", linkageName: "__msil_mc.m_abs", scope: !1, file: !1, line: 3, type: !21, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!21 = !DISubroutineType(types: !22)
!22 = !{!3, !3}
!23 = !DISubprogram(name: "m_exists", linkageName: "__msil_mc.m_exists", scope: !1, file: !1, line: 6, type: !24, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!24 = !DISubroutineType(types: !25)
!25 = !{!26, !27}
!26 = !DIBasicType(name: "_Bool", size: 8, encoding: DW_ATE_boolean)
!27 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !28, size: 64)
!28 = !DIBasicType(name: "char", size: 8, encoding: DW_ATE_signed_char)

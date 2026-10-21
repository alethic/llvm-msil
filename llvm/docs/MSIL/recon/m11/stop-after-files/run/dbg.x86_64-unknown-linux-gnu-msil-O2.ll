; ModuleID = 'dbg.c'
source_filename = "dbg.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu-msil"

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable
define noundef i32 @sel(i1 noundef zeroext %c, i32 noundef %a, i32 noundef %b) local_unnamed_addr #0 !dbg !14 {
entry:
    #dbg_value(i1 %c, !20, !DIExpression(DW_OP_LLVM_convert, 1, DW_ATE_unsigned, DW_OP_LLVM_convert, 8, DW_ATE_unsigned, DW_OP_stack_value), !23)
    #dbg_value(i32 %a, !21, !DIExpression(), !23)
    #dbg_value(i32 %b, !22, !DIExpression(), !23)
  %cond = select i1 %c, i32 %a, i32 %b, !dbg !24
  ret i32 %cond, !dbg !25
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable
define noundef i64 @rotr(i64 noundef %a, i64 noundef %b) local_unnamed_addr #0 !dbg !26 {
entry:
    #dbg_value(i64 %a, !31, !DIExpression(), !33)
    #dbg_value(i64 %b, !32, !DIExpression(), !33)
  %or = tail call i64 @llvm.fshr.i64(i64 %a, i64 %a, i64 %b), !dbg !34
  ret i64 %or, !dbg !35
}

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare i64 @llvm.fshr.i64(i64, i64, i64) #1

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none) }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3, !4, !5, !6, !7}
!llvm.ident = !{!8}
!llvm.errno.tbaa = !{!9}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, producer: "clang version 24.0.0git", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "dbg.c", directory: "<scratch>\\m11\\stopafter\\proto\\run", checksumkind: CSK_MD5, checksum: "0a3d503cd1624898e3c4de8b49987121")
!2 = !{i32 7, !"Dwarf Version", i32 5}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = !{i32 8, !"PIC Level", i32 2}
!5 = !{i32 7, !"PIE Level", i32 2}
!6 = !{i32 7, !"uwtable", i32 2}
!7 = !{i32 7, !"debug-info-assignment-tracking", i1 true}
!8 = !{!"clang version 24.0.0git"}
!9 = !{!10, !11, i64 0}
!10 = !{!"__libc_errno", !11, i64 0}
!11 = !{!"int", !12, i64 0}
!12 = !{!"omnipotent char", !13, i64 0}
!13 = !{!"Simple C/C++ TBAA"}
!14 = distinct !DISubprogram(name: "sel", scope: !1, file: !1, line: 1, type: !15, scopeLine: 1, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !19, keyInstructions: true)
!15 = !DISubroutineType(types: !16)
!16 = !{!17, !18, !17, !17}
!17 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!18 = !DIBasicType(name: "_Bool", size: 8, encoding: DW_ATE_boolean)
!19 = !{!20, !21, !22}
!20 = !DILocalVariable(name: "c", arg: 1, scope: !14, file: !1, line: 1, type: !18)
!21 = !DILocalVariable(name: "a", arg: 2, scope: !14, file: !1, line: 1, type: !17)
!22 = !DILocalVariable(name: "b", arg: 3, scope: !14, file: !1, line: 1, type: !17)
!23 = !DILocation(line: 0, scope: !14)
!24 = !DILocation(line: 1, column: 41, scope: !14, atomGroup: 2, atomRank: 1)
!25 = !DILocation(line: 1, column: 34, scope: !14, atomGroup: 1, atomRank: 1)
!26 = distinct !DISubprogram(name: "rotr", scope: !1, file: !1, line: 2, type: !27, scopeLine: 2, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !30, keyInstructions: true)
!27 = !DISubroutineType(types: !28)
!28 = !{!29, !29, !29}
!29 = !DIBasicType(name: "unsigned long long", size: 64, encoding: DW_ATE_unsigned)
!30 = !{!31, !32}
!31 = !DILocalVariable(name: "a", arg: 1, scope: !26, file: !1, line: 2, type: !29)
!32 = !DILocalVariable(name: "b", arg: 2, scope: !26, file: !1, line: 2, type: !29)
!33 = !DILocation(line: 0, scope: !26)
!34 = !DILocation(line: 2, column: 94, scope: !26, atomGroup: 1, atomRank: 2)
!35 = !DILocation(line: 2, column: 71, scope: !26, atomGroup: 1, atomRank: 1)

source_filename = "mfs.c"

define internal i32 @bar() noinline {
  ret i32 1
}

@tab = internal constant [4 x i32] [i32 1, i32 2, i32 3, i32 4]
@g = internal global i32 5

define internal i32 @baz() noinline {
  %p = getelementptr [4 x i32], ptr @tab, i64 0, i64 2
  %v = load i32, ptr %p
  %w = load i32, ptr @g
  %s = add i32 %v, %w
  ret i32 %s
}

define i32 @foo1(i1 zeroext %0) !prof !14 {
  br i1 %0, label %2, label %4, !prof !17

2:
  %3 = call i32 @bar()
  br label %6

4:
  %5 = call i32 @baz()
  br label %6

6:
  %r = phi i32 [ %3, %2 ], [ %5, %4 ]
  ret i32 %r
}

!llvm.module.flags = !{!0}
!0 = !{i32 1, !"ProfileSummary", !1}
!1 = !{!2, !3, !4, !5, !6, !7, !8, !9}
!2 = !{!"ProfileFormat", !"InstrProf"}
!3 = !{!"TotalCount", i64 10000}
!4 = !{!"MaxCount", i64 10}
!5 = !{!"MaxInternalCount", i64 1}
!6 = !{!"MaxFunctionCount", i64 1000}
!7 = !{!"NumCounts", i64 3}
!8 = !{!"NumFunctions", i64 5}
!9 = !{!"DetailedSummary", !10}
!10 = !{!11, !12, !13}
!11 = !{i32 10000, i64 100, i32 1}
!12 = !{i32 999900, i64 100, i32 1}
!13 = !{i32 999999, i64 1, i32 2}
!14 = !{!"function_entry_count", i64 7000}
!17 = !{!"branch_weights", i32 7000, i32 0}

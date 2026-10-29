target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc-msil"
declare i32 @g()
define i32 @main() { %r = call i32 @g() ret i32 %r }
!llvm.module.flags = !{!0, !1, !2, !3, !4}
!0 = !{i32 7, !"msil.max", i32 1}
!1 = !{i32 8, !"msil.min", i32 1}
!2 = !{i32 1, !"msil.error", i32 1}
!3 = !{i32 7, !"msil.max.onlya", i32 1}
!4 = !{i32 8, !"msil.min.onlya", i32 1}

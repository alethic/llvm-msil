// Late function splitting is ignored by the MSIL backend (decision D8): the
// driver rejects -fsplit-machine-functions for MSIL triples
// (clang/test/Driver/msil-section-options.c), but 'clang -cc1
// -fsplit-machine-functions', also reached through -Xclang, and '-mllvm
// -function-splitting=all' still reach the backend, which crashed in the MSIL
// AsmPrinter ('Cannot switch to a null section!') on a function with a
// profile and a cold block. Each run exits 0 without a diagnostic and prints
// the same IL as the same command without the option. The input is IR
// because the split needs profile data (an entry count and branch weights);
// llvm/test/CodeGen/MSIL/function-splitting.ll covers the llc options. The
// IR has no target triple, and clang warns that it overrides even an empty
// one (-Wno-override-module). There is no -O option: at -O2 the IR optimizer
// folds the calls to their constant results. The driver runs have their own
// baseline, because the driver adds the MSVC version to the Windows triple,
// which the IL header prints.

// REQUIRES: msil-registered-target

// RUN: split-file %s %t

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -o %t/lnx.il
// RUN: FileCheck %s --input-file=%t/lnx.il
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -fsplit-machine-functions -o %t/lnx-split.il 2>&1 | count 0
// RUN: diff %t/lnx.il %t/lnx-split.il
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -mllvm -function-splitting=all -o %t/lnx-mllvm.il 2>&1 | count 0
// RUN: diff %t/lnx.il %t/lnx-mllvm.il
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -o %t/lnx-drv.il
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -Xclang -fsplit-machine-functions -o %t/lnx-xclang.il 2>&1 | count 0
// RUN: diff %t/lnx-drv.il %t/lnx-xclang.il

// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -o %t/win.il
// RUN: FileCheck %s --input-file=%t/win.il
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -fsplit-machine-functions -o %t/win-split.il 2>&1 | count 0
// RUN: diff %t/win.il %t/win-split.il
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -mllvm -function-splitting=all -o %t/win-mllvm.il 2>&1 | count 0
// RUN: diff %t/win.il %t/win-mllvm.il
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -o %t/win-drv.il
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -Xclang -fsplit-machine-functions -o %t/win-xclang.il 2>&1 | count 0
// RUN: diff %t/win-drv.il %t/win-xclang.il

// RUN: %if ilasm %{ %ilasm-verify -output=%t/lnx-split.dll %t/lnx-split.il %}

// CHECK-LABEL: .method public static int32 'foo1'(int32) cil managed
// CHECK:       call int32 'bar'()
// CHECK:       call int32 'baz'()
// CHECK:       } // end of method 'foo1'

//--- mfs.ll
define internal i32 @bar() noinline {
  ret i32 1
}

define internal i32 @baz() noinline {
  ret i32 2
}

; The branch weights make the block that calls @baz cold (count 0).
define i32 @foo1(i1 zeroext %c) !prof !14 {
entry:
  br i1 %c, label %hot, label %cold, !prof !15

hot:
  %h = call i32 @bar()
  br label %exit

cold:
  %k = call i32 @baz()
  br label %exit

exit:
  %r = phi i32 [ %h, %hot ], [ %k, %cold ]
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
!15 = !{!"branch_weights", i32 7000, i32 0}

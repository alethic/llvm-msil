// Late function splitting is ignored by the MSIL backend (decision D8; section
// 12 row 'D8 amended (function splitting)', 2026-10-20): the driver rejects
// -fsplit-machine-functions for MSIL triples (clang/test/Driver/msil-toolchain.c),
// but 'clang -cc1 -fsplit-machine-functions', also reached through -Xclang,
// and '-mllvm -function-splitting=all' still reach the backend, which crashed
// in the MSIL AsmPrinter ('Cannot switch to a null section!') on a function
// with a profile and a cold block. Each run exits 0 without a diagnostic and
// prints the same IL as the same command without the option. The input is IR
// because the split needs profile data (an entry count and branch weights);
// llvm/test/CodeGen/MSIL/function-splitting.ll covers the llc options. The
// IR has no target triple, and clang warns that it overrides even an empty
// one (-Wno-override-module). There is no -O option: at -O2 the IR optimizer
// folds the calls to their constant results. The driver runs have their own
// baseline, because the driver adds the MSVC version to the Windows triple,
// which the IL header prints.
//
// The other section options that the driver rejects for MSIL triples are
// ignored when passed to cc1 directly: -fbasic-block-sections=all and
// -fbasic-block-sections=list=<file> (the form the driver passes on; cc1 reads
// any other value as none) with the basic block sections profile other.txt
// each exit 0 without a diagnostic and print the cc1 baseline's IL;
// -fbasic-block-address-map prints only the backend's warning that the address
// map is ignored. -fbasic-block-sections=labels, a deprecated alias that the
// driver accepts for every target and passes on as -fbasic-block-address-map,
// gives the driver's deprecation warning and then the same backend warning,
// and the driver baseline's IL.
//
// -fpartition-static-data-sections adds the static data splitter and annotator
// passes, which place jump tables, constant pools and module-internal globals
// in hot or cold sections by their profile. mfs.ll has no static data, so these
// runs use psds.ll and its own cc1 baseline: the internal globals @g_hot and
// @g_cold, a private constant array, and a switch with five cases whose branch
// weights (with an entry count and the same profile summary) make the case
// that loads @g_hot hot. Each run exits 0 without a diagnostic and prints the
// baseline's IL, in which the switch is a CIL switch instruction and each
// global is a field that points into the data segment. The native x86-64 ELF
// run below moves the jump table to .rodata.hot. and @g_hot to .data.hot., so
// the input stays partition-eligible after later upstream changes.

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
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -fbasic-block-sections=all -o %t/lnx-bbs-all.il 2>&1 | count 0
// RUN: diff %t/lnx.il %t/lnx-bbs-all.il
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -fbasic-block-sections=list=%t/other.txt -o %t/lnx-bbs-list.il 2>&1 | count 0
// RUN: diff %t/lnx.il %t/lnx-bbs-list.il
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -S -Wno-override-module -x ir %t/psds.ll \
// RUN:   -o %t/lnx-data.il
// RUN: FileCheck %s --check-prefix=DATA --input-file=%t/lnx-data.il
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -S -Wno-override-module -x ir %t/psds.ll \
// RUN:   -fpartition-static-data-sections -o %t/lnx-psds.il 2>&1 | count 0
// RUN: diff %t/lnx-data.il %t/lnx-psds.il
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -fbasic-block-address-map -o %t/lnx-map.il 2>&1 | FileCheck %s --check-prefix=ADDRMAP
// RUN: diff %t/lnx.il %t/lnx-map.il
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -o %t/lnx-drv.il
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -Xclang -fsplit-machine-functions -o %t/lnx-xclang.il 2>&1 | count 0
// RUN: diff %t/lnx-drv.il %t/lnx-xclang.il
// RUN: %clang --target=x86_64-unknown-linux-gnu-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -fbasic-block-sections=labels -o %t/lnx-labels.il 2>&1 | FileCheck %s --check-prefix=LABELS
// RUN: diff %t/lnx-drv.il %t/lnx-labels.il

// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -o %t/win.il
// RUN: FileCheck %s --input-file=%t/win.il
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -fsplit-machine-functions -o %t/win-split.il 2>&1 | count 0
// RUN: diff %t/win.il %t/win-split.il
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -mllvm -function-splitting=all -o %t/win-mllvm.il 2>&1 | count 0
// RUN: diff %t/win.il %t/win-mllvm.il
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -fbasic-block-sections=all -o %t/win-bbs-all.il 2>&1 | count 0
// RUN: diff %t/win.il %t/win-bbs-all.il
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -fbasic-block-sections=list=%t/other.txt -o %t/win-bbs-list.il 2>&1 | count 0
// RUN: diff %t/win.il %t/win-bbs-list.il
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -S -Wno-override-module -x ir %t/psds.ll \
// RUN:   -o %t/win-data.il
// RUN: FileCheck %s --check-prefix=DATA --input-file=%t/win-data.il
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -S -Wno-override-module -x ir %t/psds.ll \
// RUN:   -fpartition-static-data-sections -o %t/win-psds.il 2>&1 | count 0
// RUN: diff %t/win-data.il %t/win-psds.il
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -fbasic-block-address-map -o %t/win-map.il 2>&1 | FileCheck %s --check-prefix=ADDRMAP
// RUN: diff %t/win.il %t/win-map.il
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -o %t/win-drv.il
// RUN: %clang --target=x86_64-pc-windows-msvc-msil -S -Wno-override-module -x ir %t/mfs.ll \
// RUN:   -Xclang -fsplit-machine-functions -o %t/win-xclang.il 2>&1 | count 0
// RUN: diff %t/win-drv.il %t/win-xclang.il

// RUN: %if x86-registered-target %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -S \
// RUN:   -Wno-override-module -x ir %t/psds.ll -fpartition-static-data-sections -o - \
// RUN:   | FileCheck %s --check-prefix=PSDS-NATIVE %}

// RUN: %if ilasm %{ %ilasm-verify -output=%t/lnx-split.dll %t/lnx-split.il %}
// RUN: %if ilasm %{ %ilasm-verify -output=%t/lnx-psds.dll %t/lnx-psds.il %}

// CHECK-LABEL: .method {{.*}}'foo1'(
// CHECK-SAME:  int32) cil managed{{$}}
// CHECK:       call int32 'bar'()
// CHECK:       call int32 'baz'()
// CHECK:       } // end of method 'foo1'

// The backend's warning is the only diagnostic of cc1 -fbasic-block-address-map.
// ADDRMAP-NOT:  {{.}}
// ADDRMAP:      {{^}}warning: basic block address map is not supported for MSIL; ignored [-Wbackend-plugin]{{$}}
// ADDRMAP-NEXT: {{^}}1 warning generated.{{$}}
// ADDRMAP-NOT:  {{.}}

// LABELS-NOT:  {{.}}
// LABELS:      {{^.*}}warning: argument '-fbasic-block-sections=labels' is deprecated, use '-fbasic-block-address-map' instead [-Wdeprecated]{{$}}
// LABELS-NEXT: {{^}}warning: basic block address map is not supported for MSIL; ignored [-Wbackend-plugin]{{$}}
// LABELS-NEXT: {{^}}1 warning generated.{{$}}
// LABELS-NOT:  {{.}}

// DATA-LABEL: .method {{.*}}'sw'(
// DATA-SAME:  int32) cil managed{{$}}
// DATA:       switch (
// DATA:       ldsfld native int 'g_hot'
// DATA:       ldsfld native int 'g_cold'
// DATA:       ldsfld native int 'ro'
// DATA:       } // end of method 'sw'

// The jump table of sw and @g_hot are in hot sections.
// PSDS-NATIVE-LABEL: {{^}}sw:
// PSDS-NATIVE:       .section .rodata.hot.,
// PSDS-NATIVE-NOT:   .section
// PSDS-NATIVE:       {{^}}.LJTI0_0:
// PSDS-NATIVE:       .section .data.hot.,
// PSDS-NATIVE-NOT:   .section
// PSDS-NATIVE:       {{^}}g_hot:

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

//--- other.txt
v1
f bar
c 0

//--- psds.ll
@g_hot = internal global i32 5
@g_cold = internal global i32 7
@ro = private constant [4 x i32] [i32 1, i32 2, i32 3, i32 4]

; The branch weights make the case that loads @g_hot hot (the first weight is
; the default destination's).
define i32 @sw(i32 %x) !prof !14 {
entry:
  switch i32 %x, label %def [
    i32 0, label %a
    i32 1, label %b
    i32 2, label %c
    i32 3, label %d
    i32 4, label %e
  ], !prof !15

a:
  %va = load i32, ptr @g_hot
  ret i32 %va

b:
  %vb = load i32, ptr @g_cold
  ret i32 %vb

c:
  ret i32 30

d:
  %p = getelementptr [4 x i32], ptr @ro, i32 0, i32 %x
  %vd = load i32, ptr %p
  ret i32 %vd

e:
  ret i32 50

def:
  ret i32 0
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
!15 = !{!"branch_weights", i32 0, i32 7000, i32 0, i32 0, i32 0, i32 0}

; Late function splitting is ignored for MSIL: CIL methods have no sections,
; so there is no cold section to move blocks to (decision D8; section 12 row
; 'D8 amended (function splitting)', 2026-10-20). -function-splitting=all
; (llc, or clang -mllvm; clang -cc1 -fsplit-machine-functions sets the same
; TargetOptions field) makes TargetPassConfig::addMachinePasses add the
; BasicBlockSections pass, which gives every function with a profile, and
; with -mfs-split-ehcode every function, a section of its own and moves the
; cold blocks to a cold section; the MSIL AsmPrinter then crashed on a null
; section ('Cannot switch to a null section!'). MSILPassConfig resets the mode
; after TargetPassConfig's constructor has applied the command line option
; and before the pipeline is built. Each run exits 0 without a diagnostic
; (with --basic-block-address-map, only MSILPrepareIR's existing warning) and
; prints the same IL as the run without the option at the same optimization
; level. With -function-splitting=all, alone or together with
; --basic-block-sections (basic-block-sections.ll covers that option alone),
; the pipeline has no basic block sections pass. With
; --basic-block-address-map it keeps two inert ones, 'Basic Block Sections
; Profile Reader' and 'Basic Block Sections Transformation': TargetPassConfig
; schedules them because TM.Options.BBAddrMap is still set when the pipeline
; is built, and MSILPrepareIR clears it only when it runs, after its warning,
; so they change nothing (the MAP runs print the baseline IL; section 12 row
; 'Exit criterion of item 3 clarified', 2026-10-20).
;
; The input is modeled on llvm/test/CodeGen/Generic/machine-function-splitter.ll:
; an InstrProf profile summary, an entry count and branch weights 7000:0 make
; the block of foo1 that calls baz cold. Both callees are defined in the
; module, because Phase 1 has no linker. A function without profile data is
; never split (unless -mfs-split-ehcode), so the profile is what makes the
; input split-eligible; the native x86-64 ELF run below shows that the same
; input is split there (foo1.cold), so this test keeps exercising the
; splitter after later upstream changes. other.txt is a basic block sections
; profile that names only bar; with it a native target still splits foo1 by
; its profile data (as in the upstream test).

; RUN: split-file %s %t

; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %t/mfs.ll -o %t/lnx.il
; RUN: FileCheck %s --check-prefixes=IL,O2 --input-file=%t/lnx.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -O0 < %t/mfs.ll -o %t/lnx-O0.il
; RUN: FileCheck %s --check-prefixes=IL,O0 --input-file=%t/lnx-O0.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -function-splitting=all \
; RUN:   < %t/mfs.ll -o %t/lnx-all.il 2>&1 | count 0
; RUN: diff %t/lnx.il %t/lnx-all.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -function-splitting=all -O0 \
; RUN:   < %t/mfs.ll -o %t/lnx-all-O0.il 2>&1 | count 0
; RUN: diff %t/lnx-O0.il %t/lnx-all-O0.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -function-splitting=all \
; RUN:   -mfs-split-ehcode < %t/mfs.ll -o %t/lnx-eh.il 2>&1 | count 0
; RUN: diff %t/lnx.il %t/lnx-eh.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -function-splitting=all \
; RUN:   --basic-block-sections=all < %t/mfs.ll -o %t/lnx-bbs-all.il 2>&1 | count 0
; RUN: diff %t/lnx.il %t/lnx-bbs-all.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -function-splitting=all \
; RUN:   --basic-block-sections=%t/other.txt < %t/mfs.ll -o %t/lnx-bbs-list.il 2>&1 | count 0
; RUN: diff %t/lnx.il %t/lnx-bbs-list.il
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -function-splitting=all \
; RUN:   --basic-block-address-map < %t/mfs.ll -o %t/lnx-map.il 2>&1 | FileCheck %s --check-prefix=MAP
; RUN: diff %t/lnx.il %t/lnx-map.il

; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %t/mfs.ll -o %t/win.il
; RUN: FileCheck %s --check-prefixes=IL,O2 --input-file=%t/win.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -O0 < %t/mfs.ll -o %t/win-O0.il
; RUN: FileCheck %s --check-prefixes=IL,O0 --input-file=%t/win-O0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -function-splitting=all \
; RUN:   < %t/mfs.ll -o %t/win-all.il 2>&1 | count 0
; RUN: diff %t/win.il %t/win-all.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -function-splitting=all -O0 \
; RUN:   < %t/mfs.ll -o %t/win-all-O0.il 2>&1 | count 0
; RUN: diff %t/win-O0.il %t/win-all-O0.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -function-splitting=all \
; RUN:   -mfs-split-ehcode < %t/mfs.ll -o %t/win-eh.il 2>&1 | count 0
; RUN: diff %t/win.il %t/win-eh.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -function-splitting=all \
; RUN:   --basic-block-sections=all < %t/mfs.ll -o %t/win-bbs-all.il 2>&1 | count 0
; RUN: diff %t/win.il %t/win-bbs-all.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -function-splitting=all \
; RUN:   --basic-block-sections=%t/other.txt < %t/mfs.ll -o %t/win-bbs-list.il 2>&1 | count 0
; RUN: diff %t/win.il %t/win-bbs-list.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -function-splitting=all \
; RUN:   --basic-block-address-map < %t/mfs.ll -o %t/win-map.il 2>&1 | FileCheck %s --check-prefix=MAP
; RUN: diff %t/win.il %t/win-map.il

; No basic block sections pass (BasicBlockSections, 'bbsections-prepare', or
; its profile reader) is scheduled, even when both options ask for one.
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -function-splitting=all --basic-block-sections=all \
; RUN:   -debug-pass=Structure < %t/mfs.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=PIPE --implicit-check-not=bbsections \
; RUN:     --implicit-check-not="Basic Block Sections"
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -function-splitting=all --basic-block-sections=all \
; RUN:   -debug-pass=Structure < %t/mfs.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=PIPE --implicit-check-not=bbsections \
; RUN:     --implicit-check-not="Basic Block Sections"

; With --basic-block-address-map the profile reader and BasicBlockSections are
; still scheduled, once each, and do nothing (the MAP runs above).
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -function-splitting=all --basic-block-address-map \
; RUN:   -debug-pass=Structure < %t/mfs.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=MAPPIPE --implicit-check-not="Basic Block Sections"
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -function-splitting=all --basic-block-address-map \
; RUN:   -debug-pass=Structure < %t/mfs.ll -o /dev/null 2>&1 \
; RUN:   | FileCheck %s --check-prefix=MAPPIPE --implicit-check-not="Basic Block Sections"

; RUN: %if x86-registered-target %{ llc -mtriple=x86_64-unknown-linux-gnu -function-splitting=all \
; RUN:   < %t/mfs.ll | FileCheck %s --check-prefix=NATIVE %}

; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs \
; RUN:   -function-splitting=all < %t/mfs.ll -o %t/ilasm.il \
; RUN:   && %ilasm-verify -output=%t/ilasm.dll %t/ilasm.il %}

; The baseline keeps the cold block (it calls baz) in foo1. At -O2 both
; blocks return directly; at -O0 they branch to the block of the phi.
; IL-LABEL: .method {{.*}}'foo1'(
; IL-SAME:  int32) cil managed{{$}}
; IL:       brfalse [[COLD:@BB[0-9_]+]]
; IL:       call int32 'bar'()
; O2-NEXT:  ret
; O2-NEXT:  {{^}}[[COLD]]:
; O0:       {{^}}[[COLD]]:
; IL-NEXT:  call int32 'baz'()
; O2-NEXT:  ret
; IL:       } // end of method 'foo1'

; The address map warning of MSILPrepareIR is the only output.
; MAP-NOT: {{.}}
; MAP:     warning: basic block address map is not supported for MSIL; ignored
; MAP-NOT: {{.}}

; PIPE: Pass Arguments:
; PIPE: MSIL Assembly Printer

; MAPPIPE: Pass Arguments:
; MAPPIPE: {{^}}Basic Block Sections Profile Reader{{$}}
; MAPPIPE: {{^ +}}Basic Block Sections Transformation{{$}}
; MAPPIPE: {{^ +}}MSIL Assembly Printer{{$}}
; MAPPIPE: warning: basic block address map is not supported for MSIL; ignored

; NATIVE:      .section .text.split.foo1
; NATIVE-NEXT: foo1.cold:
; NATIVE:      baz

;--- mfs.ll
source_filename = "function-splitting.c"

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

;--- other.txt
v1
f bar
c 0

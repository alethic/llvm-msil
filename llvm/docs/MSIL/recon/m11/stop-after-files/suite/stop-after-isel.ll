; MIR printed at the msil-isel stop point must pass the machine verifier that
; llc runs at the end of a pipeline that stops before code emission (upstream
; 663669080864, -disable-mir-output-verify to opt out; no RUN line here passes
; it). The ScheduleDAG scheduler may put an ARGUMENT_<RC> after an instruction
; that has 'implicit-def dead $arguments'; the verifier then reports 'Using an
; undefined physical register' for the ARGUMENT's 'implicit $arguments'.
; MSILDAGToDAGISel moves every ARGUMENT to the top of the entry block before it
; returns, so -stop-after=msil-isel and -stop-before=msil-argument-move print
; the same verifier-clean MIR. MSILArgumentMove still runs and still repairs
; hand-written MIR (reg-argument.mir).
;
; Each MIR output is read back (the MIR parser verifies on read) and compiled
; on to IL from -start-after=msil-isel; the IL must equal the full pipeline's.

; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -O0 -stop-after=msil-isel -o %t.lnx.O0.mir
; RUN: FileCheck %s --input-file=%t.lnx.O0.mir
; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -O2 -stop-after=msil-isel -o %t.lnx.O2.mir
; RUN: FileCheck %s --input-file=%t.lnx.O2.mir
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -O0 -stop-after=msil-isel -o %t.win.O0.mir
; RUN: FileCheck %s --input-file=%t.win.O0.mir
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -O2 -stop-after=msil-isel -o %t.win.O2.mir
; RUN: FileCheck %s --input-file=%t.win.O2.mir
; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -O2 -stop-before=msil-argument-move -o - \
; RUN:   | FileCheck %s
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -O2 -stop-before=msil-argument-move -o - \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -run-pass=none %t.lnx.O2.mir -o /dev/null
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -run-pass=none %t.win.O2.mir -o /dev/null
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O2 -start-after=msil-isel %t.lnx.O2.mir -o %t.lnx.il
; RUN: llc < %s -mtriple=x86_64-unknown-linux-gnu-msil -O2 -o %t.lnx.ref.il
; RUN: diff %t.lnx.ref.il %t.lnx.il
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O2 -start-after=msil-isel %t.win.O2.mir -o %t.win.il
; RUN: llc < %s -mtriple=x86_64-pc-windows-msvc-msil -O2 -o %t.win.ref.il
; RUN: diff %t.win.ref.il %t.win.il
; RUN: %if ilasm %{ %ilasm-verify -output=%t.win.dll %t.win.il %}

; Before the fix ISel printed ARGUMENT_I32 0, CONST_I32 1, AND_I32,
; ARGUMENT_I32 2, ARGUMENT_I32 1 at -O0 and -O2.
; CHECK-LABEL: name: sel{{$}}
; CHECK:       bb.0
; CHECK-NEXT:    liveins: $arguments
; CHECK-NEXT:    {{  $}}
; CHECK-NEXT:    %{{[0-9]+}}:i32 = ARGUMENT_I32 {{[0-2]}}, implicit $arguments
; CHECK-NEXT:    %{{[0-9]+}}:i32 = ARGUMENT_I32 {{[0-2]}}, implicit $arguments
; CHECK-NEXT:    %{{[0-9]+}}:i32 = ARGUMENT_I32 {{[0-2]}}, implicit $arguments
; CHECK-NOT:     ARGUMENT_
; CHECK:         RETURN
define i32 @sel(i1 %c, i32 %a, i32 %b) {
  %r = select i1 %c, i32 %a, i32 %b
  ret i32 %r
}

; Before the fix: ARGUMENT_I64 1, CONV_I4_I64, ARGUMENT_I64 0.
; CHECK-LABEL: name: rotr_i64{{$}}
; CHECK:       bb.0
; CHECK-NEXT:    liveins: $arguments
; CHECK-NEXT:    {{  $}}
; CHECK-NEXT:    %{{[0-9]+}}:i64 = ARGUMENT_I64 {{[01]}}, implicit $arguments
; CHECK-NEXT:    %{{[0-9]+}}:i64 = ARGUMENT_I64 {{[01]}}, implicit $arguments
; CHECK-NOT:     ARGUMENT_
; CHECK:         RETURN
define i64 @rotr_i64(i64 %a, i64 %b) {
  %r = call i64 @llvm.fshr.i64(i64 %a, i64 %a, i64 %b)
  ret i64 %r
}

declare i64 @llvm.fshr.i64(i64, i64, i64)

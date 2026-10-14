; A void call that traps runs before a later remainder by zero in the same
; block (Milestone 4, review-r2 finding 2; plan section 12, 'Plan 5.4
; TrapsOnlyOnUB clarified'). g calls check, which traps (llvm.trap is 'ldnull;
; throw'), and only then computes x urem y on native int values; main calls
; g(7, 0). The remainder never runs in C or LLVM IR, so the program must die
; with check's NullReferenceException at every level, never with the
; DivideByZeroException of a rem.un that instruction scheduling or
; MSILRegStackify moved above the call. The call has no result, so nothing
; but the order of the call and the rem.un decides the outcome.
; trap-call-before-div.ll has the same check for a call whose result is used.
; Explicit RUN lines and no native oracle: a native trap is not an oracle
; (update-expected.py refuses it), so FileCheck reads the transcript of
; msil-run.py (the precedent is M6's 03-control/trap.ll).
; RUN: %llc_msil -O0 %s -o %t.O0.il
; RUN: %ilasm -dll -output=%t.O0.dll %t.O0.il
; RUN: %msil_verify %t.O0.dll
; RUN: %if msil-jitall %{ %msil_jitall %t.O0.dll %}
; RUN: %msil_exec --out %t.O0.out %t.O0.dll
; RUN: FileCheck --input-file=%t.O0.out --match-full-lines %s
; RUN: %llc_msil -O2 %s -o %t.O2.il
; RUN: %ilasm -dll -output=%t.O2.dll %t.O2.il
; RUN: %msil_verify %t.O2.dll
; RUN: %if msil-jitall %{ %msil_jitall %t.O2.dll %}
; RUN: %msil_exec --out %t.O2.out %t.O2.dll
; RUN: FileCheck --input-file=%t.O2.out --match-full-lines %s

; The transcript (the program prints nothing): the status line of an
; unhandled exception, which msil-run.py prints the same way on every OS.
; CHECK: [unhandled exception: System.NullReferenceException]

declare void @llvm.trap()

define internal void @check(i32 %y) noinline {
  call void @llvm.trap()
  unreachable
}

define internal i64 @g(i64 %x, i64 %y, i32 %y32) noinline {
  call void @check(i32 %y32)
  %d = urem i64 %x, %y
  ret i64 %d
}

define i32 @main() {
  %r = call i64 @g(i64 7, i64 0, i32 0)
  %t = trunc i64 %r to i32
  ret i32 %t
}

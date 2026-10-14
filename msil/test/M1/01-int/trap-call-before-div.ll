; A call that traps runs before a later division by zero in the same block
; (Milestone 4, review-r2 finding 2; plan section 12, 'Plan 5.4 TrapsOnlyOnUB
; clarified'). g calls f, which traps (llvm.trap is 'ldnull; throw'), and only
; then divides by y; main calls g(1, 0). The division never runs in C or LLVM
; IR, so the program must die with f's NullReferenceException at every level,
; never with the DivideByZeroException of a div that instruction scheduling or
; MSILRegStackify moved above the call. f's result feeds the add after the
; division, so only the order of the call and the div decides the outcome.
; trap-call-before-rem.ll has the same check for a void call before a urem.
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

define internal i32 @f(i32 %v) noinline {
  call void @llvm.trap()
  unreachable
}

define internal i32 @g(i32 %x, i32 %y) noinline {
  %c = call i32 @f(i32 %x)
  %d = sdiv i32 %x, %y
  %r = add i32 %d, %c
  ret i32 %r
}

define i32 @main() {
  %r = call i32 @g(i32 1, i32 0)
  ret i32 %r
}

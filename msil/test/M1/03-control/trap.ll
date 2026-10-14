; Traps (Milestone 6): llvm.trap is 'ldnull; throw', so the program ends with
; an unhandled System.NullReferenceException. The trap is reached through
; control flow: a loop that sums 0..5 branches out of its body to a call of a
; noreturn function (followed by 'unreachable', itself a trap after the
; call) whose body is the llvm.trap, but only when the sum is right: a wrong
; sum returns 2, a loop that never reaches i == 5 returns 1, and a trap that
; fires anywhere else (the unreachable after the call, a fall-through into
; the wrong block) would still need the right sum first. Explicit RUN lines
; and no native oracle: a native trap is not an oracle (update-expected.py
; refuses it), so FileCheck reads the transcript of msil-run.py, on .NET 8
; (or the runtime of msil_runner) and, with msil_run_netfx, on .NET
; Framework 4.8.
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
; RUN: %if msil-run-netfx %{ %ilasm -exe -output=%t.O2.exe %t.O2.il %}
; RUN: %if msil-run-netfx %{ %msil_exec --netfx --out %t.O2.netfx.out %t.O2.exe %}
; RUN: %if msil-run-netfx %{ FileCheck --input-file=%t.O2.netfx.out --match-full-lines %s %}

; The transcript (the program prints nothing): the status line of an
; unhandled exception, which msil-run.py prints the same way on every OS and
; runtime.
; CHECK: [unhandled exception: System.NullReferenceException]

declare void @llvm.trap()

define internal void @fail(i32 %code) noinline noreturn {
  call void @llvm.trap()
  unreachable
}

define i32 @main() {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i1, %next ]
  %acc = phi i32 [ 0, %entry ], [ %acc1, %next ]
  %acc1 = add i32 %acc, %i
  %five = icmp eq i32 %i, 5
  br i1 %five, label %check, label %next

check:
  %ok = icmp eq i32 %acc1, 15
  br i1 %ok, label %boom, label %bad

boom:
  call void @fail(i32 %acc1)
  unreachable

bad:
  ret i32 2

next:
  %i1 = add i32 %i, 1
  %more = icmp slt i32 %i1, 10
  br i1 %more, label %loop, label %done

done:
  ret i32 1
}

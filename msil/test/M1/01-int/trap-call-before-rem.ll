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
; Each level builds through the route of the lit parameter msil_pipeline
; (plan 5.12.13, 'obj variants'): under asm (the asm-oracle leg) Phase 1's
; llc text and ilasm -dll into a DLL, under obj (the default pipeline) and
; lto 'llc -filetype=obj' and the link through the clang driver and msil-ld
; (with %{msil_ldflags}: the sysroot of a Linux triple) into an EXE image, as
; %{msil_ll} builds under obj. Every image is verified, force-JITted, run and
; FileChecked, then force-JITted, run and FileChecked again once per entry of
; the leg's msil_also_run, in the parameter's order, by %{msil_also_run_check}
; (plan 5.12.3): it runs %{msil_also_run_image}, which follows %{trap_image},
; with the entry's runtimeconfig.json and, for a +tc0 entry,
; DOTNET_TieredCompilation=0 (plan D38), as the composites' also-run legs do,
; and is empty without msil_also_run. So each leg runs the test on the
; runtimes it folds in (plan 5.12.11): net8 on Windows .NET 8, then on .NET 10
; and on .NET 8 with TieredCompilation=0; wsl-net10 on the Linux triple on WSL
; .NET 10, then on the private WSL .NET 8 and on .NET 10 with
; TieredCompilation=0; lit-legs.sh's net8 (Linux CI) also on .NET 10 with
; TieredCompilation=0; asm-oracle (the text route) and more-levels on the main
; runtime only, as the composites do.
; DEFINE: %{trap_level} = O0
; DEFINE: %{trap_image} = %t.O0.dll
; REDEFINE: %{msil_also_run_image} = %{trap_image}
; DEFINE: %{trap_runs} = \
; DEFINE:   %msil_verify %{trap_image} \
; DEFINE:   %if msil-jitall %{ && %msil_jitall %{trap_image} %} \
; DEFINE:   && %msil_exec --out %{trap_image}.out %{trap_image} \
; DEFINE:   && FileCheck --input-file=%{trap_image}.out --match-full-lines %s \
; DEFINE:   %{msil_also_run_check}
; DEFINE: %{trap_asm} = \
; DEFINE:   %llc_msil -%{trap_level} %s -o %t.%{trap_level}.il \
; DEFINE:   && %ilasm -dll -output=%{trap_image} %t.%{trap_level}.il \
; DEFINE:   && %{trap_runs}
; DEFINE: %{trap_obj} = \
; DEFINE:   %llc_msil -%{trap_level} -filetype=obj %s -o %t.%{trap_level}.o \
; DEFINE:   && %clang_msil %{msil_ldflags} -%{trap_level} %t.%{trap_level}.o -o %{trap_image} \
; DEFINE:   && %{trap_runs}
; RUN: %if msil-pipeline=asm %{ %{trap_asm} %}
; REDEFINE: %{trap_image} = %t.O0.exe
; RUN: %if !msil-pipeline=asm %{ %{trap_obj} %}
; REDEFINE: %{trap_level} = O2
; REDEFINE: %{trap_image} = %t.O2.dll
; RUN: %if msil-pipeline=asm %{ %{trap_asm} %}
; REDEFINE: %{trap_image} = %t.O2.exe
; RUN: %if !msil-pipeline=asm %{ %{trap_obj} %}

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

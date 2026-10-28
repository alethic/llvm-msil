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
; (or the runtime of msil_runner), on the runtimes below and, with
; msil_run_netfx, on .NET Framework 4.8.
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
; With msil_run_netfx (net8) the -O2 image runs on .NET Framework 4.8 too:
; under obj and lto the EXE image msil-ld linked, under asm ilasm -exe's image
; of the same text.
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
; RUN: %if msil-run-netfx && msil-pipeline=asm %{ %ilasm -exe -output=%t.O2.exe %t.O2.il %}
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

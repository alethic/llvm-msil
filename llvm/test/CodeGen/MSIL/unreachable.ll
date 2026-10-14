; Traps and unreachable (ImplementationPlan.md M6.a, decision D8, plan 5.1 and
; 5.4 'TRAP'; recon isel.md 5.3; port of
; llvm/test/CodeGen/WebAssembly/unreachable.ll). llvm.trap selects TRAP and
; prints 'ldnull' + 'throw' (a NullReferenceException at run time);
; llvm.debugtrap and llvm.ubsantrap (what clang emits for the checks of
; -fsanitize-trap) are also TRAP in Phase 1. The MSIL target machine forces
; TrapUnreachable on and NoTrapAfterNoreturn off whatever llc is told
; (--trap-unreachable=false and --no-trap-after-noreturn below change
; nothing), so an IR 'unreachable' is a TRAP too, also right after a call
; of a noreturn function: no method can fall off its end, and every method
; and every path through it ends in 'ret', 'br' or 'throw' (MSILStackCheck
; rejects a fall-off). A trap in a block of its own is reached by a fused
; branch, and the block before it keeps its 'ret'.
;
; Nothing after a trap runs in Phase 1. llvm.debugtrap is not noreturn, and a
; call of llvm.trap or llvm.ubsantrap may be followed by more code, so
; MSILPrepareIR turns the instruction after every llvm.trap, llvm.debugtrap
; and llvm.ubsantrap call that is not already 'unreachable' into
; 'unreachable' (plan section 12, the M6 fix-loop row for MSILPrepareIR.cpp);
; the PREP lines check the IR right after that pass. The code after the trap,
; values computed only for it and blocks reached only through it are then
; gone before instruction selection, at every optimization level: a trap
; followed by more of its loop body (the shape of __builtin_debugtrap() in a
; loop), or by a value that feeds a PHI in the join block (a conditional
; __debugbreak()), is just 'ldnull' + 'throw' with nothing after it in its
; block, and -verify-machineinstrs stays clean. The three intrinsics have the
; same cases below (*_ret_void and *_in_loop).

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=switch \
; DEFINE:   --implicit-check-not="{{^ +b[a-z.]+\.s }}" --implicit-check-not=Debugger
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs \
; RUN:   --trap-unreachable=false < %s | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs \
; RUN:   --trap-unreachable --no-trap-after-noreturn < %s | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs \
; RUN:   --no-trap-after-noreturn < %s | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs -stop-after=msil-prepare-ir < %s \
; RUN:   | FileCheck %s --check-prefix=PREP
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs -stop-after=msil-prepare-ir < %s \
; RUN:   | FileCheck %s --check-prefix=PREP
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "unreachable.c"

declare void @llvm.trap() cold noreturn nounwind
declare void @llvm.debugtrap() nounwind
declare void @llvm.ubsantrap(i8) cold noreturn nounwind

; PREP-LABEL: define void @trap_ret_void(
; PREP-NEXT:    call void @llvm.trap()
; PREP-NEXT:    unreachable
; PREP-NEXT:  }

; CHECK-LABEL: .method {{.*}}'trap_ret_void'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  throw{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'trap_ret_void'{{$}}
define void @trap_ret_void() {
  call void @llvm.trap()
  ret void
}

; PREP-LABEL: define void @debugtrap_ret_void(
; PREP-NEXT:    call void @llvm.debugtrap()
; PREP-NEXT:    unreachable
; PREP-NEXT:  }

; CHECK-LABEL: .method {{.*}}'debugtrap_ret_void'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  throw{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'debugtrap_ret_void'{{$}}
define void @debugtrap_ret_void() {
  call void @llvm.debugtrap()
  ret void
}

; llvm.ubsantrap is noreturn like llvm.trap; its i8 operand is the kind of
; check that failed and does not change the code. clang puts 'unreachable'
; after it, but other IR may not.
; PREP-LABEL: define void @ubsan_ret_void(
; PREP-NEXT:    call void @llvm.ubsantrap(i8 0)
; PREP-NEXT:    unreachable
; PREP-NEXT:  }

; CHECK-LABEL: .method {{.*}}'ubsan_ret_void'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  throw{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ubsan_ret_void'{{$}}
define void @ubsan_ret_void() {
  call void @llvm.ubsantrap(i8 0)
  ret void
}

; llvm.trap followed by unreachable is a single trap.
; PREP-LABEL: define void @trap_unreachable(
; PREP-NEXT:    call void @llvm.trap()
; PREP-NEXT:    unreachable
; PREP-NEXT:  }

; CHECK-LABEL: .method {{.*}}'trap_unreachable'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  throw{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'trap_unreachable'{{$}}
define void @trap_unreachable() {
  call void @llvm.trap()
  unreachable
}

; Defined callees: Phase 1 has no linker, so a call of an undefined function
; that is not a P/Invoke import would be an error (decision D14).
define void @ext_func() noinline {
  ret void
}

define void @ext_never_return() noinline noreturn {
  call void @llvm.trap()
  unreachable
}

define void @ext_use(i32 %v) noinline {
  ret void
}

; IR 'unreachable' is a trap even where the method returns a value.
; CHECK-LABEL: .method {{.*}}'missing_ret_unreachable'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  call void 'ext_func'(){{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  throw{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'missing_ret_unreachable'{{$}}
define i32 @missing_ret_unreachable() {
  call void @ext_func()
  unreachable
}

; The same after a noreturn callee: the trap is kept (NoTrapAfterNoreturn is
; off), so the method cannot fall off its end.
; CHECK-LABEL: .method {{.*}}'missing_ret_noreturn_unreachable'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  call void 'ext_never_return'(){{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  throw{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'missing_ret_noreturn_unreachable'{{$}}
define i32 @missing_ret_noreturn_unreachable() {
  call void @ext_never_return()
  unreachable
}

; The 'ret' after the trap becomes 'unreachable', so the 'add' that only
; feeds it is dead and the method is just the trap, at -O0 as well.
; PREP-LABEL: define i32 @other_instruction_after_trap(
; PREP:         call void @llvm.trap()
; PREP-NEXT:    unreachable
; PREP-NEXT:  }

; CHECK-LABEL: .method {{.*}}'other_instruction_after_trap'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  throw{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'other_instruction_after_trap'{{$}}
define i32 @other_instruction_after_trap(i32 %x, i32 %b) {
  %a = add i32 %x, %b
  call void @llvm.trap()
  ret i32 %a
}

; A trap in its own block, reached by a fused branch ('blt', x < 0).
; MIR-LABEL: name: cond_trap{{$}}
; MIR:       [[X:%[0-9]+]]:i32 = ARGUMENT_I32 0,
; MIR:       {{^ *}}BLT_I32 %bb.[[T:[0-9]+]], [[X]], %{{[0-9]+}}, implicit-def dead $arguments{{$}}
; MIR:       {{^ *}}bb.[[T]].trap:
; MIR-NEXT:  {{^ *}}TRAP implicit-def dead $arguments{{$}}

; CHECK-LABEL: .method {{.*}}'cond_trap'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.0{{$}}
; CHECK-NEXT:  {{^}}  blt @BB[[F:[0-9]+]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[N:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[N]]:{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  throw{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'cond_trap'{{$}}
define i32 @cond_trap(i32 %x) {
entry:
  %c = icmp slt i32 %x, 0
  br i1 %c, label %trap, label %ok
ok:
  ret i32 %x
trap:
  call void @llvm.trap()
  unreachable
}

; A noreturn call on one path: 'brfalse' to it, then the call and a trap.
; CHECK-LABEL: .method {{.*}}'abort_on_zero'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  brfalse @BB[[F:[0-9]+]]_[[DIE:[0-9]+]]{{$}}
; CHECK:       {{^}}  div{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[DIE]]:{{$}}
; CHECK-NEXT:  {{^}}  call void 'ext_never_return'(){{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  throw{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'abort_on_zero'{{$}}
define i32 @abort_on_zero(i32 %x) {
entry:
  %z = icmp eq i32 %x, 0
  br i1 %z, label %die, label %ok
ok:
  %r = sdiv i32 100, %x
  ret i32 %r
die:
  call void @ext_never_return()
  unreachable
}

; llvm.debugtrap in a loop body, followed by the rest of the body; the
; induction variable is also returned from the exit block (M6 review-r1,
; finding 3). The rest of the body, the back edge and the exit block are
; unreachable once the debugtrap is followed by 'unreachable'.
; PREP-LABEL: define i32 @debugtrap_in_loop(
; PREP:         call void @llvm.debugtrap()
; PREP-NEXT:    unreachable

; CHECK-LABEL: .method {{.*}}'debugtrap_in_loop'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; O0-NEXT:     {{^}}  br @BB[[F:[0-9]+]]_[[L:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[L]]:{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  throw{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'debugtrap_in_loop'{{$}}
define i32 @debugtrap_in_loop(i32 %x, i32 %n) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i1, %loop ]
  call void @llvm.debugtrap()
  %y = add i32 %i, %x
  call void @ext_use(i32 %y)
  %i1 = add i32 %i, 1
  %c = icmp slt i32 %i1, %n
  br i1 %c, label %loop, label %exit
exit:
  ret i32 %i1
}

; The same with llvm.trap, which is noreturn but may still be followed by
; code in the IR.
; PREP-LABEL: define i32 @trap_in_loop(
; PREP:         call void @llvm.trap()
; PREP-NEXT:    unreachable

; CHECK-LABEL: .method {{.*}}'trap_in_loop'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; O0-NEXT:     {{^}}  br @BB[[F:[0-9]+]]_[[L:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[L]]:{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  throw{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'trap_in_loop'{{$}}
define i32 @trap_in_loop(i32 %x, i32 %n) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i1, %loop ]
  call void @llvm.trap()
  %y = add i32 %i, %x
  call void @ext_use(i32 %y)
  %i1 = add i32 %i, 1
  %c = icmp slt i32 %i1, %n
  br i1 %c, label %loop, label %exit
exit:
  ret i32 %i1
}

; The same with llvm.ubsantrap. If MSILPrepareIR left the code after it,
; instruction selection would end the loop block at TRAP but keep its back
; edge and its exit edge, and the PHI and the exit block's RETURN would read
; %i1 with no def ('Reading virtual register without a def' with
; -verify-machineinstrs).
; PREP-LABEL: define i32 @ubsan_in_loop(
; PREP:         call void @llvm.ubsantrap(i8 18)
; PREP-NEXT:    unreachable
; PREP-NOT:     @ext_use
; PREP:       }

; CHECK-LABEL: .method {{.*}}'ubsan_in_loop'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; O0-NEXT:     {{^}}  br @BB[[F:[0-9]+]]_[[L:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[L]]:{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  throw{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'ubsan_in_loop'{{$}}
define i32 @ubsan_in_loop(i32 %x, i32 %n) {
entry:
  br label %loop
loop:
  %i = phi i32 [ 0, %entry ], [ %i1, %loop ]
  call void @llvm.ubsantrap(i8 18)
  %y = add i32 %i, %x
  call void @ext_use(i32 %y)
  %i1 = add i32 %i, 1
  %c = icmp slt i32 %i1, %n
  br i1 %c, label %loop, label %exit
exit:
  ret i32 %i1
}

; A conditional debugtrap (__debugbreak() under an 'if') followed by a value
; that feeds a PHI in the join block: the trap block is only the trap, and
; the join block returns the value of the other path (%y; at -O0 possibly
; through a local, if the PHI keeps its one remaining input).
; PREP-LABEL: define i32 @debugtrap_then_join(
; PREP:       brk:
; PREP-NEXT:    call void @llvm.debugtrap()
; PREP-NEXT:    unreachable
; PREP-NOT:     mul
; PREP:       }

; CHECK-LABEL: .method {{.*}}'debugtrap_then_join'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK:       {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  bne.un @BB[[F:[0-9]+]]_[[J:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}  br @BB[[F]]_[[T:[0-9]+]]{{$}}
; O0-NEXT:     {{^}}@BB[[F]]_[[T]]:{{$}}
; CHECK-NEXT:  {{^}}  ldnull{{$}}
; CHECK-NEXT:  {{^}}  throw{{$}}
; CHECK-NEXT:  {{^}}@BB[[F]]_[[J]]:{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O0-NEXT:     {{^}}  {{ldarg.1|ldloc.0}}{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'debugtrap_then_join'{{$}}
define i32 @debugtrap_then_join(i32 %x, i32 %y) {
entry:
  %c = icmp eq i32 %x, %y
  br i1 %c, label %brk, label %join
brk:
  call void @llvm.debugtrap()
  %d = mul i32 %x, 3
  br label %join
join:
  %r = phi i32 [ %d, %brk ], [ %y, %entry ]
  ret i32 %r
}

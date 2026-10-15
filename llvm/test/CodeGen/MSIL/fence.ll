; Fences (ImplementationPlan.md decision D36, M7.a; plan 5.1; recon isel.md
; 5.9 'fence.il'): a fence with syncscope("singlethread") is a compiler
; barrier only, the form clang gives 'asm volatile("" ::: "memory")' and
; __atomic_signal_fence (plan section 12, row 'M1.d amended'). It selects the
; generic MEMBARRIER pseudo, which emits no CIL and changes no stack depth: the
; base AsmPrinter prints it as a line made of one tab and the comment
; '//MEMBARRIER', with no space after '//' (plan section 12, row 'Plan 5.1,
; D36, M7.a and M7.c amended (MEMBARRIER text)'). The CHECK lines accept only
; that text ('// MEMBARRIER' fails them; FileCheck turns the tab into one
; space), and the TAB run, with --strict-whitespace, checks that all nine
; barrier lines start with the tab and that no other line names MEMBARRIER.
; MEMBARRIER has side effects and no memory operands, so RegStackify treats it
; as a write and never moves a load or a store across it: a value loaded
; before it and used after it waits in a local, a load after it reads memory
; again, and a store before it is not forwarded to a load after it. Every
; other fence (the system scope, the default, for any ordering, and any other
; named scope) is the MEMORY_BARRIER record,
; 'call void [mscorlib]System.Threading.Interlocked::MemoryBarrier()', a BCL
; member reference that takes and returns nothing (never the __sync_synchronize
; libcall of the generic Expand action, and never a P/Invoke). It orders memory
; accesses in the same way.
;
; 'asm_barrier_clang' is the IR that clang -O2 emits for
; clang/test/CodeGen/MSIL/asm-barrier.c (a single-thread fence followed by a
; call of llvm.sideeffect, which emits nothing); clang's tests have no ilasm,
; so this file assembles that shape.
;
; Every function is straight-line code on arguments, so the CIL is the same at
; -O0 and -O2.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=__sync_synchronize --implicit-check-not=pinvokeimpl \
; DEFINE:   --implicit-check-not=localloc --implicit-check-not="LLVM ERROR"
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefix=TAB --strict-whitespace --implicit-check-not=MEMBARRIER
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=MIR
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "fence.c"

declare void @llvm.sideeffect()

; [[:cntrl:]] matches the tab and not a space.
; TAB-COUNT-9: {{^[[:cntrl:]]//MEMBARRIER$}}

; MIR-LABEL: name: singlethread_seq_cst{{$}}
; MIR:       {{^ +}}MEMBARRIER{{$}}
; MIR-NOT:   MEMORY_BARRIER
; MIR-LABEL: name: system_seq_cst{{$}}
; MIR:       {{^ +}}MEMORY_BARRIER
; MIR-NOT:   {{^ +}}MEMBARRIER{{$}}
; MIR-LABEL: name: fence_only{{$}}

; ---- Single-thread fences: compiler barriers ----

; CHECK-LABEL: .method {{.*}}'singlethread_seq_cst'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.2{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'singlethread_seq_cst'{{$}}
define void @singlethread_seq_cst(ptr %p, ptr %q) {
  store i32 1, ptr %p
  fence syncscope("singlethread") seq_cst
  store i32 2, ptr %q
  ret void
}

; Every ordering of a single-thread fence is a compiler barrier.
; CHECK-LABEL: .method {{.*}}'singlethread_orderings'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'singlethread_orderings'{{$}}
define void @singlethread_orderings() {
  fence syncscope("singlethread") acquire
  fence syncscope("singlethread") release
  fence syncscope("singlethread") acq_rel
  ret void
}

; A load before the barrier is not sunk past it into the 'ret'.
; CHECK-LABEL: .method {{.*}}'load_not_sunk'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  .locals (int32){{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @load_not_sunk(ptr %p) {
  %v = load i32, ptr %p
  fence syncscope("singlethread") seq_cst
  ret i32 %v
}

; Memory is read again after the barrier: two loads, the first kept in a
; local across the barrier.
; CHECK-LABEL: .method {{.*}}'reload'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  .locals (int32){{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  sub{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @reload(ptr %p) {
  %a = load i32, ptr %p
  fence syncscope("singlethread") seq_cst
  %b = load i32, ptr %p
  %d = sub i32 %a, %b
  ret i32 %d
}

; A store before the barrier is not forwarded to a load after it.
; CHECK-LABEL: .method {{.*}}'store_barrier_load'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @store_barrier_load(ptr %p) {
  store i32 1, ptr %p
  fence syncscope("singlethread") seq_cst
  %v = load i32, ptr %p
  ret i32 %v
}

; What clang -O2 emits for 'asm volatile("" ::: "memory")' between two stores
; (clang/test/CodeGen/MSIL/asm-barrier.c).
; CHECK-LABEL: .method {{.*}}'asm_barrier_clang'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.2{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @asm_barrier_clang(ptr noundef writeonly captures(none) initializes((0, 4)) %p, ptr noundef writeonly captures(none) initializes((0, 4)) %q) local_unnamed_addr #0 {
entry:
  store i32 1, ptr %p, align 4
  fence syncscope("singlethread") seq_cst
  tail call void @llvm.sideeffect()
  store i32 2, ptr %q, align 4
  ret void
}

; ---- System fences: Interlocked::MemoryBarrier ----

; CHECK-LABEL: .method {{.*}}'system_seq_cst'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  call void [mscorlib]System.Threading.Interlocked::MemoryBarrier(){{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.2{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'system_seq_cst'{{$}}
define void @system_seq_cst(ptr %p, ptr %q) {
  store i32 1, ptr %p
  fence seq_cst
  store i32 2, ptr %q
  ret void
}

; The call takes and returns nothing: a function with only a fence has an empty
; evaluation stack throughout.
; CHECK-LABEL: .method {{.*}}'fence_only'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
; CHECK-NEXT:  {{^}}  call void [mscorlib]System.Threading.Interlocked::MemoryBarrier(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'fence_only'{{$}}
define void @fence_only() {
  fence seq_cst
  ret void
}

; acquire, release and acq_rel system fences, and a fence in a named
; target scope, are full barriers too.
; CHECK-LABEL: .method {{.*}}'system_orderings'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
; CHECK-NEXT:  {{^}}  call void [mscorlib]System.Threading.Interlocked::MemoryBarrier(){{$}}
; CHECK-NEXT:  {{^}}  call void [mscorlib]System.Threading.Interlocked::MemoryBarrier(){{$}}
; CHECK-NEXT:  {{^}}  call void [mscorlib]System.Threading.Interlocked::MemoryBarrier(){{$}}
; CHECK-NEXT:  {{^}}  call void [mscorlib]System.Threading.Interlocked::MemoryBarrier(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'system_orderings'{{$}}
define void @system_orderings() {
  fence acquire
  fence release
  fence acq_rel
  fence syncscope("agent") seq_cst
  ret void
}

; A load before a system fence is not sunk past it either.
; CHECK-LABEL: .method {{.*}}'load_not_sunk_system'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  .locals (int32){{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i4{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  call void [mscorlib]System.Threading.Interlocked::MemoryBarrier(){{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @load_not_sunk_system(ptr %p) {
  %v = load i32, ptr %p
  fence seq_cst
  ret i32 %v
}

; Both kinds in one function, in program order.
; CHECK-LABEL: .method {{.*}}'both'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^[[:blank:]]+//MEMBARRIER$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.2{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  call void [mscorlib]System.Threading.Interlocked::MemoryBarrier(){{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.3{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @both(ptr %p) {
  store i32 1, ptr %p
  fence syncscope("singlethread") seq_cst
  store i32 2, ptr %p
  fence seq_cst
  store i32 3, ptr %p
  ret void
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn uwtable }

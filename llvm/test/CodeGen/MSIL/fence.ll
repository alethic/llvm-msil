; Fences (ImplementationPlan.md decision D36 as M24 amends it, 5.14.4 T1, T2
; and T6, 5.15.6; M7.a; plan 5.1; recon isel.md 5.9 'fence.il'): a fence with
; syncscope("singlethread") is a compiler barrier only, the form clang gives
; 'asm volatile("" ::: "memory")' and __atomic_signal_fence (plan section 12,
; row 'M1.d amended'). Every ordering of it is
; "call void '__msil_compiler_barrier'()", a call of the trailer definition of
; TrailerDefs.def's first row, which whole-program text defines once, after
; every other method of the module (5.14.4 T6):
;   .method assembly static void '__msil_compiler_barrier'() cil managed noinlining
; whose body is 'ret'. The call takes and returns nothing, so it changes no
; stack depth, and like any call it has side effects: RegStackify treats it as
; a write and never moves a load or a store across it, so a value loaded
; before it and used after it waits in a local, a load after it reads memory
; again, and a store before it is not forwarded to a load after it. The JIT
; does not inline a noinlining method, so the call stays a barrier for it too.
; The MEMBARRIER pseudo and its comment line are gone: no output names it.
; Every other fence (the system scope, the default, for any ordering, and any
; other named scope) is still the MEMORY_BARRIER record,
; 'call void [mscorlib]System.Threading.Interlocked::MemoryBarrier()', a BCL
; member reference that takes and returns nothing (never the __sync_synchronize
; libcall of the generic Expand action, and never a P/Invoke). It orders memory
; accesses in the same way.
;
; In a CIL object (-filetype=obj) the barrier is an ordinary undefined function
; (5.14.4 T1): each call is a MemberRef on [msil.extern]'<msil>Symbols', the
; symbol table has one UNDEFINED FUNCTION symbol '__msil_compiler_barrier' and
; the object defines no method of that name (msil-ld's '<trailer>' input
; defines it at link time; lld/test/MSIL checks that).
;
; 'asm_barrier_clang' is the IR that clang -O2 emits for
; clang/test/CodeGen/MSIL/asm-barrier.c (a single-thread fence followed by a
; call of llvm.sideeffect, which emits nothing); clang's tests have no ilasm,
; so this file assembles that shape.
;
; Every function is straight-line code on arguments, so the CIL is the same at
; -O0 and -O2, on the two x86_64 triples and on aarch64-unknown-linux-gnu-msil.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=__sync_synchronize --implicit-check-not=pinvokeimpl \
; DEFINE:   --implicit-check-not=localloc --implicit-check-not="LLVM ERROR" --implicit-check-not=MEMBARRIER \
; DEFINE:   --implicit-check-not="'__msil_compiler_barrier'() cil"
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; RUN: llc -mtriple=aarch64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s | %{check}
; The verbose output (the default) has nine calls and one definition, after
; the last function's '-- End function' comment, and names the barrier nowhere
; else.
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefix=CALLS --implicit-check-not=__msil_compiler_barrier \
; RUN:     --implicit-check-not=MEMBARRIER
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefix=CALLS --implicit-check-not=__msil_compiler_barrier \
; RUN:     --implicit-check-not=MEMBARRIER
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=MIR --implicit-check-not=MEMBARRIER
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | FileCheck %s --check-prefix=MIR --implicit-check-not=MEMBARRIER
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}
; RUN: %if ilasm %{ llc -mtriple=aarch64-unknown-linux-gnu-msil < %s -o %t.a64.il \
; RUN:   && %ilasm-verify -output=%t.a64.dll %t.a64.il %}

; CIL objects, on the three triples at -O2 and -O0: the symbol table, and the
; canonical listing in table order.
; DEFINE: %{obj} = FileCheck %s --check-prefix=OBJ --implicit-check-not=__msil_compiler_barrier
; DEFINE: %{objc} = FileCheck %s --check-prefix=OBJC --implicit-check-not=__msil_compiler_barrier
; DEFINE: %{objm} = FileCheck %s --check-prefix=OBJM --implicit-check-not=MemoryBarrier
; RUN: %if llvm-cil-dump %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs \
; RUN:   < %s -o %t.lnx.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ llc -mtriple=x86_64-pc-windows-msvc-msil -filetype=obj -verify-machineinstrs \
; RUN:   < %s -o %t.win.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ llc -mtriple=aarch64-unknown-linux-gnu-msil -filetype=obj -verify-machineinstrs \
; RUN:   < %s -o %t.a64.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -filetype=obj -verify-machineinstrs \
; RUN:   < %s -o %t.lnx.o0.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -filetype=obj -verify-machineinstrs \
; RUN:   < %s -o %t.win.o0.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ llc -mtriple=aarch64-unknown-linux-gnu-msil -O0 -filetype=obj -verify-machineinstrs \
; RUN:   < %s -o %t.a64.o0.o 2>&1 | count 0 %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t.lnx.o | %{obj} %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t.win.o | %{obj} %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t.a64.o | %{obj} %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t.lnx.o0.o | %{obj} %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t.win.o0.o | %{obj} %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --link-info %t.a64.o0.o | %{obj} %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --canonical --order=decl %t.lnx.o | %{objc} %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --canonical --order=decl %t.win.o0.o | %{objc} %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --canonical --order=decl %t.a64.o | %{objc} %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --canonical --order=decl %t.lnx.o | %{objm} %}
; RUN: %if llvm-cil-dump %{ llvm-cil-dump --canonical --order=decl %t.win.o0.o | %{objm} %}

source_filename = "fence.c"

declare void @llvm.sideeffect()

; CALLS-COUNT-9: {{^}}  call void '__msil_compiler_barrier'(){{$}}
; CALLS:         {{^}}                                        // -- End function{{$}}
; CALLS-NEXT:    {{^}}.method assembly static void '__msil_compiler_barrier'() cil managed noinlining{{$}}
; CALLS-NEXT:    {{^\{$}}
; CALLS-NEXT:    {{^}}  .maxstack 0{{$}}
; CALLS-NEXT:    {{^}}  ret{{$}}
; CALLS-NEXT:    {{^}}} // end of method '__msil_compiler_barrier'{{$}}
; CALLS-NOT:     {{.}}

; MIR-LABEL:   name: singlethread_seq_cst{{$}}
; MIR:         {{^ +}}CALL &__msil_compiler_barrier, implicit-def $arguments{{$}}
; MIR-NOT:     MEMORY_BARRIER
; MIR-LABEL:   name: singlethread_orderings{{$}}
; MIR-COUNT-3: {{^ +}}CALL &__msil_compiler_barrier, implicit-def $arguments{{$}}
; MIR-NEXT:    {{^ +}}RETURN
; MIR-LABEL:   name: system_seq_cst{{$}}
; MIR:         {{^ +}}MEMORY_BARRIER
; MIR-NOT:     __msil_compiler_barrier
; MIR-LABEL:   name: fence_only{{$}}

; The definitions are in program order and the barrier is the last symbol,
; UNDEFINED, with no method token.
; OBJ:      {{^}}LinkInfo v1.0 triple={{.*}}
; OBJ-NEXT: {{^}}Symbol 0 FUNCTION GLOBAL DEFAULT 'singlethread_seq_cst' method=0x06000001{{$}}
; OBJ:      {{^}}Symbol 10 FUNCTION GLOBAL DEFAULT 'both' method=0x0600000B{{$}}
; OBJ-NEXT: {{^}}Symbol 11 FUNCTION GLOBAL DEFAULT UNDEFINED '__msil_compiler_barrier'{{$}}
; OBJ-NOT:  {{.}}
; Nine calls of a MemberRef on '<msil>Symbols' and eight of the BCL member; no
; method is named __msil_compiler_barrier.
; OBJC:          {{^}}.class '<Module>' flags=0x00000000{{$}}
; OBJC-NEXT:     {{^}}  .method 'singlethread_seq_cst' void(native int, native int) flags=0x0016 impl=0x0000{{$}}
; OBJC-COUNT-9:  {{^}}    L{{[0-9]+}}: call void() [msil.extern]<msil>Symbols::'__msil_compiler_barrier'{{$}}
; OBJM-COUNT-8:  {{^}}    L{{[0-9]+}}: call void() [msil.bcl]System.Threading.Interlocked::'MemoryBarrier'{{$}}

; ---- Single-thread fences: compiler barriers ----

; CHECK-LABEL: .method {{.*}}'singlethread_seq_cst'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  stind.i4{{$}}
; CHECK-NEXT:  {{^}}  call void '__msil_compiler_barrier'(){{$}}
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
; CHECK-NEXT:  {{^}}  call void '__msil_compiler_barrier'(){{$}}
; CHECK-NEXT:  {{^}}  call void '__msil_compiler_barrier'(){{$}}
; CHECK-NEXT:  {{^}}  call void '__msil_compiler_barrier'(){{$}}
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
; CHECK-NEXT:  {{^}}  call void '__msil_compiler_barrier'(){{$}}
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
; CHECK-NEXT:  {{^}}  call void '__msil_compiler_barrier'(){{$}}
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
; CHECK-NEXT:  {{^}}  call void '__msil_compiler_barrier'(){{$}}
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
; CHECK-NEXT:  {{^}}  call void '__msil_compiler_barrier'(){{$}}
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
; CHECK-NEXT:  {{^}}  call void '__msil_compiler_barrier'(){{$}}
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

; The one definition of the barrier comes after every other method and ends
; the module.
; CHECK-LABEL: .method assembly static void '__msil_compiler_barrier'() cil managed noinlining{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '__msil_compiler_barrier'{{$}}
; CHECK-NOT:   {{.}}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn uwtable }

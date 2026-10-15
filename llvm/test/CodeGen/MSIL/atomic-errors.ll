; Atomic loads, stores, read-modify-write operations and compare-exchanges stay
; unsupported in Phase 1 (ImplementationPlan.md M7.a 'Atomic loads/stores stay
; unsupported (max atomic size 0)' and decision D36, until phase 3; plan
; section 12 rows 'fix-loop ownership: `llvm/test/CodeGen/MSIL/atomic-errors.ll`
; -> fix-4' (M7) and '... -> M8.d' (M8); plan section 2 'Diagnostics leave
; well-formed output'). The largest atomic size MSIL supports is 0 bits, so
; AtomicExpand turns every atomic instruction into a call of an __atomic_*
; library function before instruction selection: the sized helper
; (__atomic_load_4, __atomic_store_4, __atomic_fetch_add_4,
; __atomic_compare_exchange_4, __atomic_load_8) when the access is naturally
; aligned, otherwise the generic __atomic_load(size, ptr, ret, order). The
; memory order is passed as its C ABI value (seq_cst 5, release 3, acquire 2).
; The compare-exchange helper takes the expected value, and the generic load
; returns the loaded value, through a stack temporary, an ordinary frame object
; since M7.b. The helpers are only declared, so the end of the module reports
; "call to external function '<name>' needs P/Invoke support (M8)" once per
; helper, in the order of first use (M4.c; decision D14 keeps the '__' library
; calls, the __atomic_* helpers included, errors until phase 3).
;
; Every error is checked with its whole text, in function order, and no other
; error or warning is allowed (--implicit-check-not), so a doubled, a missing
; or a different diagnostic fails; llc exits with 1 and prints neither
; 'LLVM ERROR' nor a crash report. Each function is diagnosed at -O0 and -O2
; on both triples.
;
; llc deletes its -o file after an error, so the IL is taken from stdout, at
; -O0 and -O2 on both triples. Each helper call gets the atomic operand's
; address (ldarg.0) as an argument, and no ldind or stind reads or writes the
; atomic operand itself: the only ldind and stind access the frame temporary
; (the frame base that localloc leaves in local 0, plus one offset) of the
; compare-exchange and of the generic load. At -O0 the order constant of those
; two calls passes through a local. A raised maximum atomic size would either
; stop instruction selection at an atomic node or, with ATOMIC_LOAD and
; ATOMIC_STORE patterns, select plain ldind and stind; both fail here. As in
; call-errors.ll (part AVAIL), whose calls of declared functions are built too,
; the IL is not assembled: ilasm rejects every call of a helper the module does
; not define ('unresolved global member ref').
;
; Later owner (section 12): M8.d updates the expected diagnostics when M8
; replaces the P/Invoke text.

; DEFINE: %{diag} = FileCheck %s --check-prefix=ERR --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not=error: --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %s -o /dev/null 2>&1 | %{diag}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %s -o /dev/null 2>&1 \
; RUN:   | %{diag}
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %s -o /dev/null 2>&1 \
; RUN:   | %{diag}
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   > %t.lnx.o2.il
; RUN: FileCheck %s --check-prefix=IL --input-file=%t.lnx.o2.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   > %t.win.o2.il
; RUN: FileCheck %s --check-prefix=IL --input-file=%t.win.o2.il
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   > %t.lnx.o0.il
; RUN: FileCheck %s --check-prefix=IL --input-file=%t.lnx.o0.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   > %t.win.o0.il
; RUN: FileCheck %s --check-prefix=IL --input-file=%t.win.o0.il

source_filename = "atomic-errors.c"

; ERR: {{^}}error: call to external function '__atomic_load_4' needs P/Invoke support (M8){{$}}
; IL-LABEL: .method {{.*}}'aload'(
; IL-SAME:  native int) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 2{{$}}
; IL-NEXT:  {{^}}  ldarg.0{{$}}
; IL-NEXT:  {{^}}  ldc.i4.5{{$}}
; IL-NEXT:  {{^}}  call int32 '__atomic_load_4'(native int, int32){{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'aload'{{$}}
define i32 @aload(ptr %p) {
  %v = load atomic i32, ptr %p seq_cst, align 4
  ret i32 %v
}

; ERR: {{^}}error: call to external function '__atomic_store_4' needs P/Invoke support (M8){{$}}
; IL-LABEL: .method {{.*}}'astore'(
; IL-SAME:  native int, int32) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 3{{$}}
; IL-NEXT:  {{^}}  ldarg.0{{$}}
; IL-NEXT:  {{^}}  ldarg.1{{$}}
; IL-NEXT:  {{^}}  ldc.i4.3{{$}}
; IL-NEXT:  {{^}}  call void '__atomic_store_4'(native int, int32, int32){{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'astore'{{$}}
define void @astore(ptr %p, i32 %v) {
  store atomic i32 %v, ptr %p release, align 4
  ret void
}

; ERR: {{^}}error: call to external function '__atomic_fetch_add_4' needs P/Invoke support (M8){{$}}
; IL-LABEL: .method {{.*}}'armw'(
; IL-SAME:  native int, int32) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 3{{$}}
; IL-NEXT:  {{^}}  ldarg.0{{$}}
; IL-NEXT:  {{^}}  ldarg.1{{$}}
; IL-NEXT:  {{^}}  ldc.i4.5{{$}}
; IL-NEXT:  {{^}}  call int32 '__atomic_fetch_add_4'(native int, int32, int32){{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'armw'{{$}}
define i32 @armw(ptr %p, i32 %v) {
  %o = atomicrmw add ptr %p, i32 %v seq_cst
  ret i32 %o
}

; The expected value goes to the frame temporary, whose address is the second
; argument; the old value is read back from it after the call.
; ERR: {{^}}error: call to external function '__atomic_compare_exchange_4' needs P/Invoke support (M8){{$}}
; IL-LABEL: .method {{.*}}'acas'(
; IL-SAME:  native int, int32, int32) cil managed{{$}}
; IL:       {{^}}  localloc{{$}}
; IL-NEXT:  {{^}}  stloc.0{{$}}
; IL-NOT:   {{ldind|stind}}
; IL:       {{^}}  ldloc.0{{$}}
; IL-NEXT:  {{^}}  ldc.i4 [[CAS:[0-9]+]]{{$}}
; IL-NEXT:  {{^}}  conv.i{{$}}
; IL-NEXT:  {{^}}  add{{$}}
; IL-NEXT:  {{^}}  ldarg.1{{$}}
; IL-NEXT:  {{^}}  stind.i4{{$}}
; IL-NOT:   {{ldind|stind}}
; IL:       {{^}}  ldarg.0{{$}}
; IL-NEXT:  {{^}}  ldloc.0{{$}}
; IL-NEXT:  {{^}}  ldc.i4 [[CAS]]{{$}}
; IL-NEXT:  {{^}}  conv.i{{$}}
; IL-NEXT:  {{^}}  add{{$}}
; IL-NEXT:  {{^}}  ldarg.2{{$}}
; IL-NEXT:  {{^}}  {{ldc.i4.5|ldloc.1}}{{$}}
; IL-NEXT:  {{^}}  {{ldc.i4.5|ldloc.1}}{{$}}
; IL-NEXT:  {{^}}  call int32 '__atomic_compare_exchange_4'(native int, native int, int32, int32, int32){{$}}
; IL-NEXT:  {{^}}  pop{{$}}
; IL-NEXT:  {{^}}  ldloc.0{{$}}
; IL-NEXT:  {{^}}  ldc.i4 [[CAS]]{{$}}
; IL-NEXT:  {{^}}  conv.i{{$}}
; IL-NEXT:  {{^}}  add{{$}}
; IL-NEXT:  {{^}}  ldind.i4{{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'acas'{{$}}
define i32 @acas(ptr %p, i32 %e, i32 %n) {
  %r = cmpxchg ptr %p, i32 %e, i32 %n seq_cst seq_cst
  %v = extractvalue { i32, i1 } %r, 0
  ret i32 %v
}

; Under-aligned: the generic helper gets the size (native int 4), the operand,
; the address of the frame temporary that receives the value, and the order;
; the value is then read from the temporary.
; ERR: {{^}}error: call to external function '__atomic_load' needs P/Invoke support (M8){{$}}
; IL-LABEL: .method {{.*}}'aload_unaligned'(
; IL-SAME:  native int) cil managed{{$}}
; IL:       {{^}}  localloc{{$}}
; IL-NEXT:  {{^}}  stloc.0{{$}}
; IL-NOT:   {{ldind|stind}}
; IL:       {{^}}  ldc.i4 4{{$}}
; IL-NEXT:  {{^}}  conv.i{{$}}
; IL-NEXT:  {{^}}  ldarg.0{{$}}
; IL-NEXT:  {{^}}  ldloc.0{{$}}
; IL-NEXT:  {{^}}  ldc.i4 [[RES:[0-9]+]]{{$}}
; IL-NEXT:  {{^}}  conv.i{{$}}
; IL-NEXT:  {{^}}  add{{$}}
; IL-NEXT:  {{^}}  {{ldc.i4.5|ldloc.1}}{{$}}
; IL-NEXT:  {{^}}  call void '__atomic_load'(native int, native int, native int, int32){{$}}
; IL-NEXT:  {{^}}  ldloc.0{{$}}
; IL-NEXT:  {{^}}  ldc.i4 [[RES]]{{$}}
; IL-NEXT:  {{^}}  conv.i{{$}}
; IL-NEXT:  {{^}}  add{{$}}
; IL-NEXT:  {{^}}  ldind.i4{{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'aload_unaligned'{{$}}
define i32 @aload_unaligned(ptr %p) {
  %v = load atomic i32, ptr %p seq_cst, align 1
  ret i32 %v
}

; ERR: {{^}}error: call to external function '__atomic_load_8' needs P/Invoke support (M8){{$}}
; IL-LABEL: .method {{.*}}'aload_i64'(
; IL-SAME:  native int) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 2{{$}}
; IL-NEXT:  {{^}}  ldarg.0{{$}}
; IL-NEXT:  {{^}}  ldc.i4.2{{$}}
; IL-NEXT:  {{^}}  call native int '__atomic_load_8'(native int, int32){{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'aload_i64'{{$}}
define i64 @aload_i64(ptr %p) {
  %v = load atomic i64, ptr %p acquire, align 8
  ret i64 %v
}

; The module goes on after the errors.
; IL-LABEL: .method {{.*}}'after'(
; IL-SAME:  int32) cil managed{{$}}
; IL-NEXT:  {{^\{$}}
; IL-NEXT:  {{^}}  .maxstack 2{{$}}
; IL-NEXT:  {{^}}  ldarg.0{{$}}
; IL-NEXT:  {{^}}  ldc.i4.1{{$}}
; IL-NEXT:  {{^}}  add{{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'after'{{$}}
; IL-NOT:   {{^}}.method
define i32 @after(i32 %a) {
  %r = add i32 %a, 1
  ret i32 %r
}

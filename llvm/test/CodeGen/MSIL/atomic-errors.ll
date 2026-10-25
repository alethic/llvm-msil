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
; since M7.b. AtomicExpand declares the helpers as functions and calls them
; directly, so they are undefined functions of the module, and none of them is
; a Phase-1 import (decision D14 keeps the '__' library functions, the
; __atomic_* helpers included, errors until phase 3): the end of the module
; reports each helper once (M8.c),
;   undefined function '<name>' is not defined in this module and is not a
;   Phase-1 import (puts, putchar, the C math library, msil-import-module);
;   Phase 1 has no linker
; and declares no P/Invoke for it.
;
; Every error is checked with its whole text; the errors are matched as a
; group (their order is not pinned) and a second FileCheck run counts them and
; bans any other error, warning, 'LLVM ERROR' or crash report, so a doubled, a
; missing or a different diagnostic fails; llc exits with 1. Each function is
; diagnosed at -O0 and -O2 on both triples.
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
; call-errors.ll (part AVAIL), whose calls of undefined functions are built
; too, the IL is not assembled: ilasm rejects every call of a helper the module
; does not define ('unresolved global member ref'), and no pinvokeimpl
; declares one.

; DEFINE: %{diag} = FileCheck %s --check-prefix=ERR --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not=warning:
; DEFINE: %{count} = FileCheck %s --check-prefix=NERR --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not="PLEASE submit" --implicit-check-not=warning:
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -verify-machineinstrs < %s -o /dev/null 2> %t.lnx.o2.err
; RUN: %{diag} --input-file=%t.lnx.o2.err
; RUN: %{count} --input-file=%t.lnx.o2.err
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -verify-machineinstrs < %s -o /dev/null 2> %t.win.o2.err
; RUN: %{diag} --input-file=%t.win.o2.err
; RUN: %{count} --input-file=%t.win.o2.err
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -verify-machineinstrs < %s -o /dev/null 2> %t.lnx.o0.err
; RUN: %{diag} --input-file=%t.lnx.o0.err
; RUN: %{count} --input-file=%t.lnx.o0.err
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs < %s -o /dev/null 2> %t.win.o0.err
; RUN: %{diag} --input-file=%t.win.o0.err
; RUN: %{count} --input-file=%t.win.o0.err
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   > %t.lnx.o2.il
; RUN: FileCheck %s --check-prefix=IL --implicit-check-not=pinvokeimpl --input-file=%t.lnx.o2.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   > %t.win.o2.il
; RUN: FileCheck %s --check-prefix=IL --implicit-check-not=pinvokeimpl --input-file=%t.win.o2.il
; RUN: not llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   > %t.lnx.o0.il
; RUN: FileCheck %s --check-prefix=IL --implicit-check-not=pinvokeimpl --input-file=%t.lnx.o0.il
; RUN: not llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   > %t.win.o0.il
; RUN: FileCheck %s --check-prefix=IL --implicit-check-not=pinvokeimpl --input-file=%t.win.o0.il

source_filename = "atomic-errors.c"

; NERR-COUNT-6: {{^}}error:
; NERR-NOT:     {{^}}error:

; ERR-DAG: {{^}}error: {{.*}}undefined function '__atomic_load_4' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
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

; ERR-DAG: {{^}}error: {{.*}}undefined function '__atomic_store_4' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
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

; ERR-DAG: {{^}}error: {{.*}}undefined function '__atomic_fetch_add_4' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
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
; ERR-DAG: {{^}}error: {{.*}}undefined function '__atomic_compare_exchange_4' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; IL-LABEL: .method {{.*}}'acas'(
; IL-SAME:  native int, int32, int32) cil managed{{$}}
; IL:       {{^}}  localloc{{$}}
; IL-NEXT:  {{^}}  stloc.0{{$}}
; IL-NOT:   {{ldind|stind}}
; IL:       {{^}}  ldloc.0{{$}}
; IL-NEXT:  {{^}}  ldc.i4.s [[CAS:[0-9]+]]{{$}}
; IL-NEXT:  {{^}}  conv.i{{$}}
; IL-NEXT:  {{^}}  add{{$}}
; IL-NEXT:  {{^}}  ldarg.1{{$}}
; IL-NEXT:  {{^}}  stind.i4{{$}}
; IL-NOT:   {{ldind|stind}}
; IL:       {{^}}  ldarg.0{{$}}
; IL-NEXT:  {{^}}  ldloc.0{{$}}
; IL-NEXT:  {{^}}  ldc.i4.s [[CAS]]{{$}}
; IL-NEXT:  {{^}}  conv.i{{$}}
; IL-NEXT:  {{^}}  add{{$}}
; IL-NEXT:  {{^}}  ldarg.2{{$}}
; IL-NEXT:  {{^}}  {{ldc.i4.5|ldloc.1}}{{$}}
; IL-NEXT:  {{^}}  {{ldc.i4.5|ldloc.1}}{{$}}
; IL-NEXT:  {{^}}  call int32 '__atomic_compare_exchange_4'(native int, native int, int32, int32, int32){{$}}
; IL-NEXT:  {{^}}  pop{{$}}
; IL-NEXT:  {{^}}  ldloc.0{{$}}
; IL-NEXT:  {{^}}  ldc.i4.s [[CAS]]{{$}}
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
; ERR-DAG: {{^}}error: {{.*}}undefined function '__atomic_load' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
; IL-LABEL: .method {{.*}}'aload_unaligned'(
; IL-SAME:  native int) cil managed{{$}}
; IL:       {{^}}  localloc{{$}}
; IL-NEXT:  {{^}}  stloc.0{{$}}
; IL-NOT:   {{ldind|stind}}
; IL:       {{^}}  ldc.i4.4{{$}}
; IL-NEXT:  {{^}}  conv.i{{$}}
; IL-NEXT:  {{^}}  ldarg.0{{$}}
; IL-NEXT:  {{^}}  ldloc.0{{$}}
; IL-NEXT:  {{^}}  ldc.i4.s [[RES:[0-9]+]]{{$}}
; IL-NEXT:  {{^}}  conv.i{{$}}
; IL-NEXT:  {{^}}  add{{$}}
; IL-NEXT:  {{^}}  {{ldc.i4.5|ldloc.1}}{{$}}
; IL-NEXT:  {{^}}  call void '__atomic_load'(native int, native int, native int, int32){{$}}
; IL-NEXT:  {{^}}  ldloc.0{{$}}
; IL-NEXT:  {{^}}  ldc.i4.s [[RES]]{{$}}
; IL-NEXT:  {{^}}  conv.i{{$}}
; IL-NEXT:  {{^}}  add{{$}}
; IL-NEXT:  {{^}}  ldind.i4{{$}}
; IL-NEXT:  {{^}}  ret{{$}}
; IL-NEXT:  {{^}}} // end of method 'aload_unaligned'{{$}}
define i32 @aload_unaligned(ptr %p) {
  %v = load atomic i32, ptr %p seq_cst, align 1
  ret i32 %v
}

; ERR-DAG: {{^}}error: {{.*}}undefined function '__atomic_load_8' is not defined in this module and is not a Phase-1 import (puts, putchar, the C math library, msil-import-module); Phase 1 has no linker{{$}}
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

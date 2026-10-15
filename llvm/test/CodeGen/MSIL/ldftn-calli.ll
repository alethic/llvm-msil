; Function pointers within the module: address fields + managed 'calli'
; (ImplementationPlan.md M9.a and M8.a, decision D18; plan 5.1 'Calls' as
; amended by section 12 'Phase-1 function addresses' (2026-10-15, M9);
; Design.md 4.5 and 12 'Function pointers within the module (ldftn + calli)';
; recon isel.md 3.4, 8.2-8.3 and asmprinter.md 5.3, 6.4). Every function whose
; address is used gets one address field
; '.field private static initonly native int '<msil>fn_<IL name>'', and
; <Module>::.cctor sets it once with 'ldftn <member-ref>' + 'stsfld'. The
; member-ref text is exactly the function's '.method' signature (result type,
; quoted name, parameter list) for public, internal and P/Invoke methods.
; Code takes the address with 'ldsfld native int '<msil>fn_<IL name>''
; wherever it is used: returned, compared, passed, stored or selected. Every
; address of one function reads the same field, so it is the same value at
; run time on every runtime (C11 6.5.9p6; on .NET Framework two 'ldftn' of
; one method compiled in different JIT states can differ), and different
; functions read different fields; 'ldftn' appears only in the .cctor, once
; per function (no other instruction is an 'ldftn'). A call through such a
; pointer is 'calli <ret>(<params>)', and its stand-alone signature agrees
; with the ldftn target's method signature: same result type and same
; parameter list, for void, narrow (int32), mixed and demoted (void, slot
; address first) signatures, and for a P/Invoke target, which a managed calli
; calls through its stub (cil-runtime e18). Each signature below is captured
; from the '.method' header and then required, unchanged, at the .cctor's
; ldftn and at the calli of that function.
;
; The module has no data segment, so the address fields follow the P/Invoke
; declarations in module order (add1, sub1, twice, noop, mixed, narrow, pair,
; puts), then one empty line, and the .cctor, which exists only for them,
; sets them first, in the same order.
;
; Straight-line code compiles to the same CIL at -O0 and -O2 (CHECK); the
; parts with constants or narrow values are exact at -O2 (O2) and checked for
; the address and the call at -O0 (O0).

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=tail. \
; DEFINE:   --implicit-check-not=unmanaged --implicit-check-not="LLVM ERROR" --implicit-check-not=error: \
; DEFINE:   --implicit-check-not="{{^ +ldftn }}"
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check} --check-prefixes=CHECK,O2,LNX
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check} --check-prefixes=CHECK,O2,WIN
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check} --check-prefixes=CHECK,O0,LNX
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check} --check-prefixes=CHECK,O0,WIN
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "ldftn-calli.c"

; ---- targets: their signatures are captured here ------------------------------

; CHECK: {{^}}.method public static [[ADD1_RET:int32]] 'add1'[[ADD1_PARAMS:\(int32\)]] cil managed{{$}}
define i32 @add1(i32 %x) {
  %r = add i32 %x, 1
  ret i32 %r
}

; CHECK: {{^}}.method public static [[SUB1_RET:int32]] 'sub1'[[SUB1_PARAMS:\(int32\)]] cil managed{{$}}
define i32 @sub1(i32 %x) {
  %r = sub i32 %x, 1
  ret i32 %r
}

; An internal target is an 'assembly' method; its ldftn token has no access.
; CHECK: {{^}}.method assembly static [[TWICE_RET:int32]] 'twice'[[TWICE_PARAMS:\(int32\)]] cil managed{{$}}
define internal i32 @twice(i32 %x) {
  %r = shl i32 %x, 1
  ret i32 %r
}

; CHECK: {{^}}.method public static [[NOOP_RET:void]] 'noop'[[NOOP_PARAMS:\(\)]] cil managed{{$}}
define void @noop() {
  ret void
}

; CHECK: {{^}}.method public static [[MIXED_RET:float64]] 'mixed'[[MIXED_PARAMS:\(int32, native int, float32, float64, native int\)]] cil managed{{$}}
define double @mixed(i32 %a, i64 %b, float %c, double %d, ptr %e) {
  ret double %d
}

; CHECK: {{^}}.method public static [[NARROW_RET:int32]] 'narrow'[[NARROW_PARAMS:\(int32, int32\)]] cil managed{{$}}
define signext i8 @narrow(i8 signext %c, i16 zeroext %s) {
  ret i8 %c
}

; A result that needs two values is returned through a slot whose address is
; the first parameter.
; CHECK: {{^}}.method public static [[PAIR_RET:void]] 'pair'[[PAIR_PARAMS:\(native int, native int\)]] cil managed{{$}}
define { i64, i64 } @pair(i64 %x) {
  %a = insertvalue { i64, i64 } poison, i64 %x, 0
  %b = insertvalue { i64, i64 } %a, i64 7, 1
  ret { i64, i64 } %b
}

; puts is a Phase-1 import (D14): it has its own address field, and its
; ldftn in the .cctor names the P/Invoke declaration at the end of the module.
declare i32 @puts(ptr)

; ---- function addresses: the function's address field -----------------------

; CHECK-LABEL: .method {{.*}}'addr_add1'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_add1'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'addr_add1'{{$}}
define ptr @addr_add1() {
  ret ptr @add1
}

; CHECK-LABEL: .method {{.*}}'addr_twice'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_twice'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @addr_twice() {
  ret ptr @twice
}

; CHECK-LABEL: .method {{.*}}'addr_noop'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_noop'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @addr_noop() {
  ret ptr @noop
}

; CHECK-LABEL: .method {{.*}}'addr_pair'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_pair'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @addr_pair() {
  ret ptr @pair
}

; CHECK-LABEL: .method {{.*}}'addr_puts'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_puts'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @addr_puts() {
  ret ptr @puts
}

; Identity: a pointer compares equal to the function's address, read from the
; same field as in addr_add1.
; CHECK-LABEL: .method {{.*}}'is_add1'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_add1'{{$}}
; CHECK-NEXT:  {{^}}  ceq{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @is_add1(ptr %p) {
  %c = icmp eq ptr %p, @add1
  %r = zext i1 %c to i32
  ret i32 %r
}

; ---- an address passed to a function that calls it --------------------------

; CHECK-LABEL: .method {{.*}}'apply'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli [[ADD1_RET]][[ADD1_PARAMS]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @apply(ptr %f, i32 %x) {
  %r = call i32 %f(i32 %x)
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'apply_add1'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_add1'{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'apply'(native int, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @apply_add1(i32 %x) {
  %r = call i32 @apply(ptr @add1, i32 %x)
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'apply_twice'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_twice'{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'apply'(native int, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @apply_twice(i32 %x) {
  %r = call i32 @apply(ptr @twice, i32 %x)
  ret i32 %r
}

; ---- an address and calli in one function -----------------------------------

; The callee is one of two functions, chosen by a select (a branch triangle,
; D34); both address fields are read, each function's own, and the call
; pushes the argument, then the chosen pointer.
; CHECK-LABEL: .method {{.*}}'pick'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-DAG:   {{^}}  ldsfld native int '<msil>fn_add1'{{$}}
; CHECK-DAG:   {{^}}  ldsfld native int '<msil>fn_sub1'{{$}}
; CHECK:       {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldloc.{{[0-9]}}{{$}}
; CHECK-NEXT:  {{^}}  calli [[ADD1_RET]][[ADD1_PARAMS]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'pick'{{$}}
define i32 @pick(i32 %c, i32 %x) {
  %b = icmp ne i32 %c, 0
  %f = select i1 %b, ptr @add1, ptr @sub1
  %r = call i32 %f(i32 %x)
  ret i32 %r
}

; The pointer is the result of a call; the argument is pushed before it.
; CHECK-LABEL: .method {{.*}}'call_returned'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call native int 'addr_add1'(){{$}}
; CHECK-NEXT:  {{^}}  calli [[ADD1_RET]][[ADD1_PARAMS]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_returned(i32 %x) {
  %f = call ptr @addr_add1()
  %r = call i32 %f(i32 %x)
  ret i32 %r
}

; The pointer is stored, read back (volatile, so it is not forwarded) and
; called.
; CHECK-LABEL: .method {{.*}}'store_load_call'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_add1'{{$}}
; CHECK-NEXT:  {{^}}  stind.i{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  volatile.{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  calli [[ADD1_RET]][[ADD1_PARAMS]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @store_load_call(ptr %slot, i32 %x) {
  store ptr @add1, ptr %slot
  %f = load volatile ptr, ptr %slot
  %r = call i32 %f(i32 %x)
  ret i32 %r
}

; ---- signature agreement for each kind of signature ------------------------------

; void().
; CHECK-LABEL: .method {{.*}}'call_noop_ptr'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli [[NOOP_RET]][[NOOP_PARAMS]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @call_noop_ptr(ptr %f) {
  call void %f()
  ret void
}

; CHECK-LABEL: .method {{.*}}'call_noop'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_noop'{{$}}
; CHECK-NEXT:  {{^}}  call void 'call_noop_ptr'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @call_noop() {
  call void @call_noop_ptr(ptr @noop)
  ret void
}

; Every parameter type.
; CHECK-LABEL: .method {{.*}}'call_mixed_ptr'(
; CHECK-SAME:  native int, int32, native int, float32, float64, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 6{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 4{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 5{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli [[MIXED_RET]][[MIXED_PARAMS]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @call_mixed_ptr(ptr %f, i32 %a, i64 %b, float %c, double %d, ptr %e) {
  %r = call double %f(i32 %a, i64 %b, float %c, double %d, ptr %e)
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'call_mixed'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 6{{$}}
; O0:          {{^}}  .locals (native int, native int, float32, native int){{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_mixed'{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  ldc.i4 2{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0x40400000){{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O2-NEXT:     {{^}}  ldc.i4 0{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  call float64 'call_mixed_ptr'(native int, int32, native int, float32, float64, native int){{$}}
; O0:          {{^}}  call float64 'call_mixed_ptr'(native int, int32, native int, float32, float64, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @call_mixed(double %d) {
  %r = call double @call_mixed_ptr(ptr @mixed, i32 1, i64 2, float 3.0, double %d, ptr null)
  ret double %r
}

; i8 and i16 are int32 at the ldftn target and at the calli.
; CHECK-LABEL: .method {{.*}}'call_narrow_ptr'(
; CHECK-SAME:  native int, int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  conv.i1{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  conv.u2{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O0:          {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli [[NARROW_RET]][[NARROW_PARAMS]]{{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_narrow_ptr'{{$}}
define i32 @call_narrow_ptr(ptr %f, i32 %a, i32 %b) {
  %c = trunc i32 %a to i8
  %s = trunc i32 %b to i16
  %r = call signext i8 %f(i8 signext %c, i16 zeroext %s)
  %x = sext i8 %r to i32
  ret i32 %x
}

; CHECK-LABEL: .method {{.*}}'call_narrow'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_narrow'{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'call_narrow_ptr'(native int, int32, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_narrow(i32 %a) {
  %r = call i32 @call_narrow_ptr(ptr @narrow, i32 %a, i32 %a)
  ret i32 %r
}

; A demoted { i64, i64 } result: the caller passes the address of a 16-byte
; frame slot first, exactly as the definition of 'pair' expects.
; CHECK-LABEL: .method {{.*}}'call_pair_ptr'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.s 16{{$}}
; CHECK-NEXT:  {{^}}  localloc{{$}}
; CHECK-NEXT:  {{^}}  stloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli [[PAIR_RET]][[PAIR_PARAMS]]{{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4 8{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @call_pair_ptr(ptr %f, i64 %x) {
  %r = call { i64, i64 } %f(i64 %x)
  %e = extractvalue { i64, i64 } %r, 1
  ret i64 %e
}

; CHECK-LABEL: .method {{.*}}'call_pair'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_pair'{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call native int 'call_pair_ptr'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @call_pair(i64 %x) {
  %r = call i64 @call_pair_ptr(ptr @pair, i64 %x)
  ret i64 %r
}

; A P/Invoke target: the managed calli has the stub's signature.
; CHECK-LABEL: .method {{.*}}'call_puts_ptr'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli int32(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_puts_ptr(ptr %f, ptr %s) {
  %r = call i32 %f(ptr %s)
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'call_puts'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_puts'{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call int32 'call_puts_ptr'(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_puts(ptr %s) {
  %r = call i32 @call_puts_ptr(ptr @puts, ptr %s)
  ret i32 %r
}

; ---- module trailer ---------------------------------------------------------

; The only P/Invoke is puts, with the signature its ldftn and calli used.
; CHECK:       {{^}}// ---- undefined external functions: P/Invoke ----{{$}}
; LNX-NEXT:    {{^}}.method public hidebysig static pinvokeimpl("libc" cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; WIN-NEXT:    {{^}}.method public hidebysig static pinvokeimpl("ucrtbase" cdecl) int32 'puts'(native int) cil managed preservesig {}{{$}}
; CHECK-EMPTY:

; No data segment: the address fields come next, one per used function in
; module order (each function once, however many times code reads it), then
; one empty line, with no header comment.
; CHECK-NEXT:  {{^}}.field private static initonly native int '<msil>fn_add1'{{$}}
; CHECK-NEXT:  {{^}}.field private static initonly native int '<msil>fn_sub1'{{$}}
; CHECK-NEXT:  {{^}}.field private static initonly native int '<msil>fn_twice'{{$}}
; CHECK-NEXT:  {{^}}.field private static initonly native int '<msil>fn_noop'{{$}}
; CHECK-NEXT:  {{^}}.field private static initonly native int '<msil>fn_mixed'{{$}}
; CHECK-NEXT:  {{^}}.field private static initonly native int '<msil>fn_narrow'{{$}}
; CHECK-NEXT:  {{^}}.field private static initonly native int '<msil>fn_pair'{{$}}
; CHECK-NEXT:  {{^}}.field private static initonly native int '<msil>fn_puts'{{$}}
; CHECK-EMPTY:

; The module has no data and no constructors; the .cctor exists for the
; fields and sets each one once, first and in the same order, with the ldftn
; of the method header's signature (and of the calli's): the only ldftn of
; the module.
; CHECK-NEXT:  {{^}}.method private hidebysig specialname rtspecialname static void .cctor() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldftn [[ADD1_RET]] 'add1'[[ADD1_PARAMS]]{{$}}
; CHECK-NEXT:  {{^}}  stsfld native int '<msil>fn_add1'{{$}}
; CHECK-NEXT:  {{^}}  ldftn [[SUB1_RET]] 'sub1'[[SUB1_PARAMS]]{{$}}
; CHECK-NEXT:  {{^}}  stsfld native int '<msil>fn_sub1'{{$}}
; CHECK-NEXT:  {{^}}  ldftn [[TWICE_RET]] 'twice'[[TWICE_PARAMS]]{{$}}
; CHECK-NEXT:  {{^}}  stsfld native int '<msil>fn_twice'{{$}}
; CHECK-NEXT:  {{^}}  ldftn [[NOOP_RET]] 'noop'[[NOOP_PARAMS]]{{$}}
; CHECK-NEXT:  {{^}}  stsfld native int '<msil>fn_noop'{{$}}
; CHECK-NEXT:  {{^}}  ldftn [[MIXED_RET]] 'mixed'[[MIXED_PARAMS]]{{$}}
; CHECK-NEXT:  {{^}}  stsfld native int '<msil>fn_mixed'{{$}}
; CHECK-NEXT:  {{^}}  ldftn [[NARROW_RET]] 'narrow'[[NARROW_PARAMS]]{{$}}
; CHECK-NEXT:  {{^}}  stsfld native int '<msil>fn_narrow'{{$}}
; CHECK-NEXT:  {{^}}  ldftn [[PAIR_RET]] 'pair'[[PAIR_PARAMS]]{{$}}
; CHECK-NEXT:  {{^}}  stsfld native int '<msil>fn_pair'{{$}}
; CHECK-NEXT:  {{^}}  ldftn int32 'puts'(native int){{$}}
; CHECK-NEXT:  {{^}}  stsfld native int '<msil>fn_puts'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method '.cctor'{{$}}
; CHECK-NOT:   pinvokeimpl
; CHECK-NOT:   <msil>fn_

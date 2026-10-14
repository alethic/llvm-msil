; Direct calls (ImplementationPlan.md M4.a/M4.c, decision D18; plan 5.1
; 'Calls'; recon isel.md 2.4, 5.8, 8 and asmprinter.md 5.3, 6.4), ported from
; llvm/test/CodeGen/WebAssembly/call.ll (direct cases; indirect calls are M9):
; MSILISD::CALL is split into CALL_PARAMS/CALL_RESULTS at ISel and recombined
; into one CALL by the custom inserter, which prints as 'call' followed by a
; member reference whose text is exactly the callee's '.method' signature
; (result type, quoted name, parameter list, no access keyword), for public
; and internal callees, before and after their definition, and for a call of
; the function itself. The arguments are pushed in order, a result is left on
; the evaluation stack, an unused result is popped, and a void call pushes
; nothing. Tail calls are ordinary calls (no 'tail.' prefix in Phase 1), and
; the fastcc and coldcc conventions call like C. Every callee is defined in
; the module: calls to declarations need P/Invoke (M8, call-errors.ll).
;
; Calls whose arguments are arguments of the caller compile to the same CIL at
; -O0 and -O2 (CHECK); calls with constant arguments are exact at -O2 (O2).
; The MIR lines check the CALL that the custom inserter builds (finalize-isel)
; against the operand layout of plan 5.4: the defs first, then the callee
; (operand index = number of defs), then the arguments in call order, bound
; to the caller's ARGUMENT_<RC> values; a void call has no def. No
; CALL_PARAMS or CALL_RESULTS pseudo is left.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=tail. \
; DEFINE:   --implicit-check-not=calli --implicit-check-not=jmp
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; The same output with call-site info: clang sets EmitCallSiteInfo for every
; -O1 and higher compile with -g whose target supports debug entry values,
; which every MSIL triple does because it keeps the x86_64 or aarch64 arch, so
; the call pseudos (CALL_PARAMS/CALL_RESULTS) must carry the call-site info
; over to the final CALL (MachineFunction otherwise asserts 'Call site info was
; not updated!' when the custom inserter erases them).
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O2 -emit-call-site-info -verify-machineinstrs \
; RUN:   -asm-verbose=false < %s | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O2 -emit-call-site-info -verify-machineinstrs \
; RUN:   -asm-verbose=false < %s | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -emit-call-site-info -verify-machineinstrs \
; RUN:   -asm-verbose=false < %s | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -emit-call-site-info -verify-machineinstrs \
; RUN:   -asm-verbose=false < %s | %{check} --check-prefixes=CHECK,O0
; DEFINE: %{mir} = FileCheck %s --check-prefix=MIR --implicit-check-not=CALL_PARAMS \
; DEFINE:   --implicit-check-not=CALL_RESULTS
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O2 -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | %{mir}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O2 -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | %{mir}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | %{mir}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "call.c"

; ---- callees: their signatures are captured and must match every call -----

; CHECK: {{^}}.method public static [[I32_NULLARY:int32 'i32_nullary'\(\)]] cil managed{{$}}
define i32 @i32_nullary() {
  ret i32 1
}
; CHECK: {{^}}.method public static [[I64_NULLARY:native int 'i64_nullary'\(\)]] cil managed{{$}}
define i64 @i64_nullary() {
  ret i64 2
}
; CHECK: {{^}}.method public static [[FLOAT_NULLARY:float32 'float_nullary'\(\)]] cil managed{{$}}
define float @float_nullary() {
  ret float 3.0
}
; CHECK: {{^}}.method public static [[DOUBLE_NULLARY:float64 'double_nullary'\(\)]] cil managed{{$}}
define double @double_nullary() {
  ret double 4.0
}
; CHECK: {{^}}.method public static [[PTR_NULLARY:native int 'ptr_nullary'\(\)]] cil managed{{$}}
define ptr @ptr_nullary() {
  ret ptr null
}
; CHECK: {{^}}.method public static [[VOID_NULLARY:void 'void_nullary'\(\)]] cil managed{{$}}
define void @void_nullary() {
  ret void
}
; CHECK: {{^}}.method public static [[I32_UNARY:int32 'i32_unary'\(int32\)]] cil managed{{$}}
define i32 @i32_unary(i32 %a) {
  ret i32 %a
}
; CHECK: {{^}}.method public static [[I32_BINARY:int32 'i32_binary'\(int32, int32\)]] cil managed{{$}}
define i32 @i32_binary(i32 %a, i32 %b) {
  ret i32 %b
}
; CHECK: {{^}}.method public static [[VOID_MIXED:void 'void_mixed'\(int32, native int, float32, float64, native int\)]] cil managed{{$}}
define void @void_mixed(i32 %a, i64 %b, float %c, double %d, ptr %e) {
  ret void
}
; An internal callee is an 'assembly' method; the call token has no access.
; CHECK: {{^}}.method assembly static [[INTERNAL:native int 'internal_callee'\(native int, native int\)]] cil managed{{$}}
define internal i64 @internal_callee(i64 %a, ptr %p) {
  ret i64 %a
}
; CHECK: {{^}}.method public static [[FASTCC:int32 'fastcc_callee'\(int32\)]] cil managed{{$}}
define fastcc i32 @fastcc_callee(i32 %a) {
  ret i32 %a
}
; CHECK: {{^}}.method public static [[COLDCC:void 'coldcc_callee'\(\)]] cil managed{{$}}
define coldcc void @coldcc_callee() {
  ret void
}

; ---- calls ------------------------------------------------------------------

; CHECK-LABEL: .method {{.*}}'call_i32_nullary'(
; CHECK-SAME:  ) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  call [[I32_NULLARY]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_i32_nullary'{{$}}
define i32 @call_i32_nullary() {
  %r = call i32 @i32_nullary()
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'call_i64_nullary'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  call [[I64_NULLARY]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @call_i64_nullary() {
  %r = call i64 @i64_nullary()
  ret i64 %r
}

; CHECK-LABEL: .method {{.*}}'call_float_nullary'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  call [[FLOAT_NULLARY]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define float @call_float_nullary() {
  %r = call float @float_nullary()
  ret float %r
}

; CHECK-LABEL: .method {{.*}}'call_double_nullary'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  call [[DOUBLE_NULLARY]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @call_double_nullary() {
  %r = call double @double_nullary()
  ret double %r
}

; CHECK-LABEL: .method {{.*}}'call_ptr_nullary'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  call [[PTR_NULLARY]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @call_ptr_nullary() {
  %r = call ptr @ptr_nullary()
  ret ptr %r
}

; A void call of a void function pushes and pops nothing; its CALL has no def.
; MIR-LABEL: name: call_void_nullary
; MIR:       {{^ *}}CALL @void_nullary, implicit-def {{(dead )?}}$arguments{{$}}
; MIR-NEXT:  {{^ *}}RETURN implicit-def dead $arguments{{$}}
; CHECK-LABEL: .method {{.*}}'call_void_nullary'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
; CHECK-NEXT:  {{^}}  call [[VOID_NULLARY]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_void_nullary'{{$}}
define void @call_void_nullary() {
  call void @void_nullary()
  ret void
}

; CHECK-LABEL: .method {{.*}}'call_i32_unary'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call [[I32_UNARY]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_i32_unary(i32 %a) {
  %r = call i32 @i32_unary(i32 %a)
  ret i32 %r
}

; MIR-LABEL: name: call_i32_binary
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0, implicit $arguments
; MIR-DAG:   [[B:%[0-9]+]]:i32 = ARGUMENT_I32 1, implicit $arguments
; MIR:       {{^ *}}[[R:%[0-9]+]]:i32 = CALL @i32_binary, [[A]], [[B]], implicit-def {{(dead )?}}$arguments{{$}}
; MIR-NEXT:  {{^ *}}RETURN [[R]], implicit-def dead $arguments{{$}}
; CHECK-LABEL: .method {{.*}}'call_i32_binary'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call [[I32_BINARY]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_i32_binary(i32 %a, i32 %b) {
  %r = call i32 @i32_binary(i32 %a, i32 %b)
  ret i32 %r
}

; MIR-LABEL: name: call_void_mixed
; MIR-DAG:   [[A0:%[0-9]+]]:i32 = ARGUMENT_I32 0, implicit $arguments
; MIR-DAG:   [[A1:%[0-9]+]]:i64 = ARGUMENT_I64 1, implicit $arguments
; MIR-DAG:   [[A2:%[0-9]+]]:f32 = ARGUMENT_F32 2, implicit $arguments
; MIR-DAG:   [[A3:%[0-9]+]]:f64 = ARGUMENT_F64 3, implicit $arguments
; MIR-DAG:   [[A4:%[0-9]+]]:i64 = ARGUMENT_I64 4, implicit $arguments
; MIR:       {{^ *}}CALL @void_mixed, [[A0]], [[A1]], [[A2]], [[A3]], [[A4]], implicit-def {{(dead )?}}$arguments{{$}}
; MIR-NEXT:  {{^ *}}RETURN implicit-def dead $arguments{{$}}
; CHECK-LABEL: .method {{.*}}'call_void_mixed'(
; CHECK-SAME:  int32, native int, float32, float64, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 5{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 4{{$}}
; CHECK-NEXT:  {{^}}  call [[VOID_MIXED]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @call_void_mixed(i32 %a, i64 %b, float %c, double %d, ptr %e) {
  call void @void_mixed(i32 %a, i64 %b, float %c, double %d, ptr %e)
  ret void
}

; CHECK-LABEL: .method {{.*}}'call_internal'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call [[INTERNAL]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @call_internal(i64 %a, ptr %p) {
  %r = call i64 @internal_callee(i64 %a, ptr %p)
  ret i64 %r
}

; Results that are not used are popped, whatever their type. The CALL still
; defines the result.
; MIR-LABEL: name: drop_i32_result
; MIR:       {{^ *}}{{(dead )?}}%{{[0-9]+}}:i32 = CALL @i32_nullary, implicit-def {{(dead )?}}$arguments{{$}}
; MIR-NEXT:  {{^ *}}RETURN implicit-def dead $arguments{{$}}
; CHECK-LABEL: .method {{.*}}'drop_i32_result'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  call [[I32_NULLARY]]{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'drop_i32_result'{{$}}
define void @drop_i32_result() {
  %r = call i32 @i32_nullary()
  ret void
}

; CHECK-LABEL: .method {{.*}}'drop_i64_result'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  call [[I64_NULLARY]]{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @drop_i64_result() {
  %r = call i64 @i64_nullary()
  ret void
}

; CHECK-LABEL: .method {{.*}}'drop_double_result'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  call [[DOUBLE_NULLARY]]{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @drop_double_result() {
  %r = call double @double_nullary()
  ret void
}

; Two calls in a row, the result of the first is dropped and the second one is
; returned.
; CHECK-LABEL: .method {{.*}}'drop_then_return'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  call [[I32_NULLARY]]{{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call [[I32_UNARY]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @drop_then_return(i32 %a) {
  %x = call i32 @i32_nullary()
  %y = call i32 @i32_unary(i32 %a)
  ret i32 %y
}

; The result of one call is an argument of the next.
; MIR-LABEL: name: call_nested
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0, implicit $arguments
; MIR-DAG:   [[B:%[0-9]+]]:i32 = ARGUMENT_I32 1, implicit $arguments
; MIR:       {{^ *}}[[X:%[0-9]+]]:i32 = CALL @i32_unary, [[A]], implicit-def {{(dead )?}}$arguments{{$}}
; MIR-NEXT:  {{^ *}}[[Y:%[0-9]+]]:i32 = CALL @i32_binary, [[X]], [[B]], implicit-def {{(dead )?}}$arguments{{$}}
; MIR-NEXT:  {{^ *}}RETURN [[Y]], implicit-def dead $arguments{{$}}
; CHECK-LABEL: .method {{.*}}'call_nested'(
; CHECK-SAME:  int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call [[I32_UNARY]]{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call [[I32_BINARY]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_nested(i32 %a, i32 %b) {
  %x = call i32 @i32_unary(i32 %a)
  %y = call i32 @i32_binary(i32 %x, i32 %b)
  ret i32 %y
}

; A call result used by arithmetic.
; CHECK-LABEL: .method {{.*}}'call_then_add'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call [[I32_UNARY]]{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_then_add(i32 %a) {
  %x = call i32 @i32_unary(i32 %a)
  %y = add i32 %x, %a
  ret i32 %y
}

; Constant arguments.
; CHECK-LABEL: .method {{.*}}'call_consts'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 2{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  ldc.i4.m1{{$}}
; O2-NEXT:     {{^}}  call [[I32_BINARY]]{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  call [[I32_BINARY]]{{$}}
; CHECK:       {{^}}} // end of method 'call_consts'{{$}}
define i32 @call_consts() {
  %r = call i32 @i32_binary(i32 1, i32 -1)
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'call_mixed_consts'(
; O2-NEXT:     {{^\{$}}
; O2-NEXT:     {{^}}  .maxstack 5{{$}}
; O2-NEXT:     {{^}}  ldc.i4.7{{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.8|ldc\.i4 8}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0x3FC00000){{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0x4004000000000000){{$}}
; O2-NEXT:     {{^}}  {{ldc\.i4\.0|ldc\.i4 0}}{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  call [[VOID_MIXED]]{{$}}
; O2-NEXT:     {{^}}  ret{{$}}
; O0:          {{^}}  call [[VOID_MIXED]]{{$}}
; CHECK:       {{^}}} // end of method 'call_mixed_consts'{{$}}
define void @call_mixed_consts() {
  call void @void_mixed(i32 7, i64 8, float 1.5, double 2.5, ptr null)
  ret void
}

; Tail calls are ordinary calls.
; CHECK-LABEL: .method {{.*}}'tail_call_void_nullary'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
; CHECK-NEXT:  {{^}}  call [[VOID_NULLARY]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @tail_call_void_nullary() {
  tail call void @void_nullary()
  ret void
}

; CHECK-LABEL: .method {{.*}}'tail_call_i32_unary'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call [[I32_UNARY]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @tail_call_i32_unary(i32 %a) {
  %r = tail call i32 @i32_unary(i32 %a)
  ret i32 %r
}

; fastcc and coldcc call like C.
; CHECK-LABEL: .method {{.*}}'call_fastcc'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call [[FASTCC]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_fastcc(i32 %a) {
  %r = tail call fastcc i32 @fastcc_callee(i32 %a)
  ret i32 %r
}

; CHECK-LABEL: .method {{.*}}'call_coldcc'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 0{{$}}
; CHECK-NEXT:  {{^}}  call [[COLDCC]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @call_coldcc() {
  call coldcc void @coldcc_callee()
  ret void
}

; A function calls itself.
; CHECK-LABEL: .method {{.*}}'recursive'(
; CHECK-SAME:  int32, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  call int32 'recursive'(int32, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @recursive(i32 %n, ptr %p) {
  %r = call i32 @recursive(i32 %n, ptr %p)
  ret i32 %r
}

; A call of a function defined further down: the call token is computed from
; the IR declaration and matches the definition's header below.
; CHECK-LABEL: .method {{.*}}'call_forward'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call float64 'forward_callee'(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @call_forward(ptr %p) {
  %r = call double @forward_callee(ptr %p)
  ret double %r
}

; CHECK-LABEL: .method public static float64 'forward_callee'(native int) cil managed{{$}}
define double @forward_callee(ptr %p) {
  ret double 0.0
}

; ---- callee names that need quoting (decision D11) --------------------------
; The call token spells the callee's name exactly as its method header does:
; the leading \01 of an asm label is dropped (ilasm rejects a call of
; '\001asm_label' as an unresolved member reference), a quote is \', a
; backslash \134, a control byte a three-digit octal escape, and a non-ASCII
; name is raw UTF-8 (the file then starts with a byte order mark,
; identifier-quoting.ll). The ilasm round trips above resolve every call.

; CHECK: {{^}}.method public static [[ASM_LABEL:int32 'asm_label'\(int32\)]] cil managed{{$}}
define i32 @"\01asm_label"(i32 %a) {
  ret i32 %a
}
; CHECK: {{^}}.method assembly static [[QUOTE:int32 'it\\'s'\(int32\)]] cil managed{{$}}
define internal i32 @"it's"(i32 %a) {
  ret i32 %a
}
; CHECK: {{^}}.method public static [[BACKSLASH:int32 'back\\134slash'\(int32\)]] cil managed{{$}}
define i32 @"back\5Cslash"(i32 %a) {
  ret i32 %a
}
; CHECK: {{^}}.method public static [[CONTROL:int32 'ctrl\\007x'\(int32\)]] cil managed{{$}}
define i32 @"ctrl\07x"(i32 %a) {
  ret i32 %a
}
; CHECK: {{^}}.method public static [[UTF8:int32 'café'\(int32\)]] cil managed{{$}}
define i32 @"caf\C3\A9"(i32 %a) {
  ret i32 %a
}

; CHECK-LABEL: .method {{.*}}'call_quoted_names'(
; CHECK-SAME:  int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  call [[ASM_LABEL]]{{$}}
; CHECK-NEXT:  {{^}}  call [[QUOTE]]{{$}}
; CHECK-NEXT:  {{^}}  call [[BACKSLASH]]{{$}}
; CHECK-NEXT:  {{^}}  call [[CONTROL]]{{$}}
; CHECK-NEXT:  {{^}}  call [[UTF8]]{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_quoted_names'{{$}}
define i32 @call_quoted_names(i32 %a) {
  %b = call i32 @"\01asm_label"(i32 %a)
  %c = call i32 @"it's"(i32 %b)
  %d = call i32 @"back\5Cslash"(i32 %c)
  %e = call i32 @"ctrl\07x"(i32 %d)
  %f = call i32 @"caf\C3\A9"(i32 %e)
  ret i32 %f
}

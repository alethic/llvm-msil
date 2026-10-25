; Indirect calls (ImplementationPlan.md M9.a, decision D18; plan 5.1 'Calls'
; and 5.4 'CALL_INDIRECT'; recon isel.md 2.4, 5.8, 8.2-8.3 and asmprinter.md
; 5.3, 6.4), ported from llvm/test/CodeGen/WebAssembly/call-indirect.ll and
; extended. A call whose callee is not a function or an external symbol is a
; CALL_INDIRECT whose operands are the result (if any), then the arguments in
; call order, then the callee address last, which is the order 'calli' pops
; them in: the arguments are pushed first and the function pointer last. It
; prints as 'calli' followed by a stand-alone signature that MCInstLower
; builds from the instruction (its def is the result type, its register uses
; minus the trailing callee are the parameter types): '<ret>(<params>)' in the
; same type words as a '.method' header ('void', 'int32', 'native int',
; 'float32', 'float64'), and always managed in Phase 1 (no 'unmanaged',
; Design.md 4.5 thunks are phase 3). Narrow integers are int32 in the
; signature and are extended at the call site; an i128 argument is two native
; ints; a result that needs two values is demoted to a frame slot whose
; address is the first argument, so the calli returns void. Unused results are
; popped, tail calls are ordinary calls (no 'tail.'), and fastcc calls like C.
;
; Straight-line calls whose arguments are arguments of the caller compile to
; the same CIL at -O0 and -O2 (CHECK); calls with narrow or constant arguments
; are exact at -O2 (O2) and checked for the calli at -O0 (O0). The MIR lines
; check the CALL_INDIRECT that the custom inserter builds (finalize-isel)
; against the operand layout of plan 5.4. Calls through function addresses
; (address fields that <Module>::.cctor sets with ldftn), from tables in data
; and to P/Invoke stubs are in ldftn-calli.ll and fnptr-table.ll; the end of
; this file has one such call, whose pointer SelectionDAG folds to a function
; of another type: it stays an indirect call with the call site's signature.

; DEFINE: %{check} = FileCheck %s --implicit-check-not=int64 --implicit-check-not=tail. \
; DEFINE:   --implicit-check-not=unmanaged --implicit-check-not=jmp --implicit-check-not="LLVM ERROR"
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{check} --check-prefixes=CHECK,O0
; With call-site info (clang -g -O1 and above sets it for every MSIL triple),
; the custom inserter must carry the info over to CALL_INDIRECT as it does to
; CALL (call.ll).
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O2 -emit-call-site-info -verify-machineinstrs \
; RUN:   -asm-verbose=false < %s 2>&1 | %{check} --check-prefixes=CHECK,O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -emit-call-site-info -verify-machineinstrs \
; RUN:   -asm-verbose=false < %s 2>&1 | %{check} --check-prefixes=CHECK,O0
; DEFINE: %{mir} = FileCheck %s --check-prefix=MIR --implicit-check-not=CALL_PARAMS \
; DEFINE:   --implicit-check-not=CALL_RESULTS --implicit-check-not="CALL @"
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O2 -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | %{mir}
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -verify-machineinstrs -stop-after=finalize-isel < %s \
; RUN:   | %{mir}
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "call-indirect.c"

; ---- result types -----------------------------------------------------------

; MIR-LABEL: name: call_indirect_void
; MIR:       [[F:%[0-9]+]]:i64 = ARGUMENT_I64 0, implicit $arguments
; MIR:       {{^ *}}CALL_INDIRECT [[F]], implicit-def {{(dead )?}}$arguments{{$}}
; MIR-NEXT:  {{^ *}}RETURN implicit-def dead $arguments{{$}}
; CHECK-LABEL: .method {{.*}}'call_indirect_void'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli void(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_indirect_void'{{$}}
define void @call_indirect_void(ptr %callee) {
  call void %callee()
  ret void
}

; MIR-LABEL: name: call_indirect_i32
; MIR:       [[F:%[0-9]+]]:i64 = ARGUMENT_I64 0, implicit $arguments
; MIR:       {{^ *}}[[R:%[0-9]+]]:i32 = CALL_INDIRECT [[F]], implicit-def {{(dead )?}}$arguments{{$}}
; MIR-NEXT:  {{^ *}}RETURN [[R]], implicit-def dead $arguments{{$}}
; CHECK-LABEL: .method {{.*}}'call_indirect_i32'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli int32(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_indirect_i32'{{$}}
define i32 @call_indirect_i32(ptr %callee) {
  %r = call i32 %callee()
  ret i32 %r
}

; MIR-LABEL: name: call_indirect_i64
; MIR:       {{^ *}}[[R:%[0-9]+]]:i64 = CALL_INDIRECT
; CHECK-LABEL: .method {{.*}}'call_indirect_i64'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli native int(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @call_indirect_i64(ptr %callee) {
  %r = call i64 %callee()
  ret i64 %r
}

; MIR-LABEL: name: call_indirect_float
; MIR:       {{^ *}}[[R:%[0-9]+]]:f32 = CALL_INDIRECT
; CHECK-LABEL: .method {{.*}}'call_indirect_float'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli float32(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define float @call_indirect_float(ptr %callee) {
  %r = call float %callee()
  ret float %r
}

; MIR-LABEL: name: call_indirect_double
; MIR:       {{^ *}}[[R:%[0-9]+]]:f64 = CALL_INDIRECT
; CHECK-LABEL: .method {{.*}}'call_indirect_double'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli float64(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @call_indirect_double(ptr %callee) {
  %r = call double %callee()
  ret double %r
}

; A pointer result is a native int, like any pointer.
; CHECK-LABEL: .method {{.*}}'call_indirect_ptr'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli native int(){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define ptr @call_indirect_ptr(ptr %callee) {
  %r = call ptr %callee()
  ret ptr %r
}

; ---- arguments: pushed in call order, the callee last ----------------------

; MIR-LABEL: name: call_indirect_arg
; MIR-DAG:   [[F:%[0-9]+]]:i64 = ARGUMENT_I64 0, implicit $arguments
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 1, implicit $arguments
; MIR:       {{^ *}}CALL_INDIRECT [[A]], [[F]], implicit-def {{(dead )?}}$arguments{{$}}
; MIR-NEXT:  {{^ *}}RETURN implicit-def dead $arguments{{$}}
; CHECK-LABEL: .method {{.*}}'call_indirect_arg'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli void(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_indirect_arg'{{$}}
define void @call_indirect_arg(ptr %callee, i32 %arg) {
  call void %callee(i32 %arg)
  ret void
}

; MIR-LABEL: name: call_indirect_args
; MIR-DAG:   [[F:%[0-9]+]]:i64 = ARGUMENT_I64 0, implicit $arguments
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 1, implicit $arguments
; MIR-DAG:   [[B:%[0-9]+]]:i32 = ARGUMENT_I32 2, implicit $arguments
; MIR:       {{^ *}}[[R:%[0-9]+]]:i32 = CALL_INDIRECT [[A]], [[B]], [[F]], implicit-def {{(dead )?}}$arguments{{$}}
; MIR-NEXT:  {{^ *}}RETURN [[R]], implicit-def dead $arguments{{$}}
; CHECK-LABEL: .method {{.*}}'call_indirect_args'(
; CHECK-SAME:  native int, int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli int32(int32, int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_indirect_args(ptr %callee, i32 %a, i32 %b) {
  %r = call i32 %callee(i32 %a, i32 %b)
  ret i32 %r
}

; The callee is the caller's last parameter: the order is the same.
; MIR-LABEL: name: call_indirect_callee_last_param
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 0, implicit $arguments
; MIR-DAG:   [[F:%[0-9]+]]:i64 = ARGUMENT_I64 1, implicit $arguments
; MIR:       {{^ *}}CALL_INDIRECT [[A]], [[F]], implicit-def {{(dead )?}}$arguments{{$}}
; CHECK-LABEL: .method {{.*}}'call_indirect_callee_last_param'(
; CHECK-SAME:  int32, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  calli void(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @call_indirect_callee_last_param(i32 %a, ptr %callee) {
  call void %callee(i32 %a)
  ret void
}

; Every parameter type in one signature.
; MIR-LABEL: name: call_indirect_mixed
; MIR-DAG:   [[F:%[0-9]+]]:i64 = ARGUMENT_I64 0, implicit $arguments
; MIR-DAG:   [[A:%[0-9]+]]:i32 = ARGUMENT_I32 1, implicit $arguments
; MIR-DAG:   [[B:%[0-9]+]]:i64 = ARGUMENT_I64 2, implicit $arguments
; MIR-DAG:   [[C:%[0-9]+]]:f32 = ARGUMENT_F32 3, implicit $arguments
; MIR-DAG:   [[D:%[0-9]+]]:f64 = ARGUMENT_F64 4, implicit $arguments
; MIR-DAG:   [[E:%[0-9]+]]:i64 = ARGUMENT_I64 5, implicit $arguments
; MIR:       {{^ *}}[[R:%[0-9]+]]:f64 = CALL_INDIRECT [[A]], [[B]], [[C]], [[D]], [[E]], [[F]], implicit-def {{(dead )?}}$arguments{{$}}
; MIR-NEXT:  {{^ *}}RETURN [[R]], implicit-def dead $arguments{{$}}
; CHECK-LABEL: .method {{.*}}'call_indirect_mixed'(
; CHECK-SAME:  native int, int32, native int, float32, float64, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 6{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 4{{$}}
; CHECK-NEXT:  {{^}}  ldarg.s 5{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli float64(int32, native int, float32, float64, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define double @call_indirect_mixed(ptr %callee, i32 %a, i64 %b, float %c, double %d, ptr %e) {
  %r = call double %callee(i32 %a, i64 %b, float %c, double %d, ptr %e)
  ret double %r
}

; i8, i16 and i1 arguments and an i8 result are int32 in the signature; the
; arguments are extended at the call site as their attributes say (the
; signext result may be extended once more after the calli).
; CHECK-LABEL: .method {{.*}}'call_indirect_narrow'(
; CHECK-SAME:  native int, int32, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 4{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  conv.i1{{$}}
; O2-NEXT:     {{^}}  ldarg.2{{$}}
; O2-NEXT:     {{^}}  conv.u2{{$}}
; O2-NEXT:     {{^}}  ldc.i4.1{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O0:          {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli int32(int32, int32, int32){{$}}
; CHECK-NOT:   {{^}}  call
; CHECK:       {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_indirect_narrow'{{$}}
define i32 @call_indirect_narrow(ptr %callee, i32 %a, i32 %b) {
  %c = trunc i32 %a to i8
  %s = trunc i32 %b to i16
  %r = call signext i8 %callee(i8 signext %c, i16 zeroext %s, i1 zeroext true)
  %x = sext i8 %r to i32
  ret i32 %x
}

; Constant arguments of every type, then the callee.
; CHECK-LABEL: .method {{.*}}'constant_args'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 6{{$}}
; O2-NEXT:     {{^}}  ldc.i4.7{{$}}
; O2-NEXT:     {{^}}  ldc.i4.m1{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  ldc.r4 float32(0x3FC00000){{$}}
; O2-NEXT:     {{^}}  ldc.r8 float64(0x4000000000000000){{$}}
; O2-NEXT:     {{^}}  ldc.i4.0{{$}}
; O2-NEXT:     {{^}}  conv.i{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O0:          {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli int32(int32, native int, float32, float64, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @constant_args(ptr %f) {
  %r = call i32 %f(i32 7, i64 -1, float 1.5, double 2.0, ptr null)
  ret i32 %r
}

; An i128 argument is two native ints.
; MIR-LABEL: name: i128_arg
; MIR-DAG:   [[F:%[0-9]+]]:i64 = ARGUMENT_I64 0, implicit $arguments
; MIR-DAG:   [[LO:%[0-9]+]]:i64 = ARGUMENT_I64 1, implicit $arguments
; MIR-DAG:   [[HI:%[0-9]+]]:i64 = ARGUMENT_I64 2, implicit $arguments
; MIR:       {{^ *}}[[R:%[0-9]+]]:i64 = CALL_INDIRECT [[LO]], [[HI]], [[F]], implicit-def {{(dead )?}}$arguments{{$}}
; CHECK-LABEL: .method {{.*}}'i128_arg'(
; CHECK-SAME:  native int, native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli native int(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @i128_arg(ptr %f, i128 %x) {
  %r = call i64 %f(i128 %x)
  ret i64 %r
}

; A { i64, i64 } result needs two values: it is demoted to a 16-byte frame
; slot (D13), whose address is the first argument of a void calli; the second
; half is loaded from slot + 8.
; CHECK-LABEL: .method {{.*}}'pair_result'(
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
; CHECK-NEXT:  {{^}}  calli void(native int, native int){{$}}
; CHECK-NEXT:  {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.8{{$}}
; CHECK-NEXT:  {{^}}  conv.i{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i64 @pair_result(ptr %f, i64 %x) {
  %r = call { i64, i64 } %f(i64 %x)
  %e = extractvalue { i64, i64 } %r, 1
  ret i64 %e
}

; ---- results and callees in context ------------------------------------------

; MIR-LABEL: name: drop_i32_result
; MIR:       {{^ *}}{{(dead )?}}%{{[0-9]+}}:i32 = CALL_INDIRECT %{{[0-9]+}}, implicit-def {{(dead )?}}$arguments{{$}}
; MIR-NEXT:  {{^ *}}RETURN implicit-def dead $arguments{{$}}
; CHECK-LABEL: .method {{.*}}'drop_i32_result'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli int32(){{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @drop_i32_result(ptr %callee) {
  %r = call i32 %callee()
  ret void
}

; CHECK-LABEL: .method {{.*}}'drop_double_result'(
; CHECK-SAME:  native int, float64) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli float64(float64){{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @drop_double_result(ptr %callee, double %d) {
  %r = call double %callee(double %d)
  ret void
}

; The callee is loaded from memory after the argument is pushed.
; MIR-LABEL: name: callee_from_memory
; MIR-DAG:   [[PP:%[0-9]+]]:i64 = ARGUMENT_I64 0, implicit $arguments
; MIR-DAG:   [[X:%[0-9]+]]:i32 = ARGUMENT_I32 1, implicit $arguments
; MIR:       [[F:%[0-9]+]]:i64 = LDIND_I 0, [[PP]]
; MIR:       {{^ *}}[[R:%[0-9]+]]:i32 = CALL_INDIRECT [[X]], [[F]], implicit-def {{(dead )?}}$arguments{{$}}
; CHECK-LABEL: .method {{.*}}'callee_from_memory'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  calli int32(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @callee_from_memory(ptr %pp, i32 %x) {
  %f = load ptr, ptr %pp
  %r = call i32 %f(i32 %x)
  ret i32 %r
}

; Ported from call_indirect_stackify in
; llvm/test/CodeGen/WebAssembly/reg-stackify.ll: the object pointer is both
; the first argument and the base of the callee load, so it is kept (tee'd at
; -O2) and the callee is still loaded after the arguments are pushed.
; MIR-LABEL: name: call_indirect_stackify
; MIR:       [[OBJ:%[0-9]+]]:i64 = LDIND_I 0, %{{[0-9]+}}
; MIR-NEXT:  [[ADDR:%[0-9]+]]:i64 = LDIND_I 0, [[OBJ]]
; MIR-NEXT:  {{^ *}}{{(dead )?}}%{{[0-9]+}}:i32 = CALL_INDIRECT [[OBJ]], %{{[0-9]+}}, [[ADDR]], implicit-def {{(dead )?}}$arguments{{$}}
; CHECK-LABEL: .method {{.*}}'call_indirect_stackify'(
; CHECK-SAME:  native int, int32) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 3{{$}}
; O0-NEXT:     {{^}}  .locals (native int){{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; O2-NEXT:     {{^}}  dup{{$}}
; O2-NEXT:     {{^}}  starg.s 0{{$}}
; O2-NEXT:     {{^}}  ldarg.1{{$}}
; O2-NEXT:     {{^}}  ldarg.0{{$}}
; O0-NEXT:     {{^}}  stloc.0{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; O0-NEXT:     {{^}}  ldarg.1{{$}}
; O0-NEXT:     {{^}}  ldloc.0{{$}}
; CHECK-NEXT:  {{^}}  ldind.i{{$}}
; CHECK-NEXT:  {{^}}  calli int32(native int, int32){{$}}
; CHECK-NEXT:  {{^}}  pop{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define void @call_indirect_stackify(ptr %objptr, i32 %arg) {
  %obj = load ptr, ptr %objptr
  %addr = load ptr, ptr %obj
  %ret = call i32 %addr(ptr %obj, i32 %arg)
  ret void
}

; CHECK-LABEL: .method {{.*}}'result_in_arith'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli int32(int32){{$}}
; CHECK-NEXT:  {{^}}  ldc.i4.1{{$}}
; CHECK-NEXT:  {{^}}  add{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @result_in_arith(ptr %callee, i32 %x) {
  %r = call i32 %callee(i32 %x)
  %s = add i32 %r, 1
  ret i32 %s
}

; The result of one indirect call is the argument of the next; it stays on
; the evaluation stack under the second callee.
; MIR-LABEL: name: nested
; MIR-DAG:   [[F:%[0-9]+]]:i64 = ARGUMENT_I64 0, implicit $arguments
; MIR-DAG:   [[G:%[0-9]+]]:i64 = ARGUMENT_I64 1, implicit $arguments
; MIR:       {{^ *}}[[A:%[0-9]+]]:i32 = CALL_INDIRECT [[G]], implicit-def {{(dead )?}}$arguments{{$}}
; MIR-NEXT:  {{^ *}}[[B:%[0-9]+]]:i32 = CALL_INDIRECT [[A]], [[F]], implicit-def {{(dead )?}}$arguments{{$}}
; MIR-NEXT:  {{^ *}}RETURN [[B]], implicit-def dead $arguments{{$}}
; CHECK-LABEL: .method {{.*}}'nested'(
; CHECK-SAME:  native int, native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  calli int32(){{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli int32(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @nested(ptr %f, ptr %g) {
  %a = call i32 %g()
  %b = call i32 %f(i32 %a)
  ret i32 %b
}

; The same pointer is called twice.
; CHECK-LABEL: .method {{.*}}'call_twice'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli int32(int32){{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli int32(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @call_twice(ptr %f, i32 %x) {
  %a = call i32 %f(i32 %x)
  %b = call i32 %f(i32 %a)
  ret i32 %b
}

; A tail call is an ordinary calli (no 'tail.' prefix in Phase 1).
; CHECK-LABEL: .method {{.*}}'tail_indirect'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli int32(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @tail_indirect(ptr %f, i32 %x) {
  %r = tail call i32 %f(i32 %x)
  ret i32 %r
}

; fastcc calls like C.
; CHECK-LABEL: .method {{.*}}'fastcc_indirect'(
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.1{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  calli int32(int32){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
define i32 @fastcc_indirect(ptr %f, i32 %x) {
  %r = call fastcc i32 %f(i32 %x)
  ret i32 %r
}

; ---- a pointer that SelectionDAG folds to a function ------------------------

; The select folds to @folded_callee, whose type (i32 (i32)) is not the call's
; (i64 (i64)). Only a direct call, whose called operand is the Function
; itself, is checked against the callee's type ('mismatched call signature',
; call-errors.ll STRUCTMM). This call stays indirect, through the function's
; address field, and the calli has the call site's signature, not the
; callee's: the IR is undefined behaviour, and the backend keeps what it says.
; MIR-LABEL: name: call_folded_select
; MIR:       [[X:%[0-9]+]]:i64 = ARGUMENT_I64 0, implicit $arguments
; MIR:       [[F:%[0-9]+]]:i64 = FUNC_ADDR @folded_callee
; MIR:       {{^ *}}[[R:%[0-9]+]]:i64 = CALL_INDIRECT [[X]], [[F]], implicit-def {{(dead )?}}$arguments{{$}}
; MIR-NEXT:  {{^ *}}RETURN [[R]], implicit-def dead $arguments{{$}}
; CHECK-LABEL: .method {{.*}}'call_folded_select'(
; CHECK-SAME:  native int) cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 2{{$}}
; CHECK-NEXT:  {{^}}  ldarg.0{{$}}
; CHECK-NEXT:  {{^}}  ldsfld native int '<msil>fn_folded_callee'{{$}}
; CHECK-NEXT:  {{^}}  calli native int(native int){{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}
; CHECK-NEXT:  {{^}}} // end of method 'call_folded_select'{{$}}
define i32 @folded_callee(i32 %x) {
  ret i32 %x
}

define i64 @call_folded_select(i64 %x) {
  %f = select i1 true, ptr @folded_callee, ptr null
  %r = call i64 %f(i64 %x)
  ret i64 %r
}

; folded_callee's address field, the only one in the module; the .cctor sets
; it with the ldftn of the callee's own signature.
; CHECK:       {{^}}.field private static initonly native int '<msil>fn_folded_callee'{{$}}
; CHECK-EMPTY:
; CHECK-NEXT:  {{^}}.method {{.*}} .cctor() cil managed{{$}}
; CHECK-NEXT:  {{^\{$}}
; CHECK-NEXT:  {{^}}  .maxstack 1{{$}}
; CHECK-NEXT:  {{^}}  ldftn int32 'folded_callee'(int32){{$}}
; CHECK-NEXT:  {{^}}  stsfld native int '<msil>fn_folded_callee'{{$}}
; CHECK-NEXT:  {{^}}  ret{{$}}

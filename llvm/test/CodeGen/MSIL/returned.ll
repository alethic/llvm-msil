; The 'returned' parameter attribute (ImplementationPlan.md M9.b; plan 5.3
; pipeline 'OptimizeReturned (O1+)'), ported from
; llvm/test/CodeGen/WebAssembly/returned.ll. MSILOptimizeReturned, a copy of
; WebAssemblyOptimizeReturned that runs at -O1 and above, replaces the uses
; of an argument passed to a 'returned' parameter that the call dominates by
; the call's result: the result stays on the evaluation stack and is used
; directly instead of being popped and the argument reloaded. Constant
; arguments (a global's address), arguments of parameters without the
; attribute, uses that the call does not dominate and lifetime markers keep
; the argument. The attribute may be on any parameter. At -O0 the pass does
; not run: the result is popped and the argument is used again. MSIL change
; to the copy (M9.b): an argument whose type differs from the call's result
; type (the IR verifier allows that only for vectors of the same bit width)
; is kept too, since the result cannot replace a value of another type
; (WebAssembly's pass asserts 'replaceUses of value with new value of
; different type!').
;
; IR checks the module after -stop-after=msil-optimize-returned; O2 and O0
; check the IL. The callees are defined in the module (Phase 1 has no
; linker, D14).

; DEFINE: %{il} = FileCheck %s --implicit-check-not=int64 --implicit-check-not="LLVM ERROR" \
; DEFINE:   --implicit-check-not=error: --implicit-check-not=warning:
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -stop-after=msil-optimize-returned < %s -o - \
; RUN:   | FileCheck %s --check-prefix=IR
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -stop-after=msil-optimize-returned < %s -o - \
; RUN:   | FileCheck %s --check-prefix=IR
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{il} --check-prefix=O2
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{il} --check-prefix=O2
; RUN: llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{il} --check-prefix=O0
; RUN: llc -mtriple=x86_64-pc-windows-msvc-msil -O0 -asm-verbose=false -verify-machineinstrs < %s 2>&1 \
; RUN:   | %{il} --check-prefix=O0
; RUN: %if ilasm %{ llc -mtriple=x86_64-pc-windows-msvc-msil < %s -o %t.o2.il \
; RUN:   && %ilasm-verify -output=%t.o2.dll %t.o2.il %}
; RUN: %if ilasm %{ llc -mtriple=x86_64-unknown-linux-gnu-msil -O0 < %s -o %t.o0.il \
; RUN:   && %ilasm-verify -output=%t.o0.dll %t.o0.il %}

source_filename = "returned.c"

@global = global i32 0, align 4

define noalias ptr @_Znwm(i64 %n) {
  ret ptr null
}

define ptr @_ZN5AppleC1Ev(ptr returned %this) {
  ret ptr %this
}

define ptr @copy_like(ptr returned %d, ptr %s, i64 %n) {
  ret ptr %d
}

define ptr @returns_arg(ptr returned %p) {
  ret ptr %p
}

define i32 @do_something(i32 returned %a, i32 %b, double %c) {
  ret i32 %a
}

define void @do_something_with_i32(i32 %x) {
  ret void
}

define void @do_something_with_double(double %x) {
  ret void
}

define i32 @do_something_else(i32 %a, i32 returned %b) {
  ret i32 %b
}

; A constructor returns 'this': the new object is the constructor's result.
; IR-LABEL: define ptr @_Z3foov()
; IR:         %call1 = tail call ptr @_ZN5AppleC1Ev(ptr %call)
; IR-NEXT:    ret ptr %call1
; O2-LABEL: .method {{.*}}'_Z3foov'(
; O2-NEXT:  {{^\{$}}
; O2-NEXT:  {{^}}  .maxstack 1{{$}}
; O2-NEXT:  {{^}}  ldc.i4 1{{$}}
; O2-NEXT:  {{^}}  conv.i{{$}}
; O2-NEXT:  {{^}}  call native int '_Znwm'(native int){{$}}
; O2-NEXT:  {{^}}  call native int '_ZN5AppleC1Ev'(native int){{$}}
; O2-NEXT:  {{^}}  ret{{$}}
; O0-LABEL: .method {{.*}}'_Z3foov'(
; O0-NEXT:  {{^\{$}}
; O0-NEXT:  {{^}}  .maxstack 1{{$}}
; O0-NEXT:  {{^}}  .locals (native int){{$}}
; O0-NEXT:  {{^}}  ldc.i4 1{{$}}
; O0-NEXT:  {{^}}  conv.i{{$}}
; O0-NEXT:  {{^}}  call native int '_Znwm'(native int){{$}}
; O0-NEXT:  {{^}}  stloc.0{{$}}
; O0-NEXT:  {{^}}  ldloc.0{{$}}
; O0-NEXT:  {{^}}  call native int '_ZN5AppleC1Ev'(native int){{$}}
; O0-NEXT:  {{^}}  pop{{$}}
; O0-NEXT:  {{^}}  ldloc.0{{$}}
; O0-NEXT:  {{^}}  ret{{$}}
define ptr @_Z3foov() {
entry:
  %call = tail call noalias ptr @_Znwm(i64 1)
  %call1 = tail call ptr @_ZN5AppleC1Ev(ptr %call)
  ret ptr %call
}

; A memcpy-like function returns its destination.
; IR-LABEL: define ptr @_Z3barPvS_l(
; IR:         %call = tail call ptr @copy_like(ptr %p, ptr %s, i64 %n)
; IR-NEXT:    ret ptr %call
; O2-LABEL: .method {{.*}}'_Z3barPvS_l'(
; O2-SAME:  native int, native int, native int) cil managed{{$}}
; O2-NEXT:  {{^\{$}}
; O2-NEXT:  {{^}}  .maxstack 3{{$}}
; O2-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:  {{^}}  ldarg.1{{$}}
; O2-NEXT:  {{^}}  ldarg.2{{$}}
; O2-NEXT:  {{^}}  call native int 'copy_like'(native int, native int, native int){{$}}
; O2-NEXT:  {{^}}  ret{{$}}
; O0-LABEL: .method {{.*}}'_Z3barPvS_l'(
; O0:       {{^}}  call native int 'copy_like'(native int, native int, native int){{$}}
; O0-NEXT:  {{^}}  pop{{$}}
; O0-NEXT:  {{^}}  ldarg.0{{$}}
; O0-NEXT:  {{^}}  ret{{$}}
define ptr @_Z3barPvS_l(ptr %p, ptr %s, i64 %n) {
entry:
  %call = tail call ptr @copy_like(ptr %p, ptr %s, i64 %n)
  ret ptr %p
}

; A constant argument is not replaced: the global's address is used again.
; IR-LABEL: define ptr @test_constant_arg()
; IR:         call ptr @returns_arg(ptr @global)
; IR-NEXT:    ret ptr @global
; O2-LABEL: .method {{.*}}'test_constant_arg'(
; O2-NEXT:  {{^\{$}}
; O2-NEXT:  {{^}}  .maxstack 2{{$}}
; O2-NEXT:  {{^}}  ldsfld native int 'global'{{$}}
; O2-NEXT:  {{^}}  call native int 'returns_arg'(native int){{$}}
; O2-NEXT:  {{^}}  pop{{$}}
; O2-NEXT:  {{^}}  ldsfld native int 'global'{{$}}
; O2-NEXT:  {{^}}  ret{{$}}
; O0-LABEL: .method {{.*}}'test_constant_arg'(
; O0:       {{^}}  call native int 'returns_arg'(native int){{$}}
; O0-NEXT:  {{^}}  pop{{$}}
define ptr @test_constant_arg() {
  %call = call ptr @returns_arg(ptr @global)
  ret ptr @global
}

; Arguments of parameters without the attribute are not replaced.
; IR-LABEL: define void @test_other_skipped(
; IR:         call void @do_something_with_i32(i32 %b)
; IR-NEXT:    call void @do_something_with_double(double %c)
; O2-LABEL: .method {{.*}}'test_other_skipped'(
; O2-SAME:  int32, int32, float64) cil managed{{$}}
; O2-NEXT:  {{^\{$}}
; O2-NEXT:  {{^}}  .maxstack 3{{$}}
; O2-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:  {{^}}  ldarg.1{{$}}
; O2-NEXT:  {{^}}  ldarg.2{{$}}
; O2-NEXT:  {{^}}  call int32 'do_something'(int32, int32, float64){{$}}
; O2-NEXT:  {{^}}  pop{{$}}
; O2-NEXT:  {{^}}  ldarg.1{{$}}
; O2-NEXT:  {{^}}  call void 'do_something_with_i32'(int32){{$}}
; O2-NEXT:  {{^}}  ldarg.2{{$}}
; O2-NEXT:  {{^}}  call void 'do_something_with_double'(float64){{$}}
; O2-NEXT:  {{^}}  ret{{$}}
define void @test_other_skipped(i32 %a, i32 %b, double %c) {
  %call = call i32 @do_something(i32 %a, i32 %b, double %c)
  call void @do_something_with_i32(i32 %b)
  call void @do_something_with_double(double %c)
  ret void
}

; The attribute on the second parameter.
; IR-LABEL: define i32 @test_second_arg(
; IR:         %call = call i32 @do_something_else(i32 %a, i32 %b)
; IR-NEXT:    ret i32 %call
; O2-LABEL: .method {{.*}}'test_second_arg'(
; O2-NEXT:  {{^\{$}}
; O2-NEXT:  {{^}}  .maxstack 2{{$}}
; O2-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:  {{^}}  ldarg.1{{$}}
; O2-NEXT:  {{^}}  call int32 'do_something_else'(int32, int32){{$}}
; O2-NEXT:  {{^}}  ret{{$}}
; O0-LABEL: .method {{.*}}'test_second_arg'(
; O0-NEXT:  {{^\{$}}
; O0-NEXT:  {{^}}  .maxstack 2{{$}}
; O0-NEXT:  {{^}}  ldarg.0{{$}}
; O0-NEXT:  {{^}}  ldarg.1{{$}}
; O0-NEXT:  {{^}}  call int32 'do_something_else'(int32, int32){{$}}
; O0-NEXT:  {{^}}  pop{{$}}
; O0-NEXT:  {{^}}  ldarg.1{{$}}
; O0-NEXT:  {{^}}  ret{{$}}
define i32 @test_second_arg(i32 %a, i32 %b) {
  %call = call i32 @do_something_else(i32 %a, i32 %b)
  ret i32 %b
}

; Only the returned argument is replaced in a later use; the other operand
; is still the argument.
; IR-LABEL: define i32 @test_later_use(
; IR:         %call = call i32 @do_something_else(i32 %a, i32 %b)
; IR-NEXT:    %s = add i32 %call, %a
; O2-LABEL: .method {{.*}}'test_later_use'(
; O2-NEXT:  {{^\{$}}
; O2-NEXT:  {{^}}  .maxstack 2{{$}}
; O2-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:  {{^}}  ldarg.1{{$}}
; O2-NEXT:  {{^}}  call int32 'do_something_else'(int32, int32){{$}}
; O2-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O2-NEXT:  {{^}}  ret{{$}}
; O0-LABEL: .method {{.*}}'test_later_use'(
; O0:       {{^}}  call int32 'do_something_else'(int32, int32){{$}}
; O0-NEXT:  {{^}}  pop{{$}}
; O0-NEXT:  {{^}}  ldarg.1{{$}}
; O0-NEXT:  {{^}}  ldarg.0{{$}}
; O0-NEXT:  {{^}}  add{{$}}
define i32 @test_later_use(i32 %a, i32 %b) {
  %call = call i32 @do_something_else(i32 %a, i32 %b)
  %s = add i32 %b, %a
  ret i32 %s
}

; Lifetime markers keep the alloca, not the call result.
; IR-LABEL: define void @test_lifetime()
; IR:         %ret = call ptr @returns_arg(ptr %a)
; IR-NEXT:    call void @llvm.lifetime.end.p0(ptr %a)
; O2-LABEL: .method {{.*}}'test_lifetime'(
; O2:       {{^}}  localloc{{$}}
; O2:       {{^}}  call native int 'returns_arg'(native int){{$}}
; O2-NEXT:  {{^}}  pop{{$}}
; O2-NEXT:  {{^}}  ret{{$}}
define void @test_lifetime() {
entry:
  %a = alloca i32, align 4
  call void @llvm.lifetime.start.p0(ptr %a)
  %ret = call ptr @returns_arg(ptr %a)
  call void @llvm.lifetime.end.p0(ptr %a)
  ret void
}

; A use that the call does not dominate keeps the argument.
; IR-LABEL: define ptr @test_not_dominated(
; IR:       join:
; IR-NEXT:    ret ptr %p
; O2-LABEL: .method {{.*}}'test_not_dominated'(
; O2:       {{^}}  call native int 'returns_arg'(native int){{$}}
; O2-NEXT:  {{^}}  pop{{$}}
; O2-NEXT:  {{^}}@BB{{[0-9]+_[0-9]+}}:{{$}}
; O2-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:  {{^}}  ret{{$}}
define ptr @test_not_dominated(ptr %p, i1 %c) {
entry:
  br i1 %c, label %then, label %join
then:
  %r = call ptr @returns_arg(ptr %p)
  br label %join
join:
  ret ptr %p
}

; A use in a later block that the call dominates gets the result, which is
; kept (not popped) for that block.
; IR-LABEL: define ptr @test_dominated(
; IR:       a:
; IR-NEXT:    ret ptr %r
; O2-LABEL: .method {{.*}}'test_dominated'(
; O2:       {{^}}  call native int 'returns_arg'(native int){{$}}
; O2-NEXT:  {{^}}  {{starg|stloc}}{{.*}}
; O0-LABEL: .method {{.*}}'test_dominated'(
; O0:       {{^}}  call native int 'returns_arg'(native int){{$}}
; O0-NEXT:  {{^}}  pop{{$}}
define ptr @test_dominated(ptr %p, i1 %c) {
entry:
  %r = call ptr @returns_arg(ptr %p)
  br i1 %c, label %a, label %b
a:
  ret ptr %p
b:
  ret ptr null
}

; A 'returned' <2 x i32> parameter of a <4 x i16> function: the later use of
; %x keeps %x. The vectors are passed as two int32s, and the <4 x i16> result
; is demoted to a frame slot whose address is the first argument; the add
; reads the arguments again, not the slot.
; IR-LABEL: define <2 x i32> @test_returned_other_type(
; IR:         %r = call <4 x i16> @returns_vector_bits(<2 x i32> %x)
; IR-NEXT:    %y = add <2 x i32> %x, %x
; IR-NEXT:    ret <2 x i32> %y
; O2-LABEL: .method {{.*}}'test_returned_other_type'(
; O2-SAME:  native int, int32, int32) cil managed{{$}}
; O2:       {{^}}  call void 'returns_vector_bits'(native int, int32, int32){{$}}
; O2-NEXT:  {{^}}  ldarg.0{{$}}
; O2-NEXT:  {{^}}  ldarg.1{{$}}
; O2-NEXT:  {{^}}  ldarg.1{{$}}
; O2-NEXT:  {{^}}  add{{$}}
; O0-LABEL: .method {{.*}}'test_returned_other_type'(
; O0:       {{^}}  call void 'returns_vector_bits'(native int, int32, int32){{$}}
define <4 x i16> @returns_vector_bits(<2 x i32> returned %a) {
  %b = bitcast <2 x i32> %a to <4 x i16>
  ret <4 x i16> %b
}

define <2 x i32> @test_returned_other_type(<2 x i32> %x) {
  %r = call <4 x i16> @returns_vector_bits(<2 x i32> %x)
  %y = add <2 x i32> %x, %x
  ret <2 x i32> %y
}

declare void @llvm.lifetime.start.p0(ptr)
declare void @llvm.lifetime.end.p0(ptr)
